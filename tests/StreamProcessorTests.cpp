/**
 * StreamProcessorTests.cpp
 *
 * Unit tests for StreamProcessor and AM824Decoder using synthetic
 * IEC 61883-6 packets.  No hardware, DriverKit, or DMA required —
 * both classes are header-only and tested purely through ProcessPacket()
 * and the AM824Decoder static helpers.
 *
 * Packet memory layout expected by StreamProcessor::ProcessPacket():
 *   [0..7]   Isoch prefix (8 bytes: timestamp + isoch header, may be zero)
 *   [8..11]  CIP quadlet-0  — stored in host (LE) byte order, simulating OHCI pre-swap
 *   [12..15] CIP quadlet-1  — stored in host (LE) byte order, simulating OHCI pre-swap
 *   [16+]    AM824 data quadlets — stored in host (LE) byte order, simulating OHCI pre-swap
 *
 * Rationale: on LE (ARM64) hosts the OHCI hardware byte-swaps every received quadlet
 * as it writes it into the DMA buffer.  The decoders therefore read the semantic
 * big-endian value directly, with no further SwapBigToHost call.  Tests must mirror
 * this by storing values in LE/host byte order.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>
#include <optional>

#include "ASFWDriver/Isoch/Receive/StreamProcessor.hpp"
#include "ASFWDriver/Isoch/Audio/AM824Decoder.hpp"
#include "ASFWDriver/Isoch/Core/IsochTypes.hpp"

using namespace ASFW::Isoch;

// ============================================================================
// Packet builder helpers
// ============================================================================

namespace {

/// Write a host-order uint32 as 4 host-endian (LE) bytes at dst.
/// This simulates the OHCI pre-swap: the hardware writes each received wire
/// quadlet byte-reversed into the DMA buffer, so what we read back is the
/// semantic big-endian value in host byte order.
void PutLE32(uint8_t* dst, uint32_t hostVal) {
    memcpy(dst, &hostVal, 4);
}

/// Build a uint32_t representing an AM824 quadlet in OHCI-pre-swapped form.
/// The returned value is the semantic big-endian value as a host uint32_t
/// (i.e. label occupies bits 31:24, sample24 occupies bits 23:0).
/// Pass directly to AM824Decoder::DecodeSample() / IsMIDI(), or store via
/// PutLE32() into a packet buffer to simulate what OHCI writes to DMA memory.
uint32_t MakeBEQuad(uint8_t label, uint32_t sample24 = 0) {
    return (static_cast<uint32_t>(label) << 24) | (sample24 & 0x00FF'FFFFu);
}

/**
 * Build a synthetic IEC 61883-6 packet with OHCI-pre-swapped byte layout.
 *
 * Layout:
 *   [0..7]   8-byte isoch prefix (zeroed)
 *   [8..11]  CIP Q0 in host (LE) byte order: EOH=0 | SID<<24 | DBS<<16 | DBC
 *   [12..15] CIP Q1 in host (LE) byte order: EOH=1 | FMT(0x10)<<24 | fdf<<16 | syt
 *   [16+]    am824Quadlets in host (LE) byte order (use MakeBEQuad() to build each)
 *
 * @param sid  Source node ID (6 bits)
 * @param dbs  Data Block Size — quadlets per data block (e.g. 2 = stereo)
 * @param dbc  Data Block Counter
 * @param fdf  Format Dependent Field
 * @param syt  SYT timestamp (0xFFFF = no info)
 * @param am824Quadlets  Pre-encoded quadlets (each built with MakeBEQuad)
 */
std::vector<uint8_t> MakePacket(
    uint8_t sid, uint8_t dbs, uint8_t dbc,
    uint8_t fdf, uint16_t syt,
    const std::vector<uint32_t>& am824Quadlets = {})
{
    const size_t payloadBytes = am824Quadlets.size() * 4u;
    std::vector<uint8_t> pkt(8u + 8u + payloadBytes, 0x00u);

    // CIP Q0: EOH=0, SID, DBS, DBC  — store as LE to simulate OHCI pre-swap
    const uint32_t q0Host =
        (static_cast<uint32_t>(sid) << 24) |
        (static_cast<uint32_t>(dbs) << 16) |
        static_cast<uint32_t>(dbc);
    PutLE32(pkt.data() + 8, q0Host);

    // CIP Q1: EOH=1, FMT=0x10 (AM824), fdf, syt  — store as LE
    const uint32_t q1Host =
        (1u << 31) |
        (0x10u << 24) |
        (static_cast<uint32_t>(fdf) << 16) |
        static_cast<uint32_t>(syt);
    PutLE32(pkt.data() + 12, q1Host);

    // AM824 payload — store as LE to simulate OHCI pre-swap
    for (size_t i = 0; i < am824Quadlets.size(); ++i) {
        // am824Quadlets[i] is the semantic value (label in bits 31:24);
        // store in host byte order so ProcessPacket reads it back correctly.
        PutLE32(pkt.data() + 16u + i * 4u, am824Quadlets[i]);
    }

    return pkt;
}

