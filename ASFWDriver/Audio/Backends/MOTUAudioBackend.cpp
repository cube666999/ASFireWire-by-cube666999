// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2026 ASFireWire Project

#include "MOTUAudioBackend.hpp"

#include "../../Common/DriverKitOwnership.hpp"
#include "../../Logging/Logging.hpp"
#include "../../Isoch/Encoding/PacketAssembler.hpp"

#include <DriverKit/IOLib.h>
#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <net.mrmidi.ASFW.ASFWDriver/ASFWAudioNub.h>

namespace ASFW::Audio {

namespace {

inline uint32_t ComputeBandwidth(uint32_t channelCount, uint32_t sampleRateHz) noexcept {
    const uint32_t bps = channelCount * sampleRateHz * 32u;
    return IRM::CalculateBandwidthUnits({bps, 400u, 10u});
}

} // namespace

MOTUAudioBackend::MOTUAudioBackend(AudioNubPublisher& publisher,
                                   Discovery::DeviceRegistry& registry,
                                   Driver::IsochService& isoch,
                                   Driver::HardwareInterface& hardware) noexcept
    : publisher_(publisher)
    , registry_(registry)
    , isoch_(isoch)
    , hardware_(hardware) {
    lock_ = IOLockAlloc();
    if (!lock_) {
        ASFW_LOG_ERROR(Audio, "MOTUAudioBackend: Failed to allocate lock");
    }
}

MOTUAudioBackend::~MOTUAudioBackend() noexcept {
    if (lock_) {
        IOLockFree(lock_);
        lock_ = nullptr;
    }
}

// static
Async::FWAddress MOTUAudioBackend::MakeAddr(uint32_t offset) noexcept {
    // Base: 0xfffff0000000 → hi=0xffff, lo=0xf0000000+offset
    return Async::FWAddress{Async::FWAddress::AddressParts{kAddrHi, kAddrBase + offset}};
}

void MOTUAudioBackend::OnAudioConfigurationReady(uint64_t guid,
                                                  const Model::ASFWAudioDevice& config) noexcept {
    if (guid == 0) return;
    if (lock_) {
        IOLockLock(lock_);
        configByGuid_[guid] = config;
        IOLockUnlock(lock_);
    }
    (void)publisher_.EnsureNub(guid, config, "MOTU-V3");
}

void MOTUAudioBackend::OnDeviceRemoved(uint64_t guid) noexcept {
    if (guid == 0) return;
    (void)StopStreaming(guid);
    publisher_.TerminateNub(guid, "MOTU-V3-Removed");
    if (lock_) {
        IOLockLock(lock_);
        configByGuid_.erase(guid);
        IOLockUnlock(lock_);
    }
}

bool MOTUAudioBackend::WaitForIRM(std::atomic<bool>& done,
                                   std::atomic<IRM::AllocationStatus>& status,
                                   uint32_t timeoutMs) noexcept {
    constexpr uint32_t kPollMs = 5;
    for (uint32_t waited = 0; waited < timeoutMs; waited += kPollMs) {
        if (done.load(std::memory_order_acquire)) {
            return status.load(std::memory_order_acquire) == IRM::AllocationStatus::Success;
        }
        IOSleep(kPollMs);
    }
    return false;
}

bool MOTUAudioBackend::ReadRegister(FW::NodeId nodeId, FW::Generation gen,
                                     uint32_t offset, uint32_t& outValue,
                                     uint32_t timeoutMs) noexcept {
    if (!busOps_) return false;

    std::atomic<bool> done{false};
    std::atomic<uint32_t> value{0};
    std::atomic<bool> ok{false};

    busOps_->ReadQuad(gen, nodeId, MakeAddr(offset), FW::FwSpeed::S400,
        [&done, &value, &ok](Async::AsyncStatus status, std::span<const uint8_t> payload) {
            if (status == Async::AsyncStatus::kSuccess && payload.size() == 4) {
                const uint32_t v = (static_cast<uint32_t>(payload[0]) << 24) |
                                   (static_cast<uint32_t>(payload[1]) << 16) |
                                   (static_cast<uint32_t>(payload[2]) << 8)  |
                                    static_cast<uint32_t>(payload[3]);
                value.store(v, std::memory_order_release);
                ok.store(true, std::memory_order_release);
            }
            done.store(true, std::memory_order_release);
        });

    constexpr uint32_t kPollMs = 5;
    for (uint32_t waited = 0; waited < timeoutMs; waited += kPollMs) {
        if (done.load(std::memory_order_acquire)) break;
        IOSleep(kPollMs);
    }

    if (!ok.load(std::memory_order_acquire)) return false;
    outValue = value.load(std::memory_order_acquire);
    return true;
}

bool MOTUAudioBackend::WriteRegister(FW::NodeId nodeId, FW::Generation gen,
                                      uint32_t offset, uint32_t value,
                                      uint32_t timeoutMs) noexcept {
    if (!busOps_) return false;

    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};

