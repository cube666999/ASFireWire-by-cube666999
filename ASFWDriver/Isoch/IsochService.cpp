#include "IsochService.hpp"

#include <DriverKit/IOLib.h>
#include <atomic>
#include <utility>

#include "IsochReceiveContext.hpp"
#include "Transmit/IsochTransmitContext.hpp"
#include "Memory/IsochDMAMemoryManager.hpp"
#include "Config/AudioTxProfiles.hpp"
#include "Encoding/TimingUtils.hpp"
#include "../Common/DriverKitOwnership.hpp"
#include "../Hardware/HardwareInterface.hpp"
#include "../Logging/Logging.hpp"

namespace ASFW::Driver {

kern_return_t IsochService::StartReceive(uint8_t channel,
                                         HardwareInterface& hardware,
                                         OSSharedPtr<IOBufferMemoryDescriptor> rxQueueMemory,
                                         uint64_t rxQueueBytes) {
    if (isochReceiveContext_ &&
        isochReceiveContext_->GetState() == ASFW::Isoch::IRPolicy::State::Running) {
        ASFW_LOG(Controller, "[Isoch] IR already running; StartReceive is idempotent");
        return kIOReturnSuccess;
    }

    rxQueue_.Reset();

    void* rxQueueBase = nullptr;
    if (rxQueueMemory && rxQueueBytes > 0) {
        rxQueue_.memory = std::move(rxQueueMemory);
        rxQueue_.bytes = rxQueueBytes;

        const kern_return_t mappingStatus =
            Common::CreateSharedMapping(rxQueue_.memory, rxQueue_.map);
        if (mappingStatus != kIOReturnSuccess) {
            rxQueue_.Reset();
            return mappingStatus;
        }
        rxQueueBase = rxQueue_.BaseAddress();
    }

    if (!isochReceiveContext_) {
        ASFW::Isoch::Memory::IsochMemoryConfig config;
        config.numDescriptors = ASFW::Isoch::IsochReceiveContext::kNumDescriptors;
        config.packetSizeBytes = ASFW::Isoch::IsochReceiveContext::kMaxPacketSize;
        config.descriptorAlignment = 16;
        config.payloadPageAlignment = 16384;

        auto isochMem = ASFW::Isoch::Memory::IsochDMAMemoryManager::Create(config);
        if (!isochMem) {
            ASFW_LOG(Controller, "[Isoch] ❌ StartIsochReceive: Failed to create Memory Manager");
            rxQueue_.Reset();
            return kIOReturnNoMemory;
        }

        if (!isochMem->Initialize(hardware)) {
            ASFW_LOG(Controller, "[Isoch] ❌ StartIsochReceive: Failed to initialize DMA slabs");
            rxQueue_.Reset();
            return kIOReturnNoMemory;
        }

        isochReceiveContext_ = ASFW::Isoch::IsochReceiveContext::Create(&hardware, isochMem);

        if (!isochReceiveContext_) {
            ASFW_LOG(Controller, "[Isoch] ❌ StartIsochReceive: Context creation failed");
            rxQueue_.Reset();
            return kIOReturnNoMemory;
        }
        ASFW_LOG(Controller, "[Isoch] ✅ provisioned Isoch Context with Dedicated Memory");
    }

    isochReceiveContext_->SetExternalSyncBridge(&externalSyncBridge_);

    auto result = isochReceiveContext_->Configure(channel, 0);
    if (result != kIOReturnSuccess) {
        ASFW_LOG(Controller, "[Isoch] ❌ Failed to Configure IR Context: 0x%x", result);
        rxQueue_.Reset();
        return result;
    }

    isochReceiveContext_->SetSharedRxQueue(rxQueueBase, rxQueueBase ? rxQueueBytes : 0);

    result = isochReceiveContext_->Start();
    if (result != kIOReturnSuccess) {
        ASFW_LOG(Controller, "[Isoch] ❌ Failed to Start IR Context: 0x%x", result);
        isochReceiveContext_->SetSharedRxQueue(nullptr, 0);
        rxQueue_.Reset();
        return result;
    }

    ASFW_LOG(Controller, "[Isoch] ✅ Started IR Context 0 for Channel %u!", channel);

    return kIOReturnSuccess;
}

void IsochService::SetRxOverrideWireDbs(uint8_t dbs) noexcept {
    if (isochReceiveContext_) {
        isochReceiveContext_->SetOverrideWireDbs(dbs);
        ASFW_LOG(Controller, "[Isoch] IR override wire DBS set to %u", dbs);
    }
}

kern_return_t IsochService::StopReceive() {
    if (!isochReceiveContext_) {
        return kIOReturnNotReady;
    }

    isochReceiveContext_->Stop();
    isochReceiveContext_->SetSharedRxQueue(nullptr, 0);
    rxQueue_.Reset();
    ASFW_LOG(Controller, "[Isoch] Stopped IR Context 0");
    return kIOReturnSuccess;
}

kern_return_t IsochService::StartTransmit(uint8_t channel,
                                          HardwareInterface& hardware,
                                          uint8_t sid,
                                          uint32_t streamModeRaw,
                                          uint32_t pcmChannels,
                                          uint32_t am824Slots,
                                          OSSharedPtr<IOBufferMemoryDescriptor> txQueueMemory,
                                          uint64_t txQueueBytes,
                                          void* zeroCopyBase,
                                          uint64_t zeroCopyBytes,
                                          uint32_t zeroCopyFrames,
                                          bool skipSYTGate,
                                          Encoding::PacketEncoding encoding) {

    if (isochTransmitContext_ &&
        isochTransmitContext_->GetState() == ASFW::Isoch::ITState::Running) {
        ASFW_LOG(Controller, "[Isoch] IT already running; StartTransmit is idempotent");
        return kIOReturnSuccess;
    }

    txQueue_.Reset();

    void* txQueueBase = nullptr;
    if (txQueueMemory && txQueueBytes > 0) {
        txQueue_.memory = std::move(txQueueMemory);
        txQueue_.bytes = txQueueBytes;

        const kern_return_t mappingStatus =
            Common::CreateSharedMapping(txQueue_.memory, txQueue_.map);
        if (mappingStatus != kIOReturnSuccess) {
            txQueue_.Reset();
            return mappingStatus;
        }
        txQueueBase = txQueue_.BaseAddress();
    }

    if (!isochTransmitContext_) {
        ASFW::Isoch::Memory::IsochMemoryConfig config;
        config.numDescriptors = ASFW::Isoch::IsochTransmitContext::kRingBlocks;
        config.packetSizeBytes = ASFW::Isoch::IsochTransmitContext::kMaxPacketSize;
        config.descriptorAlignment = ASFW::Isoch::IsochTransmitContext::kOHCIPageSize;
        config.payloadPageAlignment = 16384;

        auto isochMem = ASFW::Isoch::Memory::IsochDMAMemoryManager::Create(config);
        if (!isochMem) {
            ASFW_LOG(Controller, "[Isoch] ❌ StartIsochTransmit: Failed to create Memory Manager");
            txQueue_.Reset();
            return kIOReturnNoMemory;
        }

        if (!isochMem->Initialize(hardware)) {
            ASFW_LOG(Controller, "[Isoch] ❌ StartIsochTransmit: Failed to initialize DMA slabs");
            txQueue_.Reset();
            return kIOReturnNoMemory;
        }

        isochTransmitContext_ = ASFW::Isoch::IsochTransmitContext::Create(&hardware, isochMem);

        if (!isochTransmitContext_) {
            ASFW_LOG(Controller, "[Isoch] ❌ StartIsochTransmit: Context creation failed");
            txQueue_.Reset();
            return kIOReturnNoMemory;
        }
        ASFW_LOG(Controller, "[Isoch] ✅ provisioned IT Context with Dedicated Memory");
    }

    uint32_t startTargetFill = ASFW::Isoch::Config::kTxBufferProfile.startWaitTargetFrames;
    isochTransmitContext_->SetSharedTxQueue(txQueueBase, txQueueBase ? txQueueBytes : 0);
    if (txQueueBase && txQueueBytes > 0) {
        ASFW_LOG(Controller, "[Isoch] Wired shared TX queue to IT context (bytes=%llu)",
                 txQueueBytes);
    }

    // Compute startTargetFill early — does not depend on assembler configuration.
    if (zeroCopyBase && zeroCopyBytes > 0 && zeroCopyFrames > 0) {
        uint32_t target = (zeroCopyFrames * 5) / 8;
        if (target < 8) target = 8;
        startTargetFill = target;
    }

    if (isochTransmitContext_->SharedTxCapacityFrames() == 0) {
        ASFW_LOG(Controller, "[Isoch] ❌ StartTransmit blocked: shared TX queue metadata missing");
        isochTransmitContext_->SetSharedTxQueue(nullptr, 0);
        txQueue_.Reset();
        return kIOReturnNotReady;
    }

    if (!isochReceiveContext_ ||
        isochReceiveContext_->GetState() != ASFW::Isoch::IRPolicy::State::Running) {
        ASFW_LOG(Controller, "[Isoch] ❌ StartTransmit blocked: IR context is not running");
        isochTransmitContext_->SetSharedTxQueue(nullptr, 0);
        txQueue_.Reset();
        return kIOReturnNotReady;
    }

    // ⚠️  SYT-gate moved to AFTER isochTransmitContext_->Start() (see below).
    // Waiting here creates a MOTU V3 deadlock:
    //   host waits for IR SYT  →  but MOTU V3 won't send IR until it receives IT packets
    //   →  IT DMA never starts (Start() is gated on SYT)  →  timeout every time.
    // Fix: start IT DMA first so MOTU sees IT packets and begins sending IR, then gate.

    isochTransmitContext_->SetExternalSyncBridge(&externalSyncBridge_);

    auto result = isochTransmitContext_->Configure(channel,
                                                   sid,
                                                   streamModeRaw,
                                                   pcmChannels,
                                                   am824Slots,
                                                   encoding);
    if (result != kIOReturnSuccess) {
        ASFW_LOG(Controller, "[Isoch] ❌ Failed to Configure IT Context: 0x%x", result);
        isochTransmitContext_->SetSharedTxQueue(nullptr, 0);
        txQueue_.Reset();
        return result;
    }

    // Fix 39: SetZeroCopyOutputBuffer MUST be called after Configure().
    // Configure() calls reconfigureAM824() which resets assembler_.zeroCopyEnabled_ = false.
    // If called before Configure(), the zero-copy source is silently cleared and the assembler
    // falls back to reading from the empty ring buffer → underruns + silence.
    if (zeroCopyBase && zeroCopyBytes > 0 && zeroCopyFrames > 0) {
        isochTransmitContext_->SetZeroCopyOutputBuffer(zeroCopyBase, zeroCopyBytes, zeroCopyFrames);
        ASFW_LOG(Controller,
                 "[Isoch] ✅ ZERO-COPY wired! AudioBuffer base=%p bytes=%llu frames=%u targetFill=%u",
                 zeroCopyBase,
                 zeroCopyBytes,
                 zeroCopyFrames,
                 startTargetFill);
    } else {
        isochTransmitContext_->SetZeroCopyOutputBuffer(nullptr, 0, 0);
    }

    const auto& txProfile = ASFW::Isoch::Config::kTxBufferProfile;
    ASFW_LOG(Controller,
             "[Isoch] IT TX profile=%{public}s startWait=%u startupPrimeLimit=%u legacy(target=%u max=%u chunks=%u)",
             txProfile.name,
             txProfile.startWaitTargetFrames,
             txProfile.startupPrimeLimitFrames,
             txProfile.legacyRbTargetFrames,
             txProfile.legacyRbMaxFrames,
             txProfile.legacyMaxChunksPerRefill);

    uint32_t fillLevel = 0;
    uint32_t targetFill = startTargetFill;
    const uint32_t queueCapacity = isochTransmitContext_->SharedTxCapacityFrames();
    if (queueCapacity > 0 && targetFill > queueCapacity) {
        ASFW_LOG(Controller, "[Isoch] IT start wait target clamped %u -> %u (queueCapacity)",
                 targetFill, queueCapacity);
        targetFill = queueCapacity;
    }
    const int maxWaitMs = 100;

    ASFW_LOG(Controller, "[Isoch] IT start wait targetFill=%u (zeroCopy=%{public}s)",
             targetFill, isochTransmitContext_->IsZeroCopyEnabled() ? "YES" : "NO");

    for (int waitMs = 0; waitMs < maxWaitMs; waitMs += 5) {
        fillLevel = isochTransmitContext_->SharedTxFillLevelFrames();
        if (fillLevel >= targetFill) {
            break;
        }
        IOSleep(5);
    }

    result = isochTransmitContext_->Start();
    if (result != kIOReturnSuccess) {
        ASFW_LOG(Controller, "[Isoch] Failed to Start IT Context: 0x%x", result);
        isochTransmitContext_->SetZeroCopyOutputBuffer(nullptr, 0, 0);
        isochTransmitContext_->SetSharedTxQueue(nullptr, 0);
        txQueue_.Reset();
        return result;
    }

    ASFW_LOG(Controller, "[Isoch] ✅ Started IT Context for Channel %u!", channel);

    // SYT gate: wait for the IR SYT clock to be established before declaring success.
    // This confirms the external device is actively transmitting isochronous data with
    // valid SYT timestamps.
    //
    // MOTU V3 exception (skipSYTGate=true): MOTU 828 MK3 ALWAYS sends syt=0x0000 in CIP
    // headers (it does not embed IEEE 1394 SYT timestamps). Linux snd-firewire-motu never
    // waits for SYT. The gate would time out unconditionally → streaming killed after 3s.
    // When skipSYTGate is set, we bypass the poll and rely on the caller to have already
    // verified that IR is running (StartReceive + ISOC_COMM_CONTROL + FETCH_PCM_FRAMES).
    if (skipSYTGate) {
        ASFW_LOG(Controller, "[Isoch] SYT gate bypassed (device uses syt=0x0000 — MOTU V3 mode)");
    } else {
        // Now that IT DMA is running, MOTU V3 will receive IT packets and begin sending IR.
        // Wait up to 3000 ms for the IR SYT clock to be established — this confirms MOTU is
        // actively transmitting isochronous data.
        // 500ms was too short: MOTU V3 needs time to lock its PLL to the isochronous bus
        // cycle and start its own isoch transmission after receiving the first IT packets.
        // Hardware tests showed IR cmdPtr never advanced in 500ms; extending to 3000ms gives
        // MOTU enough startup time without blocking the driver thread excessively.
        // (Previously this wait was before Start(), which prevented IT from ever starting and
        // caused a permanent 500ms timeout — that deadlock is gone since session 5.)
        constexpr uint32_t kSytGateTimeoutMs = 3000;
        constexpr uint32_t kSytGatePollMs = 5;
        bool sytClockEstablished = false;
        for (uint32_t waitedMs = 0; waitedMs < kSytGateTimeoutMs; waitedMs += kSytGatePollMs) {
            if (externalSyncBridge_.clockEstablished.load(std::memory_order_acquire)) {
                sytClockEstablished = true;
                break;
            }
            IOSleep(kSytGatePollMs);
        }
        if (!sytClockEstablished) {
            const uint32_t seq = externalSyncBridge_.updateSeq.load(std::memory_order_acquire);
            const uint32_t packed = externalSyncBridge_.lastPackedRx.load(std::memory_order_acquire);
            const uint16_t lastSyt = ASFW::Isoch::Core::ExternalSyncBridge::UnpackSYT(packed);
            const uint8_t lastFdf = ASFW::Isoch::Core::ExternalSyncBridge::UnpackFDF(packed);
            const uint8_t lastDbs = ASFW::Isoch::Core::ExternalSyncBridge::UnpackDBS(packed);
            const uint64_t lastTicks = externalSyncBridge_.lastUpdateHostTicks.load(std::memory_order_acquire);
            uint64_t ageMs = 0;
            if (lastTicks != 0) {
                const uint64_t nowTicks = mach_absolute_time();
                if (nowTicks >= lastTicks) {
                    ageMs = ASFW::Timing::hostTicksToNanos(nowTicks - lastTicks) / 1'000'000ULL;
                }
            }
            // Dump IR hardware state for diagnostics: cmdPtr static = MOTU not sending at all;
            // cmdPtr advanced but seq=0 = IR packets arriving but StreamProcessor not processing.
            if (isochReceiveContext_) {
                isochReceiveContext_->LogHardwareState();
            }
            ASFW_LOG(Controller,
                     "[Isoch] ❌ StartTransmit SYT timeout: IT is running but device not responding"
                     " (waited %ums seq=%u syt=0x%04x fdf=0x%02x dbs=%u ageMs=%llu active=%d established=%d)",
                     kSytGateTimeoutMs,
                     seq,
                     lastSyt,
                     lastFdf,
                     lastDbs,
                     ageMs,
                     externalSyncBridge_.active.load(std::memory_order_acquire),
                     externalSyncBridge_.clockEstablished.load(std::memory_order_acquire));
            isochTransmitContext_->Stop();
            isochTransmitContext_->SetZeroCopyOutputBuffer(nullptr, 0, 0);
            isochTransmitContext_->SetSharedTxQueue(nullptr, 0);
            txQueue_.Reset();
            return kIOReturnTimeout;
        }
    }

    return kIOReturnSuccess;
}

kern_return_t IsochService::StopTransmit() {
    if (!isochTransmitContext_) {
        return kIOReturnNotReady;
    }

    isochTransmitContext_->Stop();
    isochTransmitContext_->SetZeroCopyOutputBuffer(nullptr, 0, 0);
    isochTransmitContext_->SetSharedTxQueue(nullptr, 0);
    txQueue_.Reset();
    ASFW_LOG(Controller, "[Isoch] Stopped IT Context");
    return kIOReturnSuccess;
}

kern_return_t IsochService::StartDuplex(const IsochDuplexStartParams& params,
                                       HardwareInterface& hardware) {
    if (params.guid == 0) {
        return kIOReturnBadArgument;
    }

    if (activeGuid_ != 0 && activeGuid_ != params.guid) {
        // Transport layer is currently global. Control-plane enforces single-device too.
        return kIOReturnBusy;
    }

    const kern_return_t krRx = StartReceive(params.irChannel,
                                           hardware,
                                           params.rxQueueMemory,
                                           params.rxQueueBytes);
    if (krRx != kIOReturnSuccess) {
        return krRx;
    }

    const uint32_t streamModeRaw = static_cast<uint32_t>(params.streamMode);
    const kern_return_t krTx = StartTransmit(params.itChannel,
                                            hardware,
                                            params.sid,
                                            streamModeRaw,
                                            params.hostOutputPcmChannels,
                                            params.hostToDeviceAm824Slots,
                                            params.txQueueMemory,
                                            params.txQueueBytes,
                                            params.zeroCopyBase,
                                            params.zeroCopyBytes,
                                            params.zeroCopyFrames);
    if (krTx != kIOReturnSuccess) {
        StopReceive();
        return krTx;
    }

    activeGuid_ = params.guid;
    return kIOReturnSuccess;
}

kern_return_t IsochService::StopDuplex(uint64_t guid) {
    if (guid == 0) {
        return kIOReturnBadArgument;
    }

    if (activeGuid_ != 0 && activeGuid_ != guid) {
        return kIOReturnBusy;
    }

    (void)StopTransmit();
    (void)StopReceive();
    externalSyncBridge_.Reset();

    activeGuid_ = 0;
    return kIOReturnSuccess;
}

void IsochService::StopAll() {
    if (isochReceiveContext_) {
        isochReceiveContext_->Stop();
        isochReceiveContext_->SetSharedRxQueue(nullptr, 0);
        isochReceiveContext_.reset();
    }
    rxQueue_.Reset();
    if (isochTransmitContext_) {
        isochTransmitContext_->Stop();
        isochTransmitContext_->SetZeroCopyOutputBuffer(nullptr, 0, 0);
        isochTransmitContext_->SetSharedTxQueue(nullptr, 0);
        isochTransmitContext_.reset();
    }
    txQueue_.Reset();
    externalSyncBridge_.Reset();
    activeGuid_ = 0;
}

} // namespace ASFW::Driver
