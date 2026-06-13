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
    static constexpr uint32_t kRoutePortConfOff = 0x0c04; // ROUTE_PORT_CONF (ground-truth: official driver writes 0x100)

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

    // V3_OPT_IFACE_MODE register (0x0c94) — optical interface configuration.
    // Linux: motu-protocol-v3.c detect_packet_formats_with_opt_ifaces().
    // Determines how many optical PCM chunks to add to base TX/RX counts.
    static constexpr uint32_t kOptIfaceModeOff          = 0x0c94;
    static constexpr uint32_t kV3EnableOptInIfaceA      = 0x00000001u; // Optical IN A active  → +4 or +8 to IR PCM
    static constexpr uint32_t kV3EnableOptInIfaceB      = 0x00000002u; // Optical IN B active
    static constexpr uint32_t kV3UseOptInIfaceAAsAdat   = 0x00000004u; // IN A: ADAT=+8, else TOSLINK=+4
    static constexpr uint32_t kV3UseOptInIfaceBAsAdat   = 0x00000008u;
    static constexpr uint32_t kV3EnableOptOutIfaceA     = 0x00000100u; // Optical OUT A active → +4 or +8 to IT PCM
    static constexpr uint32_t kV3EnableOptOutIfaceB     = 0x00000200u;
    static constexpr uint32_t kV3UseOptOutIfaceAAsAdat  = 0x00000400u; // OUT A: ADAT=+8, else TOSLINK=+4
    static constexpr uint32_t kV3UseOptOutIfaceBAsAdat  = 0x00000800u;

    // MOTU 828 MK3 FW base PCM counts at 48kHz (Linux snd_motu_spec_828mk3_fw).
    static constexpr uint32_t k828Mk3BaseIrPcm = 18u; // tx_fixed_pcm_chunks[0] — IR direction
    static constexpr uint32_t k828Mk3BaseItPcm = 14u; // rx_fixed_pcm_chunks[0] — IT direction
    static constexpr uint32_t kV3MsgChunks     =  2u; // 2 MSG slots (MIDI) per audio block

    /// DBS = 1 + DIV_ROUND_UP((msg+pcm)*3, 4)  — from Linux amdtp-motu.c.
    static constexpr uint32_t ComputeV3Dbs(uint32_t pcm, uint32_t msg = kV3MsgChunks) noexcept {
        return 1u + ((msg + pcm) * 3u + 3u) / 4u;
    }

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
    // IT (host→device): Fix 66 — DBS=16 (was 21).
    //   Linux motu-protocol-v3.c: rx_fixed_pcm_chunks[0]=14 (base at 48k for 828mk3fw).
    //   Optical OUT via detect_packet_formats_with_opt_ifaces: +4 (TOSLINK A) = 18 PCM total.
    //   DBS = 1 + DIV_ROUND_UP((2 MSG + 18 PCM)×3, 4) = 1+15 = 16. Frame = 64 bytes =
    //   SPH(4) + MSG(6) + PCM(18×3=54). Stride matches IR direction (both DBS=16).
    //   ⚠️ MOTU 828 MK3 IGNORES the CIP DBS field and uses its own internal stride derived
    //   from V3_OPT_IFACE_MODE. DBS=21 → 84-byte frames; MOTU strides 64 bytes → "block 1"
    //   for MOTU starts at OUR byte 64 (20 bytes into our block 1) → our block 1's SPH bytes
    //   appear at MOTU's PCM channels 3-4 offset → phantom signal → flickering LEDs on
    //   random channels + intermittent squeal. DBS=13 was equally wrong (MOTU read 64 bytes
    //   from our 52-byte frame → SPH of next block as phantom ch14-23 → instant squeal).
    //   DBS=16 = exact stride match → no phantom channels.
    //   CoreAudio exposes stereo (outputChannelCount=2); PacketAssembler writes the 2 real PCM
    //   channels into slots 0,1 (Main L/R) and zero-pads slots 2-17 (18 total).
    // IR (device→host): 18 fixed PCM chunks (tx_fixed_pcm_chunks[0]=18 per Linux spec).
    //   DBS = 1 + DIV_ROUND_UP((2+18)*3, 4) = 16. Verified: 8 events × 64 B = 512 B payload.
    //   (OHCI headerQuadlets=0 → kIsochHeaderSize=0 → len=520 = 8 CIP + 512 payload)
    static constexpr uint8_t kMOTUV3WireDbs48k    = 13;  // IT direction wire DBS (Fix 69): 14 PCM + 2 MSG → 1+DIV_ROUND_UP(16×3,4)=13
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