    busOps_->WriteQuad(gen, nodeId, MakeAddr(offset), value, FW::FwSpeed::S400,
        [&done, &ok](Async::AsyncStatus status, std::span<const uint8_t>) {
            ok.store(status == Async::AsyncStatus::kSuccess, std::memory_order_release);
            done.store(true, std::memory_order_release);
        });

    constexpr uint32_t kPollMs = 5;
    for (uint32_t waited = 0; waited < timeoutMs; waited += kPollMs) {
        if (done.load(std::memory_order_acquire)) break;
        IOSleep(kPollMs);
    }

    return done.load(std::memory_order_acquire) && ok.load(std::memory_order_acquire);
}

IOReturn MOTUAudioBackend::AllocateIRMResources(uint8_t irChannel, uint8_t itChannel,
                                                 uint32_t irBandwidth,
                                                 uint32_t itBandwidth) noexcept {
    {
        std::atomic<bool> done{false};
        std::atomic<IRM::AllocationStatus> status{IRM::AllocationStatus::Failed};
        irmClient_->AllocateResources(irChannel, irBandwidth,
            [&done, &status](IRM::AllocationStatus s) {
                status.store(s, std::memory_order_release);
                done.store(true, std::memory_order_release);
            });
        if (!WaitForIRM(done, status, kIRMTimeoutMs)) {
            ASFW_LOG_ERROR(Audio, "MOTUAudioBackend: IRM IR ch=%u failed", irChannel);
            return kIOReturnError;
        }
    }
    {
        std::atomic<bool> done{false};
        std::atomic<IRM::AllocationStatus> status{IRM::AllocationStatus::Failed};
        irmClient_->AllocateResources(itChannel, itBandwidth,
            [&done, &status](IRM::AllocationStatus s) {
                status.store(s, std::memory_order_release);
                done.store(true, std::memory_order_release);
            });
        if (!WaitForIRM(done, status, kIRMTimeoutMs)) {
            ASFW_LOG_ERROR(Audio, "MOTUAudioBackend: IRM IT ch=%u failed", itChannel);
            irmClient_->ReleaseResources(irChannel, irBandwidth, [](IRM::AllocationStatus) {});
            return kIOReturnError;
        }
    }
    allocated_ = {irChannel, itChannel, irBandwidth, itBandwidth, true};
    ASFW_LOG(Audio, "MOTUAudioBackend: IRM allocated IR ch=%u IT ch=%u (irBW=%u itBW=%u)",
             irChannel, itChannel, irBandwidth, itBandwidth);
    return kIOReturnSuccess;
}

void MOTUAudioBackend::ReleaseIRMResources() noexcept {
    if (!allocated_.valid || !irmClient_) {
        allocated_ = {};
        return;
    }
    irmClient_->ReleaseResources(allocated_.irChannel, allocated_.irBandwidth,
        [](IRM::AllocationStatus) {});
    irmClient_->ReleaseResources(allocated_.itChannel, allocated_.itBandwidth,
        [](IRM::AllocationStatus) {});
    ASFW_LOG(Audio, "MOTUAudioBackend: IRM released IR ch=%u IT ch=%u",
             allocated_.irChannel, allocated_.itChannel);
    allocated_ = {};
}

