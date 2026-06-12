// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2026 ASFireWire Project
//
// MOTUAudioBackend.hpp
// MOTU V3 proprietary register protocol backend — NO AV/C, NO CMP, NO FCP.
// Reference: Linux kernel sound/firewire/motu/motu-protocol-v3.c
//            Linux kernel sound/firewire/motu/motu-stream.c

#pragma once

#include "IAudioBackend.hpp"
#include "../AudioNubPublisher.hpp"
#include "../../Async/Interfaces/IFireWireBusOps.hpp"
#include "../../Async/AsyncTypes.hpp"
#include "../../Common/FWCommon.hpp"
#include "../../Discovery/DeviceRegistry.hpp"
#include "../../Hardware/HardwareInterface.hpp"
#include "../../IRM/IRMClient.hpp"
#include "../../IRM/IRMTypes.hpp"
#include "../../Isoch/IsochService.hpp"

#include <atomic>
#include <cstdint>
#include <unordered_map>

namespace ASFW::Audio {

/// Audio backend for MOTU V3 devices (828mk3, 896mk3, TravelerMk3, UltraLiteMk3, etc.)
///
/// MOTU 828 MK3 uses a proprietary register protocol over async quadlet read/write
/// transactions — it does NOT implement AV/C or FCP despite declaring AV/C units
/// in its Config ROM. The Linux kernel snd-firewire-motu driver confirms this.
///
/// Streaming start sequence (mirrors Linux begin_session + switch_fetching_mode):
///   1. IRM: AllocateResources(irChannel, itChannel, bandwidth)
///   2. Write PACKET_FORMAT (0x0b10) — TX speed + differed-data flags
///   3. Start IR + IT OHCI DMA contexts
///   4. Read-modify-write ISOC_COMM_CONTROL (0x0b00) — set channels, activate
///   5. Read-modify-write CLOCK_STATUS (0x0b14) — set V3_FETCH_PCM_FRAMES
class MOTUAudioBackend final : public IAudioBackend {
public:
    MOTUAudioBackend(AudioNubPublisher& publisher,
                     Discovery::DeviceRegistry& registry,
                     Driver::IsochService& isoch,
                     Driver::HardwareInterface& hardware) noexcept;
    ~MOTUAudioBackend() noexcept override;

    MOTUAudioBackend(const MOTUAudioBackend&) = delete;
    MOTUAudioBackend& operator=(const MOTUAudioBackend&) = delete;

    [[nodiscard]] const char* Name() const noexcept override { return "MOTU-V3"; }

    void SetBusOps(Async::IFireWireBusOps* busOps) noexcept { busOps_ = busOps; }
    void SetIRMClient(IRM::IRMClient* client) noexcept { irmClient_ = client; }

    void OnAudioConfigurationReady(uint64_t guid, const Model::ASFWAudioDevice& config) noexcept;
    void OnDeviceRemoved(uint64_t guid) noexcept;

    [[nodiscard]] IOReturn StartStreaming(uint64_t guid) noexcept override;
    [[nodiscard]] IOReturn StopStreaming(uint64_t guid) noexcept override;

    // Called from watchdog (~1ms). Checks ISOC_COMM_CONTROL every ~1000 ticks;
    // starts snoop as soon as another host activates its isoch stream on MOTU.
    void TickSnoopMonitor() noexcept;

private:
    // -------------------------------------------------------------------------
    // MOTU V3 register map (offsets from base 0xfffff0000000)
    // Source: Linux sound/firewire/motu/motu-stream.c + motu-protocol-v3.c
    // -------------------------------------------------------------------------
    static constexpr uint16_t kAddrHi = 0xffff;
    static constexpr uint32_t kAddrBase = 0xf0000000u;

    static constexpr uint32_t kIsocCtrlOff    = 0x0b00; // ISOC_COMM_CONTROL
    static constexpr uint32_t kPacketFmtOff   = 0x0b10; // PACKET_FORMAT + TX speed
    static constexpr uint32_t kClockStatusOff = 0x0b14; // Clock rate/source + FETCH flag

    // ISOC_COMM_CONTROL bit layout (Linux: ISOC_COMM_CONTROL_MASK et al.)
    static constexpr uint32_t kIsocMask          = 0xffff0000u;
    static constexpr uint32_t kChangeRxIsocState = 0x80000000u;
    static constexpr uint32_t kRxIsocActivated   = 0x40000000u;
    static constexpr uint8_t  kRxChannelShift    = 24;
    static constexpr uint32_t kChangeTxIsocState = 0x00800000u;
    static constexpr uint32_t kTxIsocActivated   = 0x00400000u;
    static constexpr uint8_t  kTxChannelShift    = 16;

    // PACKET_FORMAT bit layout
    static constexpr uint32_t kTxExcludeDiffered = 0x00000080u;
    static constexpr uint32_t kRxExcludeDiffered = 0x00000040u;
    static constexpr uint32_t kTxSpeedMask       = 0x0000000fu;

