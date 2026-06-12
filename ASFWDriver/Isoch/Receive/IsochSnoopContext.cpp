// IsochSnoopContext.cpp
// ASFW — Passive isochronous snoop (see IsochSnoopContext.hpp for scope/intent).

#include "IsochSnoopContext.hpp"

#include "../../Common/DriverKitUtils.hpp"
#include "../../Hardware/OHCIConstants.hpp"
#include "../../Hardware/RegisterMap.hpp"
#include "../../Logging/Logging.hpp"

namespace ASFW::Isoch {

// ============================================================================
// Factory
// ============================================================================

OSSharedPtr<IsochSnoopContext> IsochSnoopContext::Create(::ASFW::Driver::HardwareInterface* hw,
                                                         std::shared_ptr<::ASFW::Isoch::Memory::IIsochDMAMemory> dmaMemory) {
    auto ctx = ASFW::Common::MakeOSObject<IsochSnoopContext>();
    if (!ctx) return nullptr;

    ctx->hardware_ = hw;
    ctx->dmaMemory_ = std::move(dmaMemory);

    if (!ctx->init()) return nullptr;  // OSSharedPtr destructor calls release()

    return ctx;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool IsochSnoopContext::init() {
    if (!OSObject::init()) {
        return false;
    }
    return true;
}

void IsochSnoopContext::free() {
    Stop();
    OSObject::free();
}

// ============================================================================
// Configuration
// ============================================================================

IsochSnoopContext::Registers IsochSnoopContext::GetRegisters(uint8_t index) const {
    return Registers{
        .CommandPtr          = static_cast<::ASFW::Driver::Register32>(::DMAContextHelpers::IsoRcvCommandPtr(index)),
        .ContextControlSet   = static_cast<::ASFW::Driver::Register32>(::DMAContextHelpers::IsoRcvContextControlSet(index)),
        .ContextControlClear = static_cast<::ASFW::Driver::Register32>(::DMAContextHelpers::IsoRcvContextControlClear(index)),
        .ContextMatch        = static_cast<::ASFW::Driver::Register32>(::DMAContextHelpers::IsoRcvContextMatch(index)),
    };
}

kern_return_t IsochSnoopContext::Configure(uint8_t channel, uint8_t contextIndex) {
    if (!hardware_ || !dmaMemory_) {
        return kIOReturnNotReady;
    }

    if (contextIndex >= 4) {
        return kIOReturnBadArgument;
    }

    contextIndex_ = contextIndex;
    channel_ = channel;
    registers_ = GetRegisters(contextIndex_);

    // No audio pipeline ConfigureFor48k() — snoop never decodes.
    return rxRing_.SetupRings(*dmaMemory_, kNumDescriptors, kMaxPacketSize);
}

// ============================================================================
// Runtime
// ============================================================================

kern_return_t IsochSnoopContext::Start() {
    if (GetState() != State::Stopped) {
        return kIOReturnInvalid;
    }

    if (!hardware_) {
        ASFW_LOG(Isoch, "❌ [Snoop] Start: hardware_ is null!");
        return kIOReturnNotReady;
    }

    // Passive channel match. Same programming as IsochReceiveContext::Start():
    // ContextMatch = 0xF0000000 | channel selects the isoch channel to receive.
    // This only configures the local OHCI IR DMA — it allocates nothing on the bus.
    const uint32_t contextMatch = 0xF0000000 | (channel_ & 0x3F);
    hardware_->Write(registers_.ContextMatch, contextMatch);

    const uint32_t cmdPtr = rxRing_.InitialCommandPtrWord();
    if (cmdPtr == 0) {
        ASFW_LOG(Isoch, "❌ [Snoop] Start: Invalid descriptor cmdPtr");
        return kIOReturnInternalError;
    }
    hardware_->Write(registers_.CommandPtr, cmdPtr);

    hardware_->Write(registers_.ContextControlClear, 0xFFFFFFFFu);
    // kRun (bit 15) | kWake (bit 12). Do NOT set kCycleMatchEnable — that would gate
    // reception on a cycle counter and yield zero packets.
    const uint32_t ctlValue = Driver::ContextControl::kRun | Driver::ContextControl::kWake;
    hardware_->Write(registers_.ContextControlSet, ctlValue);

    const uint32_t contextMask = 1u << contextIndex_;
    hardware_->Write(ASFW::Driver::Register32::kIsoRecvIntMaskSet, contextMask);

    const uint32_t readMatch = hardware_->Read(registers_.ContextMatch);
    const uint32_t readCmd   = hardware_->Read(registers_.CommandPtr);
    const uint32_t readCtl   = hardware_->Read(registers_.ContextControlSet);

    ASFW_LOG(Isoch, "[Snoop] Start ch=%u ctx=%u: Match=0x%08x Cmd=0x%08x Ctl=0x%08x (readback Match=0x%08x Cmd=0x%08x Ctl=0x%08x)",
             channel_, contextIndex_, contextMatch, cmdPtr, ctlValue, readMatch, readCmd, readCtl);

    if ((readCtl & Driver::ContextControl::kDead) != 0) {
        ASFW_LOG(Isoch, "❌ [Snoop] Start: Context is DEAD! Check descriptor program.");
        return kIOReturnNotPermitted;
    }

    while (rxLock_.test_and_set(std::memory_order_acquire)) {
    }

    state_.store(State::Running, std::memory_order_release);
    rxRing_.ResetForStart();

    rxLock_.clear(std::memory_order_release);

    ASFW_LOG(Isoch, "[Snoop] ✅ Listening on isoch channel %u", channel_);
    return kIOReturnSuccess;
}

void IsochSnoopContext::Stop() {
    while (rxLock_.test_and_set(std::memory_order_acquire)) {
    }

    if (GetState() == State::Stopped) {
        rxLock_.clear(std::memory_order_release);
        return;
    }

    if (hardware_) {
        hardware_->Write(registers_.ContextControlClear, Driver::ContextControl::kRun);
        const uint32_t contextMask = 1u << contextIndex_;
        hardware_->Write(ASFW::Driver::Register32::kIsoRecvIntMaskClear, contextMask);
        ASFW_LOG(Isoch, "[Snoop] Stop: Disabled IR interrupt for context %u", contextIndex_);
    }

    state_.store(State::Stopped, std::memory_order_release);

    rxLock_.clear(std::memory_order_release);
}

uint32_t IsochSnoopContext::Poll() {
    const uint32_t raw = ++rawPollCount_;
    if (raw == 1 || raw == 10 || raw == 100 || (raw % 500 == 0)) {
        ASFW_LOG(Isoch, "[Snoop] Poll raw#%u ctx=%u ch=%u state=%d",
                 raw, contextIndex_, channel_, static_cast<int>(GetState()));
    }

    if (rxLock_.test_and_set(std::memory_order_acquire)) {
        return 0;
    }

    if (GetState() != State::Running) {
        rxLock_.clear(std::memory_order_release);
        return 0;
    }

    const uint32_t processed = rxRing_.DrainCompleted(*dmaMemory_, [this](const Rx::IsochRxDmaRing::CompletedPacket& pkt) {
        if (!pkt.payload || pkt.actualLength == 0) {
            return;
        }
        const uint32_t seq = ++packetSeq_;
        // Throttle: full hex for the first few packets, then 1-in-2000 (~once every
        // ~250 ms at 8 kHz). Avoids flooding the unified log while still sampling.
        if (seq <= 8 || (seq % 2000 == 0)) {
            HexDumpPacket(pkt.payload, pkt.actualLength, pkt.descriptorIndex, seq);
        }
    });

    totalProcessedSinceLast_ += processed;
    ++pollCount_;

    if (pollCount_ >= 100) {
        ASFW_LOG(Isoch, "[Snoop] Poll[%u] ch=%u: %u pkts in last 100 polls (total seq=%u)",
                 contextIndex_, channel_, totalProcessedSinceLast_, packetSeq_);
        if (totalProcessedSinceLast_ == 0) {
            LogHardwareState();
        }
        pollCount_ = 0;
        totalProcessedSinceLast_ = 0;
    }

    rxLock_.clear(std::memory_order_release);
    return processed;
}

// ============================================================================
// Diagnostics
// ============================================================================

void IsochSnoopContext::HexDumpPacket(const uint8_t* payload, uint16_t length,
                                      uint32_t descIndex, uint32_t seq) {
    // Manual nibble->ASCII conversion (no snprintf dependency in driver code).
    // Layout: two hex chars per byte, a space every quadlet (4 bytes).
    static constexpr char kDigits[] = "0123456789abcdef";
    char hex[kHexDumpBytes * 2 + kHexDumpBytes / 4 + 4];

    const size_t n = (length < kHexDumpBytes) ? static_cast<size_t>(length) : kHexDumpBytes;
    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
        hex[j++] = kDigits[(payload[i] >> 4) & 0x0F];
        hex[j++] = kDigits[payload[i] & 0x0F];
        if ((i & 0x3) == 0x3 && (j + 1) < sizeof(hex)) {
            hex[j++] = ' ';
        }
    }
    hex[j] = '\0';

    ASFW_LOG(Isoch, "[Snoop] pkt#%u desc=%u len=%u: %{public}s", seq, descIndex, length, hex);
}

void IsochSnoopContext::LogHardwareState() {
    if (!hardware_) return;

    const uint32_t ctl = hardware_->Read(registers_.ContextControlSet);
    const uint32_t cmd = hardware_->Read(registers_.CommandPtr);

    const bool running = (ctl & Driver::ContextControl::kRun)    != 0;
    const bool active  = (ctl & Driver::ContextControl::kActive) != 0;
    const bool dead    = (ctl & Driver::ContextControl::kDead)   != 0;
    const uint8_t evt  = static_cast<uint8_t>(ctl & Driver::ContextControl::kEventCodeMask);

    ASFW_LOG(Isoch, "[Snoop] HW[%u] ch=%u: ctl=0x%08x run=%d active=%d dead=%d evt=0x%02x cmdPtr=0x%08x",
             contextIndex_, channel_, ctl,
             static_cast<int>(running), static_cast<int>(active), static_cast<int>(dead),
             evt, cmd);
}

} // namespace ASFW::Isoch
