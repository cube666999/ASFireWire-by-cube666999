// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2026 ASFireWire Project
//
// MOTU828Mk3Profile.hpp
// MOTU 828 MK3 (V3 protocol) stream geometry profile.
//
// MOTU is not a DICE chip device, but it is routed through the DICE audio
// backend (kHardcodedNub). The audio graph builder and nub publisher both
// resolve stream geometry (channel counts) through the DICE profile registry,
// so MOTU needs an entry here or it falls back to the 2ch Generic profile and
// the graph build fails on a channel-count mismatch.
//
// Geometry (48 kHz), from confirmed hardware facts (CLAUDE.md + Sequoia diag):
//   Tx (host->device, playback) = 18 PCM channels
//   Rx (device->host, capture)  = 14 PCM channels

#pragma once

#include "../../DiceDeviceProfile.hpp"

namespace ASFW::Isoch::Audio::DICE::Profiles {

class MOTU828Mk3Profile final : public IDiceDeviceProfile {
public:
    [[nodiscard]] const char* Name() const noexcept override;

    [[nodiscard]] bool Matches(const DiceDeviceIdentity& identity) const noexcept override;

    [[nodiscard]] DiceDeviceQuirks Quirks() const noexcept override;

    [[nodiscard]] bool BuildDefaultTxStreamConfig(DiceStreamConfig& outConfig) const noexcept override;
    [[nodiscard]] bool BuildDefaultRxStreamConfig(DiceStreamConfig& outConfig) const noexcept override;
};

} // namespace ASFW::Isoch::Audio::DICE::Profiles
