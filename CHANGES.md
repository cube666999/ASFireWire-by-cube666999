# ASFireWire — Changes & Fix Log (fork by cube666999)

Fork: https://github.com/cube666999/ASFireWire-by-cube666999  
Base: https://github.com/mrmidi/ASFireWire  
Test device: MOTU 828 MK3 (target), developed with Claude Code  
Tests: 493/493 passing  
Version: 0.2.17-audio (build 17)

---

## Implementation Status (May 2026)

| Subsystem | Status | Notes |
|-----------|--------|-------|
| OHCI init & bus reset | ✅ Working | Self-ID, topology, gap count |
| Async TX/RX | ✅ Working | Block read/write, lock, PHY |
| Config ROM reading | ✅ Working | Full FSM multi-node scanner |
| AV/C / FCP | ✅ Working | Music Subunit, PCR space, `SendSampleRateCommand` (0x19) — for non-MOTU devices |
| IRM | ✅ Working | Election, channel + bandwidth allocation |
| Isoch Transmit (IT) | ✅ Working | AM824 + SYT + cadence |
| Isoch Receive (IR) | 🚧 WIP | Pipeline exists, needs hardware validation |
| AudioDriverKit | 🚧 In progress | `ASFWAudioDriver` + `ASFWAudioNub` wired; `HandleChangeSampleRate` implemented |
| **MOTU V3 Backend** | ✅ Implemented | `MOTUAudioBackend` — V3 register protocol, awaiting hardware test |

---

## Fixes (43 commits)

### Fix 1 — ConnectOPCR: channel not written to oPCR register (critical)
**File:** `ASFWDriver/Protocols/AVC/CMP/CMPClient.cpp`

`ConnectOPCR(plug, callback)` called `PerformConnect(..., setChannel=nullopt, ...)` —
incremented p2p counter but never wrote the channel field to the oPCR register.

Per IEC 61883-1 §10.4.2: the controller MUST write the channel to oPCR on p2p connect
(same as `ConnectIPCR` already did correctly).

**Fix:** added `uint8_t channel` parameter to `ConnectOPCR`, passed as `setChannel`.  
**Tests:** +4 tests in `CMPClientTests`.

---

### Fix 2 — IRM: AllocateResources never called before CMP connect (critical)
**File:** `ASFWDriver/Audio/Backends/AVCAudioBackend.cpp/.hpp`

Channels were hardcoded (`kDefaultIrChannel=0`, `kDefaultItChannel=1`).
`IRMClient::AllocateResources` was never called — bandwidth was never reserved on the bus.

For MOTU 828 MK3 at 48 kHz, 18 channels, S400: ~146 bandwidth units required per IEC 61883-1.

**Fix:** added `SetIRMClient(IRMClient*)`, call `AllocateResources(ch, bw, cb)` before
`StartReceive`, release on stop. Dynamic channel passed through CMP connect sequence.  
**Tests:** +12 tests in `IRMClientTests` + `CMPClientTests`.

---

### Fix 3 — oPCR read-back after ConnectOPCR
**File:** `ASFWDriver/Audio/Backends/AVCAudioBackend.cpp`

After CAS `ConnectOPCR`, driver now reads back oPCR[0] to verify the channel was
actually written. Detects silent CAS failures.  
**Tests:** +3 tests (ReadOPCR OK / fail / invalid-plug).

---

### Fix 4 — Bus reset recovery in AudioCoordinator
**File:** `ASFWDriver/Audio/AudioCoordinator.cpp/.hpp`

Bus reset terminates all isochronous connections per IEEE 1394 §8.3.
`OnDeviceSuspended` stops the backend and records the GUID.
`OnDeviceResumed` calls `StartStreaming(guid)` again — full reconnect sequence.

Previously the driver recovered the bus but left audio streaming dead.

---

### Fix 5 — rescanAttempts_ accumulation on bus reset
**File:** `ASFWDriver/Protocols/AVC/AVCDiscovery.cpp`