/// Build stereo PCM AM824 quadlets for N events.
/// channel 0: sample = baseVal,       channel 1: sample = baseVal + 1
std::vector<uint32_t> MakeStereoAM824(size_t eventCount, uint32_t baseVal = 0x112233) {
    std::vector<uint32_t> quads;
    quads.reserve(eventCount * 2);
    for (size_t i = 0; i < eventCount; ++i) {
        quads.push_back(MakeBEQuad(0x40, baseVal));
        quads.push_back(MakeBEQuad(0x40, baseVal + 1));
    }
    return quads;
}

} // namespace

// ============================================================================
// StreamProcessor — short / malformed packet rejection
// ============================================================================

TEST(StreamProcessor, ShortPacket_LessThan16Bytes_ReturnsNoValidCip) {
    // Need at least 16 bytes (8 isoch + 8 CIP); 15 must fail.
    StreamProcessor sp;
    std::vector<uint8_t> buf(15, 0x00);

    const auto summary = sp.ProcessPacket(buf.data(), buf.size());

    EXPECT_FALSE(summary.hasValidCip);
    EXPECT_EQ(sp.ErrorCount(), 1u);
    EXPECT_EQ(sp.PacketCount(), 0u);
}

TEST(StreamProcessor, ExactlyHeaderSize_NoPayload_CountsAsEmptyPacket) {
    // 16 bytes = isoch prefix (8) + CIP header (8), payloadBytes = 0 → emptyPacketCount++
    StreamProcessor sp;
    auto pkt = MakePacket(0x3F, 2, 0, 0x00, 0xFFFF);  // DBS=2, no AM824 data
    ASSERT_EQ(pkt.size(), 16u);

    const auto summary = sp.ProcessPacket(pkt.data(), pkt.size());

    EXPECT_TRUE(summary.hasValidCip);
    EXPECT_EQ(sp.PacketCount(), 1u);
    EXPECT_EQ(sp.EmptyPacketCount(), 1u);
    EXPECT_EQ(sp.ErrorCount(), 0u);
}

TEST(StreamProcessor, InvalidCIPQ0_EOH1_ReturnsNoValidCip) {
    // CIP Q0 must have EOH=0 (bit 31 of host value = 0); set bit 31 to force rejection.
    StreamProcessor sp;
    auto pkt = MakePacket(0x3F, 2, 0, 0x00, 0xFFFF);

    // Overwrite Q0: set bit 31 → EOH=1 → invalid.
    // With LE storage Q0 starts at byte 8; bit 31 lives in the most-significant
    // byte which is at offset +3 (byte 11) on a little-endian machine.
    pkt[11] |= 0x80;

    const auto summary = sp.ProcessPacket(pkt.data(), pkt.size());

    EXPECT_FALSE(summary.hasValidCip);
    EXPECT_EQ(sp.ErrorCount(), 1u);
}

TEST(StreamProcessor, InvalidCIPQ1_EOH0_ReturnsNoValidCip) {
    // CIP Q1 must have EOH=1 (bit 31); clear it to force rejection.
    StreamProcessor sp;
    auto pkt = MakePacket(0x3F, 2, 0, 0x00, 0xFFFF);

    // Q1 starts at byte 12. Clear bit 31 of host value → EOH=0 → invalid.
    // With LE storage bit 31 lives at offset +3 from Q1 start, i.e. byte 15.
    pkt[15] &= 0x7F;

    const auto summary = sp.ProcessPacket(pkt.data(), pkt.size());

    EXPECT_FALSE(summary.hasValidCip);
    EXPECT_EQ(sp.ErrorCount(), 1u);
}

// ============================================================================
// StreamProcessor — valid stereo 48 kHz packet
// ============================================================================

