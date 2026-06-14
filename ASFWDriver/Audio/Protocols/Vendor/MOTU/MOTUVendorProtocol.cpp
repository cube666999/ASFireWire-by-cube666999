// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2026 ASFireWire Project
//
// MOTUVendorProtocol.cpp

#include "MOTUVendorProtocol.hpp"
#include "../../../../Logging/Logging.hpp"
#include "../../DICE/Core/DICERestartSession.hpp"

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

    // Write PACKET_FORMAT: S400 speed, exclude differed data in both directions.
    const uint32_t pktFmt = kTxExcludeDiffered | kRxExcludeDiffered | kSpeedS400;
    if (!WriteReg(kPacketFmtOff, pktFmt)) {
        ASFW_LOG_ERROR(Audio, "MOTUVendorProtocol: PrepareDuplex PACKET_FORMAT write failed");
        callback(kIOReturnIOError, {});
        return;
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
    // Activate MOTU's RX side (host→MOTU = IT direction).
    // MOTU ISOC_COMM_CONTROL: set kChangeRxIsocState | kRxIsocActivated | channel.
    uint32_t ctrl = 0;
    if (!ReadReg(kIsocCtrlOff, ctrl)) {
        ASFW_LOG_ERROR(Audio, "MOTUVendorProtocol: ProgramRx read ISOC_CTRL failed");
        callback(kIOReturnIOError, {});
        return;
    }

    ctrl &= ~kIsocMask;
    ctrl |= kChangeRxIsocState | kRxIsocActivated;
    ctrl |= static_cast<uint32_t>(channels_.hostToDeviceIsoChannel) << kRxChannelShift;

    if (!WriteReg(kIsocCtrlOff, ctrl)) {
        ASFW_LOG_ERROR(Audio, "MOTUVendorProtocol: ProgramRx write ISOC_CTRL failed");
        callback(kIOReturnIOError, {});
        return;
    }

    ASFW_LOG(Audio,
             "MOTUVendorProtocol: ProgramRx itCh=%u ISOC_CTRL=0x%08x",
             channels_.hostToDeviceIsoChannel, ctrl);

    const DICE::DiceDuplexStageResult result{
        .generation  = busInfo_.GetGeneration(),
        .channels    = BuildDuplexChannels(),
        .phase       = DICE::DiceRestartPhase::kDeviceRxProgrammed,
        .runtimeCaps = BuildRuntimeCaps(),
    };
    callback(kIOReturnSuccess, result);
}

void MOTUVendorProtocol::ProgramTxAndEnableDuplex(StageCallback callback) {
    // Activate MOTU's TX side (MOTU→host = IR direction) and enable PCM fetch.
    uint32_t ctrl = 0;
    if (!ReadReg(kIsocCtrlOff, ctrl)) {
        ASFW_LOG_ERROR(Audio, "MOTUVendorProtocol: ProgramTx read ISOC_CTRL failed");
        callback(kIOReturnIOError, {});
        return;
    }

    ctrl |= kChangeTxIsocState | kTxIsocActivated;
    ctrl |= static_cast<uint32_t>(channels_.deviceToHostIsoChannel) << kTxChannelShift;

    if (!WriteReg(kIsocCtrlOff, ctrl)) {
        ASFW_LOG_ERROR(Audio, "MOTUVendorProtocol: ProgramTx write ISOC_CTRL failed");
        callback(kIOReturnIOError, {});
        return;
    }

    // Write CLOCK_STATUS: set kFetchPCMFrames to start the MOTU stream.
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
             "MOTUVendorProtocol: ProgramTx irCh=%u ISOC_CTRL=0x%08x CLK=0x%08x",
             channels_.deviceToHostIsoChannel, ctrl, clk);

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
    // Read CLOCK_STATUS to check if streaming is active.
    uint32_t clk = 0;
    (void)ReadReg(kClockStatusOff, clk);
    const bool healthy = (clk & kFetchPCMFrames) != 0;
    ASFW_LOG(Audio,
             "MOTUVendorProtocol: ReadDuplexHealth CLK_STATUS=0x%08x healthy=%d",
             clk, (int)healthy);

    const DICE::DiceDuplexHealthResult result{
        .generation   = busInfo_.GetGeneration(),
        .appliedClock = appliedClock_,
        .runtimeCaps  = BuildRuntimeCaps(),
        .notification = 0,
        .status       = healthy ? 1u : 0u,  // non-zero = locked
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
