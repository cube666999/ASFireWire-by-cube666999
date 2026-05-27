//
// AM824Decoder.hpp
// ASFWDriver
//
// IEC 61883-6 AM824 Audio Decoder Helpers
//

#pragma once

#include "../Core/IsochTypes.hpp"

namespace ASFW::Isoch {

class AM824Decoder {
public:
    /// Extract 24-bit PCM from AM824 quadlet.
    /// @param quadlet_be Quadlet from OHCI DMA buffer.  On LE hosts the OHCI
    ///                   controller pre-swaps each quadlet, so this value is
    ///                   already in big-endian semantic format — no swap needed.
    /// @return 24-bit PCM sign-extended to int32, or nullopt if not MBLA data.
    [[nodiscard]] static std::optional<int32_t> DecodeSample(uint32_t quadlet_be) noexcept {
        // IEC 61883-6 Table 1: label 0x40 = Multi-bit Linear Audio (MBLA), 24-bit PCM.
        const uint8_t label = static_cast<uint8_t>((quadlet_be >> 24) & 0xFF);
        if (label == 0x40) {
            int32_t sample = static_cast<int32_t>(quadlet_be & 0x00FFFFFF);
            if (sample & 0x800000) {
                sample |= static_cast<int32_t>(0xFF000000u);
            }
            return sample;
        }
        return std::nullopt;
    }

    /// Check if quadlet carries MIDI data (IEC 61883-6 label 0x80-0x83).
    [[nodiscard]] static bool IsMIDI(uint32_t quadlet_be) noexcept {
        const uint8_t label = static_cast<uint8_t>((quadlet_be >> 24) & 0xFF);
        return (label >= 0x80 && label <= 0x83);
    }
};

} // namespace ASFW::Isoch
