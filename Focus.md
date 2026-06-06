# Focus.md — Plan pracy nad ASFireWire

Cel końcowy: MOTU 828 MK3 działający na macOS Tahoe przez sterownik DriverKit.

Archiwum ukończonych etapów i sesji debugowania → `DevLog.md`

---

## ⚡ AKTUALNY STAN — Przeczytaj to na starcie

> **Stan na 2026-06-07 (sesja 34) — v91 / Fix 68 na pulpicie, czeka na restart + test hardware.**

### Środowisko testowe

| Maszyna | System | Rola |
|---------|--------|------|
| **MacBook Pro (M3 Max)** | **macOS Tahoe 26.5.1 (zewnętrzny SSD)** | **Aktywne — build + test hardware** |
| MacBook Pro (M3 Max) | macOS Sequoia (wewnętrzny SSD) | Diagnostyka MOTU kext (DTrace/IORegistry) |

**Boot-args na Tahoe:** `amfi_get_out_of_my_way=1 cs_enforcement_disable=1`

**Build hardware-test:**
```bash
./build.sh --derived /tmp/ASFWBuild --deploy
```
Wynik: `~/Desktop/ASFW_vNN.app` (podpisany `Apple Development: j.slipiec@gmail.com (239NB3LFDQ)`).

---

## Fixy sesji 34 (v89–v91)

### ✅ Fix 66 (v89) — DBS=21→16
`kMOTUV3WireDbs48k` 21→16. DBS=21 jest nieprawidłowy dla 828mk3fw (nie istnieje w specyfikacji Linux). Prawidłowe: `1 + DIV_ROUND_UP((2MSG+18PCM)×3,4) = 16`. Efekt: prawy squeal ustąpił, ale LEDs dalej migały.

### ✅ Fix 67 (v90) — DBC ring-wrap discontinuity
`InjectNearHw` pisał PCM ale **nie aktualizował DBC** w CIP nagłówku (`payloadVirt[3]`). Ring ma 200 pkt, 150 data × 8 frames = 1200 → 1200%256=176 → po każdym obrocie (~25ms) MOTU widziało skok DBC. Fix: `injectDbc_` (uint8_t) w `IsochAudioTxPipeline`, `payloadVirt[3] = injectDbc_` + advance. Efekt: LEDs stabilizowały się po chwili → potwierdzenie działania.

### 🔄 Fix 68 (v91) — SPH ring-wrap staleness [CZEKA NA TEST]
`InjectNearHw` → `encodeInterleavedFramesToMotuV3` nie aktualizował **SPH** (bytes 0-3 każdego audio bloku). Po ring wrap MOTU dostawało SPH z PrimeRing (102ms w przeszłości) → mis-timing → cisza/pisk + phantom ch7L + DigitalL. Fix: `injectSphCursor_`/`injectSphSeeded_` w `PacketAssembler`; `encodeToWire` woła `writeMotuV3InjectSphAndAdvance` dla każdej ramki MOTU V3. Cursor niezależny od `sphTickCursor_` (prime) → no double-advance.

---

## Fixy sesji 32–33 (v74–v84)

### ✅ Fix 56 (v74) — Re-enable zero-copy
`kEnableZeroCopyOutputPath = true` — Fix 49 (wyłączenie zero-copy) był błędny; encoding IS stosowany przez `InjectNearHw`.

### ✅ Fix 57 (v75) — DBS validation dla MOTU V3
MOTU V3 używa 3-byte packed PCM → DBS=13 mieści 14 kanałów (52 bajty). Walidacja przez pojemność bajtową, nie slot count.

### ✅ Fix 58 (v76) — Latency matching El Capitan
`OutputSampleLatency=16`, `InputSampleLatency=19` — wartości z IORegistry El Capitan przy MOTU 828 MK3.

### ✅ Fix 59 (v77) — Wire DBS oddzielony od CoreAudio channel count
CoreAudio eksponuje stereo (2ch), ale wire DMA zachowuje DBS=13 (MOTU oczekuje tej geometrii klatki).
`GetMOTUV3WireDbs()` w `DeviceProtocolFactory`.

### ✅ Fix 61 (v83) — Monotonic SPH cursor
Zamiana statycznego `ct & 0x01FFFFFF` na kursor taktowy (+512 ticks/sample @ 48kHz). Tick seeding w `setCurrentCycleTime`. **Miał bug — patrz Fix 62.**

