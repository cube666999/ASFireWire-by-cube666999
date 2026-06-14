// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2026 ASFireWire Project
//
// MOTUAudioProfiles.hpp - MOTU FireWire audio device knowledge (vendor-specific V3 protocol).
// Knows ONLY MOTU devices; performs no runtime protocol construction.

#pragma once

#include "../../Common/DeviceProfileTypes.hpp"
#include "../AudioDeviceIds.hpp"
#include "../AudioProfileTypes.hpp"

#include <optional>

namespace ASFW::DeviceProfiles::Audio::MOTU {

[[nodiscard]] constexpr std::optional<DeviceIdentityHint>
LookupIdentity(const DeviceProfileQuery& query) noexcept {
    if (query.vendorId == kMotuVendorId) {
        return DeviceIdentityHint{.vendorId = query.vendorId,
                                  .modelId  = query.modelId,
                                  .vendorName = kMotuVendorName,
                                  .modelName  = kMotu828Mk3ModelName,
                                  .source = MatchSource::VendorModel};
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<AudioProfileHint>
LookupAudioProfile(const DeviceProfileQuery& query) noexcept {
    if (query.vendorId == kMotuVendorId) {
        return AudioProfileHint{.family = AudioProtocolFamily::VendorSpecific,
                                .mode   = AudioIntegrationMode::kHardcodedNub,
                                .source = MatchSource::VendorModel};
    }
    return std::nullopt;
}

} // namespace ASFW::DeviceProfiles::Audio::MOTU
