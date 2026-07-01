#pragma once

#include "DirectRxTypes.hpp"
#include "../../../Wire/AM824/AM824Decoder.hpp"
#include <cstdint>

namespace ASFW::AudioEngine::Direct::Rx {

namespace Detail {

[[nodiscard]] constexpr float Signed24ToFloat32(int32_t sample) noexcept {
    if (sample <= -8388608) {
        return -1.0f;
    }
    return static_cast<float>(sample) / 8388607.0f;
}

[[nodiscard]] inline float DecodeAm824SlotToFloat32(uint32_t wireQuadlet) noexcept {
    auto sample = ASFW::Isoch::AM824Decoder::DecodeSample(wireQuadlet);
    return sample ? Signed24ToFloat32(*sample) : 0.0f;
}

[[nodiscard]] inline float DecodeRawSlotAsLabeledMBLAToFloat32(uint32_t wireQuadlet) noexcept {
    const uint32_t hostQuadlet = OSSwapBigToHostInt32(wireQuadlet);
    // Saffire raw capture carries native signed 24-in-32 slots. Normalize it
    // to an AM824 MBLA slot by adding the 0x40 label at the wire/content seam;
    // cross-validated with Linux amdtp-am824.c:170 and :200.
    const uint32_t labeledHostQuadlet =
        0x40000000u | (hostQuadlet & 0x00FFFFFFu);
    return DecodeAm824SlotToFloat32(OSSwapHostToBigInt32(labeledHostQuadlet));
}

} // namespace Detail

inline void DecodeDirectRxFrame(const uint32_t* inWireQuadlets,
                                uint32_t pcmChannels,
                                uint32_t am824Slots,
                                ASFW::Encoding::AudioWireFormat format,
                                float* outPcmFrame) noexcept {
    (void)am824Slots;
    for (uint32_t ch = 0; ch < pcmChannels; ++ch) {
        if (format == ASFW::Encoding::AudioWireFormat::kRawPcm24In32) {
            outPcmFrame[ch] =
                Detail::DecodeRawSlotAsLabeledMBLAToFloat32(inWireQuadlets[ch]);
        } else {
            outPcmFrame[ch] =
                Detail::DecodeAm824SlotToFloat32(inWireQuadlets[ch]);
        }
    }
}

// MOTU 828 MK3 V3 device->host PCM decode. Unlike AM824/RawPcm24In32 (one
// 32-bit slot per channel), MOTU packs each channel as 3 big-endian bytes with
// no per-channel label. `pcmBase` points at the first PCM byte inside a data
// block (after SPH(4B) + 2 MSG chunks => block byte offset 10). Output is
// Float32, matching the AM824/RawPcm24In32 RX path (Detail::Signed24ToFloat32).
inline void DecodeMotuV3Frame(const uint8_t* pcmBase,
                              uint32_t pcmChannels,
                              float* outPcmFrame) noexcept {
    for (uint32_t ch = 0; ch < pcmChannels; ++ch) {
        const uint8_t* b = pcmBase + static_cast<size_t>(ch) * 3u;
        int32_t sample = (static_cast<int32_t>(b[0]) << 16) |
                         (static_cast<int32_t>(b[1]) << 8) |
                         static_cast<int32_t>(b[2]);
        if (sample & 0x00800000) {
            sample |= static_cast<int32_t>(0xFF000000u);
        }
        outPcmFrame[ch] = Detail::Signed24ToFloat32(sample);
    }
}

} // namespace ASFW::AudioEngine::Direct::Rx