### ✅ Fix 62 (v84) — SPH seed w złym miejscu (root cause: diody nie świecą)
**Problem potwierdzony logami:** `setCurrentCycleTime` był wołany przy init drivera (cycle≈8). Seed ustawiał `sphTickCursor_` na cycle=8. Przy faktycznym starcie IT hardware był na cycle≈1113 → SPH timestamps **138ms w przeszłości** → MOTU odrzucał każdą ramkę.

**Fix:** seed przeniesiony do `writeMotuV3SphAndAdvance` (pierwsze wywołanie przy assembly pierwszego pakietu), gdzie `currentCycleTime_` jest już aktualny.

**Plik:** `ASFWDriver/Isoch/Encoding/PacketAssembler.hpp`

---

## ⏳ DO ZROBIENIA — sesja 33

### Krok 1: Restart + test v84
```bash
# Po restarcie:
systemextensionsctl list   # → (1.0/84) [activated enabled]
/usr/bin/log stream --debug --info 2>/dev/null | grep "ASFWDriver.dext" | grep -E "(DBG|sph|Cycle tracking)"
```
Szukamy `sph0` ≈ aktualnego `currentCycle` (nie cycle=8).

### Krok 2: Zweryfikuj czy diody świecą stabilnie
Odtwórz Spotify przez MOTU 828 MK3 jako wyjście. Diody input/output powinny świecić bez mrugania.

---

## Ważne odkrycie — mrmidi Discord (2026-06-06)

Źródło: `#general` na serwerze FireWire Audio Discord (4 czerwca 2026).

mrmidi opisał fundamentalne problemy ze swoim stosem audio DICE — **identyczne z potencjalnymi problemami w naszym kodzie:**

### 1. Nieprawidłowe blokowanie na SYT
> *"i was trying to block rx stream to sync SYT timing, but that was wrong behavior. original drivers never tried to do this — check clock is stable, start everything, sync tx when rx is available (until than — send 0xFFFF SYT)"*

Nasza SYT gate (Fix 22) był identycznym błędem — naprawiony. ✅

### 2. Fundamentalny błąd modelu danych AudioDriverKit ⚠️
> *"i've misused AudioDriverKit. i was trying to build custom shared queues instead of IOBufferMemoryDescriptor. the core idea how hw should receive audio data was wrong."*
> *"we should sync hw timestamps with CoreAudio, and hw should request new data on interrupt. i was trying to synthesize ZTS (zero time stamp) and push the data from CoreAudio — that's wrong design."*

**To jest potencjalnie aktywny problem w naszym kodzie.** Nasz `Ring-Buffer / Indirect copy` mode to właśnie "custom shared queues + push from CoreAudio". Prawidłowy model:
- `IOBufferMemoryDescriptor` dla DMA bufferów audio
- Hardware **ciągnie** dane na przerwaniu DMA
- CoreAudio **nie pcha** danych — tylko synchronizuje timestamps (ZTS)

**Tłumaczy to 38K underrunów przy 144% buffer fill** — dane są w buforze ale mechanizm ciągnięcia jest odwrócony.

### 3. Referencja: libffado
mrmidi wskazał libffado jako lepszą referencję niż Linux sound/firewire stack — bliższy oryginalnym Saffire.kext.

### Co robić z tym odkryciem
mrmidi przepisuje swój stos audio na `IOBufferMemoryDescriptor + ZTS`. Należy:
1. Poczekać aż wypchnie nową implementację do repo DICE
2. Przestudiować podejście przed dalszym debugowaniem naszego audio pipeline
3. Rozważyć czy nasz `IsochAudioTxPipeline` wymaga refaktoru w tym kierunku

---

## Odkrycie — WWDC21 + Apple Dev Forums (2026-06-06)

Źródła: WWDC21 "Create audio drivers with DriverKit", Apple Developer Forums thread/771504.
Szczegóły → `documentation/AUDIODRIVERKIT_PIPELINE.md`

### Prawidłowy model AudioDriverKit (Apple WWDC21)

```
OHCI DMA interrupt
  → UpdateCurrentZeroTimestamp(ohci_cycle_time, sample_time)
  → zapis audio do IOBufferMemoryDescriptor
  → HAL czyta z IOBufferMemoryDescriptor
```

