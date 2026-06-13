// PacketAssembler.hpp
// ASFW - Phase 1.5 Encoding Layer
//
// Assembles complete AM824/CIP isochronous packets by combining:
//   - BlockingCadence48k / NonBlockingCadence48k (packet type + frame count)
//   - BlockingDbcGenerator (DBC tracking)
//   - AudioRingBuffer (sample source)
//   - AM824Encoder (sample encoding)
//   - CIPHeaderBuilder (header generation)
//
// Reference: docs/Isoch/PHASE_1_5_ENCODING.md
// Verified against: 000-48kORIG.txt FireBug capture
//

#pragma once

#include "AM824Encoder.hpp"
#include "CIPHeaderBuilder.hpp"
#include "BlockingCadence48k.hpp"
#include "NonBlockingCadence48k.hpp"
#include "BlockingDbcGenerator.hpp"
#include "AudioRingBuffer.hpp"
#include "../Config/AudioConstants.hpp"
#include "../../Logging/Logging.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace ASFW {
namespace Encoding {

enum class StreamMode : uint8_t {
    kNonBlocking = 0,
    kBlocking = 1,
};

/// Packet data encoding format.
enum class PacketEncoding : uint8_t {
    kAM824 = 0,  ///< Standard IEC 61883-6 AM824 (4 bytes/slot: label + 24-bit PCM)
    kMotuV3 = 1, ///< MOTU V3 packed format (3-byte PCM, SPH header, no AM824 labels)
};

/// Compile-time maximum frames per DATA packet (48k blocking path).
constexpr uint32_t kSamplesPerDataPacket = 8;

/// CIP header size in bytes
constexpr uint32_t kCIPHeaderSize = 8;

/// Compile-time max audio payload size (8 frames × max AM824 slots × 4 bytes)
constexpr size_t kMaxAudioDataSize =
    static_cast<size_t>(kSamplesPerDataPacket) * Isoch::Config::kMaxAmdtpDbs * sizeof(uint32_t);

/// Compile-time max assembled packet size (CIP header + max audio data)
constexpr size_t kMaxAssembledPacketSize = kCIPHeaderSize + kMaxAudioDataSize;

/// Underrun diagnostic snapshot (1A: detection).
/// All fields atomically updated in assembleDataPacket() hot path.
/// Read/reset from Poll() non-RT path for logging.
struct UnderrunDiag {
    std::atomic<uint64_t> underrunCount{0};
    std::atomic<uint32_t> lastFillLevel{0};
    std::atomic<uint32_t> lastRequestedFrames{0};
    std::atomic<uint32_t> lastAvailableFrames{0};
    std::atomic<uint64_t> lastCycleNumber{0};
    std::atomic<uint8_t>  lastDbc{0};
};

/// Assembled packet structure
struct AssembledPacket {
    uint8_t data[kMaxAssembledPacketSize]; ///< Packet bytes (big-endian wire order)
    uint32_t size;                   ///< Actual size: 8 for NO-DATA, varies for DATA
    bool isData;                     ///< True if DATA packet, false if NO-DATA
    uint8_t dbc;                     ///< DBC value used
    uint64_t cycleNumber;            ///< Cycle this packet is for
};

/// Assembles complete isochronous packets from audio samples.
///
/// Usage:
///   1. Create assembler with SID
///   2. Write audio to the ring buffer (from CoreAudio callback)
///   3. Call assembleNext() for each FireWire cycle (8000/sec)
///   4. Transmit or validate the assembled packet
///
// Hot-path layout intentionally keeps cadence, DBC, ring-buffer, and diagnostics separate.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
class PacketAssembler {
public:
    /// Construct a packet assembler.
    /// @param channels Number of PCM audio channels (1..Isoch::Config::kMaxPcmChannels)
    /// @param sid Source node ID (6 bits)
    explicit PacketAssembler(uint32_t channels = 2, uint8_t sid = 0) noexcept
        : channelCount_(channels)
        , am824SlotCount_(channels)
        , midiSlotsPerEvent_(0)
        , cipBuilder_(sid, static_cast<uint8_t>(channels)) {}

    /// Get channel count.
    uint32_t channelCount() const noexcept { return channelCount_; }

    /// Get AM824 slot count per event (CIP DBS on wire).
    uint32_t am824SlotCount() const noexcept { return am824SlotCount_; }

    /// Get additional non-audio AM824 slots (e.g. MIDI) per event.
    uint32_t midiSlotsPerEvent() const noexcept { return midiSlotsPerEvent_; }

    /// Get runtime data packet size in bytes.
    uint32_t dataPacketSize() const noexcept {
        const size_t payloadBytes =
            static_cast<size_t>(samplesPerDataPacket()) * am824SlotCount_ * sizeof(uint32_t);
        return static_cast<uint32_t>(kCIPHeaderSize + payloadBytes);
    }

