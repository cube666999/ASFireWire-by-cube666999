//
// MotuV3Decoder.hpp
// ASFWDriver
//
// MOTU Protocol V3 Packet Decoder (3-byte packed PCM without AM824 labeling)
//

#pragma once

#include <cstdint>
#include <optional>

namespace ASFW::Isoch {

class MotuV3Decoder {
public:
    /// Extract 24-bit PCM from MOTU V3 3-byte big-endian sequence.
    /// MOTU V3 uses raw 24-bit big-endian samples without AM824 labels.
    /// Each sample occupies exactly 3 bytes in the data block.
    ///
    /// @param byte0 Most significant byte (bits [23:16])
    /// @param byte1 Middle byte (bits [15:8])
    /// @param byte2 Least significant byte (bits [7:0])
    /// @return 24-bit PCM sign-extended to int32
    [[nodiscard]] static int32_t DecodeSample(uint8_t byte0, uint8_t byte1, uint8_t byte2) noexcept {
        // Reconstruct 24-bit value: (byte0 << 16) | (byte1 << 8) | byte2
        int32_t sample = static_cast<int32_t>(
            (static_cast<uint32_t>(byte0) << 16) |
            (static_cast<uint32_t>(byte1) << 8) |
            static_cast<uint32_t>(byte2)
        );

        // Sign-extend from 24-bit to 32-bit (if bit 23 is set, set bits [31:24])
        if (sample & 0x800000) {
            sample |= static_cast<int32_t>(0xFF000000u);
        }

        return sample;
    }

    /// Decode a complete MOTU V3 data block from a received IR packet.
    ///
    /// Data block structure (at bytes [8:] after isoch header + CIP header):
    /// [0:3]   SPH (Source Packet Header) — 1 quadlet, skip
    /// [4:9]   msg field — 2 chunks × 3 bytes, skip (control/sync info)
    /// [10:+]  PCM data — numChannels × 3 bytes each
    ///
    /// @param dataBlockStart Pointer to start of data block (after 8-byte isoch header + 8-byte CIP header)
    /// @param dataBlockSize Size of data block in bytes
    /// @param numChannels Number of PCM channels to decode
    /// @param[out] samples Output array (must hold at least numChannels int32_t values)
    /// @return Number of samples successfully decoded, or 0 if insufficient data
    [[nodiscard]] static size_t DecodeDataBlock(const uint8_t* dataBlockStart,
                                                size_t dataBlockSize,
                                                size_t numChannels,
                                                int32_t* samples) noexcept {
        // Minimum size: SPH (4) + msg (6) = 10 bytes
        if (dataBlockSize < 10 || !samples) {
            return 0;
        }

        // Verify we have enough data for all channels
        const size_t requiredBytes = 10 + (numChannels * 3);
        if (dataBlockSize < requiredBytes) {
            return 0;
        }

        // Skip SPH (4 bytes) and msg (6 bytes), start reading PCM at byte 10
        const uint8_t* pcmStart = dataBlockStart + 10;

        for (size_t ch = 0; ch < numChannels; ++ch) {
            const uint8_t* p = pcmStart + (ch * 3);
            samples[ch] = DecodeSample(p[0], p[1], p[2]);
        }

        return numChannels;
    }
};

} // namespace ASFW::Isoch