    // CLOCK_STATUS bit layout (V3)
    // kFetchPCMFrames confirmed by MOTU kext data table (Box828mk3 format word[1]).
    // kClockRateMask: kext uses andl $0x700 — rate code is exactly bits[10:8].
    static constexpr uint32_t kFetchPCMFrames    = 0x02000000u;
    static constexpr uint32_t kClockRateMask     = 0x00000700u;
    static constexpr uint8_t  kClockRateShift    = 8;

    // Speed codes used in PACKET_FORMAT (match IEEE 1394 / OHCI speed field)
    static constexpr uint8_t kSpeedS400 = 0x2;

    // IRM bandwidth: candidate channels
    static constexpr uint8_t kCandidateIrChannel = 0;
    static constexpr uint8_t kCandidateItChannel = 1;
    static constexpr uint32_t kIRMTimeoutMs = 500;
    static constexpr uint32_t kRegTimeoutMs = 200;

    // MOTU V3 stream format at 48kHz — DBS overrides for StreamProcessor.
    // MOTU's CIP DBS field is a cycling counter, not the true block size.
    //
    // IT (host→device): 18+6 PCM chunks from Linux README = DBS=21.
    //   DBS = 1 + DIV_ROUND_UP(26*3, 4) = 21. Used when we transmit to MOTU.
    // IR (device→host): 18 fixed PCM chunks (tx_fixed_pcm_chunks[0]=18 per Linux spec).
    //   DBS = 1 + DIV_ROUND_UP((2+18)*3, 4) = 16. Verified: 8 events × 64 B = 512 B payload.
    //   (OHCI headerQuadlets=0 → kIsochHeaderSize=0 → len=520 = 8 CIP + 512 payload)
    static constexpr uint8_t kMOTUV3WireDbs48k    = 21;  // IT direction
    static constexpr uint8_t kMOTUV3WireDbs48k_IR = 16;  // IR direction (MOTU→host)

    [[nodiscard]] static Async::FWAddress MakeAddr(uint32_t offset) noexcept;

    // Blocking-style helpers (poll with IOSleep, same pattern as AVCAudioBackend::WaitForCMP).
    [[nodiscard]] bool ReadRegister(FW::NodeId nodeId, FW::Generation gen,
                                    uint32_t offset, uint32_t& outValue,
                                    uint32_t timeoutMs = kRegTimeoutMs) noexcept;
    [[nodiscard]] bool WriteRegister(FW::NodeId nodeId, FW::Generation gen,
                                     uint32_t offset, uint32_t value,
                                     uint32_t timeoutMs = kRegTimeoutMs) noexcept;

    [[nodiscard]] bool WaitForIRM(std::atomic<bool>& done,
                                  std::atomic<IRM::AllocationStatus>& status,
                                  uint32_t timeoutMs) noexcept;

    [[nodiscard]] IOReturn AllocateIRMResources(uint8_t irChannel, uint8_t itChannel,
                                                uint32_t irBandwidth,
                                                uint32_t itBandwidth) noexcept;
    void ReleaseIRMResources() noexcept;

    // Reads ISOC_COMM_CONTROL; if another host is already streaming (kRxIsocActivated),
    // extracts its IT channel (bits [29:24]) and starts the passive snoop on that channel.
    void TryStartSnoop(FW::NodeId nodeId, FW::Generation gen) noexcept;

    // Pending snoop target — set at OnAudioConfigurationReady, cleared when snoop starts.
    // GUID stored instead of nodeId/gen so TickSnoopMonitor can look up fresh values after bus resets.
    uint64_t          pendingSnoopGuid_{0};
    bool              pendingSnoopValid_{false};
    uint32_t          snoopTickCount_{0};
    // Guards against issuing a second async ISOC_COMM_CONTROL read while one is
    // still pending. The snoop read MUST be async — it runs from the watchdog
    // tick, and the AR completion is delivered on the same serial queue, so a
    // blocking IOSleep here would deadlock (completion can never be processed).
    std::atomic<bool> snoopReadInflight_{false};

    AudioNubPublisher&         publisher_;
    Discovery::DeviceRegistry& registry_;
    Driver::IsochService&      isoch_;
    Driver::HardwareInterface& hardware_;

    Async::IFireWireBusOps* busOps_{nullptr};
    IRM::IRMClient*         irmClient_{nullptr};

    struct AllocatedResources {
        uint8_t  irChannel{0xFF};
        uint8_t  itChannel{0xFF};
        uint32_t irBandwidth{0};
        uint32_t itBandwidth{0};
        bool     valid{false};
    } allocated_{};

    IOLock* lock_{nullptr};
    std::unordered_map<uint64_t, Model::ASFWAudioDevice> configByGuid_{};
};

} // namespace ASFW::Audio