    /// Get DATA packet frame count for the active stream mode (48k paths only).
    uint32_t samplesPerDataPacket() const noexcept {
        switch (streamMode_) {
            case StreamMode::kBlocking:
                return kSamplesPerPacket48k;
            case StreamMode::kNonBlocking:
                return kNonBlockingSamplesPerPacket48k;
        }
        return kSamplesPerDataPacket;
    }

    /// Reconfigure channel count and SID (resets all state).
    /// Use this instead of assignment since atomics prevent copy/move.
    void reconfigure(uint32_t channels, uint8_t sid) noexcept {
        reconfigureAM824(channels, channels, sid);
    }

    /// Reconfigure PCM channels and wire AM824 slot count (CIP DBS).
    /// `am824Slots` may exceed `channels` when the stream carries non-audio slots.
    /// `encoding` selects AM824 (standard) or MOTU V3 (3-byte packed) wire format.
    // Positional arguments mirror PCM-channels / wire-slots / SID reconfiguration.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void reconfigureAM824(uint32_t channels, uint32_t am824Slots, uint8_t sid,
                          PacketEncoding encoding = PacketEncoding::kAM824) noexcept {
        channelCount_ = channels;
        am824SlotCount_ = am824Slots;
        midiSlotsPerEvent_ = (am824SlotCount_ > channelCount_)
            ? (am824SlotCount_ - channelCount_)
            : 0;
        encoding_ = encoding;
        cipBuilder_ = CIPHeaderBuilder(sid, static_cast<uint8_t>(am824SlotCount_));
        cipBuilder_.setMotuV3Mode(encoding_ == PacketEncoding::kMotuV3);
        ringBuffer_.reconfigure(channels);
        blockingCadence_.reset();
        nonBlockingCadence_.reset();
        dbcGen_.reset();
        zeroCopyReadPos_ = 0;
        zeroCopyEnabled_ = false;
        zeroCopyBase_ = nullptr;
        zeroCopyCapacity_ = 0;
        dbgDataPackets_.store(0, std::memory_order_relaxed);
        dbgUnderrunPackets_.store(0, std::memory_order_relaxed);
        underrunDiag_.underrunCount.store(0, std::memory_order_relaxed);
        sweepChunk_ = 0;          // Reproducible sweep: always start at chunk 0 each StartIO
        sweepFrameCtr_ = 0;
        sweepSquarePhase_ = 0;
    }

    /// Set the source node ID.
    void setSID(uint8_t sid) noexcept {
        cipBuilder_.setSID(sid);
    }

    /// Set stream mode for upcoming packetization.
    void setStreamMode(StreamMode mode) noexcept {
        streamMode_ = mode;
    }

    /// Get configured stream mode.
    StreamMode streamMode() const noexcept {
        return streamMode_;
    }
    
    /// Get reference to the audio ring buffer for writing samples.
    AudioRingBuffer<>& ringBuffer() noexcept {
        return ringBuffer_;
    }

    /// Get const reference to the ring buffer.
    const AudioRingBuffer<>& ringBuffer() const noexcept {
        return ringBuffer_;
    }
    
    /// ZERO-COPY: Set direct audio source buffer (bypasses ring buffer)
    /// @param base Pointer to interleaved int32_t samples (channelCount_ channels)
    /// @param frameCapacity Total frames in buffer
    void setZeroCopySource(const int32_t* base, uint32_t frameCapacity) noexcept {
        zeroCopyBase_ = base;
        zeroCopyCapacity_ = frameCapacity;
        zeroCopyReadPos_ = 0;
        zeroCopyEnabled_ = (base != nullptr && frameCapacity > 0);
    }
    
    /// Check if zero-copy mode is enabled
    bool isZeroCopyEnabled() const noexcept { return zeroCopyEnabled_; }
    bool isMotuV3Encoding() const noexcept { return encoding_ == PacketEncoding::kMotuV3; }
    static constexpr bool isChannelSweepTest() noexcept { return kChannelSweepTest; }

    /// Update the OHCI cycle time used for MOTU V3 SPH timestamps.
    /// Call once per refill tick (before encodeToWire) from IsochAudioTxPipeline::UpdateSPH().
    /// SPH per Linux amdtp-motu.c write_sph(): sph = (cycleCount<<12)|cycleOffset = ct & 0x01FFFFFF.
    void setCurrentCycleTime(uint32_t ohciCycleTime) noexcept {
        currentCycleTime_ = ohciCycleTime;
        // Fix 62: seeding moved to writeMotuV3SphAndAdvance (first packet assembly).
        // Seeding here caused cycle=8 anchoring at driver init time vs actual TX cycle ~1100+,
        // resulting in SPH timestamps ~140ms in the past → MOTU rejected every frame.
    }
    
    /// Get zero-copy read position (for diagnostics)
    uint32_t zeroCopyReadPosition() const noexcept { return zeroCopyReadPos_; }

    /// Force zero-copy read position (used to synchronize with shared counters).
    void setZeroCopyReadPosition(uint32_t framePos) noexcept {
        if (zeroCopyCapacity_ == 0) return;
        zeroCopyReadPos_ = framePos % zeroCopyCapacity_;
    }