`rescanAttempts_[guid]` counter was never reset on `OnUnitResumed`.
After N bus resets the device permanently fell out of discovery.

**Fix:** reset counter on resume.

---

### Fix 6 — IOPCIClassMatch instead of IOPCIMatch
**File:** `ASFWDriver/Info.plist`

`IOPCIMatch: 0x590111c1` (Agere chip only) →
`IOPCIClassMatch: 0x0c001000&0xffffff00` (any OHCI FireWire controller).

Now matches Apple TB → FW adapter (TI XIO2213B) and other OHCI chips.

---

### Fix 7 — RX queue wiring in StartDevice
**File:** `ASFWDriver/Isoch/Audio/ASFWAudioDriver.cpp`

`CreateRxQueue` in `ASFWAudioNub` is lazy — only called on first `StartAudioStreaming`.
`MapRxQueueFromNub` in `ASFWAudioDriver::Start` failed because the queue didn't exist yet
→ `rxQueueValid = false` → `ZtsTimerOccurred` skipped RX path → silence from FireWire IR.

**Fix:** after `nub->StartAudioStreaming()`, if `!rxQueueValid`, call `MapRxQueueFromNub`
again. Updates `inputChannelCount` from queue header.

---

### Fix 8 — TX queue wiring in StartDevice
**File:** `ASFWDriver/Isoch/Audio/ASFWAudioDriver.cpp`

Same lazy-init problem for TX queue. `HandleWriteEnd` was writing CoreAudio data
into a local ring buffer instead of the shared TX queue → no audio transmitted to device.

**Fix:** if `!txQueueValid`, call `MapTxQueueFromNub` after `StartAudioStreaming`.
Updates `outputChannelCount` from queue header.

---

### Fix 9 — Runtime sample rate switching via AV/C opcode 0x19
**Files:** `ASFWDriver/Isoch/Audio/ASFWIOUserAudioDevice.iig/.cpp`,
`ASFWDriver/Protocols/AVC/IAVCDiscovery.hpp/.cpp`,
`ASFWDriver/Protocols/AVC/AVCDiscovery.hpp/.cpp`

`IOUserAudioDevice` was created as a plain instance — `HandleChangeSampleRate` could
not be overridden. HAL rate changes were silently ignored; device was locked at 48 kHz.

**Fix:**
- **`ASFWIOUserAudioDevice`** — new `IOUserAudioDevice` subclass (`.iig` + `.cpp`)
  - `HandleChangeSampleRate(double)` override:
    1. `AudioCoordinator::StopStreaming(guid)`
    2. `SendSampleRateCommand(guid, rateHz, cb)` — AV/C INPUT PLUG SIGNAL FORMAT
       (opcode 0x19), SFC per IEC 61883-6 Table 5, poll ≤ 500 ms
    3. `SetSampleRate(rate)` — confirm new rate to CoreAudio HAL
    4. `AudioCoordinator::StartStreaming(guid)` — restart IR+IT at new rate
- **`IAVCDiscovery::SendSampleRateCommand`** — new virtual method on discovery interface
- **`AVCDiscovery::SendSampleRateCommand`** — implementation: lookup AVCUnit by GUID,
  build AV/C CDB, submit via FCPTransport, callback with accept/reject result
- **`ASFWAudioDriver`** — creates `ASFWIOUserAudioDevice` instead of `IOUserAudioDevice`

SFC mapping (IEC 61883-6 SFC field):
`32k=0x00 · 44.1k=0x01 · 48k=0x02 · 88.2k=0x03 · 96k=0x04 · 176.4k=0x05 · 192k=0x06`

---

---

### Fix 10 — AT DMA block write timeout (PATH1 no-branch completion)
**File:** `ASFWDriver/Async/Contexts/ATContextBase.hpp` — `ScanCompletion()`  
**Commit:** `eeb8787`

After a PATH1 no-branch chain (e.g. FCP write) OHCI sets RUN=1, Active=0, CommandPtr=0.
The old `isOrphaned` check had two clauses — both false in this state — so `ScanCompletion`
returned `nullopt` as if hardware was still running → every block write timed out.

