#include "RxAudioPacketProcessor.hpp"
#include "DirectRxPacketDecoder.hpp"
#include "../../../Wire/CIP/CIPHeader.hpp"
#include "../../../../Isoch/Receive/IsochRxTiming.hpp"

#include <algorithm>
#include <cstring>

namespace ASFW::AudioEngine::Direct::Rx {

static constexpr size_t kIsochHeaderSize = 8; // Timestamp + isoch header

RxAudioPacketProcessorResult RxAudioPacketProcessor::ProcessPacket(const uint8_t* payload,
                                                                   size_t length,
                                                                   uint64_t absoluteFrame,
                                                                   uint32_t channels,
                                                                   uint32_t am824Slots,
                                                                   ASFW::Encoding::AudioWireFormat format) noexcept {
    RxAudioPacketProcessorResult result{};

    if (length < kIsochHeaderSize + 8) {
        result.status = DirectRxWriteStatus::kInvalidRange;
        return result;
    }

    result.hasReceiveCycleTimestamp =
        ASFW::Isoch::Rx::DecodeReceiveTimestamp(
            payload, length, result.receiveCycleTimestamp);

    // MOTU 828 MK3 V3 device->host (IR): the 8-byte "CIP" header is a fixed
    // non-standard constant (0d040400 22ffffff, EOH1=0 — NOT a valid IEC 61883
    // two-quadlet CIP), so CIPHeader::Decode would reject every packet. The
    // geometry is fixed and known from the wire ground-truth (DBS=16, 8 blocks,
    // each = SPH(4B) + 2 MSG chunks + 18 PCM x 3B at block byte offset 10).
    // See main repo MOTU_V3_WIRE_GROUNDTRUTH.md §"IR ground truth".
    if (format == ASFW::Encoding::AudioWireFormat::kMotuV3Packed) {
        constexpr size_t kMotuHeaderBytes = 8;     // fixed non-standard header
        constexpr size_t kMotuDbsQuadlets = 16;    // DBS = 16 (fixed)
        constexpr size_t kMotuBlockBytes = kMotuDbsQuadlets * 4u; // 64
        constexpr size_t kMotuPcmByteOffset = 10;  // SPH(4) + 2 MSG chunks(6)

        result.hasValidCip = true;  // treat as valid so the ZTS path runs
        result.syt = 0xFFFF;        // MOTU V3 never sends SYT
        result.fdf = 0x22;
        result.dbs = static_cast<uint8_t>(kMotuDbsQuadlets);
        result.dbc = 0;             // MOTU IR header DBC byte is always 0

        const size_t wireLen = length - kIsochHeaderSize;
        if (wireLen < kMotuHeaderBytes) {
            result.status = DirectRxWriteStatus::kInvalidRange;
            return result;
        }
        const size_t blockBytes = wireLen - kMotuHeaderBytes;
        const size_t eventCount = blockBytes / kMotuBlockBytes;
        result.framesDecoded = static_cast<uint32_t>(eventCount);
        if (eventCount == 0) {
            result.status = DirectRxWriteStatus::kAvailable; // empty packet
            return result;
        }
        if (!writer_.IsBound()) {
            result.status = DirectRxWriteStatus::kInvalidBinding;
            return result;
        }
        const uint8_t* block0 = payload + kIsochHeaderSize + kMotuHeaderBytes;
        for (size_t i = 0; i < eventCount; ++i) {
            int32_t* frameOut = writer_.Frame(absoluteFrame + i);
            if (!frameOut) {
                result.status = DirectRxWriteStatus::kInvalidRange;
                return result;
            }
            const uint8_t* block = block0 + i * kMotuBlockBytes;
            DecodeMotuV3Frame(block + kMotuPcmByteOffset, channels, frameOut);
        }
        writer_.PublishProducedEnd(absoluteFrame + eventCount,
                                   static_cast<uint32_t>(eventCount));
        result.status = DirectRxWriteStatus::kAvailable;
        return result;
    }

    const uint8_t* cipStart = payload + kIsochHeaderSize;
    const auto* quadlets = reinterpret_cast<const uint32_t*>(cipStart);
    
    // Decode CIP Header (quadlets[0] and quadlets[1])
    const auto cip = ASFW::Isoch::CIPHeader::Decode(quadlets[0], quadlets[1]);
    if (!cip) {
        result.status = DirectRxWriteStatus::kInvalidRange;
        return result;
    }

    result.hasValidCip = true;
    result.syt = cip->syt;
    result.fdf = cip->fdf;
    result.dbs = cip->dataBlockSize;
    result.dbc = cip->dataBlockCounter;

    const size_t payloadBytes = length - kIsochHeaderSize - 8;
    const size_t dbsBytes = static_cast<size_t>(cip->dataBlockSize) * 4u;
    if (dbsBytes == 0) {
        result.status = DirectRxWriteStatus::kInvalidRange;
        return result;
    }

    const size_t eventCount = payloadBytes / dbsBytes;
    result.framesDecoded = static_cast<uint32_t>(eventCount);

    if (eventCount == 0) {
        result.status = DirectRxWriteStatus::kAvailable;
        return result;
    }

    // If unarmed: parse timing/counters, and drop PCM
    if (!writer_.IsBound()) {
        result.status = DirectRxWriteStatus::kInvalidBinding;
        return result;
    }

    // Geometry validation
    if (channels == 0 ||
        cip->dataBlockSize < channels ||
        am824Slots != cip->dataBlockSize) {
        result.status = DirectRxWriteStatus::kInvalidRange;
        return result;
    }

    // If armed: decode quadlets directly to ADK input memory
    const uint32_t* dataBlocks = &quadlets[2];
    for (size_t i = 0; i < eventCount; ++i) {
        int32_t* frameOut = writer_.Frame(absoluteFrame + i);
        if (!frameOut) {
            result.status = DirectRxWriteStatus::kInvalidRange;
            return result;
        }

        const uint32_t* frameIn = dataBlocks + (i * cip->dataBlockSize);
        DecodeDirectRxFrame(frameIn, channels, cip->dataBlockSize, format, frameOut);
    }

    const uint64_t producedEnd = absoluteFrame + eventCount;
    writer_.PublishProducedEnd(producedEnd, static_cast<uint32_t>(eventCount));
    
    result.status = DirectRxWriteStatus::kAvailable;
    return result;
}

} // namespace ASFW::AudioEngine::Direct::Rx