TEST(StreamProcessor, ValidStereoPacket_ParsesAllCIPFields) {
    // Stereo 48kHz: DBS=2, FDF=0x02, SYT=0x1234
    constexpr uint8_t  kSID = 0x3F;
    constexpr uint8_t  kDBS = 2;
    constexpr uint8_t  kDBC = 0;
    constexpr uint8_t  kFDF = 0x02;
    constexpr uint16_t kSYT = 0x1234;
    constexpr size_t   kEvents = 8;

    auto quads = MakeStereoAM824(kEvents);
    auto pkt   = MakePacket(kSID, kDBS, kDBC, kFDF, kSYT, quads);

    StreamProcessor sp;
    const auto summary = sp.ProcessPacket(pkt.data(), pkt.size());

    EXPECT_TRUE(summary.hasValidCip);
    EXPECT_EQ(summary.dbs, kDBS);
    EXPECT_EQ(summary.fdf, kFDF);
    EXPECT_EQ(summary.syt, kSYT);
    EXPECT_EQ(sp.PacketCount(), 1u);
    EXPECT_EQ(sp.SamplePacketCount(), 1u);
    EXPECT_EQ(sp.EmptyPacketCount(), 0u);
    EXPECT_EQ(sp.ErrorCount(), 0u);
}

// ============================================================================
// StreamProcessor — DBC continuity tracking
// ============================================================================

TEST(StreamProcessor, DBCContinuity_SequentialPackets_NoDiscontinuity) {
    // Two packets with consecutive DBCs.  Each packet has 8 events and DBS=2
    // so the next expected DBC = (lastDBC + 8) & 0xFF.
    StreamProcessor sp;

    auto quads = MakeStereoAM824(8);

    // Packet 1: DBC=0
    auto pkt1 = MakePacket(0x3F, 2, 0, 0x00, 0xFFFF, quads);
    (void)sp.ProcessPacket(pkt1.data(), pkt1.size());
    ASSERT_EQ(sp.PacketCount(), 1u);

    // Packet 2: DBC=8 (expected = (0 + 8) & 0xFF)
    auto pkt2 = MakePacket(0x3F, 2, 8, 0x00, 0xFFFF, quads);
    (void)sp.ProcessPacket(pkt2.data(), pkt2.size());

    EXPECT_EQ(sp.PacketCount(), 2u);
    EXPECT_EQ(sp.DiscontinuityCount(), 0u);
}

TEST(StreamProcessor, DBCContinuity_SkippedPacket_IncrementsDiscontinuityCount) {
    // Packet 1: DBC=0, 8 events.  Packet 2: DBC=20 (expected 8) → discontinuity.
    StreamProcessor sp;

    auto quads = MakeStereoAM824(8);

    auto pkt1 = MakePacket(0x3F, 2, 0,  0x00, 0xFFFF, quads);
    (void)sp.ProcessPacket(pkt1.data(), pkt1.size());

    auto pkt2 = MakePacket(0x3F, 2, 20, 0x00, 0xFFFF, quads);
    (void)sp.ProcessPacket(pkt2.data(), pkt2.size());

    EXPECT_EQ(sp.DiscontinuityCount(), 1u);
}

TEST(StreamProcessor, DBCContinuity_WrapAround_Accepted) {
    // DBC wraps at 256. After 248 (first packet, 8 events), expected = (248+8)&0xFF = 0.
    StreamProcessor sp;

    auto quads = MakeStereoAM824(8);

    auto pkt1 = MakePacket(0x3F, 2, 248, 0x00, 0xFFFF, quads);
    (void)sp.ProcessPacket(pkt1.data(), pkt1.size());

    auto pkt2 = MakePacket(0x3F, 2, 0,   0x00, 0xFFFF, quads);
    (void)sp.ProcessPacket(pkt2.data(), pkt2.size());

    EXPECT_EQ(sp.DiscontinuityCount(), 0u);
}

TEST(StreamProcessor, DBCContinuity_FirstPacketSkipsCheck) {
    // The first packet never triggers a discontinuity regardless of DBC value.
    StreamProcessor sp;

    auto quads = MakeStereoAM824(8);
    auto pkt   = MakePacket(0x3F, 2, 99, 0x00, 0xFFFF, quads);
    (void)sp.ProcessPacket(pkt.data(), pkt.size());

    EXPECT_EQ(sp.DiscontinuityCount(), 0u);
}

// ============================================================================
// StreamProcessor — Reset()
// ============================================================================