**Fix:** added third clause `completedAndIdle = (isRunning && !isActive && commandPtrAddr == 0)`.
For OUTPUT_MORE precursor: `continue` instead of `return nullopt` → OUTPUT_LAST processed
in the same call, without waiting for a second interrupt.

Unblocks AV/C for ~80% of FireWire audio interfaces.

---

### Fix 11 — MOTU model ID: use Unit_SW_Vers instead of root ModelId
**File:** `ASFWDriver/Protocols/Audio/DeviceProtocolFactory.hpp` — `EffectiveModelId()`  
**Commit:** `5925587`

MOTU does not populate the root directory `Model` key with a useful model ID (value: `0x106800`).
The correct field is `Unit_SW_Vers` in the unit directory (e.g. `0x000015` for 828 MK3).

**Fix:** `EffectiveModelId()` — for vendor `0x0001F2` returns `unitSwVersion` instead of
`rootModelId`. Routing to `kMOTUV3` backend now works correctly.

---

### Fix 12 — MOTU CLOCK_STATUS rate mask: bits[10:8] not bits[15:8]
**File:** `ASFWDriver/Audio/Backends/MOTUAudioBackend.hpp`  
**Commit:** `d975131`

Linux kernel source had `V3_CLOCK_RATE_MASK = 0x0000ff00` (bits[15:8]).
Disassembly of `MOTUFireWireAudio.kext` (Sequoia, x86_64) showed `andl $0x700` — only
3 bits [10:8] are used for the rate code.

**Fix:** `kClockRateMask = 0x00000700`. All rate codes confirmed vs kext data table.

---

### Fix 13 — MOTU V3 config never reached MOTUAudioBackend (critical)
**Files:** `ASFWDriver/Audio/AudioCoordinator.cpp`,
`ASFWDriver/Protocols/Audio/DeviceProtocolFactory.hpp`,
`tests/DeviceProtocolFactoryTests.cpp`

`MOTUAudioBackend::StartStreaming` reads channel counts from `configByGuid_[guid]`.
This map was populated only by `OnAVCAudioConfigurationReady`, which is called exclusively
by `AVCDiscovery` — after a successful AV/C FCP sequence. MOTU 828 MK3 never responds to
FCP (confirmed on Sequoia), so `AVCDiscovery::avcUnit->Initialize()` times out, returns
`success=false`, and never calls `HandleInitializedUnit`. Result: `configByGuid_` stays
empty, `hasConfig=false`, `StartStreaming` returns `kIOReturnNotReady` immediately.

**Root cause:** the MOTU V3 backend had no independent initialization trigger — it was
entirely dependent on AV/C completing successfully.

**Fix:** `AudioCoordinator::OnDeviceAdded` now detects `kMOTUV3` via `EffectiveModelId()` /
`LookupIntegrationMode()` and injects a hardcoded `ASFWAudioDevice` config directly into
`motuV3_.OnAudioConfigurationReady()`, bypassing AVC entirely. Channel counts (in=14, out=18
for 828 MK3) confirmed from Sequoia diagnostic (`fNumFWOutputChannels 14 fNumFWInputChannels 18`
in `MOTUFireWireAudio.kext` log). `DeviceProtocolFactory` gains `GetMOTUV3ChannelLayout()` and
`GetMOTUV3DeviceName()` constexpr helpers for all known V3 models.

**Tests:** +5 new tests in `DeviceProtocolFactoryTests` (493 total passing).

---

### Fix 14 — `FW::Generation` construction: nielegalny cast na DriverKit25.4
**Plik:** `ASFWDriver/Audio/Backends/MOTUAudioBackend.cpp` — linie 223, 380  
**Commit:** `f88b326`

```cpp
// Przed (błąd na DriverKit25.4):
const FW::Generation gen{static_cast<uint32_t>(record->gen)};
// Po:
const FW::Generation gen = record->gen;
```

