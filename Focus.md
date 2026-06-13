# Focus.md — Plan pracy nad ASFireWire

Cel końcowy: MOTU 828 MK3 działający na macOS Tahoe przez sterownik DriverKit.

Archiwum ukończonych etapów i sesji debugowania → `DevLog.md`

---

## ⚡ AKTUALNY STAN — Przeczytaj to na starcie

> **Stan na 2026-06-12 — ZAGADKA "świeci tylko ch7 + pisk" ROZWIĄZANA.**
> Pełne podsumowanie: `documentation/SESSION_2026-06-12_GROUNDTRUTH.md`


Stworzyłeś /Users/cube666/Library/Mobile\ Documents/com\~apple\~CloudDocs/FireWire/ASFireWire-snoop/SNOOP_IMPLEMENTATION_NOTES.md - przeczytaj go i wykonaj to



### 🎯 Root cause ustalony (ground-truth z Linux + El Capitan)

Bug NIE był w timingu/DBS/Command DSP — był w **slocie PCM**:
- **DBS=13** potwierdzony (Fix 66→16 BŁĘDNY, Fix 69→13 OK), **SYT=0xFFFF**, 14 kanałów
- Mapa slotów (z CueMix): **slot 0 = Main L (byte 10), slot 1 = Main R (byte 13)**, 2-9=Analog 1-8,
  10-11=Phones, 12-13=S/PDIF. Mapowanie slot→wyjście STAŁE w sprzęcie.
- **Wkładaliśmy stereo na slot 8 (Analog 7, byte 34)** → stąd "świeci ch7".

### 📋 ROADMAP (kolejność)

```
Krok 1 — Fix IT (następna sesja)
  PacketAssembler/IsochAudioTxPipeline:
  • L → slot 0 (byte offset 10), R → slot 1 (byte 13), sloty 2–13 = zero
  • DBS=13, SYT=0xFFFF, CIP Q0 byte2=0x04 (QPC=1, SPH=0)
  • Górne 24 bity int32 → 3 bajty big-endian na wire (val>>8 pattern)
  Cel: Spotify gra przez MOTU Main Out

Krok 2 — IR ground truth capture
  Access Virus TI → MOTU Analog In 1 → sinus 440 Hz
  Dodać 2. pasywny IR context w snoop-mode branchu (MOTU TX channel)
  Zmapować sloty IR: który wejście fizyczne = który slot
  Szczegóły: MOTU_V3_WIRE_GROUNDTRUTH.md sekcja "IR ground truth"

Krok 3 — IR pipeline w dextcie
  DBS_IR=16, 18 kanałów PCM, mapa slotów z kroku 2
  Cel: nagrywanie syntezatorów (Virus TI + inne) przez MOTU wejścia analogowe
```

### Maszyna referencyjna (NOWE)
MacBook 2009 + Linux Mint. Dostęp/setup: `documentation/LINUX_MBP2009_SSH.md`
(SSH `cube666@192.168.0.38`/`72044277`, quirks=0x10 FW643).
- ✅ **Capture nagłówków CIP** (DBS=13, SYT) przez tracepoint `amdtp_packet` — dane ważne.
- ⛔ **Czyste odtwarzanie audio na Linuxie = ŚLEPA ULICZKA** (udokumentowany bug, issue #27:
  lost interrupts / zegar 828mk3). NIE wracać do prób "zmuśmy Linux żeby zagrał".

### ✅ Snoop El Capitan→MOTU — zakończony (2026-06-13, sesja 35–36)

Branch `snoop-mode` (v112, `efe0eac`) — pasywny IR snoop działającego strumienia El Cap→MOTU.
**1.3 mln+ pakietów** przechwyconych. Auto-resume po restarcie MB2009 (~3.5s przerwa) ✅.
Pełna dokumentacja faktów: `documentation/MOTU_V3_WIRE_GROUNDTRUTH.md` (sekcja "Snoop El Capitan").

**Kluczowe potwierdzenia z snoopa:**
- **DBS=13** — 2. niezależne źródło (Apple IOFireWireAVC, nie tylko Linux)
- **CIP Q0 byte 2 = 0x04** → QPC=1, **SPH=0** (CIP SPH bit NIE ustawiony — poprzedni doc miał błąd!)
- **CIP Q1 = 0x8222ffff** — FMT=0x02 (AM824), FDF=0x22 (48kHz), SYT=0xFFFF
- **DATA packet = 424 B** (8 bloków × 13 quad × 4B + 8 CIP), EMPTY = 8 B
- **MOTU timestamp** w pierwszym quadlecie każdego bloku (QPC=1), format: [0:4][secs:3][cycles:13][offset:12]
- **~6000 data + 2000 empty pkt/s** przy 48 kHz (8 frames/pkt × 6000 = 48000 ✅)

