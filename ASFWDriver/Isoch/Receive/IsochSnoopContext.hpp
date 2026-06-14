// IsochSnoopContext.hpp
// ASFW — Passive isochronous snoop.
//
// Listens on a FIXED isochronous channel and hex-dumps the raw packet payload to
// the dext log. It is a pure passive leaf:
//   • NO IRM (no CHANNELS_AVAILABLE / bandwidth allocation)
//   • NO bus discovery, NO Config-ROM publish/scan
//   • NO MOTU register control, NO AV/C
//   • NO AM824/CIP decode, NO CoreAudio / AudioDriverKit
//
// Purpose: capture the FULL on-wire payload of ANOTHER host's IT stream — e.g. the
// official macOS MOTU driver running on a second Mac plugged into the MOTU's second
// FireWire port — to obtain ground-truth that the Linux ALSA tracepoint could not
// give (it only logged the CIP header, not the PCM bytes / byte offsets).
//
// Reuses Rx::IsochRxDmaRing (the generic OHCI IR DMA engine — no audio semantics).
// The OHCI IR ContextMatch register already matches a channel passively, so opening
// a receive context perturbs nothing on the bus: it is read-only listening.
//
// This file is a SCAFFOLD. Wiring it into a driver entry point (allocating the DMA
// memory manager and pumping Poll() from the work queue) is a separate integration
// step that requires the two-host hardware setup and is done with the user present.
#pragma once

#include <DriverKit/OSObject.h>
#include <new>
#include <DriverKit/IOLib.h>
#include <memory>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../../Hardware/HardwareInterface.hpp"
#include "../Memory/IIsochDMAMemory.hpp"

#include "IsochRxDmaRing.hpp"

namespace ASFW::Isoch {

class IsochSnoopContext : public OSObject {
public:
    enum class State : uint8_t { Stopped, Running };

    virtual bool init() override;
    virtual void free() override;

    void* operator new(size_t size) { return IOMallocZero(size); }
    void* operator new(size_t size, std::nothrow_t const&) { return IOMallocZero(size); }
    void operator delete(void* ptr, size_t size) { IOFree(ptr, size); }

    static OSSharedPtr<IsochSnoopContext> Create(::ASFW::Driver::HardwareInterface* hw,
                                                 std::shared_ptr<::ASFW::Isoch::Memory::IIsochDMAMemory> dmaMemory);

    static constexpr size_t kNumDescriptors = 512;
    static constexpr size_t kMaxPacketSize  = 4096;

    // Bytes of each packet payload to hex-dump. 64 bytes = 16 quadlets, covering the
    // 2-quadlet CIP header plus the first MOTU V3 data block (SPH[4] + MSG[6] + the
    // leading PCM slots). Enough to confirm slot/byte layout against the ground-truth.
    static constexpr size_t kHexDumpBytes = 64;

    kern_return_t Configure(uint8_t channel, uint8_t contextIndex);
    kern_return_t Start();
    void Stop();
    uint32_t Poll();

    [[nodiscard]] State GetState() const noexcept { return state_.load(std::memory_order_acquire); }

    void LogHardwareState();

private:
    void ParseAndLogBlock0(const uint8_t* payload, uint16_t length, uint32_t seq);
    struct Registers {
        ::ASFW::Driver::Register32 CommandPtr;
        ::ASFW::Driver::Register32 ContextControlSet;
        ::ASFW::Driver::Register32 ContextControlClear;
        ::ASFW::Driver::Register32 ContextMatch;
    };

    Registers GetRegisters(uint8_t index) const;
    void HexDumpPacket(const uint8_t* payload, uint16_t length, uint32_t descIndex, uint32_t seq);

    Registers registers_{};
    uint8_t contextIndex_{0xFF};
    uint8_t channel_{0xFF};

    ::ASFW::Driver::HardwareInterface* hardware_{nullptr};
    std::shared_ptr<::ASFW::Isoch::Memory::IIsochDMAMemory> dmaMemory_{nullptr};

    Rx::IsochRxDmaRing rxRing_{};

    std::atomic<State> state_{State::Stopped};
    std::atomic_flag rxLock_ = ATOMIC_FLAG_INIT;

    // Diagnostic counters. rawPollCount_ is bumped unconditionally to confirm Poll()
    // is being reached; the others are guarded by rxLock_.
    uint32_t rawPollCount_{0};
    uint32_t pollCount_{0};
    uint32_t totalProcessedSinceLast_{0};
    uint32_t packetSeq_{0};
};

} // namespace ASFW::Isoch
