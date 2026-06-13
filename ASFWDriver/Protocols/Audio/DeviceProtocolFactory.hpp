// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2024 ASFireWire Project
//
// DeviceProtocolFactory.hpp - Factory for creating device-specific protocol handlers

#pragma once

#include "IDeviceProtocol.hpp"
#include "../Ports/FireWireBusPort.hpp"
#include <cstdint>
#include <memory>
#include <optional>

namespace ASFW::Audio {

/// Integration mode for a recognized device profile.
enum class DeviceIntegrationMode : uint8_t {
    kNone = 0,
    kHardcodedNub,  // Legacy path using hardcoded ASFWAudioDevice profile.
    kAVCDriven,     // AV/C discovery path with vendor extension controls.
    kMOTUV3,        // MOTU proprietary register protocol (V3, no AV/C).
};

/// Factory for creating device-specific protocol handlers
///
/// Call Create() during device discovery to instantiate the appropriate
/// protocol handler for known devices. Returns nullptr for unknown devices.
class DeviceProtocolFactory {
public:
    static constexpr uint32_t kFocusriteVendorId  = 0x00130e;
    static constexpr uint32_t kSPro24DspModelId   = 0x000008;
    static constexpr uint32_t kApogeeVendorId     = 0x0003db;
    static constexpr uint32_t kApogeeDuetModelId  = 0x01dddd;

    // MOTU vendor ID (IEEE OUI) and V3 model IDs.
    // Source: Linux sound/firewire/motu/motu.c device table.
    static constexpr uint32_t kMOTUVendorId       = 0x0001f2;
    static constexpr uint32_t kMOTU828MK3FWModel  = 0x000015; // 828mk3 FireWire-only
    static constexpr uint32_t kMOTU828MK3HybModel = 0x000035; // 828mk3 Hybrid (FW+USB)
    static constexpr uint32_t kMOTU896MK3Model    = 0x000016;
    static constexpr uint32_t kMOTUTravelerMK3Model= 0x000017;
    static constexpr uint32_t kMOTUUltraLiteMK3Model=0x000019;

    /// Resolve the effective model ID for a device.
    ///
    /// MOTU devices store the model identifier in Unit_SW_Vers (unit directory
    /// key 0x13), NOT in the root directory ModelId (key 0x17), which MOTU
    /// always leaves as 0x000000. All other known vendors use root ModelId.
    static constexpr uint32_t EffectiveModelId(
        uint32_t vendorId,
        uint32_t rootModelId,
        std::optional<uint32_t> unitSwVersion
    ) noexcept {
        if (vendorId == kMOTUVendorId && unitSwVersion.has_value()) {
            return *unitSwVersion;
        }
        return rootModelId;
    }

    /// Resolve integration mode for a known vendor/model pair.
    static constexpr DeviceIntegrationMode LookupIntegrationMode(
        uint32_t vendorId,
        uint32_t modelId
    ) noexcept {
        if (vendorId == kFocusriteVendorId && modelId == kSPro24DspModelId) {
            return DeviceIntegrationMode::kHardcodedNub;
        }
        if (vendorId == kApogeeVendorId && modelId == kApogeeDuetModelId) {
            return DeviceIntegrationMode::kAVCDriven;
        }
        if (vendorId == kMOTUVendorId &&
            (modelId == kMOTU828MK3FWModel    ||
             modelId == kMOTU828MK3HybModel   ||
             modelId == kMOTU896MK3Model      ||
             modelId == kMOTUTravelerMK3Model ||
             modelId == kMOTUUltraLiteMK3Model)) {
            return DeviceIntegrationMode::kMOTUV3;
        }
        return DeviceIntegrationMode::kNone;
    }

    /// Check if a device is recognized by the factory.
    static constexpr bool IsKnownDevice(uint32_t vendorId, uint32_t modelId) noexcept {
        return LookupIntegrationMode(vendorId, modelId) != DeviceIntegrationMode::kNone;
    }

    /// Channel counts for a MOTU V3 device's isochronous streams.
    ///
    /// These are the total slot counts per isoch packet (PCM + padding/MIDI),
    /// matching the values the MOTU kext reports as fNumFWOutputChannels /
    /// fNumFWInputChannels at 44.1/48 kHz.
    ///
    /// inputChannels  = channels device sends to host   (IR, talker side)
    /// outputChannels = channels host sends to device   (IT, listener side)
    struct MOTUChannelLayout {
        uint32_t inputChannels{2};   ///< IR slots (device → host)
        uint32_t outputChannels{2};  ///< IT slots (host → device)
    };

    /// Return a human-readable device name for a known MOTU V3 model.
    static constexpr const char* GetMOTUV3DeviceName(uint32_t modelId) noexcept {
        switch (modelId) {
            case kMOTU828MK3FWModel:     return "MOTU 828mk3";
            case kMOTU828MK3HybModel:    return "MOTU 828mk3 Hybrid";
            case kMOTU896MK3Model:       return "MOTU 896mk3";
            case kMOTUTravelerMK3Model:  return "MOTU Traveler mk3";
            case kMOTUUltraLiteMK3Model: return "MOTU UltraLite mk3";
            default:                     return "MOTU FireWire";
        }
    }