### Drogi do słyszalnego dowodu
El Capitan (oficjalny, gra ✅) · nasz DriverKit na Tahoe (cel, po fixie slotu) ·
~~snoop El Capitan→M3~~ ✅ zakończony, dane w `MOTU_V3_WIRE_GROUNDTRUTH.md`.

---

> **Poprzedni stan (sesja 34, 2026-06-07) — v91 / Fix 68 na pulpicie.**

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

## 📅 Zaplanowane — Czwartek (reset limitu): KOMPLEKSOWY TEST GROUND-TRUTH

Cel: **wyeliminować zgadywanie** przy bugu „świeci tylko ch7 Analog + pisk na prawym".
Łączymy 3 metody zbierania danych w **jednej sesji hardware** na **jednej magistrali FireWire**,
zakotwiczonej w **jednym znanym sygnale testowym** (skalibrowany sinus 440 Hz, pełna skala).

### Topologia fizyczna (jeden bus, dwa hosty)

```
[2009 MBP, El Capitan]              [M3 Max, Tahoe]
  - oficjalny sterownik MOTU          - nasz dext w trybie SNOOP (pasywny IR capture)
  - DTrace na CopyPacket              - dump bajtów pakietu IT z przewodu
       │                                       │
       └──FW── [MOTU 828 MK3  port A | port B] ──FW──┘
                      (gra kalibrowany sinus 440 Hz)
```

MOTU repetuje sygnał między swoimi dwoma portami → wszystkie 3 węzły na jednej magistrali.
FireWire isoch = broadcast → M3 Max widzi pakiety IT które 2009 MBP wysyła do MOTU.

### Co zbieramy RÓWNOLEGLE (cross-validation w jednym przebiegu)

| Źródło | Co łapie | Odpowiada na pytanie |
|--------|----------|----------------------|
| DTrace `*CopyPacket*` (2009 MBP) | float wchodzący + 6 bajtów wychodzących | **bit-shift** (górne vs dolne 24 bity int32 → amplituda 1/256?) |
| DTrace `*SetStartingChannel*` (2009 MBP) | dokładny `pcm_param` | **pcm_byte_offset** (10 vs 12 vs 9) |
| DTrace `*takeTimeStamp*` (2009 MBP) | kiedy/jak kext kotwiczy sample-time | **zasada ZTS** — czy kext kotwiczy do zegara FW (nie mach_time) |
| DTrace `*getCurrentSampleFrame*` (2009 MBP) | bieżąca pozycja sampla z zegara HW | **ZTS** — jak HAL pyta kext o pozycję (stary odpowiednik UpdateCurrentZeroTimestamp) |
| SNOOP IR (M3 Max dext) — CIP **SYT** | wartości SYT w nagłówkach CIP na przewodzie | **pacing/ZTS** — jak daleko w przód MOTU oczekuje timestampu, jaki interwał |
| SNOOP IR (M3 Max dext) — PCM | te same bajty **na przewodzie** | **wire layout + channel map** (który chunk = Main L/R) |
| `read_motu_regs.command` (już zrobione) | rejestry 0x0b00–0x0c98 | sekwencja init (0x0c04, 0x0b04) |

**Uwaga o ZTS / push-vs-pull:** sondy `takeTimeStamp` / `getCurrentSampleFrame` + SYT z przewodu
NIE walidują API `IOBufferMemoryDescriptor` (kext to stara epoka IOKit, sprzed AudioDriverKit).
Walidują **zasadę pull** (timing kotwiczony do zegara sprzętu, nie do `mach_absolute_time()`)
i dają **konkretne liczby SYT** do strojenia Etapu 3 w `REFACTOR_PLAN_IOBUFFER_ZTS.md`.
Sama poprawność `IOBufferMemoryDescriptor` jest już rozstrzygnięta (WWDC21 + mrmidi).

**Dlaczego razem > osobno:** DTrace mówi „kext wysłał bajt X", snoop mówi „na przewodzie był bajt X".
Zgodność = 100% pewności. Różnica = znaleziony punkt gdzie kext robi coś niewidocznego w Ghidrze.
Kalibrowany sinus → amplituda 3-bajtowych próbek na przewodzie wprost rozstrzyga bit-shift.

