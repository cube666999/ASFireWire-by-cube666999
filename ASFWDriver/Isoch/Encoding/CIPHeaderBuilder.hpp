// CIPHeaderBuilder.hpp
// ASFW - Phase 1.5 Encoding Layer
//
// Builds CIP (Common Isochronous Packet) headers per IEC 61883-1.
// Q0: [EOH][SID][DBS][FN][QPC][SPH][rsv][DBC]
// Q1: [EOH][FMT][FDF][SYT]
//
// Reference: docs/Isoch/PHASE_1_5_ENCODING.md
// Verified against: 000-48kORIG.txt FireBug capture
//

#pragma once

#include <cstdint>

namespace ASFW {
namespace Encoding {

/// FMT value for AM824 format (IEC 61883-6)
constexpr uint8_t kCIPFormatAM824 = 0x10;

/// FMT value for MOTU V3 custom format.
/// Per amdtp-motu.c CIP_FMT_MOTU=0x02, MOTU_FDF_AM824=0x22.
/// IT (host→device): FMT=0x02, FDF=0x22, SPH=1 in Q0, SYT=0x0000 for data.
constexpr uint8_t kCIPFormatMotuV3 = 0x02;

/// FDF field for MOTU V3 IT stream (MOTU_FDF_AM824 in amdtp-motu.c).
constexpr uint8_t kFDFMotuV3 = 0x22;

/// SYT value indicating NO-DATA packet
constexpr uint16_t kSYTNoData = 0xFFFF;

/// Sample Frequency Code for 48 kHz
constexpr uint8_t kSFC_48kHz = 0x02;

/// CIP header pair (Q0 and Q1)
struct CIPHeader {
    uint32_t q0;  ///< First quadlet: [EOH][SID][DBS][FN][QPC][SPH][rsv][DBC]
    uint32_t q1;  ///< Second quadlet: [EOH][FMT][FDF][SYT]
};

/// Builds CIP headers for AM824 audio at 48 kHz.
class CIPHeaderBuilder {
public:
    /// Construct a CIP header builder.
    /// @param sid Source node ID (6 bits, from OHCI NodeID register)
    /// @param dbs Data block size in quadlets (2 for stereo)
    // Positional `(sid, dbs)` matches the CIP header field order.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    explicit CIPHeaderBuilder(uint8_t sid = 0, uint8_t dbs = 2) noexcept
        : sid_(sid & 0x3F), dbs_(dbs) {}
    
    /// Set the source node ID.
    void setSID(uint8_t sid) noexcept { sid_ = sid & 0x3F; }
    
    /// Get the current source node ID.
    uint8_t getSID() const noexcept { return sid_; }
    
    /// Set the data block size.
    void setDBS(uint8_t dbs) noexcept { dbs_ = dbs; }

    /// Get the data block size.
    uint8_t getDBS() const noexcept { return dbs_; }

    /// Enable MOTU V3 CIP framing.
    /// Per amdtp-motu.c: FMT=0x02, FDF=0x22, SPH=1 in Q0, SYT=0x0000 for data.
    /// SPH=1 tells the receiver each data block begins with a 4-byte timestamp.
    void setMotuV3Mode(bool enable) noexcept { motuV3_ = enable; }
    
    /// Build a CIP header pair.
    ///
    /// @param dbc Data block counter (8 bits)
    /// @param syt Presentation timestamp (16 bits), or kSYTNoData for NO-DATA
    /// @param isNoData True if this is a NO-DATA packet (SYT forced to 0xFFFF)
    /// @return CIP header pair in big-endian wire order
    ///
    /// Q0 format (32 bits):
    ///   [31:30] EOH = 0b00
    ///   [29:24] SID = Source node ID (6 bits)
    ///   [23:16] DBS = Data block size (8 bits)
    ///   [15:14] FN = Fraction number (0 for audio)
    ///   [13:11] QPC = Quadlet padding count (0)
    ///   [10]    SPH = Source packet header (0 for AM824, 1 for MOTU V3)
    ///   [9:8]   rsv = Reserved (0)
    ///   [7:0]   DBC = Data block counter (8 bits)
    ///
    /// Q1 format (32 bits):
    ///   [31:30] EOH = 0b10 (indicates FMT present)
    ///   [29:24] FMT = 0x10 for AM824
    ///   [23:16] FDF = Format dependent field (SFC for audio)
    ///   [15:0]  SYT = Presentation timestamp (0xFFFF for NO-DATA)
    ///
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    CIPHeader build(uint8_t dbc, uint16_t syt, bool isNoData = false) const noexcept {
        CIPHeader header;
        
        // Q0: [EOH=00][SID:6][DBS:8][FN=00][QPC=000][SPH][rsv=00][DBC:8]
        // MOTU V3: SPH=1 (bit 10) — each data block starts with a 4-byte timestamp.
        // Without SPH=1, MOTU interprets the SPH bytes as PCM → silence.
        const uint32_t sphBit = motuV3_ ? (1u << 10) : 0u;
        uint32_t q0 = (static_cast<uint32_t>(sid_) << 24) |
                      (static_cast<uint32_t>(dbs_) << 16) |
                      sphBit |
                      (static_cast<uint32_t>(dbc));

        // Q1: [EOH=10][FMT:6][FDF:8][SYT:16]
        // MOTU V3 IT: FMT=0x02 (CIP_FMT_MOTU), FDF=0x22 (MOTU_FDF_AM824).
        // Ground-truth (2026-06-13): both working drivers — Linux snd-firewire-motu AND
        // Apple El Capitan (IOFireWireAVC), captured via M3 snoop — send CIP Q1 = 0x8222ffff,
        // i.e. SYT=0xFFFF on EVERY packet, data included (timing carried by per-block SPH,
        // not SYT). Earlier SYT=0x0000-for-data was an unverified guess; the wire disproves it.
        // Reference: amdtp-motu.c + documentation/MOTU_V3_WIRE_GROUNDTRUTH.md.
        uint16_t sytValue = isNoData ? kSYTNoData : syt;
        const uint8_t fmt = motuV3_ ? kCIPFormatMotuV3 : kCIPFormatAM824;
        const uint8_t fdf = motuV3_ ? kFDFMotuV3 : kSFC_48kHz;
        if (motuV3_) { sytValue = kSYTNoData; }  // 0xFFFF — matches captured ground-truth
        uint32_t q1 = (0x02U << 30) |
                      (static_cast<uint32_t>(fmt) << 24) |
                      (static_cast<uint32_t>(fdf) << 16) |
                      sytValue;
        
        // Byte swap both for big-endian wire order
        header.q0 = byteSwap32(q0);
        header.q1 = byteSwap32(q1);
        
        return header;
    }
    
    /// Build a NO-DATA packet header (convenience method).
    CIPHeader buildNoData(uint8_t dbc) const noexcept {
        return build(dbc, kSYTNoData, true);
    }
    
private:
    uint8_t sid_;  ///< Source node ID (6 bits)
    uint8_t dbs_;  ///< Data block size (quadlets per source packet)
    bool motuV3_{false};
    
    /// Byte swap for endianness conversion.
    static constexpr uint32_t byteSwap32(uint32_t x) noexcept {
        return ((x & 0xFF000000) >> 24) |
               ((x & 0x00FF0000) >> 8)  |
               ((x & 0x0000FF00) << 8)  |
               ((x & 0x000000FF) << 24);
    }
};

} // namespace Encoding
} // namespace ASFW
