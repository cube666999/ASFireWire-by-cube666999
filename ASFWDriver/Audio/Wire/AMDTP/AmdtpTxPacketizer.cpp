#include "AmdtpTxPacketizer.hpp"

#include "../IEC61883/Syt.hpp"

namespace ASFW::Protocols::Audio::AMDTP {

// Design decisions (see ../../../README.md, Step 3):
//
// 1. Configure() selects the cadence from streamMode + sampleRate and rejects
//    anything but 48 kHz — honest failure over an untested rate path.
// 2. packetIndex comes from the caller's TxPacketSlotView; the packetizer owns
//    no cycle numbering.
// 3. Slot bytes are wire-order (big-endian); this is the single
//    logical-to-bus conversion point for the packet image.
// 4. Frame continuity is owned here (nextAudioFrame_, seeded by Reset);
//    AmdtpTimingState.nextAudioFrame is reserved for rebase logic
//    (Milestone 2) and ignored for now — the timeline stays gapless by
//    construction.
// 5. Golden rules (Linux amdtp + FFADO, see README): no-data packets are
//    CIP-header-only (8 bytes) with DBC carried unchanged; data packets carry
//    DBC of their first data block, advanced after emission.
//
// Failure contract: PrepareNextPacket mutates no state (cadence, DBC, frame
// counter) on any failure path, so a failed call can be retried with a
// corrected slot.

namespace {

constexpr uint32_t kCipHeaderBytes = 8;
constexpr uint32_t kBytesPerSlot = 4;

inline void WriteBE32(uint8_t* dest, uint32_t value) noexcept {
    dest[0] = static_cast<uint8_t>(value >> 24);
    dest[1] = static_cast<uint8_t>(value >> 16);
    dest[2] = static_cast<uint8_t>(value >> 8);
    dest[3] = static_cast<uint8_t>(value);
}

} // namespace

bool AmdtpTxPacketizer::Configure(const AmdtpStreamConfig& streamConfig,
                                  const AmdtpTxPolicy& txPolicy) noexcept {
    if (streamConfig.sampleRate != 48000) {
        return false; // only 48 kHz cadences exist; reject untested rates
    }

    AmdtpStreamConfig config = streamConfig;
    if (config.dbs == 0) {
        config.dbs = static_cast<uint8_t>(config.pcmChannels + config.midiSlots);
    }
    if (config.dbs == 0 || config.framesPerDataPacket == 0) {
        return false;
    }

    const uint32_t dataPacketBytes =
        kCipHeaderBytes + static_cast<uint32_t>(config.framesPerDataPacket) *
                              config.dbs * kBytesPerSlot;
    if (dataPacketBytes > config.maxPacketBytes) {
        return false;
    }

    streamConfig_ = config;
    txPolicy_ = txPolicy;

    IEC61883::CipHeaderConfig cipConfig{};
    cipConfig.sid = config.sid;
    cipConfig.dbs = config.dbs;
    cipConfig.fn = 0;
    cipConfig.qpc = 0;
    cipConfig.sph = config.sph;
    cipConfig.fmt = config.fmt;
    cipConfig.fdf = config.fdf;
    cipConfig.noDataFdf =
        txPolicy.preserveFdfInNoDataPackets ? config.fdf : 0xFF;
    cipBuilder_.Configure(cipConfig);

    cadence_ = (config.streamMode == StreamMode::Blocking)
                   ? static_cast<IAmdtpCadence*>(&blocking48kCadence_)
                   : static_cast<IAmdtpCadence*>(&nonBlocking48kCadence_);

    Reset(0, 0);
    return true;
}

void AmdtpTxPacketizer::BindTimeline(AmdtpPacketTimeline* timeline) noexcept {
    timeline_ = timeline;
}

void AmdtpTxPacketizer::Reset(uint8_t initialDbc,
                              uint64_t initialAudioFrame) noexcept {
    dbcCounter_.Reset(initialDbc);
    nextAudioFrame_ = initialAudioFrame;
    frameCursorAligned_ = false;
    if (cadence_ != nullptr) {
        cadence_->Reset();
    }
}

bool AmdtpTxPacketizer::AlignFrameCursorOnce(uint64_t frameIndex) noexcept {
    if (frameCursorAligned_) {
        return false;
    }
    nextAudioFrame_ = frameIndex;
    frameCursorAligned_ = true;
    return true;
}

bool AmdtpTxPacketizer::PrepareNextPacket(TxPacketSlotView slot,
                                          const AmdtpTimingState& timing,
                                          PreparedTxPacket& outPacket) noexcept {
    if (cadence_ == nullptr || timeline_ == nullptr || slot.bytes == nullptr) {
        return false;
    }

    const bool cadenceData = cadence_->CurrentCycleIsData();
    const bool isData =
        timing.disposition == AmdtpPacketDisposition::Data &&
        (timing.replayValid
             ? timing.replayDataBlocks != 0
             : cadenceData);
    const uint8_t frames =
        isData
            ? static_cast<uint8_t>(
                  timing.replayValid
                      ? timing.replayDataBlocks
                      : cadence_->CurrentCycleDataFrames())
            : 0;
    if (frames > streamConfig_.framesPerDataPacket) {
        return false;
    }
    const uint32_t payloadBytes =
        static_cast<uint32_t>(frames) * streamConfig_.dbs * kBytesPerSlot;
    const uint32_t byteCount =
        isData ? (kCipHeaderBytes + payloadBytes) : kCipHeaderBytes;

    if (slot.capacityBytes < byteCount) {
        return false; // no state advanced; caller may retry
    }

    const uint8_t dbc = dbcCounter_.ValueForNextPacket();

    outPacket = PreparedTxPacket{};
    outPacket.packetIndex = slot.packetIndex;
    outPacket.byteCount = byteCount;
    outPacket.isData = isData;
    outPacket.dbc = dbc;
    outPacket.dbs = streamConfig_.dbs;
    outPacket.firstAudioFrame = nextAudioFrame_;
    outPacket.framesInPacket = isData ? frames : 0;

    if (isData) {
        outPacket.syt = timing.txClockValid
                            ? timing.nextDataSyt
                            : IEC61883::SytFormatter::kNoInfo;

        WriteCipHeader(slot.bytes, cipBuilder_.BuildData(dbc, outPacket.syt));
        WriteDataPacketDefaults(slot.bytes, slot.capacityBytes, payloadBytes);
        if (txPolicy_.hostToDevicePcmEncoding ==
            PcmSlotEncoding::MotuV3Packed) {
            WriteMotuSph(slot.bytes, frames, timing);
        }

        if (!timeline_->ExposeDataPacket(outPacket, slot.bytes,
                                         slot.capacityBytes)) {
            return false; // bytes written but no counters advanced
        }

        dbcCounter_.AdvanceDataBlocks(frames);
        nextAudioFrame_ += frames;
    } else {
        outPacket.syt = IEC61883::SytFormatter::kNoInfo;

        // CIP-header-only: no payload, even as padding (DICE-II rejects it).
        WriteCipHeader(slot.bytes, cipBuilder_.BuildNoData(dbc));

        timeline_->MarkNoDataPacket(slot.packetIndex);
        // DBC deliberately not advanced.
    }

    cadence_->AdvanceCycle();
    return true;
}

const AmdtpStreamConfig& AmdtpTxPacketizer::StreamConfig() const noexcept {
    return streamConfig_;
}

const AmdtpTxPolicy& AmdtpTxPacketizer::TxPolicy() const noexcept {
    return txPolicy_;
}

bool AmdtpTxPacketizer::NextPacketWouldCarryData() const noexcept {
    return cadence_ != nullptr && cadence_->CurrentCycleIsData();
}

void AmdtpTxPacketizer::WriteDataPacketDefaults(uint8_t* packetBytes,
                                                uint32_t packetCapacityBytes,
                                                uint32_t payloadBytes) noexcept {
    (void)packetCapacityBytes; // capacity validated by the caller

    uint8_t* payload = packetBytes + kCipHeaderBytes;

    if (txPolicy_.clearPayloadBeforeExposure) {
        for (uint32_t i = 0; i < payloadBytes; ++i) {
            payload[i] = 0;
        }
    }

    // MOTU V3 data blocks are not 4-byte AM824 slots: the SPH quadlet (block
    // bytes 0-3) and the 2 MSG chunks (bytes 4-9) must stay zero here (the
    // clearPayload pass above already zeroed them; the SPH is written by
    // WriteMotuSph after exposure). Writing AM824 "non-audio slot" words would
    // corrupt the MSG/PCM region, so skip it for MOTU-packed.
    if (txPolicy_.hostToDevicePcmEncoding == PcmSlotEncoding::MotuV3Packed) {
        return;
    }

    if (txPolicy_.initializeNonAudioSlots &&
        streamConfig_.dbs > streamConfig_.pcmChannels) {
        const uint32_t frames = payloadBytes / (streamConfig_.dbs * kBytesPerSlot);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            for (uint32_t s = streamConfig_.pcmChannels; s < streamConfig_.dbs;
                 ++s) {
                WriteBE32(payload + (frame * streamConfig_.dbs + s) * kBytesPerSlot,
                          txPolicy_.defaultNonAudioSlotWord);
            }
        }
    }
}