IOReturn MOTUAudioBackend::StartStreaming(uint64_t guid) noexcept {
    if (guid == 0) return kIOReturnBadArgument;

    if (!busOps_) {
        ASFW_LOG_ERROR(Audio, "MOTUAudioBackend: busOps not set");
        return kIOReturnNotReady;
    }

    Model::ASFWAudioDevice config{};
    bool hasConfig = false;
    if (lock_) {
        IOLockLock(lock_);
        auto it = configByGuid_.find(guid);
        if (it != configByGuid_.end()) {
            config = it->second;
            hasConfig = true;
        }
        IOLockUnlock(lock_);
    }
    if (!hasConfig) {
        ASFW_LOG(Audio, "MOTUAudioBackend: no config for GUID=0x%016llx", guid);
        return kIOReturnNotReady;
    }

    const auto* record = registry_.FindByGuid(guid);
    if (!record) {
        ASFW_LOG(Audio, "MOTUAudioBackend: no device record for GUID=0x%016llx", guid);
        return kIOReturnNotReady;
    }

    const FW::NodeId  nodeId{static_cast<uint8_t>(record->nodeId)};
    const FW::Generation gen = record->gen;

    // Step 1 — Read CLOCK_STATUS: log current sample rate reported by MOTU.
    {
        uint32_t clockStatus = 0;
        if (ReadRegister(nodeId, gen, kClockStatusOff, clockStatus)) {
            const uint8_t rateCode = static_cast<uint8_t>(
                (clockStatus & kClockRateMask) >> kClockRateShift);
            ASFW_LOG(Audio,
                     "MOTUAudioBackend: CLOCK_STATUS=0x%08x rateCode=0x%02x GUID=0x%016llx",
                     clockStatus, rateCode, guid);
        } else {
            ASFW_LOG_WARNING(Audio,
                             "MOTUAudioBackend: CLOCK_STATUS read failed GUID=0x%016llx", guid);
        }
    }

    // Step 2 — IRM: allocate isochronous channels and bandwidth.
    const uint32_t sampleRateHz = config.currentSampleRate > 0
                                      ? static_cast<uint32_t>(config.currentSampleRate)
                                      : 48000u;
    const uint32_t irBW = ComputeBandwidth(config.inputChannelCount,  sampleRateHz);
    const uint32_t itBW = ComputeBandwidth(config.outputChannelCount, sampleRateHz);

    if (irmClient_) {
        const IOReturn kr = AllocateIRMResources(kCandidateIrChannel, kCandidateItChannel,
                                                 irBW, itBW);
        if (kr != kIOReturnSuccess) return kr;
    } else {
        ASFW_LOG_WARNING(Audio, "MOTUAudioBackend: no IRMClient — using ch=%u/%u without reservation",
                         kCandidateIrChannel, kCandidateItChannel);
        allocated_ = {kCandidateIrChannel, kCandidateItChannel, 0u, 0u, false};
    }

    const uint8_t irChannel = allocated_.irChannel;
    const uint8_t itChannel = allocated_.itChannel;

    // Step 3 — Write PACKET_FORMAT: TX speed S400 + exclude differed data chunks.
    {
        const uint32_t pktFmt = kTxExcludeDiffered | kRxExcludeDiffered | kSpeedS400;
        if (!WriteRegister(nodeId, gen, kPacketFmtOff, pktFmt)) {
            ASFW_LOG_ERROR(Audio, "MOTUAudioBackend: PACKET_FORMAT write failed");
            ReleaseIRMResources();
            return kIOReturnError;
        }
        ASFW_LOG(Audio, "MOTUAudioBackend: PACKET_FORMAT=0x%08x written", pktFmt);
    }

    // Step 4 — Publish nub and ensure queues exist.
    auto* nub = publisher_.GetNub(guid);
    if (!nub) {
        (void)publisher_.EnsureNub(guid, config, "MOTU-V3-Start");
        nub = publisher_.GetNub(guid);
        if (!nub) {
            ReleaseIRMResources();
            return kIOReturnNotReady;
        }
    }
    nub->EnsureRxQueueCreated();

    IOBufferMemoryDescriptor* rxMemRaw = nullptr;
    uint64_t rxBytes = 0;
    const kern_return_t rxCopy = nub->CopyRxQueueMemory(&rxMemRaw, &rxBytes);
    auto rxMem = Common::AdoptRetained(rxMemRaw);
    if (rxCopy != kIOReturnSuccess || !rxMem || rxBytes == 0) {
        ReleaseIRMResources();
        return (rxCopy == kIOReturnSuccess) ? kIOReturnNoMemory : rxCopy;
    }

    // Step 5 — Start IR DMA (capture from MOTU).
    {
        // MOTU V3 IR: CIP DBS field is a cycling counter, not true block size.
        // Override BEFORE StartReceive so the first packet is processed with correct stride.
        // IR block: SPH(4B) + msg(6B) + 18ch×3B = 64B = 16 quadlets.
        // Verified: OHCI headerQuadlets=0, so len=520 = 8B CIP + 512B payload = 8 events × 64B.
        isoch_.SetRxOverrideWireDbs(kMOTUV3WireDbs48k_IR);

        const kern_return_t kr = isoch_.StartReceive(irChannel, hardware_, rxMem, rxBytes);
        if (kr != kIOReturnSuccess) {
            ASFW_LOG_ERROR(Audio, "MOTUAudioBackend: StartReceive failed kr=0x%x", kr);
            ReleaseIRMResources();
            return kr;
        }
    }

    // Step 6 — ISOC_COMM_CONTROL: activate both RX and TX channels on MOTU.
    // ⚠️ MUST happen BEFORE StartTransmit. StartTransmit blocks waiting for the IR SYT
    // clock. MOTU will not send any IR packets until this register is written — it tells
    // MOTU which isochronous channels to use.
    // Linux equivalent: begin_session() called before start_streams().
    //
    // Channel mapping is MOTU-centric (device perspective, opposite of host perspective):
    //   bits [29:24]  kRxChannelShift=24  MOTU "RX" = host→device = itChannel (IT)
    //   bits [21:16]  kTxChannelShift=16  MOTU "TX" = device→host = irChannel (IR)
    // MOTU may return non-zero lower-word state from read (observed: 0x3000 in idle,
    // 0x1900 in stale-streaming state); kIsocMask clears only the bits we control.
    //
    // ⚠️ Two-step approach: first send deactivate, then activate.
    // If MOTU is in a stale streaming state from a previous session, a direct activate
    // write may be ignored. A prior deactivate (Change bits set, Activated bits CLEAR)
    // forces MOTU through a known "stopped" transition before the activate write.
    // This handles the case where CLOCK_STATUS already had FETCH_PCM_FRAMES set on entry.
    {
        uint32_t lowerBits = 0;
        (void)ReadRegister(nodeId, gen, kIsocCtrlOff, lowerBits);
        lowerBits &= ~kIsocMask; // keep only MOTU's lower-word status bits

        // 6a — Deactivate: tell MOTU to stop both channels (Change=1, Activated=0).
        const uint32_t deactCtrl = lowerBits | kChangeRxIsocState | kChangeTxIsocState;
        (void)WriteRegister(nodeId, gen, kIsocCtrlOff, deactCtrl);
        ASFW_LOG(Audio, "MOTUAudioBackend: ISOC_COMM_CONTROL deactivate=0x%08x", deactCtrl);
        IOSleep(20); // allow MOTU state machine to settle

        // 6b — Activate: set channels and activate both TX and RX.
        const uint32_t actCtrl = lowerBits |
                kChangeRxIsocState | kRxIsocActivated |
                (static_cast<uint32_t>(itChannel) << kRxChannelShift) |
                kChangeTxIsocState | kTxIsocActivated |
                (static_cast<uint32_t>(irChannel) << kTxChannelShift);

        if (!WriteRegister(nodeId, gen, kIsocCtrlOff, actCtrl)) {
            ASFW_LOG_ERROR(Audio, "MOTUAudioBackend: ISOC_COMM_CONTROL write failed");
            (void)isoch_.StopReceive();
            ReleaseIRMResources();
            return kIOReturnError;
        }
        ASFW_LOG(Audio, "MOTUAudioBackend: ISOC_COMM_CONTROL activate=0x%08x (irCh=%u itCh=%u)",
                 actCtrl, irChannel, itChannel);
    }

    // Step 7 — CLOCK_STATUS: set V3_FETCH_PCM_FRAMES to trigger MOTU to begin IR transmission.
    // ⚠️ MUST happen BEFORE StartTransmit. MOTU V3 will NOT send any IR isochronous packets
    // until BOTH ISOC_COMM_CONTROL (step 6) AND this FETCH_PCM_FRAMES bit are written.
    // This bit is the gate that causes MOTU to start sending; without it MOTU stays silent
    // regardless of ISOC_COMM_CONTROL. Linux equivalent: switch_fetching_mode() before start_streams().
    // Previous wrong ordering (FETCH_PCM_FRAMES after StartTransmit) caused a 500ms deadlock.
    {
        uint32_t clockStatus = 0;
        if (ReadRegister(nodeId, gen, kClockStatusOff, clockStatus)) {
            clockStatus |= kFetchPCMFrames;
            if (!WriteRegister(nodeId, gen, kClockStatusOff, clockStatus)) {
                ASFW_LOG_WARNING(Audio,
                    "MOTUAudioBackend: FETCH_PCM_FRAMES write failed (may still stream)");
            } else {
                ASFW_LOG(Audio, "MOTUAudioBackend: FETCH_PCM_FRAMES set (clockStatus=0x%08x)",
                         clockStatus);
            }
        } else {
            // CLOCK_STATUS read failed — attempt blind write of FETCH_PCM_FRAMES.
            ASFW_LOG_WARNING(Audio,
                "MOTUAudioBackend: CLOCK_STATUS read failed — blind write FETCH_PCM_FRAMES");
            (void)WriteRegister(nodeId, gen, kClockStatusOff, kFetchPCMFrames);
        }
    }

    // Step 8 — Start IT DMA (playback to MOTU).
    // MOTU is now sending IR packets (ISOC_COMM_CONTROL + FETCH_PCM_FRAMES written above),
    // so the IR SYT clock will be established and StartTransmit won't time out.
    {
        IOBufferMemoryDescriptor* txMemRaw = nullptr;
        uint64_t txBytes = 0;
        const kern_return_t txCopy = nub->CopyTransmitQueueMemory(&txMemRaw, &txBytes);
        auto txMem = Common::AdoptRetained(txMemRaw);
        if (txCopy != kIOReturnSuccess || !txMem || txBytes == 0) {
            (void)isoch_.StopReceive();
            // Deactivate MOTU isochronous channels — MOTU was told to start but IT DMA can't.
            uint32_t cs = 0;
            if (ReadRegister(nodeId, gen, kClockStatusOff, cs))
                (void)WriteRegister(nodeId, gen, kClockStatusOff, cs & ~kFetchPCMFrames);
            (void)WriteRegister(nodeId, gen, kIsocCtrlOff, kChangeRxIsocState | kChangeTxIsocState);
            ReleaseIRMResources();
            return (txCopy == kIOReturnSuccess) ? kIOReturnNoMemory : txCopy;
        }

        const uint8_t sid = static_cast<uint8_t>(hardware_.ReadNodeID() & 0x3Fu);
        const uint32_t streamModeRaw = static_cast<uint32_t>(config.streamMode);
        // Fix 29: Use MOTU V3 packet encoding per amdtp-motu.c.
        // MOTU V3 uses 3-byte packed PCM (not AM824 quadlets):
        //   Data block = SPH(4B) + msg×3B×2 + PCM×3B×N, padded to DBS quadlets
        // kMOTUV3WireDbs48k=21: DBS=21 matches observed wire packets (pcm_chunks=24).
        // skipSYTGate=true: MOTU V3 uses SPH for sync, never SYT (always syt=0x0000).
        const kern_return_t kr = isoch_.StartTransmit(
            itChannel, hardware_, sid, streamModeRaw,
            config.outputChannelCount, kMOTUV3WireDbs48k,
            txMem, txBytes, nullptr, 0, 0,
            /*skipSYTGate=*/true,
            /*encoding=*/Encoding::PacketEncoding::kMotuV3);
        if (kr != kIOReturnSuccess) {
            ASFW_LOG_ERROR(Audio, "MOTUAudioBackend: StartTransmit failed kr=0x%x", kr);
            (void)isoch_.StopReceive();
            // Clear FETCH_PCM_FRAMES and deactivate MOTU isochronous channels.
            uint32_t cs = 0;
            if (ReadRegister(nodeId, gen, kClockStatusOff, cs))
                (void)WriteRegister(nodeId, gen, kClockStatusOff, cs & ~kFetchPCMFrames);
            (void)WriteRegister(nodeId, gen, kIsocCtrlOff, kChangeRxIsocState | kChangeTxIsocState);
            ReleaseIRMResources();
            return kr;
        }
    }

    ASFW_LOG(Audio,
             "MOTUAudioBackend: Streaming started GUID=0x%016llx irCh=%u itCh=%u (in=%u out=%u)",
             guid, irChannel, itChannel,
             config.inputChannelCount, config.outputChannelCount);
    return kIOReturnSuccess;
}

