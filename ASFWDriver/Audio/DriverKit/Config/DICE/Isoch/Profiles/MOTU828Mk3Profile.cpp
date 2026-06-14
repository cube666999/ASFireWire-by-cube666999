// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2026 ASFireWire Project
//
// MOTU828Mk3Profile.cpp
// MOTU 828 MK3 (V3 protocol) stream geometry profile.

#include "MOTU828Mk3Profile.hpp"

namespace ASFW::Isoch::Audio::DICE::Profiles {

namespace {

constexpr uint32_t kMotuVendorId = 0x0001F2;

// PCM channel counts at 48 kHz, from the El Capitan wire ground-truth capture
// (diagnostics/elcap_groundtruth/README.md, main branch):
//   host->device (IT, playback/output): len=424 -> DBS=13 -> 14 PCM slots
//   device->host (IR, capture/input):   len=520 -> DBS=16 -> 18 PCM slots
// This matches MOTUVendorProtocol::BuildRuntimeCaps (the values that drive the
// actual wire format). Linux's channel map differs and is NOT authoritative for
// the macOS host path. Tx == host->device, Rx == device->host.
constexpr uint32_t kTxPcmChannels = 14; // host->device (playback / IT)
constexpr uint32_t kRxPcmChannels = 18; // device->host (capture  / IR)

// Data block size in quadlets, El Cap ground-truth (IT=13, IR=16).
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