    /// Encode PCM frames using the configured encoding (AM824 or MOTU V3).
    /// Used by InjectNearHw to encode samples directly into the DMA payload buffer
    /// without going through assembleDataPacket (zero-copy inline path).
    ///
    /// Fix 68: for MOTU V3, also writes fresh SPH using a separate inject cursor.
    /// PrimeRing advanced sphTickCursor_ through all ring slots at startup; InjectNearHw
    /// overwrites only PCM bytes, leaving PrimeRing's stale SPH in each slot.  After
    /// one ring wrap (~25 ms) MOTU sees SPH jump back ~102 ms → frames mis-timed →
    /// silence / squeal.  The inject cursor (injectSphCursor_ / injectSphSeeded_) seeds
    /// from currentCycleTime_ on the first InjectNearHw write and advances monotonically,
    /// independent of the prime cursor so there is no double-advance.
    void encodeToWire(const int32_t* pcmInterleaved,
                      uint32_t frames,
                      uint32_t* outWireQuadlets) const noexcept {
        if (encoding_ == PacketEncoding::kMotuV3) {
            // Fix 70: zero the ENTIRE data block before writing SPH + PCM.
            // encodeToWire is the production InjectNearHw path. It previously wrote only
            // SPH (bytes 0-3) and the active PCM channels (ch0→byte10, ch1→byte13), leaving
            // MSG bytes 4-9 and all unused PCM slots (bytes 16..) holding whatever stale DMA
            // payload was there. MOTU then played that garbage on Analog 7 (slot 8, byte 34)
            // and S/PDIF (slot 12, byte 46) → squeal + wrong LEDs, even though Main Out L/R
            // were correct. Mirror assembleDataPacket's fillSilent-then-overwrite: zero the
            // whole block, lay down SPH, then encodeInterleavedFramesToMotuV3 overwrites only
            // the active PCM bytes — every other slot stays true silence (El Cap default).
            for (uint32_t f = 0; f < frames; ++f) {
                uint32_t* blockQuad = outWireQuadlets + static_cast<size_t>(f) * am824SlotCount_;
                for (uint32_t q = 0; q < am824SlotCount_; ++q) { blockQuad[q] = 0; }
                writeMotuV3InjectSphAndAdvance(reinterpret_cast<uint8_t*>(blockQuad));
            }
            encodeInterleavedFramesToMotuV3(pcmInterleaved, frames, outWireQuadlets);
        } else {
            encodeInterleavedFramesToAm824(pcmInterleaved, frames, outWireQuadlets);
        }
    }

    /// Assemble the next packet based on current cadence position.
    ///
    /// @param syt Presentation timestamp (SYT) for DATA packets
    /// @param silent When true, DATA packets get zero-filled audio (no ring buffer read,
    ///               no underrun counters). Cadence/DBC/CIP all advance normally.
    /// @return Assembled packet ready for transmission
    ///
    AssembledPacket assembleNext(uint16_t syt = 0, bool silent = false) noexcept {
        AssembledPacket packet{};
        packet.cycleNumber = currentCycleNumber();
        packet.isData = nextIsData();
        const uint8_t samplesInPacket = static_cast<uint8_t>(samplesPerDataPacket());
        packet.dbc = dbcGen_.getDbc(packet.isData, samplesInPacket);

        if (packet.isData) {
            if (silent) {
                assembleDataPacketSilent(packet, syt);
            } else {
                assembleDataPacket(packet, syt);
            }
        } else {
            assembleNoDataPacket(packet);
        }

        // Advance cadence for next cycle
        advanceCadence();

        return packet;
    }
    
    /// Get current fill level of the ring buffer in frames.
    uint32_t bufferFillLevel() const noexcept {
        return ringBuffer_.fillLevel();
    }
    
    /// Get underrun count (cycles where buffer was empty).
    uint64_t underrunCount() const noexcept {
        return ringBuffer_.underrunCount();
    }
    
    /// Get current cycle number.
    uint64_t currentCycle() const noexcept {
        return currentCycleNumber();
    }
    
    /// Check if next packet will be DATA.
    bool nextIsData() const noexcept {
        switch (streamMode_) {
            case StreamMode::kBlocking:
                return blockingCadence_.isDataPacket();
            case StreamMode::kNonBlocking:
                return nonBlockingCadence_.isDataPacket();
        }
        return true;
    }
    
    /// Reset all state to initial conditions.
    void reset() noexcept {
        blockingCadence_.reset();
        nonBlockingCadence_.reset();
        dbcGen_.reset();
        ringBuffer_.reset();
        zeroCopyReadPos_ = 0;  // Reset zero-copy read position
        sphSeeded_ = false;         // Fix 61: re-anchor SPH cursor on next write
        sphTickCursor_ = 0;
        injectSphSeeded_ = false;   // Fix 68: re-anchor inject cursor on next InjectNearHw
        injectSphCursor_ = 0;
    }