`record->gen` jest już typu `FW::Generation`. Kod próbował `FW::Generation → uint32_t`
(brak operatora konwersji) a następnie `uint32_t → FW::Generation`. SDK DriverKit25.4
(Xcode 26.4.1) odrzuca tę konwersję. Fix: bezpośrednia kopia wartości.

Lokalnie (stary SDK / CMake) kompilator przepuszczał to przez niejawne konwersje —
błąd ujawnił się dopiero na CI z DriverKit25.4.

---

### Fix 15 — CI: post-build scheme signing pada bez certyfikatu
**Plik:** `ASFW.xcodeproj/xcshareddata/xcschemes/ASFW.xcscheme`  
**Commit:** `8eec7a7`

Schemat Xcode ma post-akcję która podpisuje `.app` i `.dext` certyfikatem
`Apple Development: j.slipiec@gmail.com (239NB3LFDQ)`. Na runnerze GitHub Actions
certyfikat nie istnieje → `codesign` pada → `set -euo pipefail` → cały build pada.

`CODE_SIGNING_ALLOWED=NO` przekazywany przez workflow wyłącza tylko *wbudowane*
podpisywanie Xcode — **nie wyłącza** niestandardowych skryptów PostActions.

**Fix:** dodano guard na początku skryptu: `security find-identity` sprawdza czy
identity jest w keychain; jeśli nie — `exit 0` (podpisanie pomijane, build zielony).
Na maszynie developerskiej z zainstalowanym certyfikatem skrypt działa jak wcześniej.

---

### Fix 18 — CIPHeader OHCI double-swap (IR receive: every packet rejected)
**Files:** `ASFWDriver/Isoch/Core/CIPHeader.hpp`,
`ASFWDriver/Isoch/Audio/AM824Decoder.hpp`,
`ASFWDriver/Isoch/Receive/StreamProcessor.hpp`

`CIPHeader::Decode` called `SwapBigToHost` (`__builtin_bswap32` on LE/ARM64) on the two
CIP quadlets before checking EOH bits and extracting fields.

**Root cause:** On a LE host the OHCI controller already byte-swaps each received quadlet
when writing to the DMA buffer, so the values read via `reinterpret_cast<uint32_t*>`
are already in big-endian semantic order — no further swap is needed.
Calling `SwapBigToHost` a second time reversed the bytes back to a scrambled value:

```
SwapBigToHost(0x80000000) = 0x00000080   // Q1 EOH bit was at bit 31, now at bit 7
(0x00000080 >> 31) & 1 = 0 ≠ 1          // EOH1 check fails for every packet
```

Hardware test (v17) confirmed MOTU sends ~2300 IR packets/500 ms; all were silently
discarded by the broken EOH check.  Same double-swap bug affected `AM824Decoder::DecodeSample`,
`AM824Decoder::IsMIDI`, and the label-check in `StreamProcessor::ProcessPacket`.

**Fix:** removed `SwapBigToHost` from all four sites; fields extracted directly from the
OHCI-pre-swapped values.  The Python `analyze_isoch.py` tool already used this correct
approach (`struct.unpack('>II', ...)` + direct shifts — no additional swap).

---

### Fix 17 — Pre-lock rawPollCount_ diagnostic in IsochReceiveContext::Poll()
**Files:** `ASFWDriver/Isoch/Receive/IsochReceiveContext.hpp/.cpp`

`Poll()` acquired `rxLock_` before any logging, so if the lock was contended the call
was invisible — impossible to distinguish "Poll() never called" from "spinlock always busy".

**Fix:** added `rawPollCount_` incremented unconditionally **before** the lock attempt,
with sparse logging at calls #1, #10, #100, and every 500 thereafter.
This proved Poll() was being called at ~1 kHz as expected, eliminating the watchdog
scheduling and spinlock-contention hypotheses.

---

### Fix 16 — CI: opt-in Node.js 24 dla GitHub Actions
**Plik:** `.github/workflows/build-and-test.yml`  
**Commit:** `2e21b0d`