    /// Return the known channel layout for a MOTU V3 model at ≤48 kHz.
    /// Returns {2, 2} for unrecognised models (safe but non-functional).
    static constexpr MOTUChannelLayout GetMOTUV3ChannelLayout(uint32_t modelId) noexcept {
        switch (modelId) {
            case kMOTU828MK3FWModel:    // unitSwVersion 0x15
            case kMOTU828MK3HybModel:   // unitSwVersion 0x35
                // Fix 59: expose CLEAN STEREO to CoreAudio (outputChannels=2).
                //   Hardware [DBG] logs proved: with outputChannels=10 the HAL UPMIXES
                //   Spotify stereo across the unlabeled 10-channel buffer — ch0 carried clean
                //   left, but ch1 (right) and ch6/ch8 received a high-frequency upmix artifact
                //   (the squeal) which the encoder faithfully forwarded to Analog 7 / DIG L.
                //   With outputChannels=2 the HAL can only write L→ch0, R→ch1, no upmix.
                //   The WIRE frame is decoupled (see GetMOTUV3WireDbs): MOTU still receives a
                //   valid DBS=13 frame with the 2 PCM channels in slots 0,1 (Analog 1/2),
                //   remaining slots zero-padded.
                return {18u, 2u};
            case kMOTU896MK3Model:      // unitSwVersion 0x16 — 28 input / 24 output physical
                return {20u, 16u};
            case kMOTUTravelerMK3Model: // unitSwVersion 0x17
                return {14u, 14u};
            case kMOTUUltraLiteMK3Model:// unitSwVersion 0x19
                return {14u, 10u};
            default:
                return {2u, 2u};
        }
    }

    /// Wire-level AM824 data-block size (DBS = quadlets per data block) for a MOTU V3
    /// model's host→device (IT) stream at ≤48 kHz.
    ///
    /// DECOUPLED from GetMOTUV3ChannelLayout().outputChannels: CoreAudio exposes a small
    /// PCM channel count (e.g. stereo) to avoid HAL upmixing, while the FireWire wire frame
    /// must still carry MOTU's expected physical frame size. The PacketAssembler writes the
    /// real PCM channels into the first slots and zero-pads the remainder.
    ///
    /// 828mk3: rx_fixed_pcm_chunks[48k]=14 is only the BASE; the device adds dynamic
    /// S/PDIF + ADAT chunks (read from V3_OPT_IFACE_MODE register). Our unit runs with
    /// S/PDIF active (DIG outputs lit), so it expects the full 24-PCM frame:
    ///   frame = SPH(4) + MSG(6) + 24×PCM(3) = 82 bytes → DBS = 1+DIV_ROUND_UP(26×3,4) = 21.
    /// This matches the empirically-verified Fix 21 value (IT ran with DBS=21). Sending the
    /// smaller DBS=13 frame caused MOTU to slip frame boundaries → channel scatter + squeal.
    /// PacketAssembler writes the real PCM channels into slots 0,1 (Analog 1/2) and zero-pads
    /// the remaining 22 slots. TODO: read V3_OPT_IFACE_MODE to compute this dynamically.
    /// Returns 0 for models without a known override (caller keeps DBS = pcmChannels).
    /// Wire DBS for IT (host→device) in MOTU V3 protocol.
    /// Formula from Linux amdtp-motu.c: DBS = ceil((SPH + MSG + channels * 3) / 4)
    ///   SPH = 4 bytes, MSG = 6 bytes, channels = 18 (828mk3 at 48kHz)
    ///   DBS = ceil((4 + 6 + 18*3) / 4) = ceil(64/4) = 16
    ///
    /// Fix 64: was 21 (incorrect — 21×4=84 bytes → (84-10)/3=24.67 channels, non-integer!).
    /// Old value traced back to faulty Sequoia diagnostic capture (confused IT vs IR or rate).
    /// Linux snd-firewire-motu uses 16 for 828mk3 IT at 48kHz (verified against amdtp-motu.c).
    static constexpr uint8_t GetMOTUV3WireDbs(uint32_t modelId) noexcept {
        switch (modelId) {
            case kMOTU828MK3FWModel:
            case kMOTU828MK3HybModel:
                return 16u;  // Fix 64: was 21 (wrong) → 16 correct for 18ch IT at 48kHz
            default:
                return 0u;
        }
    }

    /// Create a protocol handler for the given vendor/model
    /// @param vendorId   IEEE OUI vendor ID from Config ROM
    /// @param modelId    Model ID from Config ROM
    /// @param busOps     FireWire bus operations port
    /// @param busInfo    FireWire bus info port
    /// @param nodeId     Target device node ID
    /// @return Protocol handler, or nullptr if device is not recognized
    static std::unique_ptr<IDeviceProtocol> Create(
        uint32_t vendorId,
        uint32_t modelId,
        Protocols::Ports::FireWireBusOps& busOps,
        Protocols::Ports::FireWireBusInfo& busInfo,
        uint16_t nodeId
    );
};

} // namespace ASFW::Audio