Kluczowe API:
- `IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut, size, 0, buf)` — shared buffer z HAL
- `UpdateCurrentZeroTimestamp(sample_time, host_time)` — anchor dla HAL
- `GetZeroTimestampPeriod()` — ile sample frames na okres ZTS
- Timer/interrupt: `host_time += host_ticks_per_buffer`, `sample_time += GetZeroTimestampPeriod()`

**Apple:** *"It is vital to track the hardware clock's timestamps as close as possible."*

### Dla nas — Priorytet: ZTS z OHCI CycleTimer

Zamiast `mach_absolute_time()` w `PerformIO`:
```cpp
// Czytaj OHCI CurrentIsochronousCycleTime (offset 0x1E8)
// bits[25:12] = cycleCount (0-7999), bits[11:0] = cycleOffset
// Przelicz na host_time i przekaż do UpdateCurrentZeroTimestamp
```

### Apple Dev Forums — znany bug (thread/771504)
Przy jednoczesnym Input+Output: `in_io_buffer_frame_size` nie synchronizuje się z `UpdateCurrentZeroTimestamp`. Apple nie ma publicznego rozwiązania — tylko "wyślij sysdiagnose".

---

## Stan implementacji (czerwiec 2026)

| Subsystem | Status | Uwagi |
|-----------|--------|-------|
| OHCI init & bus reset | ✅ Działa | Self-ID, topology, gap count |
| Async TX/RX | ✅ Działa | Block read/write, lock, PHY — częściowo |
| Config ROM reading | ✅ Działa | Pełny scanner z FSM multi-node |
| AV/C / FCP | ✅ Działa (kod) | Nie używane dla MOTU V3 |
| IRM | ✅ Działa | Alokacja kanału + bandwidth |
| Isoch Transmit (IT) | ✅ Running | 8001 pkts/s, MOTU V3 DBS=13, SYT bypass |
| Isoch Receive (IR) | ✅ Running | ~480 pkts/100polls od MOTU |
| AudioDriverKit | ✅ IO aktywne | Ring-buffer indirect copy; underruny — patrz odkrycie mrmidi |
| MOTU V3 — diody | ⏳ Fix 62 (v84) | SPH był 138ms w przeszłości → MOTU odrzucał. Fix: seed przy 1. pakiecie. |
| MOTU V3 — dźwięk | ❓ Nieznane | Zależy od wyniku Fix 62 + kwestia IOBufferMemoryDescriptor |

---

## Następne priorytety (po teście v84)

### Priorytet 1 — Weryfikacja Fix 62
Restart → v84 → Spotify → diody na MOTU.

### Priorytet 2 — Zbadaj IOBufferMemoryDescriptor approach
Jeśli diody świecą ale brak dźwięku (lub dźwięk jest zły), fundamentalny problem może leżeć w architekturze push vs. pull. Obserwować repo mrmidi/DICE.

### Priorytet 3 — IR drops
IR context traci ~75% pakietów (ring buffer za mały? overrideWireDbs_?).

### Priorytet 4 — HALS_IORawClock
Zastąpić `mach_absolute_time()` czytaniem OHCI `CurrentIsochronousCycleTime` (rejestr `0x1E8`).

### Priorytet 5 — Rozszerzyć do 18ch IT / 14ch IR
Po potwierdzeniu działającego audio.

---

## Znane nierozwiązane problemy

| Problem | Priorytet | Status |
|---------|-----------|--------|
| Diody MOTU nie świecą | 🔴 Krytyczny | Fix 62 (v84) — do testu po restarcie |
| Underruny przy pełnym buforze | 🔴 Krytyczny | Prawdopodobnie architektura push vs pull — patrz odkrycie mrmidi |
| IR drops (~75% pakietów) | 🟡 Średni | Ring buffer IR za mały? overrideWireDbs_? |
| HALS_IORawClock jitter | 🟡 Średni | `mach_absolute_time()` zamiast OHCI cycle counter |
| Liczba kanałów 2/2 vs 18/14 | 🟢 Niski | Po potwierdzeniu audio |
| FCP spam do MOTU | 🟢 Niski | AVC discovery co ~2s; MOTU V3 nie używa AV/C |

---

## Status etapów

| Etap | Status |
|------|--------|
| 1–9 — szczegóły w DevLog.md | ✅ |
| 10 — MOTU V3 Protocol Backend | ✅ |
| 11 — Stable audio output | ⏳ |
