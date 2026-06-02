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
                // PCM channel counts from Linux snd_motu_spec_828mk3_fw (motu-protocol-v3.c):
                //   tx_fixed_pcm_chunks[0] = 18  → device→host (IR) = 18 input channels
                //   rx_fixed_pcm_chunks[0] = 14  → host→device (IT) = 14 output channels
                // Wire DBS=21 (kMOTUV3WireDbs48k) is NOT the PCM channel count — it is the
                // data block size on the wire (2 msg slots + up to 24 PCM slots rounded up to
                // a quadlet boundary). DBS=21 stays in MOTUAudioBackend; what we publish to
                // CoreAudio is the actual usable PCM count: 18 in / 14 out.
                return {18u, 14u};
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
