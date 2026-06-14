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
        // Log first 50 packets fully (covers ~8 ms of El Cap stream — enough for ground
        // truth), then 1-in-2000 (~once per 250 ms) to confirm ongoing reception.
        const bool doLog = (seq <= 50) || (seq % 2000 == 0);
        if (doLog) {
            HexDumpPacket(pkt.payload, pkt.actualLength, pkt.descriptorIndex, seq);
            // Structured slot parser for DATA packets (skip NO-DATA, which are <64 bytes).
            if (pkt.actualLength >= 64) {
                ParseAndLogBlock0(pkt.payload, pkt.actualLength, seq, pkt.xferStatus);
            }
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

void IsochSnoopContext::ParseAndLogBlock0(const uint8_t* payload, uint16_t length, uint32_t seq,
                                          uint16_t rxXferStatus) {
    // The captured payload buffer holds the bare CIP + data blocks — NO OHCI isoch
    // header is prepended (confirmed from the raw hex dump: payload[0..3] decodes as a
    // valid CIP Q0 with DBS=13). Buffer layout (big-endian wire values where noted):
    //   [0..3]   CIP Q0 (BE): [EOH:1][sid:6][DBS:8][fn:2][qpc:1][sph:1][rsv:2][dbc:8]
    //   [4..7]   CIP Q1 (BE): [EOH:1=1][FMT:6][FDF:8][SYT:16]
    //   [8..59]  First data block (52 bytes): SPH[4] + MSG×2[6] + slot[0..12][39]
    //            slot N PCM (3 bytes) at block byte 10 + N*3.
    if (length < 60) return;  // should not happen (caller guards), but be safe

    const uint8_t* cip = payload;      // CIP starts at byte 0 (no OHCI header in buffer)
    const uint8_t dbs     = cip[1];                                     // DBS = CIP Q0 bits[23:16]
    const uint8_t cipB2   = cip[2];                                     // fn/qpc/sph nibble
    const uint8_t dbc     = cip[3];                                     // DBC = CIP Q0 bits[7:0]
    const uint8_t fmt     = cip[4] & 0x3F;                             // FMT = CIP Q1 bits[29:24]
    const uint8_t fdf     = cip[5];                                     // FDF = CIP Q1 bits[23:16]
    const uint16_t syt    = (uint16_t(cip[6]) << 8) | cip[7];         // SYT = CIP Q1 bits[15:0]

    const uint8_t* blk = payload + 8;   // first data block (after 2-quadlet CIP)

    const uint32_t sph  = (uint32_t(blk[0]) << 24) | (uint32_t(blk[1]) << 16) |
                          (uint32_t(blk[2]) << 8)  | blk[3];
    const uint32_t msg0 = (uint32_t(blk[4]) << 16) | (uint32_t(blk[5]) << 8) | blk[6];
    const uint32_t msg1 = (uint32_t(blk[7]) << 16) | (uint32_t(blk[8]) << 8) | blk[9];

    uint32_t s[13];
    for (int n = 0; n < 13; ++n) {
        const uint8_t* p = blk + 10 + n * 3;
        s[n] = (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
    }

    // === SPH presentation-ahead vs the cycle the packet was actually transmitted ===
    // The SPH (MOTU format) encodes the presentation time: bits[24:12] = cycleCount,
    // bits[11:0] = cycleOffset (1/3072 of a cycle). The OHCI IR descriptor records the
    // RECEIVE timestamp in its status word (xferStatus): bits[12:0] = cycleCount at the
    // cycle the packet arrived. On a shared FireWire bus, receive cycle == transmit
    // cycle, so (sphCycle - rxCycle) mod 8000 is exactly how far ahead El Capitan stamps
    // its presentation time — the number we need to match (ours currently = aheadCyc=-2).
    // Cross-check: ReadCycleTime() sampled now (has poll latency, so a few cycles later).
    const uint32_t sphCyc = (sph >> 12) & 0x1FFF;
    const uint32_t sphOff = sph & 0x0FFF;
    const uint32_t rxCyc  = static_cast<uint32_t>(rxXferStatus) & 0x1FFF;
    int aheadRx = static_cast<int>(sphCyc) - static_cast<int>(rxCyc);
    if (aheadRx < -4000) aheadRx += 8000;
    if (aheadRx >  4000) aheadRx -= 8000;
    const uint32_t ct     = hardware_ ? hardware_->ReadCycleTime() : 0u;
    const uint32_t hwCyc  = (ct >> 12) & 0x1FFF;
    int aheadHw = static_cast<int>(sphCyc) - static_cast<int>(hwCyc);
    if (aheadHw < -4000) aheadHw += 8000;
    if (aheadHw >  4000) aheadHw -= 8000;

    ASFW_LOG(Isoch, "[Snoop] pkt#%u len=%u DBS=%u FMT=0x%02x FDF=0x%02x SYT=0x%04x DBC=%u b2=0x%02x",
             seq, length, dbs, fmt, fdf, syt, dbc, cipB2);
    ASFW_LOG(Isoch, "[Snoop] pkt#%u sph=0x%08x sphCyc=%u sphOff=%u | rxCyc=%u aheadRx=%d | hwCyc=%u aheadHw=%d (xfer=0x%04x)",
             seq, sph, sphCyc, sphOff, rxCyc, aheadRx, hwCyc, aheadHw, rxXferStatus);
    ASFW_LOG(Isoch, "[Snoop] pkt#%u msg=[0x%06x 0x%06x]", seq, msg0, msg1);
    ASFW_LOG(Isoch, "[Snoop] pkt#%u s0=%06x s1=%06x s2=%06x s3=%06x s4=%06x s5=%06x s6=%06x",
             seq, s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
    ASFW_LOG(Isoch, "[Snoop] pkt#%u s7=%06x s8=%06x s9=%06x s10=%06x s11=%06x s12=%06x",
             seq, s[7], s[8], s[9], s[10], s[11], s[12]);
}

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