### Ocena ryzyka (snoop — dwa hosty na jednej magistrali)

Ryzyko **inżynierskie, NIE sprzętowe** (FW odporny):
- **Bus reset** przy wpięciu M3 Maxa — oficjalny sterownik musi wznowić stream (zwykle przeżywa).
- **Kontencja IRM/Bus Manager** — 2009 MBP jest BM+IRM i zarezerwował kanał. Nasz dext MUSI być
  pasywnym liściem: **zero IRM, zero discovery, zero config-ROM publish, zero sterowania MOTU**.
  Domyślnie dext robi odwrotnie (chce być kierowcą) → tryb snoop musi to wyłączyć.

### ✅ Przygotowanie infrastruktury (zrobione 2026-06-11)

**Linux Mint 22.3 na MBP 2009** zainstalowany i gotowy:
- SSH: `ssh -i ~/.ssh/mbp2009 cube666@192.168.0.38` (klucz, bez hasła)
- WiFi PLAY4279456 autoconnect ✅
- `snd-firewire-motu` + `firewire-ohci` + `trace-cmd` zainstalowane ✅
- Szczegóły → [`documentation/LINUX_MBP2009_SSH.md`](documentation/LINUX_MBP2009_SSH.md)

**Po podłączeniu MOTU 828 MK3 kablem FireWire do MBP 2009 — weryfikacja:**
```bash
dmesg | grep -i motu
aplay -l | grep MOTU
```

### DO ZROBIENIA przed sesją (kod — czwartek, po resecie limitu)

1. **Tryb SNOOP w dextcie** — pasywny IR capture na zadanym kanale isoch, dump surowych bajtów
   do logów. Bez IRM/discovery/sterowania. (Średni wysiłek — patrz „Ryzyko" wyżej.)
2. Wykrycie/ustawienie kanału IT oficjalnego sterownika (z `read_motu_regs` lub skan 0–63).

### Przygotowanie sprzętowe (możesz zrobić TERAZ, bez limitu)

- 2009 MBP: Remote Login (SSH) ON · MOTU driver El Capitan · `csrutil disable` (recovery) ·
  MOTU przez FW800 · zanotuj IP + username
- Drugi kabel FW + adapter TB→FW dla M3 Maxa (do drugiego portu MOTU)
- Przygotuj plik z kalibrowanym sinusem 440 Hz / pełna skala / 48 kHz

### Metoda 4 (NOWA) — Linux + snd-firewire-motu na MacBooku 2009 (najszybszy wire-truth)

**Dlaczego mocne:** open source (mamy źródło w `docs/linux/motu/`) + **ALSA ma wbudowany tracepoint
pakietów** → gotowy, sparsowany ground-truth BEZ inżynierii wstecznej, BEZ snoopu w dextcie.
MOTU to ten sam sprzęt → Linux produkuje **identyczne bajty na przewodzie** co kext Apple
(inaczej by nie grał). Dla pytań o wire layout Linux jest w 100% miarodajny.

**Tracepoint pakietów (sedno):**
```bash
# Ścieżka:
/sys/kernel/debug/tracing/events/snd_firewire_lib/amdtp_packet
# Loguje: data_block_counter (DBC), syt, data_blocks, payload_quadlets, raw CIP header, isoch channel

# Najprościej (trace-cmd):
sudo trace-cmd record -e snd_firewire_lib:amdtp_packet
#   → puść kalibrowany sinus 440 Hz przez MOTU (ALSA: aplay / Spotify / pavucontrol)
#   → Ctrl-C → sudo trace-cmd report > ~/motu_amdtp_trace.txt

# Albo surowo:
echo 1 | sudo tee /sys/kernel/debug/tracing/events/snd_firewire_lib/amdtp_packet/enable
sudo cat /sys/kernel/debug/tracing/trace_pipe   # live
```

**printk dla wartości których tracepoint nie pokazuje** (pcm_byte_offset, channel map):
- `docs/linux/motu/amdtp-motu.c` — funkcja składania data block (PCM byte offset, MSG/SPH packing)
- `docs/linux/motu/motu-protocol-v3.c` — `snd_motu_spec_828mk3_fw`, detekcja opt iface, kanały
- Rebuild modułu z `printk(KERN_INFO ...)` w punktach kodowania → `dmesg -w`

**Co Linux rozstrzyga wprost:**
| Pytanie | Źródło na Linuxie |
|---------|-------------------|
| pcm_byte_offset (10/12/9) | printk w `amdtp-motu.c` |
| channel map (chunk → Main L/R) | source + payload z tracepointu |
| SYT / pacing (dla ZTS Etap 3) | pole `syt` w tracepoincie + logika w source |
| DBS | jawne w `motu.h` |

**Zastrzeżenia:**
- `nosy` (linuksowy sniffer FW) **raczej NIE zadziała** — wymaga karty PCILynx (TI); wbudowany
  kontroler 2009 MBP to inny chip. Ale tracepoint `amdtp_packet` działa na KAŻDYM kontrolerze.
- To sterownik Linuxa, nie Apple → timing/SYT pokazuje „jak Linux taktuje" (miarodajne dla zasady
  pull, ale nie dokładnie ścieżka AudioDriverKit). Wire layout — identyczny z Apple.