void AmdtpTxPacketizer::WriteMotuSph(uint8_t* packetBytes,
                                    uint8_t frames,
                                    const AmdtpTimingState& timing) noexcept {
    // MOTU V3 source-packet-header: the first quadlet of each data block is a
    // presentation timestamp in OHCI cycle-time form (bits[24:12]=cycle 0-7999,
    // bits[11:0]=offset 0-3071), big-endian. Per block it advances one sample
    // period (512 ticks @ 48 kHz) so the timestamps are monotonic in transmit
    // order. Sourced from the per-packet presentation tick computed by the
    // driver (AmdtpTimingState.motuSphBaseTicks); the pre-replay / prefill
    // phase has no anchor (motuSphValid=false → SPH=0, which is enough for MOTU
    // to start IR from DATA packets but carries no real audio yet).
    // See main DevLog Fix 62: a stale/past SPH makes MOTU reject every frame.
    constexpr uint64_t kTicksPerCycle = 3072;
    constexpr uint64_t kCyclesPerSecond = 8000;
    constexpr uint64_t kTicksPerSecond = kTicksPerCycle * kCyclesPerSecond;
    constexpr uint64_t kTicksPerSample = 512; // 48 kHz: 3072*8000/48000

    for (uint8_t f = 0; f < frames; ++f) {
        uint32_t sph = 0;
        if (timing.motuSphValid) {
            const uint64_t base = static_cast<uint64_t>(timing.motuSphBaseTicks);
            const uint64_t tick =
                (base + static_cast<uint64_t>(f) * kTicksPerSample) %
                kTicksPerSecond;
            const uint32_t cyc =
                static_cast<uint32_t>((tick / kTicksPerCycle) % kCyclesPerSecond);
            const uint32_t off = static_cast<uint32_t>(tick % kTicksPerCycle);
            sph = (cyc << 12) | off;
        }
        uint8_t* block = packetBytes + kCipHeaderBytes +
                         static_cast<uint32_t>(f) * streamConfig_.dbs *
                             kBytesPerSlot;
        WriteBE32(block, sph);
    }
}

void AmdtpTxPacketizer::WriteCipHeader(
    uint8_t* packetBytes, const IEC61883::CipHeaderWords& header) noexcept {
    WriteBE32(packetBytes, header.q0);
    WriteBE32(packetBytes + 4, header.q1);
}

uint32_t AmdtpTxPacketizer::DataPacketBytes() const noexcept {
    return kCipHeaderBytes + PayloadBytes();
}

uint32_t AmdtpTxPacketizer::PayloadBytes() const noexcept {
    return static_cast<uint32_t>(streamConfig_.framesPerDataPacket) *
           streamConfig_.dbs * kBytesPerSlot;
}

} // namespace ASFW::Protocols::Audio::AMDTP
