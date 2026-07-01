// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2026 ASFireWire Project
//
// MOTUVendorProtocol.cpp

#include "MOTUVendorProtocol.hpp"
#include "../../../../Logging/Logging.hpp"
#include "../../DICE/Core/DICERestartSession.hpp"
#include "../../DICE/Core/DICETypes.hpp"   // StatusBits, NominalRate helpers

#include <DriverKit/IOLib.h>   // IOSleep
#include <atomic>

namespace ASFW::Audio::Vendor {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MOTUVendorProtocol::MOTUVendorProtocol(Async::IFireWireBusOps& busOps,
                                         Async::IFireWireBusInfo& busInfo,
                                         uint16_t nodeId,
                                         IRM::IRMClient* irmClient) noexcept
    : busOps_(busOps)
    , busInfo_(busInfo)
    , nodeId_(nodeId)
    , irmClient_(irmClient)
{}

// ---------------------------------------------------------------------------
// IDeviceProtocol
// ---------------------------------------------------------------------------

IOReturn MOTUVendorProtocol::Initialize() {
    ASFW_LOG(Audio, "MOTUVendorProtocol: Initialize node=0x%04x", nodeId_);
    return kIOReturnSuccess;
}

IOReturn MOTUVendorProtocol::Shutdown() {
    ASFW_LOG(Audio, "MOTUVendorProtocol: Shutdown node=0x%04x", nodeId_);
    return kIOReturnSuccess;
}

bool MOTUVendorProtocol::GetRuntimeAudioStreamCaps(AudioStreamRuntimeCaps& outCaps) const {
    outCaps = BuildRuntimeCaps();
    return true;
}

void MOTUVendorProtocol::UpdateRuntimeContext(uint16_t nodeId,
                                               Protocols::AVC::FCPTransport*) {
    nodeId_ = nodeId;
    ASFW_LOG(Audio, "MOTUVendorProtocol: UpdateRuntimeContext node=0x%04x", nodeId_);
}

// ---------------------------------------------------------------------------
// IDICEDuplexProtocol — startup sequence
// ---------------------------------------------------------------------------

void MOTUVendorProtocol::PrepareDuplex(const AudioDuplexChannels& channels,
                                        const DICE::DiceDesiredClockConfig& desiredClock,
                                        PrepareCallback callback) {
    channels_     = channels;
    appliedClock_ = desiredClock;

    ASFW_LOG(Audio,
             "MOTUVendorProtocol: PrepareDuplex irCh=%u itCh=%u",
             channels_.deviceToHostIsoChannel,
             channels_.hostToDeviceIsoChannel);

    // Ground-truth device init (El Capitan wire snoop, mirrors working main MOTUAudioBackend):
    // the official Apple driver writes these every init *before* stream setup. Without them a
    // freshly power-cycled MOTU never begins IR transmission. All device-facing — no host data path.

    // --- COMPLETE missing init (v141): the 3 registers the official driver writes that
    // we didn't — 0x0b38 (8-byte block), 0x0b08 (doorbell), 0x0b1c (in ProgramTx). Real
    // values from El Cap DTrace deref (SEQUOIA_REGREAD_RESULT.md). v139 had only 0b08+0b1c
    // → no effect; this adds the last one, 0x0b38 (V3 stream-2 control). Order mirrors the
    // official cold-start cluster: 0b38 first, then 0b08-bracketed 0b04. All non-fatal.
    // If the full set still doesn't stop the misframe, init is exhausted → timing/SPH-echo.
    if (!WriteRegBlock8(kStream2CtrlOff, kStream2CtrlQ0, kStream2CtrlQ1)) {
        ASFW_LOG_WARNING(Audio, "MOTUVendorProtocol: PrepareDuplex 0x0b38 block write failed (non-fatal)");
    }
    if (!WriteReg(kDoorbellOff, kDoorbellArm)) {
        ASFW_LOG_WARNING(Audio, "MOTUVendorProtocol: PrepareDuplex 0x0b08 arm failed (non-fatal)");
    }
    // 0x0b04 — V3 stream control. Official driver writes 0xffc10001 each init; non-fatal.
    if (!WriteReg(kStreamCtrlOff, kStreamCtrlInit)) {
        ASFW_LOG_WARNING(Audio, "MOTUVendorProtocol: PrepareDuplex 0x0b04 write failed (non-fatal)");
    }
    if (!WriteReg(kDoorbellOff, kDoorbellClear)) {
        ASFW_LOG_WARNING(Audio, "MOTUVendorProtocol: PrepareDuplex 0x0b08 clear failed (non-fatal)");
    }

    // PACKET_FORMAT (0x0b10) = 0x00000002 — S400 only, NO exclude-differed flags.
    // (El Cap ground-truth: 0xc2 with the exclude flags shifts the device's channel positions.)
    if (!WriteReg(kPacketFmtOff, kPacketFmtValue)) {
        ASFW_LOG_ERROR(Audio, "MOTUVendorProtocol: PrepareDuplex PACKET_FORMAT write failed");
        callback(kIOReturnIOError, {});
        return;
    }

    // ROUTE_PORT_CONF (0x0c04) = 0x00000100 — official driver leaves this set; non-fatal.
    if (!WriteReg(kRoutePortConfOff, kRoutePortConf)) {
        ASFW_LOG_WARNING(Audio, "MOTUVendorProtocol: PrepareDuplex ROUTE_PORT_CONF write failed (non-fatal)");
    }

    const FW::Generation gen = busInfo_.GetGeneration();
    const DICE::DiceDuplexPrepareResult result{
        .generation   = gen,
        .channels     = BuildDuplexChannels(),
        .appliedClock = appliedClock_,
        .runtimeCaps  = BuildRuntimeCaps(),
    };
    callback(kIOReturnSuccess, result);
}

void MOTUVendorProtocol::ProgramRx(StageCallback callback) {
    // Program ISOC_COMM_CONTROL: activate BOTH MOTU isoch directions in one transition.
    //   RX (MOTU side) = host→MOTU = IT = hostToDevice channel
    //   TX (MOTU side) = MOTU→host = IR = deviceToHost channel
    //
    // ⚠️ Two-step deactivate→activate is REQUIRED, not optional. MOTU V3 will NOT begin IR
    // transmission from a single activate write even with the correct value — it must see the
    // deactivate→activate transition. Confirmed on hardware in the working main driver (Fix 19):
    // seq=0 (MOTU silent) persisted with a correct 0xC1C00000 activate alone, and only adding a
    // prior deactivate (+settle) made IR packets flow. A power-cycle does NOT substitute for it.
    // Device-facing protocol only — host data path (DMA) untouched.
    uint32_t ctrl = 0;
    if (!ReadReg(kIsocCtrlOff, ctrl)) {
        ASFW_LOG_ERROR(Audio, "MOTUVendorProtocol: ProgramRx read ISOC_CTRL failed");
        callback(kIOReturnIOError, {});
        return;
    }
    const uint32_t lowerBits = ctrl & ~kIsocMask; // keep MOTU's lower-word status bits

    // Deactivate both (Change=1, Activated=0), then let the MOTU state machine settle.
    const uint32_t deactivate = lowerBits | kChangeRxIsocState | kChangeTxIsocState;
    if (!WriteReg(kIsocCtrlOff, deactivate)) {
        ASFW_LOG_ERROR(Audio, "MOTUVendorProtocol: ProgramRx deactivate write failed");
        callback(kIOReturnIOError, {});
        return;
    }
    IOSleep(20);

    // Activate both directions with their channels.
    const uint32_t activate = lowerBits
        | kChangeRxIsocState | kRxIsocActivated
        | (static_cast<uint32_t>(channels_.hostToDeviceIsoChannel) << kRxChannelShift)
        | kChangeTxIsocState | kTxIsocActivated
        | (static_cast<uint32_t>(channels_.deviceToHostIsoChannel) << kTxChannelShift);
    if (!WriteReg(kIsocCtrlOff, activate)) {
        ASFW_LOG_ERROR(Audio, "MOTUVendorProtocol: ProgramRx activate write failed");
        callback(kIOReturnIOError, {});
        return;
    }

    ASFW_LOG(Audio,
             "MOTUVendorProtocol: ProgramRx deactivate=0x%08x activate=0x%08x (itCh=%u irCh=%u)",
             deactivate, activate,
             channels_.hostToDeviceIsoChannel, channels_.deviceToHostIsoChannel);

    const DICE::DiceDuplexStageResult result{
        .generation  = busInfo_.GetGeneration(),
        .channels    = BuildDuplexChannels(),
        .phase       = DICE::DiceRestartPhase::kDeviceRxProgrammed,
        .runtimeCaps = BuildRuntimeCaps(),
    };
    callback(kIOReturnSuccess, result);
}

void MOTUVendorProtocol::ProgramTxAndEnableDuplex(StageCallback callback) {
    // ISOC_COMM_CONTROL (both directions) is already fully programmed in ProgramRx via the
    // required deactivate→activate transition. Here we set the missing 0x0b1c stream-format
    // config, then CLOCK_STATUS kFetchPCMFrames.
    //
    // 0x0b1c — official driver writes this in the createDCLProgram cluster, AFTER ISOC
    // activate (0x0b00, our ProgramRx) and BEFORE FETCH (0x0b14, below): order 0b10→0b00→
    // 0b1c→0b14. Value 0x00120000 @ 48 kHz (rate-correlated). Write-only (blind to read-
    // back) — value from El Cap DTrace deref. One of the missing init writes; best timing
    // match for the misframe (fires at stream restart, like our duplex start).
    // See SEQUOIA_REGREAD_RESULT.md. Non-fatal.
    if (!WriteReg(kStreamCfgOff, kStreamCfg48k)) {
        ASFW_LOG_WARNING(Audio, "MOTUVendorProtocol: ProgramTx 0x0b1c write failed (non-fatal)");
    }

    uint32_t clk = 0;
    if (!ReadReg(kClockStatusOff, clk)) {
        ASFW_LOG_ERROR(Audio, "MOTUVendorProtocol: ProgramTx read CLOCK_STATUS failed");
        callback(kIOReturnIOError, {});
        return;
    }
    clk |= kFetchPCMFrames;
    if (!WriteReg(kClockStatusOff, clk)) {
        ASFW_LOG_ERROR(Audio, "MOTUVendorProtocol: ProgramTx write CLOCK_STATUS failed");
        callback(kIOReturnIOError, {});
        return;
    }

    ASFW_LOG(Audio,
             "MOTUVendorProtocol: ProgramTx FETCH_PCM set CLK=0x%08x (irCh=%u)",
             clk, channels_.deviceToHostIsoChannel);

    const DICE::DiceDuplexStageResult result{
        .generation  = busInfo_.GetGeneration(),
        .channels    = BuildDuplexChannels(),
        .phase       = DICE::DiceRestartPhase::kDeviceTxArmed,
        .runtimeCaps = BuildRuntimeCaps(),
    };
    callback(kIOReturnSuccess, result);
}

void MOTUVendorProtocol::ConfirmDuplexStart(ConfirmCallback callback) {
    // MOTU V3 has no "lock acquired" register — treat as instant success.
    ASFW_LOG(Audio, "MOTUVendorProtocol: ConfirmDuplexStart (stub OK)");
    const DICE::DiceDuplexConfirmResult result{
        .generation   = busInfo_.GetGeneration(),
        .channels     = BuildDuplexChannels(),
        .appliedClock = appliedClock_,
        .runtimeCaps  = BuildRuntimeCaps(),
        .notification = 0,
        .status       = 0,
        .extStatus    = 0,
    };
    callback(kIOReturnSuccess, result);
}

void MOTUVendorProtocol::ApplyClockConfig(const DICE::DiceDesiredClockConfig& desiredClock,
                                           ClockApplyCallback callback) {
    // MOTU 828 MK3 always runs at 48kHz internal. Accept without writing registers.
    appliedClock_ = desiredClock;
    const DICE::DiceClockApplyResult result{
        .generation   = busInfo_.GetGeneration(),
        .appliedClock = appliedClock_,
        .runtimeCaps  = BuildRuntimeCaps(),
    };
    callback(kIOReturnSuccess, result);
}

void MOTUVendorProtocol::ReadDuplexHealth(HealthCallback callback) {
    // The generic DiceDuplexRestartCoordinator::WaitForStableGlobalClock gate
    // polls this before isoch start and requires, on a DICE GLOBAL_STATUS value:
    //   IsSourceLocked(status) && NominalRateHz(status) == desiredClock.sampleRateHz
    // MOTU V3 has no DICE GLOBAL_STATUS register and no "lock acquired" bit, but
    // CLOCK_STATUS does carry the device's current nominal rate in bits [15:8]
    // (ref Linux motu-protocol-v3.c snd_motu_protocol_v3_get_clock_rate). We read
    // the real rate and re-encode it into the DICE status format so the gate
    // validates honestly: it passes only when the MOTU is actually running at the
    // rate CoreAudio requested. (The kFetchPCMFrames bit is a write-only command,
    // never a readable status — polling it here would never succeed.)
    uint32_t clk = 0;
    (void)ReadReg(kClockStatusOff, clk);

    // MOTU rate index -> Hz (snd_motu_clock_rates; note: no 32 kHz entry).
    const uint32_t motuRateIndex = (clk & kClockRateMask) >> kClockRateShift;
    uint32_t rateHz = 0;
    switch (motuRateIndex) {
        case 0: rateHz = 44100;  break;
        case 1: rateHz = 48000;  break;
        case 2: rateHz = 88200;  break;
        case 3: rateHz = 96000;  break;
        case 4: rateHz = 176400; break;
        case 5: rateHz = 192000; break;
        default: rateHz = 0;     break; // unknown -> report unlocked
    }

    // Hz -> DICE nominal-rate index (RateHzFromIndex; DICE index 0 == 32 kHz).
    uint32_t diceStatus = 0;
    switch (rateHz) {
        case 32000:  diceStatus = (0u << DICE::StatusBits::kNominalRateShift); break;
        case 44100:  diceStatus = (1u << DICE::StatusBits::kNominalRateShift); break;
        case 48000:  diceStatus = (2u << DICE::StatusBits::kNominalRateShift); break;
        case 88200:  diceStatus = (3u << DICE::StatusBits::kNominalRateShift); break;
        case 96000:  diceStatus = (4u << DICE::StatusBits::kNominalRateShift); break;
        case 176400: diceStatus = (5u << DICE::StatusBits::kNominalRateShift); break;
        case 192000: diceStatus = (6u << DICE::StatusBits::kNominalRateShift); break;
        default:     diceStatus = 0; break;
    }
    if (rateHz != 0) {
        // Internal/external clock locked: MOTU V3 exposes no lock bit, so a valid
        // decoded rate is treated as locked.
        diceStatus |= DICE::StatusBits::kSourceLocked;
    }

    ASFW_LOG(Audio,
             "MOTUVendorProtocol: ReadDuplexHealth CLK_STATUS=0x%08x motuRateIdx=%u rate=%u diceStatus=0x%08x",
             clk, motuRateIndex, rateHz, diceStatus);

    const DICE::DiceDuplexHealthResult result{
        .generation   = busInfo_.GetGeneration(),
        .appliedClock = appliedClock_,
        .runtimeCaps  = BuildRuntimeCaps(),
        .notification = 0,
        .status       = diceStatus,
        .extStatus    = 0,
    };
    callback(kIOReturnSuccess, result);
}

IOReturn MOTUVendorProtocol::StopDuplex() {
    ASFW_LOG(Audio, "MOTUVendorProtocol: StopDuplex");

    // Clear CLOCK_STATUS kFetchPCMFrames first.
    uint32_t clk = 0;
    if (ReadReg(kClockStatusOff, clk)) {
        clk &= ~kFetchPCMFrames;
        (void)WriteReg(kClockStatusOff, clk);
    }

    // Clear ISOC_COMM_CONTROL: deactivate both RX and TX sides.
    uint32_t ctrl = 0;
    if (ReadReg(kIsocCtrlOff, ctrl)) {
        ctrl &= ~kIsocMask;
        ctrl |= kChangeRxIsocState | kChangeTxIsocState;
        (void)WriteReg(kIsocCtrlOff, ctrl);
    }

    return kIOReturnSuccess;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

Async::FWAddress MOTUVendorProtocol::MakeAddr(uint32_t offset) const noexcept {
    return Async::FWAddress(Async::FWAddress::AddressParts{
        .addressHi = kAddrHi,
        .addressLo = kAddrBase + offset,
    });
}

bool MOTUVendorProtocol::WriteReg(uint32_t offset, uint32_t value) noexcept {
    struct State { std::atomic<bool> done{false}; IOReturn status{kIOReturnTimeout}; };
    auto state = std::make_shared<State>();

    busOps_.WriteQuad(
        busInfo_.GetGeneration(),
        FW::NodeId{static_cast<uint8_t>(nodeId_)},
        MakeAddr(offset),
        value,
        FW::FwSpeed::S400,
        [state](Async::AsyncStatus s, std::span<const uint8_t>) {
            state->status = (s == Async::AsyncStatus::kSuccess)
                                ? kIOReturnSuccess
                                : kIOReturnIOError;
            state->done.store(true, std::memory_order_release);
        });

    for (uint32_t ms = 0; ms < kRegTimeoutMs; ++ms) {
        if (state->done.load(std::memory_order_acquire)) {
            break;
        }
        IOSleep(1);
    }
    return state->done.load() && state->status == kIOReturnSuccess;
}

bool MOTUVendorProtocol::WriteRegBlock8(uint32_t offset, uint32_t q0, uint32_t q1) noexcept {
    struct State { std::atomic<bool> done{false}; IOReturn status{kIOReturnTimeout}; };
    auto state = std::make_shared<State>();

    // Two big-endian quadlets (same serialization as WriteQuad), 8-byte block payload.
    const std::array<uint8_t, 8> data = {
        static_cast<uint8_t>(q0 >> 24), static_cast<uint8_t>(q0 >> 16),
        static_cast<uint8_t>(q0 >> 8),  static_cast<uint8_t>(q0),
        static_cast<uint8_t>(q1 >> 24), static_cast<uint8_t>(q1 >> 16),
        static_cast<uint8_t>(q1 >> 8),  static_cast<uint8_t>(q1),
    };
    busOps_.WriteBlock(
        busInfo_.GetGeneration(),
        FW::NodeId{static_cast<uint8_t>(nodeId_)},
        MakeAddr(offset),
        std::span<const uint8_t>{data},
        FW::FwSpeed::S400,
        [state](Async::AsyncStatus s, std::span<const uint8_t>) {
            state->status = (s == Async::AsyncStatus::kSuccess)
                                ? kIOReturnSuccess
                                : kIOReturnIOError;
            state->done.store(true, std::memory_order_release);
        });

    for (uint32_t ms = 0; ms < kRegTimeoutMs; ++ms) {
        if (state->done.load(std::memory_order_acquire)) {
            break;
        }
        IOSleep(1);
    }
    return state->done.load() && state->status == kIOReturnSuccess;
}

bool MOTUVendorProtocol::ReadReg(uint32_t offset, uint32_t& outValue) noexcept {
    struct State {
        std::atomic<bool> done{false};
        IOReturn status{kIOReturnTimeout};
        uint32_t value{0};
    };
    auto state = std::make_shared<State>();

    busOps_.ReadQuad(
        busInfo_.GetGeneration(),
        FW::NodeId{static_cast<uint8_t>(nodeId_)},
        MakeAddr(offset),
        FW::FwSpeed::S400,
        [state](Async::AsyncStatus s, std::span<const uint8_t> data) {
            if (s == Async::AsyncStatus::kSuccess && data.size() >= 4) {
                state->value = (static_cast<uint32_t>(data[0]) << 24) |
                               (static_cast<uint32_t>(data[1]) << 16) |
                               (static_cast<uint32_t>(data[2]) << 8)  |
                                static_cast<uint32_t>(data[3]);
                state->status = kIOReturnSuccess;
            } else {
                state->status = kIOReturnIOError;
            }
            state->done.store(true, std::memory_order_release);
        });

    for (uint32_t ms = 0; ms < kRegTimeoutMs; ++ms) {
        if (state->done.load(std::memory_order_acquire)) {
            break;
        }
        IOSleep(1);
    }

    if (state->done.load() && state->status == kIOReturnSuccess) {
        outValue = state->value;
        return true;
    }
    return false;
}

AudioDuplexChannels MOTUVendorProtocol::BuildDuplexChannels() const noexcept {
    return AudioDuplexChannels{
        .deviceToHostIsoChannel = channels_.deviceToHostIsoChannel,
        .hostToDeviceIsoChannel = channels_.hostToDeviceIsoChannel,
    };
}

AudioStreamRuntimeCaps MOTUVendorProtocol::BuildRuntimeCaps() const noexcept {
    AudioStreamRuntimeCaps caps{};
    caps.hostInputPcmChannels    = kHostInputPcm;   // 18 — IR (MOTU→host)
    caps.hostOutputPcmChannels   = kHostOutputPcm;  // 14 — IT (host→MOTU)
    caps.deviceToHostAm824Slots  = kIrDbs;           // 16
    caps.hostToDeviceAm824Slots  = kItDbs;           // 13
    caps.sampleRateHz            = 48000u;
    caps.deviceToHostIsoChannel  = channels_.deviceToHostIsoChannel;
    caps.hostToDeviceIsoChannel  = channels_.hostToDeviceIsoChannel;
    return caps;
}

} // namespace ASFW::Audio::Vendor
