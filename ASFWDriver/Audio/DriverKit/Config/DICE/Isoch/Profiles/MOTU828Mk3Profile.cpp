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
// (MOTU_V3_WIRE_GROUNDTRUTH.md, main branch — confirmed by Linux tracepoint +
// El Cap snoop):
//   host->device (IT, playback/output): 424 B DATA -> DBS=13 -> 14 PCM slots
//   device->host (IR, capture/input):   520 B DATA -> DBS=16 -> 18 PCM slots
// Tx == host->device, Rx == device->host.
//
// MOTU V3 packs PCM as 24-bit (3 bytes), NOT 4-byte AM824 slots: each 52-byte
// data block = SPH(4) + MSG(6 = 2x3) + PCM(42 = 14x3). So channel count (14) is
// NOT dbs-1; the SPH quadlet does not consume a PCM channel. Keep 14 (matches
// MOTUVendorProtocol::BuildRuntimeCaps and MOTU_828_MK3_FACTS.md canon).
constexpr uint32_t kTxPcmChannels = 14; // host->device (playback / IT)
// device->host (IR): 18 PCM, from El Cap wire ground-truth (confirmed 2026-06-22:
// El Capitan negotiated an 18-channel IR stream; ALSA arecord accepted only -c 18).
// MOTU V3 packs 18 PCM + 2 MSG into a DBS=16 data block as 3-byte chunks (NOT
// 4-byte AM824 slots), so channels (18) > DBS (16). The standard AM824 geometry
// check no longer applies: IR is decoded via AudioWireFormat::kMotuV3Packed
// (RxAudioPacketProcessor MOTU path + DecodeMotuV3Frame), set in
// DiceDuplexRestartCoordinator for this vendor. See MOTU_V3_WIRE_GROUNDTRUTH.md.
constexpr uint32_t kRxPcmChannels = 18; // device->host PCM (was 16 quick-fix)

// Data block size in quadlets, El Cap ground-truth (IT=13, IR=16).
constexpr uint32_t kTxDbs = 13;
constexpr uint32_t kRxDbs = 16;

void FillDefaultStreamConfig(DiceStreamConfig& outConfig,
                             DiceStreamDirection direction) noexcept {
    outConfig = DiceStreamConfig{};
    outConfig.direction = direction;
    outConfig.sampleRate = 48000;
    outConfig.streamMode = Encoding::StreamMode::kBlocking;
    // CIP SID = source node id of the transmitter. The official driver (El Cap +
    // Sequoia wire) sends SID = its OWN node id; ASFW Tahoe diagnostics show we are
    // node 1 / 0xFFC1 (root, S800 vs MOTU S400) so the correct SID here is 1, not 0.
    // We were hardcoding 0 (the El Cap "we're node 0" assumption, disproven by the
    // Tahoe report). Low-probability squeal suspect but a confirmed wire deviation.
    // TODO: plumb the live OHCI NodeID physical_id here instead of a constant —
    // this is wrong if a bus reset makes MOTU root (then we become node 0). Stable
    // for the current 2-node bench where the M3 always wins root.
    outConfig.sid = 1;
    outConfig.midiSlots = 0;
    outConfig.framesPerDataPacket = 8;
    outConfig.fdf = 0x02;
    outConfig.fmt = 0x10;
    if (direction == DiceStreamDirection::HostToDevice) {
        outConfig.pcmChannels = kTxPcmChannels;
        outConfig.dbs = kTxDbs;
        // MOTU V3 IT, from wire ground-truth (MOTU_V3_WIRE_GROUNDTRUTH.md) —
        // CIP Q1 on the wire is 0x8222ffff (byte-for-byte from El Cap snoop +
        // Linux tracepoint):
        //   CIP Q0 byte2 = 0x04 -> SPH=1 (first quadlet of each data block is
        //                                 the MOTU Source Packet Header).
        //   CIP Q1 byte4 = 0x82 -> EOH=1, FMT=0x02 (MOTU's vendor format, NOT
        //                          the standard AM824 FMT=0x10).
        //   CIP Q1 byte5 = 0x22 -> FDF=0x22 (NOT the 0x02 spec default).
        outConfig.sph = true;
        outConfig.fmt = 0x02;
        outConfig.fdf = 0x22;
    } else {
        outConfig.pcmChannels = kRxPcmChannels;
        outConfig.dbs = kRxDbs;
    }
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
    // MOTU V3 IT is NOT standard AM824: each DBS=13 data block is SPH(4B) +
    // 2 MSG chunks + 14 PCM chunks (3-byte big-endian @ block byte offset 10),
    // stereo on slots 0/1 (Main Out). Encoded via the kMotuV3Packed TX path
    // (AmdtpPayloadWriter 3-byte packing + AmdtpTxPacketizer SPH), mirroring the
    // RX MOTU-packed decode. See MOTU_V3_WIRE_GROUNDTRUTH.md.
    quirks.tx.hostToDevicePcmEncoding = Encoding::AudioWireFormat::kMotuV3Packed;
    quirks.tx.dbsPolicy = DbsPolicy::Constant;
    // MOTU V3 emits IR only after it receives IT. Start IT immediately rather
    // than deferring it behind IR replay (which would deadlock). See
    // DiceTxQuirks::startTxBeforeRxReplay and main DevLog "Fix II".
    quirks.tx.startTxBeforeRxReplay = true;
    // MOTU starts IR the instant it sees FETCH_PCM_FRAMES — host IR DMA must be
    // running first. Start host RX before device ProgramRx/ProgramTx (matches
    // working main MOTUAudioBackend StartReceive→ISOC→FETCH order).
    quirks.tx.startHostReceiveBeforeDeviceProgram = true;
    // MOTU V3 IR is NOT standard AM824 — fixed non-standard CIP header + 3-byte
    // packed PCM. Decoded via the kMotuV3Packed path. (Note: the active RX format
    // is currently selected in DiceDuplexRestartCoordinator by vendor id; this
    // mirrors that for profile-driven consumers.)
    quirks.rx.deviceToHostPcmEncoding = Encoding::AudioWireFormat::kMotuV3Packed;
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