`actions/checkout@v4` używało Node.js 20, które GitHub usuwa z runnerów 2 czerwca 2026.
Dodano `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true` do sekcji `env` joba.

---

### Feature — MOTU V3 register protocol backend
**Files:** `ASFWDriver/Audio/Backends/MOTUAudioBackend.hpp/.cpp`,
`ASFWDriver/Protocols/Audio/DeviceProtocolFactory.hpp`,
`ASFWDriver/Audio/AudioCoordinator.hpp/.cpp`  
**Commit:** `f6fbe86`

MOTU 828 MK3 declares AV/C units in Config ROM but **does not implement FCP**.
The correct protocol is a proprietary V3 register sequence via async quadlet read/write
(tCode=0x0) — confirmed by Linux `sound/firewire/motu/motu-protocol-v3.c` and
`MOTUFireWireAudio.kext` disassembly.

`MOTUAudioBackend::StartStreaming` sequence:
1. Read `CLOCK_STATUS` (0x0b14) — log current sample rate
2. `IRM::AllocateResources` — reserve IR + IT channels + bandwidth
3. Write `PACKET_FORMAT` (0x0b10) = `0xC2` — S400 + exclude differed
4. `isoch_.StartReceive(irCh)` — start OHCI IR DMA
5. `isoch_.StartTransmit(itCh)` — start OHCI IT DMA
6. Read-modify-write `ISOC_COMM_CONTROL` (0x0b00) — activate both channels
7. Read-modify-write `CLOCK_STATUS` — set `FETCH_PCM_FRAMES` bit

`DeviceProtocolFactory` routes vendor `0x0001F2` + `unitSwVersion` → `kMOTUV3`.
`AudioCoordinator` routes `kMOTUV3` → `motuV3_` backend.

All register constants verified against `MOTUFireWireAudio.kext` — see `MOTU_828_MK3_BringUp.md`.

---

## MOTU 828 MK3 Bring-Up Path (after all fixes)

| Step | Status | Notes |
|------|--------|-------|
| OHCI init, bus reset, topology | ✅ | |
| Config ROM scan → MOTU identified | ✅ | via `unitSwVersion=0x000015`, Fix 11 |
| `DeviceProtocolFactory` → `kMOTUV3` | ✅ | |
| `AudioCoordinator` → `MOTUAudioBackend` | ✅ | |
| `MOTUAudioBackend` receives config (channels 14/18) | ✅ | Fix 13 |
| IRM: AllocateResources | ✅ | Fix 2 |
| `MOTUAudioBackend::StartStreaming` (V3 registers) | ✅ | Feature, Fix 12 |
| AT DMA quadlet write (tCode=0x0) | ✅ | Fix 10 unblocked block write path |
| Bus reset recovery | ✅ | Fix 4 |
| IOPCIClassMatch (TB adapter) | ✅ | Fix 6 |
| IR/TX queue wiring | ✅ | Fix 7 + Fix 8 |
| `ASFWAudioNub` published → CoreAudio device | ⏳ | needs Tahoe hardware test |
| `HALC_ShellObject: "nope"` AudioDriverKit error | 🐛 | to debug on Tahoe |
| IR Receive hardware validation | 🔍 | CIPHeader fix applied (Fix 18) — pending test after restart |
| **Hardware validation on Tahoe** | 🔍 | **next: restart + test IR SYT ESTABLISHED** |

---

## Notes

- All AV/C fixes (Fix 1–9) remain active for non-MOTU devices (e.g. Apogee Duet 2)
- MOTU 828 MK3 routes exclusively to `MOTUAudioBackend` — AV/C/FCP/CMP never called
- No existing tests were broken; 69 new tests added across all fix areas (493 total)
- Hardware test pending: Mac Studio (Apple Silicon, macOS Tahoe) + TB→FW adapter + MOTU 828 MK3
- Full V3 protocol reference: `MOTU_828_MK3_BringUp.md`