IOReturn MOTUAudioBackend::StopStreaming(uint64_t guid) noexcept {
    if (guid == 0) return kIOReturnBadArgument;

    const auto* record = registry_.FindByGuid(guid);
    if (record && busOps_) {
        const FW::NodeId   nodeId{static_cast<uint8_t>(record->nodeId)};
        const FW::Generation gen = record->gen;

        // Clear FETCH_PCM_FRAMES first to stop PCM delivery.
        uint32_t clockStatus = 0;
        if (ReadRegister(nodeId, gen, kClockStatusOff, clockStatus)) {
            clockStatus &= ~kFetchPCMFrames;
            (void)WriteRegister(nodeId, gen, kClockStatusOff, clockStatus);
        }

        // Deactivate isoch channels on device.
        uint32_t ctrl = 0;
        if (ReadRegister(nodeId, gen, kIsocCtrlOff, ctrl)) {
            ctrl &= ~kIsocMask;
            ctrl |= kChangeRxIsocState | kChangeTxIsocState; // activate bits cleared = deactivate
            (void)WriteRegister(nodeId, gen, kIsocCtrlOff, ctrl);
        }
    }

    (void)isoch_.StopTransmit();
    (void)isoch_.StopReceive();
    ReleaseIRMResources();

    ASFW_LOG(Audio, "MOTUAudioBackend: Streaming stopped GUID=0x%016llx", guid);
    return kIOReturnSuccess;
}

} // namespace ASFW::Audio
