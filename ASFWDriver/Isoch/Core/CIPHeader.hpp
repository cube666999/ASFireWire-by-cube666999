//
// CIPHeader.hpp
// ASFWDriver
//
// IEC 61883-1 Common Isochronous Packet Header
//

#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include "IsochTypes.hpp"

namespace ASFW::Isoch {

struct CIPHeader {
    // Source node ID (filled by hardware on TX, parsed on RX)
    uint8_t sourceNodeId{0};

    // Data Block Size (quadlets per data block)
    uint8_t dataBlockSize{0};

    // Source Packet Header flag
    bool sourcePacketHeader{false};

    // Data Block Counter (0-255, wraps)
    uint8_t dataBlockCounter{0};

    // Format code (0x00 = DVCR, 0x10 = AM824)
    uint8_t format{0x10};  // AM824 for audio

    // Format Dependent Field (sample rate for AM824)
    uint8_t fdf{0};

    // Synchronization timestamp (0xFFFF = no info)
    uint16_t syt{0xFFFF};

    /// Decode from two quadlets (bus order).
    ///
    /// On LE hosts (ARM64/x86), the OHCI controller pre-swaps each received quadlet
    /// when writing to the DMA buffer, so q0_be/q1_be already hold the big-endian
    /// semantic values — direct bit extraction works without any further swap.
    ///
    /// IEC 61883-1 Figure 3 field layout (MSB = bit 31):
    ///   Q0: [31]=EOH(0) [30]=RES [29:24]=SID [23:16]=DBS [15:14]=FN
    ///       [13:11]=QPC [10]=SPH [9:8]=RSV [7:0]=DBC
    ///   Q1: [31]=EOH(1) [30]=RES [29:24]=FMT [23:16]=FDF [15:0]=SYT
    [[nodiscard]] static std::optional<CIPHeader> Decode(uint32_t q0_be, uint32_t q1_be) noexcept {
        // Q0: first bit on wire (bit 31) must be 0.
        if ((q0_be >> 31) & 0x1) return std::nullopt;

        CIPHeader h;
        h.sourceNodeId       = (q0_be >> 24) & 0x3F;
        h.dataBlockSize      = (q0_be >> 16) & 0xFF;
        h.sourcePacketHeader = (q0_be >> 10) & 0x1;
        h.dataBlockCounter   =  q0_be        & 0xFF;

        // Q1: first bit on wire (bit 31) must be 1.
        if (!((q1_be >> 31) & 0x1)) return std::nullopt;

        h.format = (q1_be >> 24) & 0x3F;
        h.fdf    = (q1_be >> 16) & 0xFF;
        h.syt    =  q1_be        & 0xFFFF;

        return h;
    }
};

} // namespace ASFW::Isoch