    /// Reset with specific initial DBC value.
    void reset(uint8_t initialDbc) noexcept {
        blockingCadence_.reset();
        nonBlockingCadence_.reset();
        dbcGen_.reset(initialDbc);
        ringBuffer_.reset();
        zeroCopyReadPos_ = 0;  // Reset zero-copy read position
        sphSeeded_ = false;         // Fix 61: re-anchor SPH cursor on next write
        sphTickCursor_ = 0;
        injectSphSeeded_ = false;   // Fix 68: re-anchor inject cursor on next InjectNearHw
        injectSphCursor_ = 0;
    }
    
private:
    /// Assemble a DATA packet (CIP + audio).
    void assembleDataPacket(AssembledPacket& packet, uint16_t syt) noexcept {
        const uint32_t framesPerPacket = samplesPerDataPacket();
        packet.size = dataPacketSize();

        // Build CIP header
        CIPHeader cip = cipBuilder_.build(packet.dbc, syt, false);

        // Copy CIP header (already in wire order)
        std::memcpy(packet.data, &cip.q0, 4);
        std::memcpy(packet.data + 4, &cip.q1, 4);

        // Read audio samples - ZERO-COPY path or ring buffer fallback
        int32_t samples[kSamplesPerDataPacket * Isoch::Config::kMaxPcmChannels];
        uint32_t framesRead = 0;

        if (zeroCopyEnabled_ && zeroCopyBase_) {
            // ZERO-COPY: Read directly from CoreAudio buffer
            // Buffer is interleaved, wraps at zeroCopyCapacity_
            for (uint32_t f = 0; f < framesPerPacket; ++f) {
                uint32_t frameIdx = (zeroCopyReadPos_ + f) % zeroCopyCapacity_;
                uint32_t sampleIdx = frameIdx * channelCount_;
                for (uint32_t ch = 0; ch < channelCount_; ++ch) {
                    samples[f * channelCount_ + ch] = zeroCopyBase_[sampleIdx + ch];
                }
            }
            zeroCopyReadPos_ = (zeroCopyReadPos_ + framesPerPacket) % zeroCopyCapacity_;
            framesRead = framesPerPacket;
        } else {
            // Fallback: Read from ring buffer (old path)
            framesRead = ringBuffer_.read(samples, framesPerPacket);
        }

        // Track counters (NO LOGGING IN HOT PATH - can stall for milliseconds)
        dbgDataPackets_.fetch_add(1, std::memory_order_relaxed);
        if (framesRead < framesPerPacket) {
            dbgUnderrunPackets_.fetch_add(1, std::memory_order_relaxed);

            // 1A: Underrun snapshot (RT-safe atomic stores, no logging)
            underrunDiag_.underrunCount.fetch_add(1, std::memory_order_relaxed);
            underrunDiag_.lastFillLevel.store(ringBuffer_.fillLevel(), std::memory_order_relaxed);
            underrunDiag_.lastRequestedFrames.store(framesPerPacket, std::memory_order_relaxed);
            underrunDiag_.lastAvailableFrames.store(framesRead, std::memory_order_relaxed);
            underrunDiag_.lastCycleNumber.store(packet.cycleNumber, std::memory_order_relaxed);
            underrunDiag_.lastDbc.store(packet.dbc, std::memory_order_relaxed);

            // SAFETY: Zero remaining samples to prevent encoding stale stack data
            size_t samplesRead = static_cast<size_t>(framesRead) * channelCount_;
            size_t totalSamples = static_cast<size_t>(framesPerPacket) * channelCount_;
            std::memset(&samples[samplesRead], 0, (totalSamples - samplesRead) * sizeof(int32_t));
        }

        // Encode samples: AM824 or MOTU V3 packed format
        uint32_t* audioQuadlets = reinterpret_cast<uint32_t*>(packet.data + kCIPHeaderSize);
        if (encoding_ == PacketEncoding::kMotuV3) {
            // Fix 61: non-zero-copy fallback. Lay down SPH (+ zero MSG/PCM, advance cursor)
            // first, then overwrite only the PCM bytes — mirrors the zero-copy two-phase write
            // so the SPH cursor advances exactly once per data packet here too.
            fillSilentMotuV3Frames(framesPerPacket, audioQuadlets);
            encodeInterleavedFramesToMotuV3(samples, framesPerPacket, audioQuadlets);
        } else {
            encodeInterleavedFramesToAm824(samples, framesPerPacket, audioQuadlets);
        }
    }
    
    /// Assemble a silent DATA packet (CIP header + zero-filled audio).
    /// Cadence/DBC advance normally, but no ring buffer read and no underrun counters.
    void assembleDataPacketSilent(AssembledPacket& packet, uint16_t syt) noexcept {
        const uint32_t framesPerPacket = samplesPerDataPacket();
        packet.size = dataPacketSize();

        CIPHeader cip = cipBuilder_.build(packet.dbc, syt, false);
        std::memcpy(packet.data, &cip.q0, 4);
        std::memcpy(packet.data + 4, &cip.q1, 4);

        uint32_t* audioQuadlets = reinterpret_cast<uint32_t*>(packet.data + kCIPHeaderSize);
        if (encoding_ == PacketEncoding::kMotuV3) {
            fillSilentMotuV3Frames(framesPerPacket, audioQuadlets);
        } else {
            // AM824/MBLA label 0x40 required — prevents garbage noise on strict devices.
            fillSilentAm824Frames(framesPerPacket, audioQuadlets);
        }
    }