- Wbudowany kontroler FW (Agere/LSI) działa z `firewire-ohci` od lat; `lsmod | grep firewire`.

**Strategia dnia:** zacznij od Linuxa (najszybszy wire-truth — tracepoint gotowy, bez kodu w dextcie).
El Capitan/snoop trzymaj jako weryfikację „czy Apple robi tak samo" + timing/ZTS bliższy AudioDriverKit.

### Metoda 1 (Ghidra na El Capitan) — TYLKO fallback

Statyczna analiza starszej binarki = najsłabsza opcja (Ghidra nie widzi runtime; DTrace na tym samym
sterowniku powie pewniej). **Jedyny zysk:** starszy kompilator = mniej inliningu → czytelniejszy
`InputBuffer828mk3::InitHook` (mogłoby rozjaśnić pcm_byte_offset z sekcji 5, która utknęła przez
inlining w nowszym kexcie). Robić **tylko jeśli** DTrace+snoop zostawią niejasność w układzie bajtów.

**Szczegóły techniczne** → `documentation/MOTU_KEXT_GHIDRA.md`

---

## Infrastruktura testowa — MacBook Pro 2009 z Linux Mint

Linux Mint 22.3 zainstalowany na MBP 2009 (2026-06-11) jako platforma dla Metody 4 (snd-firewire-motu tracepoints).
Dane połączenia SSH, partycje, sterownik WiFi BCM4322 → [`documentation/LINUX_MBP2009_SSH.md`](documentation/LINUX_MBP2009_SSH.md)

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

## Narzędzia developerskie — rekomendacje (2026-06-09)

### 1. `clang-tidy` — wdrożyć TERAZ (przed testem Fix 68)

**Kiedy:** przed kolejną sesją kodowania (jutro po resecie limitu).
**Dlaczego teraz:** `compile_commands.json` już istnieje (`./build.sh --commands`). Zajmie ~10 minut. Nowy kod (18ch, IOBufferMemoryDescriptor) lepiej pisać z analizą statyczną od początku.
**Co zrobić:**
```bash
# Wygeneruj compile_commands.json:
./build.sh --commands
# Zainstaluj (jeśli brak):
brew install llvm  # daje clang-tidy w /opt/homebrew/opt/llvm/bin/
# Utwórz .clang-tidy w katalogu ASFireWire/ — Claude Code skonfiguruje przy resecie limitu
```
**Ograniczenie:** pliki `.iig` poza zasięgiem (wymagają preprocessora Xcode). Reszta (~90% kodu) działa.

### 2. Instruments / System Trace — wdrożyć przy Priorytecie 4 (HALS_IORawClock)

**Kiedy:** dopiero gdy Fix 68 potwierdzi audio i zaczniesz refaktor ZTS/OHCI CycleTimer.
**Dlaczego nie teraz:** premature — problem jest w architekturze (push vs pull), nie w timing jitterze który Instruments mierzy.
**Co zrobić:** Instruments → File → New → System Trace → nagraj sesję z grającym MOTU → sprawdź spacing callbacków PerformIO vs. cykli OHCI.

### 3. DTrace skrypty — wdrożyć przy sesji ground-truth (środa)

Już zaplanowane w sekcji wyżej. Nie wymaga osobnej konfiguracji.

### 4. LSP (clangd) — opcjonalne, niska priorytet

Jeśli w przyszłości zaczniesz spędzać dużo czasu w edytorze bez AI: dodaj `.clangd` z DriverKit SDK paths. Nie wdrażać dopóki CodeGraph + Claude Code wystarczają.

---

## Status etapów

| Etap | Status |
|------|--------|
| 1–9 — szczegóły w DevLog.md | ✅ |
| 10 — MOTU V3 Protocol Backend | ✅ |
| 11 — Stable audio output | ⏳ |
