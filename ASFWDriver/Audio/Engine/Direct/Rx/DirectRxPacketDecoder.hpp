#pragma once

#include "DirectRxTypes.hpp"
#include "../../../Wire/AM824/AM824Decoder.hpp"
#include "../../../Wire/RawPcm24In32/RawPcm24In32Decoder.hpp"
#include <cstdint>

namespace ASFW::AudioEngine::Direct::Rx {

inline void DecodeDirectRxFrame(const uint32_t* inWireQuadlets,
                                uint32_t pcmChannels,
                                uint32_t am824Slots,
                                ASFW::Encoding::AudioWireFormat format,
                                int32_t* outPcmFrame) noexcept {
    for (uint32_t ch = 0; ch < pcmChannels; ++ch) {
        if (format == ASFW::Encoding::AudioWireFormat::kRawPcm24In32) {
            auto sample = ASFW::Encoding::RawPcm24In32::Decode(inWireQuadlets[ch]);
            outPcmFrame[ch] = sample ? *sample : 0;
        } else {
            auto sample = ASFW::Isoch::AM824Decoder::DecodeSample(inWireQuadlets[ch]);
            outPcmFrame[ch] = sample ? *sample : 0;
        }
    }
}

// MOTU 828 MK3 V3 device->host PCM decode. Unlike AM824/RawPcm24In32 (one
// 32-bit slot per channel), MOTU packs each channel as 3 big-endian bytes with
// no per-channel label. `pcmBase` points at the first PCM byte inside a data
// block (after SPH(4B) + 2 MSG chunks => block byte offset 10). Output matches
// the AM824/RawPcm24In32 convention: right-justified sign-extended 24-bit int32.
inline void DecodeMotuV3Frame(const uint8_t* pcmBase,
                              uint32_t pcmChannels,
                              int32_t* outPcmFrame) noexcept {
    for (uint32_t ch = 0; ch < pcmChannels; ++ch) {
        const uint8_t* b = pcmBase + static_cast<size_t>(ch) * 3u;
        int32_t sample = (static_cast<int32_t>(b[0]) << 16) |
                         (static_cast<int32_t>(b[1]) << 8) |
                         static_cast<int32_t>(b[2]);
        if (sample & 0x00800000) {
            sample |= static_cast<int32_t>(0xFF000000u);
        }
        outPcmFrame[ch] = sample;
    }
}

} // namespace ASFW::AudioEngine::Direct::Rx