    /// Assemble a NO-DATA packet (8 bytes: CIP only).
    void assembleNoDataPacket(AssembledPacket& packet) noexcept {
        packet.size = kCIPHeaderSize;
        
        // Build CIP header with SYT=0xFFFF
        CIPHeader cip = cipBuilder_.buildNoData(packet.dbc);
        
        // Copy CIP header (already in wire order)
        std::memcpy(packet.data, &cip.q0, 4);
        std::memcpy(packet.data + 4, &cip.q1, 4);
    }
    
    static constexpr uint32_t encodeMidiPlaceholder(uint32_t midiSlotIndex) noexcept {
        const uint8_t label = static_cast<uint8_t>(
            kAM824LabelMIDIConformantBase + (midiSlotIndex & 0x03u));
        return AM824Encoder::encodeLabelOnly(label);
    }

    void encodeInterleavedFramesToAm824(const int32_t* pcmInterleaved,
                                        uint32_t frames,
                                        uint32_t* outWireQuadlets) const noexcept {
        for (uint32_t f = 0; f < frames; ++f) {
            const int32_t* frameIn = pcmInterleaved + (static_cast<size_t>(f) * channelCount_);
            uint32_t* frameOut = outWireQuadlets + (static_cast<size_t>(f) * am824SlotCount_);

            for (uint32_t ch = 0; ch < channelCount_; ++ch) {
                frameOut[ch] = AM824Encoder::encode(frameIn[ch]);
            }
            for (uint32_t s = 0; s < midiSlotsPerEvent_; ++s) {
                frameOut[channelCount_ + s] = encodeMidiPlaceholder(s);
            }
        }
    }

    void fillSilentAm824Frames(uint32_t frames, uint32_t* outWireQuadlets) const noexcept {
        const uint32_t silence = AM824Encoder::encodeSilence();
        for (uint32_t f = 0; f < frames; ++f) {
            uint32_t* frameOut = outWireQuadlets + (static_cast<size_t>(f) * am824SlotCount_);
            for (uint32_t ch = 0; ch < channelCount_; ++ch) {
                frameOut[ch] = silence;
            }
            for (uint32_t s = 0; s < midiSlotsPerEvent_; ++s) {
                frameOut[channelCount_ + s] = encodeMidiPlaceholder(s);
            }
        }
    }

