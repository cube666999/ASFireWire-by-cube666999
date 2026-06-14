// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2026 ASFireWire Project
//
// MOTUVendorProtocol.hpp - MOTU V3 vendor-specific protocol for DICE audio backend.
//
// MOTU 828 MK3 uses proprietary async register writes — no AV/C, no CMP, no FCP.
// Implements IDICEDuplexProtocol so the DiceAudioBackend's restart coordinator
// can drive the MOTU startup sequence just like a DICE device:
//   PrepareDuplex   → write PACKET_FORMAT (0x0b10)
//   ProgramRx       → write ISOC_COMM_CONTROL RX activation (0x0b00)
//   ProgramTx       → write ISOC_COMM_CONTROL TX + CLOCK_STATUS FETCH (0x0b14)
//   ConfirmStart    → stub (MOTU has no lock-status register)
//   StopDuplex      → clear ISOC_COMM_CONTROL + CLOCK_STATUS
//
// Reference: Linux sound/firewire/motu/motu-protocol-v3.c
//            Linux sound/firewire/motu/motu-stream.c

#pragma once

#include "../../IDeviceProtocol.hpp"
#include "../../AudioTypes.hpp"
#include "../../DICE/Core/IDICEDuplexProtocol.hpp"
#include "../../../../Async/AsyncTypes.hpp"
#include "../../../../Async/Interfaces/IFireWireBusOps.hpp"
#include "../../../../Async/Interfaces/IFireWireBusInfo.hpp"
#include "../../../../Common/FWCommon.hpp"
#include "../../../../Bus/IRM/IRMClient.hpp"

#include <atomic>
#include <cstdint>

namespace ASFW::Audio::Vendor {

class MOTUVendorProtocol final : public IDeviceProtocol,
                                  public DICE::IDICEDuplexProtocol {
public:
    MOTUVendorProtocol(Async::IFireWireBusOps& busOps,
                       Async::IFireWireBusInfo& busInfo,
                       uint16_t nodeId,
                       IRM::IRMClient* irmClient) noexcept;
    ~MOTUVendorProtocol() noexcept override = default;

    MOTUVendorProtocol(const MOTUVendorProtocol&) = delete;
    MOTUVendorProtocol& operator=(const MOTUVendorProtocol&) = delete;

    // ---- IDeviceProtocol ----
    IOReturn Initialize() override;
    IOReturn Shutdown() override;
    const char* GetName() const override { return "MOTU-828MK3-V3"; }
    bool GetRuntimeAudioStreamCaps(AudioStreamRuntimeCaps& outCaps) const override;
    DICE::IDICEDuplexProtocol* AsDiceDuplexProtocol() noexcept override { return this; }
    const DICE::IDICEDuplexProtocol* AsDiceDuplexProtocol() const noexcept override { return this; }
    void UpdateRuntimeContext(uint16_t nodeId, Protocols::AVC::FCPTransport*) override;
    IRM::IRMClient* GetIRMClient() const override { return irmClient_; }

    // ---- IDICEDuplexProtocol ----
    void PrepareDuplex(const AudioDuplexChannels& channels,
                       const DICE::DiceDesiredClockConfig& desiredClock,
                       PrepareCallback callback) override;
    void ProgramRx(StageCallback callback) override;
    void ProgramTxAndEnableDuplex(StageCallback callback) override;
    void ConfirmDuplexStart(ConfirmCallback callback) override;
    void ApplyClockConfig(const DICE::DiceDesiredClockConfig& desiredClock,
                          ClockApplyCallback callback) override;
    void ReadDuplexHealth(HealthCallback callback) override;
    IOReturn StopDuplex() override;

private:
    // MOTU register base (0xFFFF_F000_0000)
    static constexpr uint16_t kAddrHi    = 0xFFFFu;
    static constexpr uint32_t kAddrBase  = 0xF0000000u;

    // Register offsets
    static constexpr uint32_t kIsocCtrlOff    = 0x0b00u; // ISOC_COMM_CONTROL
    static constexpr uint32_t kPacketFmtOff   = 0x0b10u; // PACKET_FORMAT
    static constexpr uint32_t kClockStatusOff = 0x0b14u; // CLOCK_STATUS

    // ISOC_COMM_CONTROL bits — MOTU perspective: RX = host→MOTU (IT), TX = MOTU→host (IR)
    static constexpr uint32_t kIsocMask          = 0xFFFF0000u;
    static constexpr uint32_t kChangeRxIsocState = 0x80000000u;
    static constexpr uint32_t kRxIsocActivated   = 0x40000000u;
    static constexpr uint8_t  kRxChannelShift    = 24u; // hostToDevice channel
    static constexpr uint32_t kChangeTxIsocState = 0x00800000u;
    static constexpr uint32_t kTxIsocActivated   = 0x00400000u;
    static constexpr uint8_t  kTxChannelShift    = 16u; // deviceToHost channel

    // PACKET_FORMAT bits
    static constexpr uint32_t kTxExcludeDiffered = 0x00000080u;
    static constexpr uint32_t kRxExcludeDiffered = 0x00000040u;
    static constexpr uint8_t  kSpeedS400         = 0x2u;

    // CLOCK_STATUS bits (MOTU V3, ref Linux motu-protocol-v3.c)
    static constexpr uint32_t kFetchPCMFrames = 0x02000000u; // write-only command bit, not status
    static constexpr uint32_t kClockRateMask  = 0x0000FF00u; // V3_CLOCK_RATE_MASK
    static constexpr uint8_t  kClockRateShift = 8u;          // V3_CLOCK_RATE_SHIFT

    // MOTU 828 MK3 stream geometry at 48kHz
    // IT (host→MOTU): 14 PCM + 2 MSG = DBS 13
    // IR (MOTU→host): 18 PCM + 2 MSG = DBS 16
    static constexpr uint32_t kHostOutputPcm = 14u; // IT slots (playback)
    static constexpr uint32_t kHostInputPcm  = 18u; // IR slots (capture)
    static constexpr uint32_t kItDbs         = 13u;
    static constexpr uint32_t kIrDbs         = 16u;

    static constexpr uint32_t kRegTimeoutMs = 200u;

    [[nodiscard]] Async::FWAddress MakeAddr(uint32_t offset) const noexcept;
    [[nodiscard]] bool WriteReg(uint32_t offset, uint32_t value) noexcept;
    [[nodiscard]] bool ReadReg(uint32_t offset, uint32_t& outValue) noexcept;

    AudioDuplexChannels BuildDuplexChannels() const noexcept;
    AudioStreamRuntimeCaps    BuildRuntimeCaps()    const noexcept;

    Async::IFireWireBusOps& busOps_;
    Async::IFireWireBusInfo& busInfo_;
    uint16_t nodeId_;
    IRM::IRMClient* irmClient_;

    AudioDuplexChannels channels_{};
    DICE::DiceDesiredClockConfig appliedClock_{};
};

} // namespace ASFW::Audio::Vendor
