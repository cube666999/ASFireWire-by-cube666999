// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2026 ASFireWire Project
//
// MOTU828Mk3Profile.cpp
// MOTU 828 MK3 (V3 protocol) stream geometry profile.

#include "MOTU828Mk3Profile.hpp"

namespace ASFW::Isoch::Audio::DICE::Profiles {

namespace {

constexpr uint32_t kMotuVendorId = 0x0001F2;

// PCM channel counts at 48 kHz (confirmed hardware facts).
constexpr uint32_t kTxPcmChannels = 18; // host->device (playback / IT)
constexpr uint32_t kRxPcmChannels = 14; // device->host (capture  / IR)

// Data block size in quadlets. MOTU V3 wire DBS as observed on the bus
// (IT=13, IR=16). Used only by the streaming engine; the channel count above
// is what makes the CoreAudio device appear with the correct geometry.
constexpr uint32_t kTxDbs = 13;
constexpr uint32_t kRxDbs = 16;

void FillDefaultStreamConfig(DiceStreamConfig& outConfig,
                             DiceStreamDirection direction) noexcept {
    outConfig = DiceStreamConfig{};
    outConfig.direction = direction;
    outConfig.sampleRate = 48000;
    outConfig.streamMode = Encoding::StreamMode::kBlocking;
    outConfig.sid = 0;
    if (direction == DiceStreamDirection::HostToDevice) {
        outConfig.pcmChannels = kTxPcmChannels;
        outConfig.dbs = kTxDbs;
    } else {
        outConfig.pcmChannels = kRxPcmChannels;
        outConfig.dbs = kRxDbs;
    }
    outConfig.midiSlots = 0;
    outConfig.framesPerDataPacket = 8;
    outConfig.fdf = 0x02;
    outConfig.fmt = 0x10;
}

} // namespace

const char* MOTU828Mk3Profile::Name() const noexcept {
    return "MOTU 828 MK3 (V3)";
}

bool MOTU828Mk3Profile::Matches(const DiceDeviceIdentity& identity) const noexcept {
    return identity.vendorId == kMotuVendorId;
}

DiceDeviceQuirks MOTU828Mk3Profile::Quirks() const noexcept {
    DiceDeviceQuirks quirks{};
    quirks.tx.hostToDevicePcmEncoding = Encoding::AudioWireFormat::kAM824;
    quirks.tx.dbsPolicy = DbsPolicy::Constant;
    quirks.rx.deviceToHostPcmEncoding = Encoding::AudioWireFormat::kAM824;
    quirks.rx.dbsPolicy = DbsPolicy::Constant;
    return quirks;
}

bool MOTU828Mk3Profile::BuildDefaultTxStreamConfig(DiceStreamConfig& outConfig) const noexcept {
    FillDefaultStreamConfig(outConfig, DiceStreamDirection::HostToDevice);
    return true;
}

bool MOTU828Mk3Profile::BuildDefaultRxStreamConfig(DiceStreamConfig& outConfig) const noexcept {
    FillDefaultStreamConfig(outConfig, DiceStreamDirection::DeviceToHost);
    return true;
}

} // namespace ASFW::Isoch::Audio::DICE::Profiles