    /// MOTU V3 packet encoding: 3-byte packed PCM per amdtp-motu.c.
    ///
    /// Data block layout (am824SlotCount_ quadlets = am824SlotCount_×4 bytes):
    ///   Byte  0– 3: SPH — presentation timestamp (big-endian uint32_t), derived from OHCI CycleTimer.
    ///               Formula per amdtp-motu.c write_sph(): sph = (cycleCount << 12) | cycleOffset
    ///                 = ct & 0x01FFFFFF  (cycleCount=ct[24:12], cycleOffset=ct[11:0])
    ///               Updated once per refill tick via setCurrentCycleTime().
    ///   Byte  4– 9: 2 × msg_chunk (3 bytes each, MIDI/control — zeros = no MIDI)
    ///   Byte 10-10+N×3-1: PCM channels 0..N-1 (3 bytes each, 24-bit big-endian)
    ///   Remaining: zero padding to quadlet boundary
    void encodeInterleavedFramesToMotuV3(const int32_t* pcmInterleaved,
                                         uint32_t frames,
                                         uint32_t* outWireQuadlets) const noexcept {
        const uint32_t totalBytes = am824SlotCount_ * 4u;
        const uint32_t pcmSlots   = (totalBytes - 10u) / 3u; // how many 3-byte slots fit
        const uint32_t chCount    = (channelCount_ < pcmSlots) ? channelCount_ : pcmSlots;

        // ===== CHANNEL SWEEP DIAGNOSTIC (set kChannelSweepTest=true to enable) =====
        // Writes a ~440 Hz square-wave tone to exactly ONE PCM chunk at a time, rotating
        // to the next chunk every 3 s. Zeros every other PCM chunk. Lets the listener map
        // chunk index → physical output empirically, and locate which chunk squeaks.
        if (kChannelSweepTest) {
            constexpr uint32_t kSweepFramesPerChunk = 480000u; // 10 s @ 48 kHz
            constexpr uint32_t kSquarePeriod        = 109u;    // ~440 Hz @ 48 kHz
            constexpr uint32_t kSquareHalf          = 54u;
            constexpr int32_t  kAmp                 = 0x10000000; // ~ -8 dBFS, high-aligned
            for (uint32_t f = 0; f < frames; ++f) {
                uint32_t* blockQuad = outWireQuadlets + static_cast<size_t>(f) * am824SlotCount_;
                auto* block = reinterpret_cast<uint8_t*>(blockQuad);
                // Zero entire PCM region (keep SPH bytes 0-3 + MSG bytes 4-9 intact).
                for (uint32_t b = 10u; b < totalBytes; ++b) block[b] = 0u;
                // Generate square-wave sample.
                const int32_t s = (sweepSquarePhase_ < kSquareHalf) ? kAmp : -kAmp;
                sweepSquarePhase_ = (sweepSquarePhase_ + 1u) % kSquarePeriod;
                // Write tone into the active chunk.
                if (sweepChunk_ < pcmSlots) {
                    uint8_t* dst = block + 10u + sweepChunk_ * 3u;
                    const uint32_t u = static_cast<uint32_t>(s);
                    dst[0] = static_cast<uint8_t>((u >> 24) & 0xFFu);
                    dst[1] = static_cast<uint8_t>((u >> 16) & 0xFFu);
                    dst[2] = static_cast<uint8_t>((u >>  8) & 0xFFu);
                }
                // Advance chunk every 3 s; log each transition.
                if (++sweepFrameCtr_ >= kSweepFramesPerChunk) {
                    sweepFrameCtr_ = 0u;
                    sweepChunk_ = (sweepChunk_ + 1u) % pcmSlots;
                    ASFW_LOG(Isoch, "[SWEEP] tone now on chunk %u (of %u PCM chunks)",
                             sweepChunk_, pcmSlots);
                }
            }
            return;
        }
        // ===== END CHANNEL SWEEP DIAGNOSTIC =====

        // Fix 61: write ONLY the PCM bytes. SPH (bytes 0-3) and MSG (bytes 4-9) are written by
        // the silent pre-fill (fillSilentMotuV3Frames) which owns the monotonic per-block SPH
        // cursor. This path overwrites the PCM region only, so the SPH cursor is never
        // double-advanced and stays monotonic in transmit order — the prior code rewrote SPH
        // here, resetting it to the per-refill base each packet → backward jumps → MOTU
        // rejected every frame (total silence). Unused PCM slots stay zero from the pre-fill.
        for (uint32_t f = 0; f < frames; ++f) {
            uint32_t* blockQuad = outWireQuadlets + static_cast<size_t>(f) * am824SlotCount_;
            auto* block = reinterpret_cast<uint8_t*>(blockQuad);

            const int32_t* frameIn = pcmInterleaved + static_cast<size_t>(f) * channelCount_;
            // DEBUG: log ch0/ch1 PCM values once per ~1s to diagnose right-channel squeak
            if (f == 0 && frames > 0 && chCount > 0) {
                static uint32_t dbgPktCount = 0;
                ++dbgPktCount;
                if (dbgPktCount % 6000u == 1u) {  // ~1s at 6000 data pkts/s (48kHz/8)
                    const uint32_t s0 = static_cast<uint32_t>(frameIn[0]);
                    const uint32_t s1 = (chCount > 1) ? static_cast<uint32_t>(frameIn[1]) : 0u;
                    ASFW_LOG(Isoch, "[DBG-PCM] ch0=0x%08x ch1=0x%08x chCount=%u sph=0x%02x%02x%02x%02x",
                             s0, s1, chCount, block[0], block[1], block[2], block[3]);
                }
            }
            for (uint32_t ch = 0; ch < chCount; ++ch) {
                const uint32_t s = static_cast<uint32_t>(frameIn[ch]);
                // Fix 72: OS stereo goes to PCM slots 10/11, not 0/1.
                // Ground-truth from the El Capitan snoop (MB2009, working Apple driver,
                // playing on Main+Phones): across all 194 captured DATA packets the ONLY
                // non-zero PCM was at block byte 40 (slot 10) and byte 43 (slot 11) — e.g.
                // slot10=0x38b524, slot11=0x389e5f. Every other slot was zero. MOTU's
                // internal CueMix fans that one stereo pair out to Main + Phones. Our prior
                // byte-10/13 (slot 0/1) placement was wrong (the old doc misread the map),
                // which is why MOTU squealed / lit Analog 7 + S/PDIF instead of playing.
                // kMotuV3OutputSlotBase=10 shifts ch0→slot10 (byte40), ch1→slot11 (byte43).
                constexpr uint32_t kMotuV3OutputSlotBase = 10u;
                uint8_t* dst = block + 10u + (kMotuV3OutputSlotBase + ch) * 3u;
                // Fix 52: high-aligned int32 (ADK with FormatFlagIsAlignedHigh → bits [31:8])
                // IORegistry confirms: IOAudioStreamAlignment=1 (kIOAudioStreamAlignmentHighByte)
                dst[0] = static_cast<uint8_t>((s >> 24) & 0xFFu); // MSB — bits [31:24]
                dst[1] = static_cast<uint8_t>((s >> 16) & 0xFFu);
                dst[2] = static_cast<uint8_t>((s >>  8) & 0xFFu); // LSB — bits [15:8]
            }
        }
    }

