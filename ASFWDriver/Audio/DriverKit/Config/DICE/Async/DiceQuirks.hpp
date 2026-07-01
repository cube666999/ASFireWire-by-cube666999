// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (c) 2026 ASFireWire Project
//
// DiceQuirks.hpp
// Quirks and slot encoding settings for DICE devices.

#pragma once

#include "../../../../Wire/AMDTP/AmdtpTypes.hpp"
#include <cstdint>

namespace ASFW::Isoch::Audio::DICE {

enum class DbsPolicy : uint8_t {
    Constant = 0,
    VariablePerPacket = 1
};

struct DiceTxQuirks final {
    Encoding::AudioWireFormat hostToDevicePcmEncoding{
        Encoding::AudioWireFormat::kAM824
    };

    DbsPolicy dbsPolicy{
        DbsPolicy::Constant
    };

    uint32_t defaultNonAudioSlotWord{0x80000000};
    bool initializeNonAudioSlots{true};
    bool preserveFdfInNoDataPackets{false};

    // MOTU V3 (and other devices that gate their TX clock on host IT) will not
    // begin transmitting IR (device->host) until they are *receiving* IT
    // (host->device) packets from us. The default ZTS startup defers IT until
    // the IR replay/cadence is established — which never happens for these
    // devices, because IR never starts without IT first. Deadlock.
    //
    // When true, IsochService::StartPreparedTransmit starts IT immediately
    // (sending the prefilled NO-DATA packets) instead of deferring, so the
    // device receives IT, begins IR, and ZTS establishes. This mirrors the
    // working main-branch ordering (DevLog "Fix II", commit 2dc6600: start IT,
    // THEN wait for IR SYT). Device-facing behavior only — host data path
    // (DMA) is untouched. Leave false for pure DICE devices, whose PLL needs a
    // valid RX reference before TX.
    bool startTxBeforeRxReplay{false};

    // MOTU V3 begins IR transmission the instant it receives the CLOCK_STATUS
    // FETCH_PCM_FRAMES gate (device ProgramTx). The host IR DMA must already be
    // RUNNING at that moment, or MOTU transmits into a context that is not
    // listening and never establishes the stream → zero IR → ZTS timeout.
    //
    // When true, DiceDuplexRestartCoordinator starts the host IR DMA
    // (StartPreparedReceive) BEFORE device ProgramRx/ProgramTx, matching the
    // working main MOTUAudioBackend order (StartReceive → ISOC_COMM_CONTROL →
    // FETCH_PCM → StartTransmit). Leave false for pure DICE chips, whose PLL
    // requires the device programmed before the host RX context runs.
    bool startHostReceiveBeforeDeviceProgram{false};
};

struct DiceRxQuirks final {
    Encoding::AudioWireFormat deviceToHostPcmEncoding{
        Encoding::AudioWireFormat::kAM824
    };

    DbsPolicy dbsPolicy{
        DbsPolicy::Constant
    };
};

struct DiceDeviceQuirks final {
    DiceTxQuirks tx{};
    DiceRxQuirks rx{};
};

} // namespace ASFW::Isoch::Audio::DICE
