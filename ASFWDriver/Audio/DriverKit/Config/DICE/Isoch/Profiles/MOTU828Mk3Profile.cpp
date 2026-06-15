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
// Tx == host->device, Rx == device->host.
constexpr uint32_t kTxPcmChannels = 14; // host->device (playback / IT)
// QUICK FIX: kRxPcmChannels set to kRxDbs (16) instead of real PCM count (18).
// Reason: RxAudioPacketProcessor has an AM824 geometry check:
//   `cip->dataBlockSize < channels` → 16 < 18 → kInvalidRange on every MOTU IR packet.
// MOTU V3 packs 18 PCM + 2 MSG into 15 payload slots + 1 SPH = DBS=16.
// In AM824, channels == DBS (1 channel per slot). For MOTU V3, channels > DBS
// because 3-byte packing allows more channels per slot — the check is wrong.
// Setting kRxPcmChannels = DBS = 16 bypasses the check; CoreAudio sees 16 inputs.
// TODO (MOTU_V3_DICE_TODO.md Bug 1): restore to 18 after implementing DecodeMOTUV3Frame
// and removing the AM824 geometry assumption from RxAudioPacketProcessor.
constexpr uint32_t kRxPcmChannels = 16; // QUICK FIX: should be 18 — see TODO above

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