TEST(StreamProcessor, Reset_ClearsAllCounters) {
    StreamProcessor sp;

    auto quads = MakeStereoAM824(8);
    auto pkt1  = MakePacket(0x3F, 2, 0,  0x00, 0xFFFF, quads);
    auto pkt2  = MakePacket(0x3F, 2, 20, 0x00, 0xFFFF, quads); // discontinuity

    (void)sp.ProcessPacket(pkt1.data(), pkt1.size());
    (void)sp.ProcessPacket(pkt2.data(), pkt2.size());
    ASSERT_GT(sp.PacketCount(), 0u);

    sp.Reset();

    EXPECT_EQ(sp.PacketCount(),       0u);
    EXPECT_EQ(sp.SamplePacketCount(), 0u);
    EXPECT_EQ(sp.EmptyPacketCount(),  0u);
    EXPECT_EQ(sp.ErrorCount(),        0u);
    EXPECT_EQ(sp.DiscontinuityCount(),0u);
}

// ============================================================================
// AM824Decoder — DecodeSample
// ============================================================================

TEST(AM824Decoder, DecodeSample_Label40_PositiveSample_ReturnsCorrectValue) {
    // Sample 0x123456 (positive 24-bit, fits in bits 0-22)
    constexpr uint32_t kSample24 = 0x123456u;
    const auto result = AM824Decoder::DecodeSample(MakeBEQuad(0x40, kSample24));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<int32_t>(kSample24));
}

TEST(AM824Decoder, DecodeSample_Label40_MaxPositive_NoSignExtension) {
    // 0x7FFFFF = largest positive 24-bit value; bit 23 = 0 → no sign extension.
    constexpr uint32_t kSample24 = 0x7FFFFFu;
    const auto result = AM824Decoder::DecodeSample(MakeBEQuad(0x40, kSample24));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<int32_t>(0x7FFFFF));
}

TEST(AM824Decoder, DecodeSample_Label40_NegativeSample_SignExtended) {
    // 0x800000 sets bit 23 → sign-extend to 0xFF800000 = -8388608
    constexpr uint32_t kSample24 = 0x800000u;
    const auto result = AM824Decoder::DecodeSample(MakeBEQuad(0x40, kSample24));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<int32_t>(0xFF800000u));
}

TEST(AM824Decoder, DecodeSample_Label40_MinusOne_SignExtended) {
    // 0xFFFFFF = -1 in 24-bit two's complement → 0xFFFFFFFF = -1 as int32
    constexpr uint32_t kSample24 = 0xFFFFFFu;
    const auto result = AM824Decoder::DecodeSample(MakeBEQuad(0x40, kSample24));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, -1);
}

TEST(AM824Decoder, DecodeSample_OtherLabel_ReturnsNullopt) {
    // Label 0x00 (Empty slot) → nullopt
    EXPECT_FALSE(AM824Decoder::DecodeSample(MakeBEQuad(0x00, 0x000000)).has_value());
    // Label 0x01 → nullopt
    EXPECT_FALSE(AM824Decoder::DecodeSample(MakeBEQuad(0x01, 0x112233)).has_value());
    // Label 0xFF → nullopt
    EXPECT_FALSE(AM824Decoder::DecodeSample(MakeBEQuad(0xFF, 0xABCDEF)).has_value());
}

// ============================================================================
// AM824Decoder — IsMIDI
// ============================================================================

TEST(AM824Decoder, IsMIDI_Labels0x80To0x83_ReturnsTrue) {
    EXPECT_TRUE(AM824Decoder::IsMIDI(MakeBEQuad(0x80)));
    EXPECT_TRUE(AM824Decoder::IsMIDI(MakeBEQuad(0x81)));
    EXPECT_TRUE(AM824Decoder::IsMIDI(MakeBEQuad(0x82)));
    EXPECT_TRUE(AM824Decoder::IsMIDI(MakeBEQuad(0x83)));
}

TEST(AM824Decoder, IsMIDI_NonMIDILabels_ReturnsFalse) {
    EXPECT_FALSE(AM824Decoder::IsMIDI(MakeBEQuad(0x40)));  // PCM
    EXPECT_FALSE(AM824Decoder::IsMIDI(MakeBEQuad(0x00)));  // Empty
    EXPECT_FALSE(AM824Decoder::IsMIDI(MakeBEQuad(0x7F)));  // Just below range
    EXPECT_FALSE(AM824Decoder::IsMIDI(MakeBEQuad(0x84)));  // Just above range
    EXPECT_FALSE(AM824Decoder::IsMIDI(MakeBEQuad(0xFF)));
}