    void fillSilentMotuV3Frames(uint32_t frames, uint32_t* outWireQuadlets) const noexcept {
        // Same alignment constraint as encodeInterleavedFramesToMotuV3 — use uint32_t writes.
        // Fix 61: this path owns the monotonic SPH cursor. Every transmitted data block (silent
        // or, after PCM overwrite, audio) gets a presentation timestamp advancing one sample
        // period (512 ticks @ 48 kHz). Called in transmit/fill order (Prime + Refill Phase 2),
        // so the cursor stays monotonic across packets — MOTU V3 needs this to time-align and
        // place fetched PCM frames into its channel matrix.
        for (uint32_t f = 0; f < frames; ++f) {
            uint32_t* blockQuad = outWireQuadlets + static_cast<size_t>(f) * am824SlotCount_;
            for (uint32_t q = 0; q < am824SlotCount_; ++q) { blockQuad[q] = 0; }
            writeMotuV3SphAndAdvance(reinterpret_cast<uint8_t*>(blockQuad));
        }
    }

    /// Write the next monotonic SPH (big-endian) into a data block's first 4 bytes and advance
    /// the cursor by one sample period. amdtp-motu.c: tick = cycle*3072 + offset, advancing
    /// 512 ticks/sample @ 48 kHz; cycle wraps at 8000 (TICKS_PER_SECOND = 3072*8000).
    void writeMotuV3SphAndAdvance(uint8_t* block) const noexcept {
        constexpr uint32_t kTicksPerCycle   = 3072u;
        constexpr uint32_t kCyclesPerSecond = 8000u;
        constexpr uint64_t kTicksPerSecond  =
            static_cast<uint64_t>(kTicksPerCycle) * kCyclesPerSecond;
        constexpr uint32_t kTicksPerSample  = 512u; // 48 kHz: 3072*8000/48000

        // Fix 62: seed at first packet assembly using currentCycleTime_ (updated each refill tick).
        // Previously seeded in setCurrentCycleTime which fires at driver init (cycle≈8), long
        // before actual TX starts (cycle≈1100+) → SPH was ~140ms in the past → MOTU rejected.
        if (!sphSeeded_) {
            const uint32_t cyc = (currentCycleTime_ >> 12) & 0x1FFFu;
            const uint32_t off = currentCycleTime_ & 0x0FFFu;
            sphTickCursor_ = static_cast<uint64_t>((cyc % kCyclesPerSecond) * kTicksPerCycle + off)
                           + 2ull * kTicksPerCycle;
            sphSeeded_ = true;
        }

        const uint64_t tick = sphTickCursor_ % kTicksPerSecond;
        const uint32_t cyc  = static_cast<uint32_t>((tick / kTicksPerCycle) % kCyclesPerSecond);
        const uint32_t off  = static_cast<uint32_t>(tick % kTicksPerCycle);
        const uint32_t sph  = (cyc << 12) | off;
        block[0] = static_cast<uint8_t>((sph >> 24) & 0xFFu);
        block[1] = static_cast<uint8_t>((sph >> 16) & 0xFFu);
        block[2] = static_cast<uint8_t>((sph >>  8) & 0xFFu);
        block[3] = static_cast<uint8_t>( sph        & 0xFFu);
        sphTickCursor_ += kTicksPerSample;
    }

    /// Fix 68: separate SPH cursor for InjectNearHw path.
    /// Identical logic to writeMotuV3SphAndAdvance but uses injectSphCursor_ /
    /// injectSphSeeded_ so the prime cursor (sphTickCursor_) is never advanced
    /// from the inject path — avoids the double-advance that caused Fix 65 to regress.
    void writeMotuV3InjectSphAndAdvance(uint8_t* block) const noexcept {
        constexpr uint32_t kTicksPerCycle   = 3072u;
        constexpr uint32_t kCyclesPerSecond = 8000u;
        constexpr uint64_t kTicksPerSecond  =
            static_cast<uint64_t>(kTicksPerCycle) * kCyclesPerSecond;
        constexpr uint32_t kTicksPerSample  = 512u;

        if (!injectSphSeeded_) {
            const uint32_t cyc = (currentCycleTime_ >> 12) & 0x1FFFu;
            const uint32_t off = currentCycleTime_ & 0x0FFFu;
            injectSphCursor_ = static_cast<uint64_t>((cyc % kCyclesPerSecond) * kTicksPerCycle + off)
                             + 2ull * kTicksPerCycle;
            injectSphSeeded_ = true;
        }

        const uint64_t tick = injectSphCursor_ % kTicksPerSecond;
        const uint32_t cyc  = static_cast<uint32_t>((tick / kTicksPerCycle) % kCyclesPerSecond);
        const uint32_t off  = static_cast<uint32_t>(tick % kTicksPerCycle);
        const uint32_t sph  = (cyc << 12) | off;
        block[0] = static_cast<uint8_t>((sph >> 24) & 0xFFu);
        block[1] = static_cast<uint8_t>((sph >> 16) & 0xFFu);
        block[2] = static_cast<uint8_t>((sph >>  8) & 0xFFu);
        block[3] = static_cast<uint8_t>( sph        & 0xFFu);
        injectSphCursor_ += kTicksPerSample;
    }

