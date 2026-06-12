#pragma once

#include "AudioConstants.hpp"

#include <cstdint>

namespace ASFW::Isoch::Config {

/// Profile identifiers. Override build-time default with -DASFW_TX_TUNING_PROFILE=1 (B) or =2 (C).
enum class TxProfileId : uint8_t { A = 0, B = 1, C = 2 };

#if defined(ASFW_TX_TUNING_PROFILE)
inline constexpr uint8_t kTxTuningProfileRaw = ASFW_TX_TUNING_PROFILE;
#else
inline constexpr uint8_t kTxTuningProfileRaw = 1;  // 0=A, 1=B (default), 2=C
#endif
static_assert(kTxTuningProfileRaw <= 2, "Invalid ASFW_TX_TUNING_PROFILE — use 0 (A), 1 (B), or 2 (C)");
inline constexpr TxProfileId kActiveTxProfileId = static_cast<TxProfileId>(kTxTuningProfileRaw);

struct TxBufferProfile {
    const char* name;
    uint32_t startWaitTargetFrames;
    uint32_t startupPrimeLimitFrames;    // 0 = unbounded pre-prime
    uint32_t legacyRbTargetFrames;
    uint32_t legacyRbMaxFrames;
    uint32_t legacyMaxChunksPerRefill;
    uint32_t safetyOffsetFrames;         // 2A: HAL safety offset (frames)
    uint32_t minPrimeDataPackets;        // 2B: Minimum DATA packets after PrimeRing
};

inline constexpr uint32_t kTransferChunkFrames = 256;

inline constexpr TxBufferProfile kTxProfileA{
    "A",
    256,   // startWaitTargetFrames
    512,   // startupPrimeLimitFrames
    512,   // legacyRbTargetFrames
    768,   // legacyRbMaxFrames
    6,     // legacyMaxChunksPerRefill
    64,    // safetyOffsetFrames (2A)
    48     // minPrimeDataPackets (2B)
};

inline constexpr TxBufferProfile kTxProfileB{
    "B",
    4096,  // startWaitTargetFrames  (Fix 53: raised from 2048 → 4096.
           //                        Requires 8 PerformIO writes before IT start (~85ms).
           //                        Fix 54: pre-prime now consumes ALL 4096 TxQ frames
           //                        (startupPrimeLimitFrames=4096), so TxQ=0 after prime.
           //                        Natural TxQ sawtooth average = 256 = PLL targetFillLevel.
           //                        PLL sees zero steady-state error → no saturation.)
    4096,  // startupPrimeLimitFrames (Fix 54: was 2048. Was leaving TxQ=2048 after prime,
           //                        causing PLL to see persistent error = 2048-256 = +1792
           //                        → PLL saturated at -400 ppm → slowed PerformIO 19 fps →
           //                        TxQ drained in ~107s → underruns → adaptive fill burst
           //                        (192 fps/call) → TxQ drained faster → runaway oscillation.
           //                        Fix: consume ALL TxQ during pre-prime. Post-prime TxQ=0,
           //                        natural average = 256 = target. PLL at neutral. Stable.)
    256,   // legacyRbTargetFrames (Fix 55: was 2048. Target=2048 is unreachable from a 512-frame
           //                      PerformIO burst: burst pump (burstCap=192fps/IRQ) drains TxQ
           //                      in 2.67ms, adding 384 frames net while OHCI drains 512/period.
           //                      Net ring change = 0 → ring stuck at starting level forever.
           //                      Any start level < 2048 → burst mode always active → death loop.
           //                      Fix: target=256 < single-burst yield (455 frames net) so ring
           //                      escapes burst mode and stabilizes at ~455 frames above target.
           //                      At normal startup (pre-prime fills ring to ~3584), ring >> 256,
           //                      no burst mode at all, only steady pump = wire rate → stable.)
    4096,  // legacyRbMaxFrames     (kAudioRingBufferFrames=4096)
    8,     // legacyMaxChunksPerRefill (8×256=2048 frames max per IRQ)
    96,    // safetyOffsetFrames (2A)
    48     // minPrimeDataPackets (2B)
};

inline constexpr TxBufferProfile kTxProfileC{
    "C",
    128,   // startWaitTargetFrames
    256,   // startupPrimeLimitFrames
    256,   // legacyRbTargetFrames
    384,   // legacyRbMaxFrames
    4,     // legacyMaxChunksPerRefill
    32,    // safetyOffsetFrames (2A)
    48     // minPrimeDataPackets (2B)
};

constexpr bool IsValidProfile(const TxBufferProfile& profile) noexcept {
    return profile.startWaitTargetFrames > 0 &&
           profile.legacyRbTargetFrames > 0 &&
           profile.legacyRbTargetFrames <= profile.legacyRbMaxFrames &&
           profile.legacyMaxChunksPerRefill > 0 &&
           profile.safetyOffsetFrames > 0;
}

static_assert(IsValidProfile(kTxProfileA), "Profile A is invalid");
static_assert(IsValidProfile(kTxProfileB), "Profile B is invalid");
static_assert(IsValidProfile(kTxProfileC), "Profile C is invalid");

static_assert(kTxProfileA.startWaitTargetFrames <= kTxQueueCapacityFrames,
              "Profile A startWait exceeds shared queue capacity");
static_assert(kTxProfileB.startWaitTargetFrames <= kTxQueueCapacityFrames,
              "Profile B startWait exceeds shared queue capacity");
static_assert(kTxProfileC.startWaitTargetFrames <= kTxQueueCapacityFrames,
              "Profile C startWait exceeds shared queue capacity");

constexpr TxBufferProfile SelectTxProfile(TxProfileId id) noexcept {
    switch (id) {
        case TxProfileId::B: return kTxProfileB;
        case TxProfileId::C: return kTxProfileC;
        default:             return kTxProfileA;
    }
}
inline constexpr TxBufferProfile kTxBufferProfile = SelectTxProfile(kActiveTxProfileId);

static_assert(IsValidProfile(kTxBufferProfile), "Selected TX buffer profile is invalid");
static_assert(kTransferChunkFrames == 256, "Transfer chunk size must stay fixed at 256");

/// Runtime-selectable profile pointer (defaults to the compile-time kTxBufferProfile).
/// Callers wishing to support hot-switching should use GetActiveTxProfile() instead of
/// kTxBufferProfile directly.
inline const TxBufferProfile* gActiveTxProfile = &kTxBufferProfile;

[[nodiscard]] inline const TxBufferProfile& GetActiveTxProfile() noexcept {
    return *gActiveTxProfile;
}

inline void SetActiveTxProfile(TxProfileId id) noexcept {
    switch (id) {
        case TxProfileId::A: gActiveTxProfile = &kTxProfileA; break;
        case TxProfileId::B: gActiveTxProfile = &kTxProfileB; break;
        case TxProfileId::C: gActiveTxProfile = &kTxProfileC; break;
    }
}

}  // namespace ASFW::Isoch::Config