    uint32_t channelCount_{2};               ///< Number of PCM audio channels
    uint32_t am824SlotCount_{2};             ///< Wire slots per event (CIP DBS)
    PacketEncoding encoding_{PacketEncoding::kAM824}; ///< Wire encoding format
    uint32_t midiSlotsPerEvent_{0};          ///< Extra AM824 slots after PCM (MIDI, etc.)
    uint32_t currentCycleTime_{0};           ///< OHCI CycleTimer snapshot for MOTU V3 SPH (updated per refill tick)
    mutable uint64_t sphTickCursor_{0};      ///< Fix 61: monotonic SPH tick cursor (prime path), +512/sample
    mutable bool sphSeeded_{false};          ///< Fix 61: whether sphTickCursor_ has been anchored to HW cycle time
    mutable uint64_t injectSphCursor_{0};   ///< Fix 68: separate SPH cursor for InjectNearHw path
    mutable bool injectSphSeeded_{false};   ///< Fix 68: whether injectSphCursor_ has been seeded

    // Channel sweep diagnostic state (active only when kChannelSweepTest == true).
    static constexpr bool kChannelSweepTest = false;  ///< DIAGNOSTIC: rotate test tone across PCM chunks (off — real PCM → slots 0/1 Main Out)
    mutable uint32_t sweepChunk_{0};         ///< Active PCM chunk index for the sweep tone
    mutable uint32_t sweepFrameCtr_{0};      ///< Frames elapsed on the current chunk
    mutable uint32_t sweepSquarePhase_{0};   ///< Square-wave phase counter
    uint64_t currentCycleNumber() const noexcept {
        switch (streamMode_) {
            case StreamMode::kBlocking:
                return blockingCadence_.getTotalCycles();
            case StreamMode::kNonBlocking:
                return nonBlockingCadence_.getTotalCycles();
        }
        return 0;
    }

    void advanceCadence() noexcept {
        switch (streamMode_) {
            case StreamMode::kBlocking:
                blockingCadence_.advance();
                break;
            case StreamMode::kNonBlocking:
                nonBlockingCadence_.advance();
                break;
        }
    }

    BlockingCadence48k blockingCadence_;     ///< 48k blocking cadence pattern
    NonBlockingCadence48k nonBlockingCadence_; ///< 48k non-blocking cadence pattern
    BlockingDbcGenerator dbcGen_;            ///< DBC tracker
    CIPHeaderBuilder cipBuilder_;            ///< CIP header builder
    AudioRingBuffer<> ringBuffer_;           ///< Audio sample buffer (fallback)
    
    // ZERO-COPY: Direct audio source (bypasses ring buffer)
    const int32_t* zeroCopyBase_{nullptr};
    uint32_t zeroCopyCapacity_{0};
    mutable uint32_t zeroCopyReadPos_{0}; // mutable for read position tracking
    bool zeroCopyEnabled_{false};
    StreamMode streamMode_{StreamMode::kBlocking};
    
    // 1A: Underrun diagnostics (RT-safe atomics, read from Poll)
    UnderrunDiag underrunDiag_;

    // Debug counters (for 1Hz logging instead of hot-path logging)
    std::atomic<uint64_t> dbgDataPackets_{0};
    std::atomic<uint64_t> dbgUnderrunPackets_{0};
    
public:
    /// Snapshot debug counters for 1Hz logging (resets counters atomically)
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void snapshotDebug(uint64_t& dataPkts, uint64_t& underruns) noexcept {
        dataPkts = dbgDataPackets_.exchange(0, std::memory_order_relaxed);
        underruns = dbgUnderrunPackets_.exchange(0, std::memory_order_relaxed);
    }

    /// 1A: Record an underrun from external caller (zero-copy path).
    /// Called by IsochTransmitContext when zeroCopyFillBefore < framesPerPacket.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void recordUnderrun(uint32_t fillLevel, uint32_t requestedFrames,
                        uint32_t availableFrames, uint64_t cycleNumber, // NOLINT(bugprone-easily-swappable-parameters)
                        uint8_t dbc) noexcept {
        underrunDiag_.underrunCount.fetch_add(1, std::memory_order_relaxed);
        underrunDiag_.lastFillLevel.store(fillLevel, std::memory_order_relaxed);
        underrunDiag_.lastRequestedFrames.store(requestedFrames, std::memory_order_relaxed);
        underrunDiag_.lastAvailableFrames.store(availableFrames, std::memory_order_relaxed);
        underrunDiag_.lastCycleNumber.store(cycleNumber, std::memory_order_relaxed);
        underrunDiag_.lastDbc.store(dbc, std::memory_order_relaxed);
    }

    /// 1A: Read underrun diagnostic snapshot (returns current count, not delta).
    const UnderrunDiag& underrunDiag() const noexcept { return underrunDiag_; }
};

} // namespace Encoding
} // namespace ASFW
