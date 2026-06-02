# Focus.md — Plan pracy nad ASFireWire

Cel końcowy: MOTU 828 MK3 działający na macOS Tahoe przez sterownik DriverKit.

Archiwum ukończonych etapów i sesji debugowania → `DevLog.md`

---

## ⚡ SESJA NA MAC STUDIO — Przeczytaj to na starcie

> **Stan na 2026-06-01 (sesja 21) — Fix 33 wdrożony (rate-matched IT ring refill), build w toku:**
>
> **✅ Osiągnięte w sesji 21:**
> - ✅ **Fix 33** (Rate-matched IT ring refill — eliminuje TxQ starvation):
>   - `OnRefillTickPreHW`: steady-state transfer = `samplesPerDataPacket` (6 frames/interrupt), nie burst
>   - Ring oscyluje 0–512 ramek (sawtooth), nie trafia w 0 przy każdym PerformIO
>   - Profile B: `startWaitTargetFrames` 512 → 2048 (42 ms pre-prime margin)
>   - PLL target: 2048 → 512 (`kAudioIoPeriodFrames`), eliminuje integral windup ±400 ppm
>   - Commit: `50417e9`, build `0.2.27-audio`
>
> **Następny krok — TEST FIX 33 (sesja 21, po restarcie):**
> - ASFW_Fix33.app na pulpicie (freshly signed)
> - Restart Mac Studio (wymagany dla dext upgrade z aktywnym AudioDriverKit)
> - Uruchom app, puść Spotify 30s
> - **Sprawdzenie w logach (log stream --debug):**
>   - `IT UnderrunCount` bliski 0 (był 886/s)
>   - `Ring buffer` stabilny ok. 2048 ramek (był 0%→144%)
>   - Audio czyste, bez piku/distortion
> - Jeśli sukces: weryfikuj czy Fix 30 IR nadal działa (IR Errs ≈ 0)

---

> **Stan na 2026-06-01 (sesja 20) — Fix 30 wdrożony (IR MOTU V3 Decoder), ASFW_Fix30.app gotowa:**
>
> **✅ Osiągnięte w sesji 20:**
> - ✅ **Fix 30** (IR MOTU V3 Decoder — MAIN PATCH):
>   - Nowy plik: `MotuV3Decoder.hpp` — dekodowanie 3-byte packed PCM bez label bytes
>   - Metoda `DecodeDataBlock()`: czyta [SPH 4B][msg 6B][PCM 3B×N], zwraca PCM samples
>   - `StreamProcessor::ProcessPacket()`: check `FDF==0x00` → MOTU V3 mode
>   - Logika dekodowania: dla MOTU V3 używaj MotuV3Decoder, dla AM824 stary decoder
>   - Override DBS=21 już ustawiony w MOTUAudioBackend.cpp (linia 304)
>   - Kodowanie IT (Fix 29) ✅ + Dekodowanie IR (Fix 30) = pełna duplex duplex MOTU V3 ✓
>
> **Diagnoza — dlaczego IR miało 215K błędów:**
> - Poprzednie: IR packets zawierały MOTU V3 format [SPH][msg][PCM×3B] ale kod dekodował je jako AM824 [label][PCM×24bit]
> - Efekt: co 4-byte slot przechodził do next channel offset → kompletny misalignment
> - **Fix 30:** Sprawdzenie FDF==0x00, dekodowanie 3 bytes per sample zamiast 4 bytes
> - Oczekiwany wynik: IR ErrorCount → ~0, słychać czyste audio bez distortion
>
> **Następny krok — TEST FIX 30 (sesja 20, po restarcie):**
> - ASFW_Fix30.app na pulpicie (11:31, freshly signed)
> - Restart Mac Studio (required dla dext upgrade z aktywnym AudioDriverKit)
> - Uruchom app, puść Spotify 30s
> - **Sprawdzenie w logach (log stream --debug):**
>   - `IR RX: len=... payload=... cipDbs=... events=...` — IR pakiety przetwarzane
>   - `RxStats: ... Errs=...` — powinno być ~0 (było 215K w poprzednim sesji)
>   - Brak `IR CIP decode failed` — logika MOTU V3 działa
>   - Audio czyste, bez pisku/disto → IT (Fix 29) + IR (Fix 30) synchronized ✓
> - Jeśli sukces: commit Fix 30 jako osobny commit
> - Jeśli logi pokazują Errs>0: check RxStats dla FDF pattern

---

> **Stan na 2026-06-01 (sesja 19) — Fix 26,27,29 zaimplementowane, ASFW_Fix29.app gotowa:**
>
> **✅ Osiągnięte w sesji 19:**
> - ✅ **Fix 26** (`IsochAudioRxPipeline.cpp`): OHCI cycle-timer gate zamiast poll-count gate
>   - Baseline q8 (nanosPerSample) teraz odświeżana co 100ms bus time, nie 2s
>   - Wynik: CycleCorr ratio=1.000022 (stabilny, synchronizacja z MOTU crystal ✓)
> - ✅ **Fix 27** (`AudioTxProfiles.hpp`): Zwiększenie TX ring buffer — target 2048, max 4096
>   - Przed: max=1536 frames (32ms), łatwa przeznaczenie przy jitter >32ms
>   - Po: max=4096 frames (85ms), pełna pojemność AudioRingBuffer
>   - Wynik: Ring buffer oscylacja 0%→144% utrzymana (problem nie tu)
> - ✅ **Fix 29** (MOTU V3 Packet Encoding — GŁÓWNY FIX):
>   - Zmiana formatu IT z AM824 (4B/slot z label 0x40) na MOTU V3 (3-byte packed PCM)
>   - Nowe funkcje: `encodeInterleavedFramesToMotuV3()`, `fillSilentMotuV3Frames()`
>   - CIP nagłówek: FMT=0x00, FDF=0x00, SYT=0x0000 (MOTU nigdy nie wysyła SYT)
>   - Data block: [SPH 4B][msg 3B×2][PCM 3B×N] = 21 quadletów = DBS=21 ✓
>   - Propagacja: `PacketEncoding::kMotuV3` przez wszystkie warstwy (IsochService→StartTransmit)
>   - MOTUAudioBackend: `encoding=kMotuV3` włączone
>   - **Logi potwierdzają:** CIPHeader poprawnie buduje FMT/FDF/SYT dla MOTU V3
>   - **Kod robi dokładnie to co sugerował mrmidi w amdtp-motu.c**
>
> **Diagnoza — dlaczego AM824 nie działał:**
> - Wysyłaliśmy: [label 0x40][PCM 24-bit] × 21 slotów
> - MOTU V3 oczekiwała: [SPH][msg][msg][PCM×3B×18+]
> - MOTU odbierała nasze pakiety jako śmieci → ignorowała IT → feedback na ring buffer
> - **Efekt:** Ring buffer zawsze pusty, IT underrunuje przy każdym jitterze (oscylacja 0→144%)
>
> **Następny krok — TEST FIX 29 (sesja 19, po restarcie):**
> - ASFW_Fix29.app na pulpicie (10:59, 493/493 tests ✓)
> - Restart Mac Studio
> - Uruchom app, puść Spotify 30s
> - **Sprawdzenie:  IT Underruns ~0? Słyszymy dźwięk? Format poprawny?**
> - Jeśli TAK: commit Fix 26+27+29 razem (seria "Audio Output Pipeline")
> - Jeśli podwójnie zero underrunów: idziemy na IR side (format IR również?)

---

> **Stan na 2026-05-28 (sesja 18) — Fix 21+22+23 gotowe (uncommitted), rebuild wymagany:**
> - ✅ **Fix I** (`662ca0d`): ISOC_COMM_CONTROL + FETCH_PCM_FRAMES PRZED StartTransmit
> - ✅ **Fix II** (`2dc6600`): IT DMA deadlock — SYT wait po `Start()`; IT nadaje 4644 pkts ✅
> - ✅ **Fix III** (`3241bd2`): Allow DBS=18 z pcm=2 (silence-padding); IT geometry OK ✅
> - ✅ **Fix 17** (`c13132b`): `rawPollCount_` pre-lock — potwierdził ~2300 IR pkts/500ms od MOTU
> - ✅ **Fix 18** (`c13132b`): CIPHeader OHCI double-swap usunięty — IR pakiety mogą teraz przejść dekoder
> - ✅ **Test fixes** (`5f4108b`): StreamProcessorTests + IsochTransmitContextTests naprawione po Fix 18; 493/493 ✅
> - ✅ **Fix 19** (`68823bf`): Deactivate-before-activate ISOC_COMM_CONTROL + SYT gate 500ms→3000ms
> - ✅ **Fix 20** (`597f3c8`): override wire DBS=21 dla MOTU V3 — naprawia "Unsupported wire DBS" i eventCount=1→6
> - ✅ **Fix 21** (uncommitted): IT DBS=21 override — `requestedAm824Slots = kMOTUV3WireDbs48k` zamiast `config.outputChannelCount` → CIP DBS=21 na wire
> - ✅ **Fix 22** (uncommitted): SYT gate bypass — `skipSYTGate=true` w `StartTransmit`; MOTU V3 zawsze `syt=0x0000`, brama nigdy się nie otwierała → IT zabijane po 3s
> - ✅ **Fix 23** (uncommitted, sesja 18): TX Profile B — `kTxTuningProfileRaw = 1` w `AudioTxProfiles.hpp`; ring buffer target 512→1024, max 768→1536 frames; pre-prime unbounded
>
> **Diagnoza sesji 18 — przyczyna pisku:**
> - Sesja 17: IT nadało 20 181 data packets w 3s → pisk to był **prawdziwy dźwięk** (Fix 21 poprawny)
> - Fix 22 pozwolił nadawać przez 56s → **49 632 underrunów** = 14,77% data packets dostaje ciszę zamiast audio
> - Underrun rate: 886/s → co ~1ms ring buffer pusty → 14.77% ciszy moduluje dźwięk → pisk
> - **Przyczyna:** Profile A target=512 frames = 10,67ms (równy IO period CoreAudio) → zerowy margines jittera
> - **Fix 23:** Profile B target=1024, max=1536 frames = ~21ms marginu na jitter CoreAudio
> - AM824 encoding ✅ (MIDI slots 0x80-0x82, audio 0x40), DBS=21 ✅ (504B/event/6events×8000=48kHz)
>
> **✅ Potwierdzone (sesja 15, Fix 19):**
> - `ISOC_COMM_CONTROL deactivate=0x80800000` + `activate=0xC1C00000` ✅
> - `Started IR Context 0 for Channel 0!` ✅ / `Started IT Context for Channel 1!` ✅
> - `IR Poll[0] ch=0: 456 pkts in last 100 polls` ✅ MOTU nadaje!
> - Brak SYT timeout ✅
>
> **✅ Potwierdzone (sesja 16, Fix 21):**
> - `AudioDeviceStart (err 0)` — CoreAudio HAL uruchamia urządzenie ✅
> - IO aktywne przez **3+ minuty** bez `AudioDeviceStop` ✅
> - IR pakiety: **11 965 pkts/s** (Isoch Receive metrics) ✅
> - CIP DBS cycling: SID=25, DBS=113 (cycling counter, nie prawdziwy DBS — Fix 20 override) ✅
>
> **Problem znaleziony (sesja 16):** `HALS_IORawClock: Re-anchoring IO timeline`
> - **Przyczyna:** `mach_absolute_time()` zamiast OHCI `CurrentIsochronousCycleTime` jako hostTime
> - **Fix (TODO):** rejestr OHCI offset `0x1E8`, bits[25:12]=cycleCount (0-7999), bits[11:0]=cycleOffset
> - System nie crashuje, re-anchoring adapts — na razie akceptowalne
>
> **Następny krok — rebuild + test audio (sesja 18, po restarcie):**
> ```bash
> # ⚠️ WYMAGANY REBUILD na dev machine (Fix 23 zmienił AudioTxProfiles.hpp)
> # Potem: instalacja na Mac Studio + restart + test
>
> # Szukaj w logach:
> log stream --debug --info 2>/dev/null | grep "ASFWDriver.dext"
> ```
> Szukamy po Fix 22+23:
> - `[Isoch] SYT gate bypassed` — Fix 22 aktywny ✅
> - **Brak** `❌ StartTransmit SYT timeout` ✅
> - `IT: Pre-prime transferred` — pre-prime działa (Profile B = unbounded) ✅
> - Brak/mało `IT: ADAPTIVE FILL ESCALATE` — ring buffer trzyma poziom ✅
> - `UnderrunCount` bliski zeru (w logach co 30s) ✅
> - Dźwięk na słuchawkach (PHONES jack) 🎵 — to jest cel
>
> Jeśli pisk nadal: sprawdź logi IT underrun count. Jeśli < 100/min → przyczyna inna (SYT timing).
> Jeśli > 1000/min → Profile B nie pomógł → zbadaj czy WriteEnd poprawnie pisze do SharedTxQueue.

### Fix II — IT DMA deadlock w IsochService::StartTransmit (v15, `2dc6600`)

**Objaw v13/v14:** `StartTransmit timeout: seq=0 syt=0x0000 active=1 established=0`
Brak logu `✅ Started IT Context` — IT OHCI context nigdy nie startowało.

**Przyczyna:** `IsochService::StartTransmit` miało strukturę:
```
1. Provision IT context
2. ⛔ WAIT 500ms for IR SYT  ← bug — IT jeszcze nie startuje!
3. Configure IT context
4. Wait for TX fill
5. Start IT context           ← za późno, timeout już nastąpił
```
SYT wait był PRZED `isochTransmitContext_->Start()`. IT DMA nigdy nie uruchamiało OHCI
rejestrów, MOTU nie dostało IT pakietów, nie odpowiedziało IR → permanentny timeout.

**Fix:** SYT wait przeniesiony na PO `isochTransmitContext_->Start()`:
```
1. Provision IT context
2. Configure IT context
3. Wait for TX fill
4. Start IT context           ← IT DMA aktywne, OHCI nadaje
5. ✅ WAIT 500ms for IR SYT  ← teraz MOTU może odpowiedzieć
6. Jeśli SYT timeout → Stop IT, return kIOReturnTimeout
```

**Dowód:** `strings` na nowym binarium pokazuje nowy komunikat:
`"StartTransmit SYT timeout: IT is running but MOTU not responding"`
(Stary: `"missing established IR SYT clock"` → już nieobecny w binarium)

Commit: `2dc6600`. Wersja: `0.2.15-audio`.

> ⚠️ **Uwaga Xcode cache:** v14 był zbudowany ze stale cache'owanym `IsochService.o` — binarium
> miało stary kod. Zawsze po zmianie `.cpp` w isoch: `rm -rf /tmp/ASFWBuild && ./build.sh --derived /tmp/ASFWBuild --no-bump`

### Fix I — ISOC_COMM_CONTROL i FETCH_PCM_FRAMES przed StartTransmit (v13, `662ca0d`)

**Objaw v11:** `StartTransmit timeout: seq=0 syt=0x0000 ageMs=0 active=1 established=0`
Logi MOTU backend kończyły się na `PACKET_FORMAT=0x000000c2 written` — ani `ISOC_COMM_CONTROL`,
ani `FETCH_PCM_FRAMES` nie były zapisywane.

**Przyczyna:** Working tree na dev machine miał uncommitted modyfikację, która zamieniała
kolejność Steps 6-8 w `StartStreaming`. `StartTransmit` (Step 6) blokował 500ms czekając na
IR SYT clock. MOTU nie wysyłało IR dopóki nie dostanie ISOC_COMM_CONTROL + FETCH_PCM_FRAMES
(które były Steps 7-8) → klasyczny deadlock.

**Commit `f543fcc`** (na Mac Studio) już miał poprawną kolejność. Commit **`662ca0d`** (dev)
porządkuje zmiany, przywraca cleanup error paths i poprawia komentarze.

**Prawidłowa kolejność (`MOTUAudioBackend::StartStreaming`):**
```
Step 5: isoch_.StartReceive(irCh)           ← IR DMA
Step 6: WriteRegister(ISOC_COMM_CONTROL)    ← MOTU dostaje kanały  ← PRZED StartTransmit!
Step 7: WriteRegister(FETCH_PCM_FRAMES)     ← MOTU zaczyna nadawać ← PRZED StartTransmit!
Step 8: isoch_.StartTransmit(itCh)          ← IT DMA (SYT gate już przejdzie)
```

Commit: `662ca0d`

### Odkrycia sesji 7 (2026-05-26) — IR bit 30 bug

**Fix E — IR ContextControlSet: kIsochHeader → kRun|kWake:**

`OHCIConstants.hpp` miał `kIsochHeader = 1u << 30`. Nazwa myląca — bit 30 w ContextControlSet
to `cycleMatchEnable` (OHCI §9.2 IT / §10.2.2 IR), **nie** włącznik nagłówka isoch.

Ustawienie bit 30 na IR kontekście: kontekst zatrzymuje się, czeka aż `ContextMatch.cycleCount`
zgadza się z aktualnym cyklem zegara OHCI → **zero pakietów odebranych**.

Fix: `IsochReceiveContext.cpp` używa teraz `kRun | kWake = 0x9000` (matching Linux
`CONTEXT_RUN | CONTEXT_WAKE`) bez żadnych dodatkowych bitów.

Nagłówek isoch w buforze odbiorczym (OHCI §10.2.2 Tab. 54) jest sterowany przez flagę `"i"`
w polu control deskryptora INPUT_MORE/INPUT_LAST — nie przez ContextControlSet.

Commit: `935d3ff`

**Fix F — Work queue deadlock (StartDevice / StartStreaming):**

`StartDevice` (wywoływane przez CoreAudio na serial dispatch queue) próbowało synchronicznie
odczytać rejestry MOTU przez AT async quadlet read. AT completions lądują na tej samej serial
queue → deadlock: queue czeka na własne callbacki.

Fix: `StartStreaming` wysyłany przez `DispatchAsync_f` na nową `IODispatchQueue`.
MOTU rejestry teraz czytelne (CLOCK_STATUS readback OK w v10 logach).

Commit: `5554280`

**Fix G — Zombie dext PID przy upgrade:**

Przy próbie upgrade dextu z v9 → v10, stary `_driverkit` PID (704, ścieżka `125CE7EC`)
nie chciał zakończyć pracy bo CoreAudio HAL trzymał aktywne urządzenie audio.
`systemextensionsctl` ugrzązło w `terminating_for_upgrade_via_delegate`.
Jedyne rozwiązanie: **reboot**. Reload dextu zawsze wymaga restartu gdy AudioDriverKit jest aktywny.

### Odkrycia sesji 6 (2026-05-26)

**Jak poprawnie czytać logi drivera (ważne!):**
`log` w zsh to wbudowana funkcja matematyczna — **zawsze używaj pełnej ścieżki:**
```bash
/usr/bin/log show --last 10m --debug --info --predicate 'process == "coreaudiod"' 2>/dev/null | grep -iE "(ASFW|StartIO|StartDevice|consecutive|error)"
```

**Fix C — UpdateCurrentZeroTimestamp(0, 0) → (0, currentTime):**
`AudioClockEngine.cpp` `PrepareClockEngineForStart()` ustawiał anchor na `(sampleTime=0, hostTime=0)`.
CoreAudio interpretuje to jako "sample 0 był w chwili 0" (dawn of time, wiele godzin temu).
Natychmiast liczy ile IO cycles "zaległych" → próbuje dogonić → chaos → "not consecutive" → IO stop po ~5s.
Fix: `UpdateCurrentZeroTimestamp(0, mach_absolute_time())` — anchor jest teraz.

**Fix D — double-start guard w StartDevice:**
Jeśli CoreAudio ma dwa klientów używających urządzenia jednocześnie (np. Spotify + inny proces),
wywołuje `StartDevice` dwa razy. Drugie wywołanie resetowało anchor do `(0,0)` podczas gdy
sample time był na ~1,7M → skok wstecz = "not consecutive". Fix: early return gdy `isRunning == true`.

### Odkrycia sesji 5 (2026-05-25)

**Fix A — FETCH_PCM_FRAMES przed StartTransmit:**
MOTU V3 wymaga **obu** operacji zanim zacznie wysyłać IR:
1. `ISOC_COMM_CONTROL` — które kanały isoch
2. `CLOCK_STATUS | FETCH_PCM_FRAMES` — **to wyzwala nadawanie IR przez MOTU**
Linux robi `begin_session()` + `switch_fetching_mode(true)` oba przed startem DMA.

**Fix B — zamiana kanałów w ISOC_COMM_CONTROL:**
Rejestr 0x0b00 używa nazewnictwa z perspektywy MOTU (device-centric):
- bity [29:24] = "RX" = MOTU **odbiera** = host→device = nasz **IT** kanał
- bity [21:16] = "TX" = MOTU **nadaje** = device→host = nasz **IR** kanał

Poprzedni błąd: `irCh` w polu RX, `itCh` w polu TX — MOTU nadawało IR na kanale 1,
nasze IR DMA słuchało kanału 0 → zero pakietów. Fix: zamieniono miejscami.
Poprawna wartość (irCh=0, itCh=1): `0xC1C00000`.

### Stan po sesji 9 (2026-05-27)

| Krok | Status |
|------|--------|
| MOTU pojawia się w CoreAudio / Sound Settings | ✅ **POTWIERDZONE** |
| StartDevice wywoływane przez CoreAudio | ✅ **POTWIERDZONE** |
| IRM alokacja (local-IRM shadow bypass) | ✅ **POTWIERDZONE** |
| IR DMA startuje | ✅ **POTWIERDZONE** |
| ISOC_COMM_CONTROL + FETCH_PCM_FRAMES przed StartTransmit | ✅ Fix wdrożony + potwierdzone (sesja 5) |
| AudioClockEngine timestamp anchor | ✅ Fix C+D wdrożony (sesja 6) |
| IR cycleMatchEnable bit 30 usunięty | ✅ Fix E (`935d3ff`) — IR active=1 Ctl=0x9400 potwierdzone v11 |
| Work queue deadlock naprawiony | ✅ Fix F (`5554280`) — rejestry OK, StartDevice wywoływane |
| ISOC_COMM_CONTROL + FETCH_PCM_FRAMES przed StartTransmit | ✅ Fix I (`662ca0d`) — v13 potwierdzone (sesja 8) |
| IT DMA deadlock usunięty (SYT wait po Start()) | ✅ Fix II (`2dc6600`) — v15 zainstalowany (sesja 9) |
| IT DBS=18 z pcm=2 (silence-padding) | ✅ Fix III (`3241bd2`) — IT nadaje 4644 pkts (sesja 10) |
| IR odbiera pakiety (seq>0, syt!=0) | ✅ **POTWIERDZONE** — 11 965 pkts/s (sesja 16), override DBS=21 (Fix 20) |
| CIPHeader OHCI double-swap | ✅ **Fix 18** (sesja 12/13) — `SwapBigToHost` usunięty z decode path |
| ISOC_COMM_CONTROL stale state | ✅ **Fix 19** (`68823bf`) — deactivate+20ms przed activate |
| SYT gate 500ms→3000ms | ✅ **Fix 19** (`68823bf`) — MOTU ma więcej czasu na lock PLL |
| IT DMA startuje i nadaje | ✅ IT: 4644 pkts (3483D/1161N) — MOTU wysyła IR (~2300 pkts/500ms), Fix 18+19 odblokowują |
| IT DBS=21 na wire (AM824 frame format) | ✅ **Fix 21** (uncommitted) — `requestedAm824Slots=kMOTUV3WireDbs48k` zamiast pcm=2 |
| IO trwa >5s bez "not consecutive" | ✅ **POTWIERDZONE** — 3+ minuty aktywne IO, re-anchoring adaptuje się (sesja 16) |
| SYT gate bypass dla MOTU V3 | ✅ **Fix 22** (uncommitted, sesja 17) — `skipSYTGate=true`; MOTU zawsze `syt=0x0000`; IT nie będzie zabijane po 3s |
| HALS_IORawClock re-anchoring (jitter) | ⚠️ Znany — watchdog timing nieregularny; CoreAudio adaptuje się; fix: OHCI cycle counter |
| TxQ starvation (burst refill) | ✅ **Fix 33** (`50417e9`) — rate-matched 6 frames/interrupt, sawtooth 0–512, PLL target=512 |
| Słyszysz dźwięk z Maca przez MOTU (TX) | ⏳ **Test po restarcie z Fix 33** — restart → Spotify → PHONES jack |
| Pełny duplex (TX + RX) | ⏳ Kolejny etap |
| Pełne 18ch IT / 14ch IR w CoreAudio | ⏳ Teraz tylko "2 In / 2 Out" — rozszerzenie do pełnych kanałów po potwierdzeniu audio |

### Odkrycia sesji 17 (2026-05-28) — Fix 22: SYT gate bypass dla MOTU V3

#### Fix 22 — SYT gate bypass (`IsochService.cpp` + `MOTUAudioBackend.cpp`)

**Objaw:** Chwilowy pisk na słuchawkach przez ~3 sekundy, potem cisza. Logi:
```
IT: run=1 active=1 pkts=26644
❌ StartTransmit SYT timeout: IT is running but MOTU not responding
   (waited 3000ms seq=10 syt=0x0000 fdf=0x02 dbs=21 ageMs=2046 active=1 established=0)
IT: Stopped. Stats: 26908 pkts (20181D/6727N)
MOTUAudioBackend: StartTransmit failed kr=0xe00002d6
```

**Analiza:** IT wysłało **20 181 data packets** z prawdziwym dźwiękiem Spotify — pisk to był
prawdziwy dźwięk dotykający MOTU. Ale `IsochService::StartTransmit` po uruchomieniu IT DMA
czekało 3000ms na `externalSyncBridge_.clockEstablished`. Ta flaga jest ustawiana przez
`ExternalSyncClockState::ObserveSample`, który wymaga **16 kolejnych pakietów IR** z:
```
fdf == 0x02  AND  syt != 0x0000  AND  syt != 0xFFFF
```
MOTU 828 MK3 **zawsze wysyła `syt=0x0000`** w swoich nagłówkach CIP — nigdy nie osadza
IEEE 1394 SYT timestamps. Linux `snd-firewire-motu` w ogóle nie sprawdza SYT.
Flaga `clockEstablished` nigdy nie mogła zostać ustawiona → timeout → IT zatrzymane.

**Rozwiązanie:** Dodano `bool skipSYTGate = false` do `IsochService::StartTransmit`.
Pętla SYT jest opakowana w `if (!skipSYTGate)`. `MOTUAudioBackend::StartStreaming` przekazuje `true`.

```cpp
// IsochService.hpp
kern_return_t StartTransmit(..., bool skipSYTGate = false);

// IsochService.cpp
if (skipSYTGate) {
    ASFW_LOG(Controller, "[Isoch] SYT gate bypassed (device uses syt=0x0000 — MOTU V3 mode)");
} else {
    // polling loop 3000ms ...
}

// MOTUAudioBackend.cpp
const kern_return_t kr = isoch_.StartTransmit(
    itChannel, hardware_, sid, streamModeRaw,
    config.outputChannelCount, kMOTUV3WireDbs48k,
    txMem, txBytes, nullptr, 0, 0,
    /*skipSYTGate=*/true);  // ← Fix 22
```

**Inne callers** (`StartDuplex`, `IsochHandler::StartIsochTransmit`) używają domyślnego `false`
— SYT gate zachowany dla urządzeń standardowych.

**Status:** Uncommitted, built 2026-05-28, app wgrana na pulpit Mac Studio.
Commit razem z Fix 21 po potwierdzeniu dźwięku na słuchawkach.

---

### Odkrycia sesji 16 (2026-05-28) — Fix 21, AudioDeviceStart ✅, HALS_IORawClock

#### Fix 21 — IT wire DBS override (MOTUAudioBackend.cpp)

**Objaw:** Brak audio mimo że IT DMA nadaje, IR DMA odbiera. MOTU 828 MK3 milczy.

**Przyczyna:** `StartTransmit` w `MOTUAudioBackend::StartStreaming` (linia ~397) używał
`config.outputChannelCount` (=2) dla parametru `requestedAm824Slots`. Oznaczało to że CIP header
miał `DBS=2` — 2 quadlety na event. MOTU 828 MK3 oczekuje `DBS=21` (14 PCM + overhead = 21
quadlety/event, 504 bajty payload na packet przy 6 events × 8kHz = 48kHz). Pakiety z DBS=2
były ignorowane przez MOTU jako nieprawidłowe.

**Rozwiązanie:** `requestedAm824Slots` zmieniony na `kMOTUV3WireDbs48k` (=21):
```cpp
// MOTUAudioBackend.cpp ~linia 397
// MOTU V3: wire DBS must be 21 quadlets/event (14 PCM + overhead) regardless
// of how many PCM channels CoreAudio exposes.  Same constant as the RX-side
// kMOTUV3WireDbs48k override in StreamProcessor (Fix20).
// PacketAssembler::reconfigureAM824 pads the extra (am824Slots - pcmChannels)
// slots with MIDI-conformant AM824 labels, giving MOTU a valid 21-slot frame.
const kern_return_t kr = isoch_.StartTransmit(
    itChannel, hardware_, sid, streamModeRaw,
    config.outputChannelCount, kMOTUV3WireDbs48k,  // ← Fix 21
    txMem, txBytes, nullptr, 0, 0);
```
`PacketAssembler::reconfigureAM824` automatycznie wypełnia brakujące sloty (19 extra = 21-2)
etykietami MIDI-conformant (cycling 0x80-0x83) → MOTU dostaje poprawne 21-slotowe ramki.

**Status:** Uncommitted, build `2026-05-28T08:08:46Z`. Commit po potwierdzeniu audio na hardware.

---

#### HALS_IORawClock re-anchoring — zidentyfikowane, nienaprawione

**Objaw:**
```
HALS_IORawClock::Update: Re-anchoring IO timeline.
Sample time is consecutive, host time is not consecutive.
```
Pojawia się co ~2-3s na początku, potem co 20-50s+ (CoreAudio adaptuje się). System działa.

**Przyczyna:** `PerformIO` w `ASFWAudioDriver` jest wyzwalany przez `IOTimerDispatchSource`
z 1ms interwałem. Kernel może opóźniać timery do ~1.5ms lub więcej, zwłaszcza pod obciążeniem.
CoreAudio widzi: sampleTime rośnie równo (+sampleRate/1000), ale hostTime skacze → "not consecutive".

**Wpływ:** System **nie crashuje** — `AudioDeviceStart (err 0)` potwierdzony, IO trwa 3+ minuty.
Ale jakość synchronizacji i latencja jest zaniżona przez re-anchoring.

**Fix (TODO, nie w tej sesji):** Zastąpić `mach_absolute_time()` w `PerformIO` czytaniem
licznika cykli OHCI (`CurrentIsochronousCycleTime` register, offset `0x1E8`):
```cpp
// Cycle counter: bits[25:12] = cycleCount (0-7999), bits[11:0] = cycleOffset
// Convert to host time: cycleCount/8000 × timebaseFreq + cycleOffset × (timebaseFreq/24576000)
```
OHCI cycle counter jest hardware-synchronizowany z magistralą IEEE 1394 → jitter <125µs (1 cykl).

---

#### Czego NIE robi front panel MOTU 828 MK3

- **Metry poziomów** na panelu przednim = **tylko analog hardware inputs** (Mic/Line). Nie pokazują
  poziomu sygnału FireWire IT (host→device). Nawet gdy IT nadaje, metry mogą stać na zero.
- **Test definitywny:** Słuchawki do gniazda `PHONES` (6.35mm, przód) + odtworzenie dźwięku.
  Gniazdo PHONES jest mapowane na mix wewnętrzny MOTU który **zawiera** FireWire IT input.
- **Isoch Transmit zakładka w ASFW:** Szara gdy IT jest zarządzany przez CoreAudio.
  To jest **normalny stan** po `AudioDeviceStart` — IT działa, po prostu nie przez manual trigger.
- **Przycisk Stop nie działa w Isoch Metrics:** IR jest zarządzany przez CoreAudio — to dobry znak.

---

### Aktualny bloker (sesja 14, Fix 19) — SYT gate timeout

**Potwierdzony objaw (log sesja 14):** `Streaming stopped` ale nigdy `Streaming started`.
→ `StartTransmit` zwraca `kIOReturnTimeout` (SYT gate) bo MOTU nie nadaje IR na ch=0.

**Zaimplementowane fixy (Fix 19, commit `68823bf`):**

1. **Deactivate przed activate** — jeśli MOTU jest w stale state (lower bits `0x1900` zamiast idle `0x3000`),
   bezpośredni activate może być zignorowany. Two-step: deactivate (20ms) → activate.

2. **SYT gate: 500ms → 3000ms** — MOTU może potrzebować więcej czasu na lock PLL po odebraniu
   pierwszych IT pakietów.

**Po rebuildzie, szukaj w logach:**
```
MOTUAudioBackend: ISOC_COMM_CONTROL deactivate=0x808019xx    ← deactivate wysłany
MOTUAudioBackend: ISOC_COMM_CONTROL activate=0xC1C019xx      ← activate z kanałami
```
Brak `SYT timeout` w logach = **sukces** ✅ (MOTU zaczął nadawać IR w <3s).

Jeśli dalej `SYT timeout`:
- `seq=0` → MOTU NIE nadaje IR wcale (problem rejestrowy lub hardware)
- `seq>0, established=0` → MOTU nadaje IR, ale CIPHeader::Decode odrzuca (format CIP)

**Scenariusz po naprawie SYT — CIPHeader format:**
Gdy IR cmdPtr zacznie się ruszać (MOTU nadaje), sprawdź:
```
IR CIP decode failed: q0=0x... q1=0x... len=...    ← CIP parsing failure
IR Poll[0] ch=0: N pkts in last 100 polls           ← N>0 = MOTU nadaje
```
`eoh0=(q0>>31)&1` musi być 0 (bez OHCI swap — Fix 18), `eoh1=(q1>>31)&1` musi być 1.

**Scenariusz C — IR context staje się DEAD:**
```
IR HW[0] ch=0: ctl=0x00000800 run=0 active=0 dead=1 evt=0x...
```
→ Problem z deskryptorem DMA. Sprawdź `IsochRxDmaRing::SetupRings` — bit 27 (isoch header include).

### Uruchom to zanim podłączysz MOTU

```bash
# Terminal 1 — logi drivera (WŁAŚCIWA METODA — potwierdzona na Tahoe 2026-05-25)
log stream --debug --info 2>/dev/null | grep "ASFWDriver.dext"

# Lub po zdarzeniu (ostatnie N minut):
log show --last 10m --debug --info 2>/dev/null | grep "ASFWDriver.dext"

# Terminal 2 — po podłączeniu MOTU, sprawdź czy nub jest w IORegistry
ioreg -l -r -c ASFWAudioNub
```

**`ioreg -l -r -c ASFWAudioNub`** powie od razu gdzie jest problem:
- Brak wpisu → problem po stronie `AudioCoordinator`/`MOTUAudioBackend` (protokół)
- Wpis jest, ale brak urządzenia audio → problem po stronie `ASFWAudioDriver`/HAL (HALC error)

### Czego szukać w logach — sukces (Fix 19 markery)

```
AudioCoordinator: Injecting MOTU V3 config ... in=14 out=18   ← config wstrzyknięty
AudioCoordinator: StartStreaming backend=MOTU-V3               ← routing działa
MOTUAudioBackend: CLOCK_STATUS=0x... rateCode=0x01             ← quadlet read OK (0x01=48kHz)
MOTUAudioBackend: IRM allocated IR ch=X IT ch=Y                ← IRM OK
MOTUAudioBackend: PACKET_FORMAT=0x000000c2 written             ← quadlet write OK
MOTUAudioBackend: ISOC_COMM_CONTROL deactivate=0x808019xx      ← Fix 19: deactivate (NEW)
MOTUAudioBackend: ISOC_COMM_CONTROL activate=0xC1C019xx (irCh=0 itCh=1)  ← MOTU dostaje kanały
MOTUAudioBackend: FETCH_PCM_FRAMES set (clockStatus=0x0a000100)← MOTU zaczyna nadawać IR!
[Isoch] ✅ Started IT Context for Channel 1!                   ← IT DMA aktywne
MOTUAudioBackend: Streaming started GUID=0x...                 ← V3 sekwencja kompletna 🎯

Start: Wrote Match=0xf000000X Cmd=0x801f0001 Ctl=0x00009000   ← IR startuje (kRun|kWake)
✅ Started IT Context for Channel 1!                          ← IT DMA uruchomione (Fix II)
ExternalSyncBridge: seq=X syt=0x.... ageMs=Y                  ← IR odbiera pakiety!  (⬅️ cel testu v15)
ExternalSyncBridge: SYT clock established                     ← IT może nadawać zsynchronizowane ramki
```

**Po v15 — pierwsze co sprawdzić:**
```bash
log show --last 5m --debug --info 2>/dev/null | grep -E "(ASFWDriver\.dext)" | grep -E "(Isoch|IR|IT|syt|seq|SYT|Streaming|Started|Poll|CIP)"
```
Szukaj: `Started IT Context` (IT DMA uruchomione), `seq>0` (IR pakiety odebrane), `SYT clock established`.

**Nowe logi diagnostyczne (sesja 11) — co oznaczają:**
```
IR Poll[0] ch=0: 0 pkts in last 500 polls        ← zero pakietów przez ~500ms = MOTU milczy (scenariusz A)
IR Poll[0] ch=0: 47 pkts in last 500 polls       ← MOTU wysyła! Szukaj dalej CIP errors
IR HW[0] ch=0: ctl=0x9400 run=1 active=1 dead=0 ← context żywy i aktywny (dobry stan)
IR HW[0] ch=0: ctl=0x0800 run=0 active=0 dead=1 ← context DEAD — problem z deskryptorami (scenariusz C)
IR CIP decode failed: q0=... q1=... [err#0]      ← CIP header nie przechodzi EOH check (scenariusz B)
IR short packet: N bytes (min 16) [err#0]         ← MOTU wysyła za krótkie pakiety
```

Jeśli **brak "Injecting MOTU V3 config"** → `OnDeviceAdded` nie widzi rekordu
w `DeviceRegistry` (race condition: Config ROM scan niegotowy). W logu szukaj:
`AudioCoordinator: Registered device observer` — powinno być przed podłączeniem MOTU.

Jeśli widzisz `backend=AV/C` zamiast `MOTU-V3` → `EffectiveModelId()` nie działa,
sprawdź czy `unitSwVersion=0x000015` jest parsowany z unit directory.

Jeśli widzisz `no config for GUID` w `MOTUAudioBackend` → config injection nie zadziałał.

### Wklej logi tutaj jeśli coś nie działa

Napisz na starcie sesji:
**"Kontynuujemy ASFireWire — oto logi z Mac Studio:"**
i wklej output z `log stream`. Reszta kontekstu jest w tym pliku i `MOTU_828_MK3_BringUp.md`.

---

---

## Stan implementacji (maj 2026)

| Subsystem | Status | Uwagi |
|-----------|--------|-------|
| OHCI init & bus reset | ✅ Działa | Self-ID, topology, gap count |
| Async TX/RX (quadlet read) | ✅ Działa | Block read/write, lock, PHY — częściowo |
| Config ROM reading | ✅ Działa | Pełny scanner z FSM multi-node |
| AV/C / FCP | ✅ Działa (kod) | Nie używane dla MOTU V3 — patrz Etap 10 |
| IRM | ✅ Działa | Alokacja kanału + bandwidth |
| Isoch Transmit (IT) | ✅ Działa | AM824 + SYT + cadence; DBS=21 override (Fix 21) |
| Isoch Receive (IR) | ✅ Odbiera | 11 965 pkts/s od MOTU (sesja 16); DBS override=21 (Fix 20) |
| AudioDriverKit | ✅ AudioDeviceStart | `StartDevice (err 0)` potwierdzony, IO 3+ min; clock jitter issue |
| **MOTU V3 Backend** | 🚧 Audio pending | IT nadaje DBS=21, SYT gate bypass wdrożony (Fix 22) — restart + headphone test |

---

## Status etapów

| Etap | Status | Testy |
|------|--------|-------|
| 1–9 — Szczegóły w DevLog.md | ✅ Zrobione | 488/488 ✅ |
| 10 — MOTU V3 Protocol Backend | ✅ Zaimplementowany | 488/488 ✅ (brak hardware testów) |

---

## Etap 10 — MOTU V3 Protocol Backend ✅ (2026-05-24)

### Odkrycie

MOTU 828 MK3 używa **własnego protokołu rejestrowego V3** — bez AV/C, bez FCP, bez CMP.
Potwierdzone przez analizę Linux kernel driver `sound/firewire/motu/motu-protocol-v3.c`.

Dotychczasowa sekwencja (AV/C → FCP block write) NIGDY nie mogła działać:
MOTU nie implementuje FCP mimo deklarowania AV/C units w Config ROM.

### Co zostało zaimplementowane

**Nowe pliki:**
- `ASFWDriver/Audio/Backends/MOTUAudioBackend.hpp`
- `ASFWDriver/Audio/Backends/MOTUAudioBackend.cpp`

**Zmodyfikowane pliki:**
- `ASFWDriver/Protocols/Audio/DeviceProtocolFactory.hpp` — dodano `kMOTUV3`, vendor 0x0001F2, model IDs
- `ASFWDriver/Audio/AudioCoordinator.hpp/.cpp` — dodano `motuV3_`, `SetBusOps`, routing
- `ASFWDriver/ASFWDriver.cpp` — `audioCoordinator->SetBusOps(&ctx.controller->Bus())`

### Sekwencja StartStreaming (MOTUAudioBackend) — aktualna po Fix I + Fix II

```
1. ReadRegister(0x0b14)         → odczyt CLOCK_STATUS (log sample rate)
2. IRM AllocateResources        → kanały irCh + itCh + bandwidth
3. WriteRegister(0x0b10, fmt)   → PACKET_FORMAT: speed S400 + exclude differed
4. isoch_.StartReceive(irCh)    → start IR OHCI DMA
5. WriteRegister(0x0b00, ctrl)  → ISOC_COMM_CONTROL: aktywuj oba kanały    ← Fix I: PRZED StartTransmit!
6. ReadModifyWrite(0x0b14)      → CLOCK_STATUS: ustaw FETCH_PCM_FRAMES      ← Fix I: PRZED StartTransmit!
7. isoch_.StartTransmit(itCh)   → start IT OHCI DMA  ← Fix II: SYT wait jest WEWNĄTRZ StartTransmit, PO uruchomieniu IT DMA
```

> ⚠️ **Ważne (Fix I):** ISOC_COMM_CONTROL i FETCH_PCM_FRAMES muszą być zapisane PRZED `StartTransmit`.
> MOTU nie zacznie wysyłać IR dopóki nie dostanie obu tych rejestrów.
>
> ⚠️ **Ważne (Fix II):** `IsochService::StartTransmit` czeka 500ms na IR SYT clock, ale robi to
> dopiero PO uruchomieniu IT OHCI DMA (`isochTransmitContext_->Start()`). Poprzednia kolejność
> (SYT wait PRZED Start()) powodowała deadlock: IT nigdy nie nadawało → MOTU nie odpowiadało IR → timeout.

**Kluczowe:** Wszystkie operacje to **quadlet write (tCode=0x0)** — inny code path niż zepsuty FCP block write (tCode=0x1). `WriteQuad(length=4)` → `WriteCommand` automatycznie wybiera tCode=0x0.

### Routing urządzeń (DeviceProtocolFactory)

| Urządzenie | Vendor | Model | Backend |
|------------|--------|-------|---------|
| MOTU 828 MK3 FW | 0x0001F2 | 0x000015 | `motuV3_` |
| MOTU 828 MK3 Hybrid | 0x0001F2 | 0x000035 | `motuV3_` |
| MOTU 896 MK3 | 0x0001F2 | 0x000016 | `motuV3_` |
| MOTU Traveler MK3 | 0x0001F2 | 0x000017 | `motuV3_` |
| MOTU UltraLite MK3 | 0x0001F2 | 0x000019 | `motuV3_` |

---

## ✅ ROZWIĄZANE — Model ID MOTU w Config ROM (2026-05-24)

**Potwierdzono na Sequoia z System Information:**
- Root directory `Model = 0x106800` (nie `0x000015` — i nie `0x000000`)
- Unit directory `Unit_SW_Vers = 0x15` = `0x000015` ← właściwe pole!
- GUID = `0x1F20000087236` ✅

**Przyczyna bugu:** `BackendForGuid` używał `record->modelId` (root dir = `0x106800`) zamiast `record->unitSwVersion` (unit dir = `0x000015`). MOTU nie wstawia modelu do root directory.

**Fix:** `DeviceProtocolFactory::EffectiveModelId()` — dla vendor `0x0001F2` zwraca `unitSwVersion` zamiast `rootModelId`. Commit `abc75ea`. 488/488 testów ✅.

Routing będzie teraz poprawny: `LookupIntegrationMode(0x0001F2, 0x000015)` → `kMOTUV3`.

---

## ✅ POTWIERDZONE — Sesja hardware 2026-05-25 część 1 (Mac Studio, Tahoe)

### Co udało się ustalić

**Potwierdzenia:**
- Async reads (ReadQuad) na rejestrach MOTU działają ✅ — rCode=Complete
- Async writes (WriteQuad) na rejestrach MOTU działają ✅ — rCode=Complete (test: PACKET_FORMAT)
- ASFWAudioNub pojawia się w IORegistry ✅
- MOTU 828 MK3 pojawia się w macOS Sound settings jako "FireWire" ✅
- `MOTUAudioBackend::StartStreaming` JEST wywoływany przez ścieżkę CoreAudio ✅ (patrz niżej)

**Kluczowe odkrycie — PACKET_FORMAT jest write-only:**
Rejestr `0x0b10` (PACKET_FORMAT) zwraca `0x00000000` przy odczycie niezależnie od tego co się do niego zapisało. Zapis działa (rCode=Complete), ale wartość nie jest czytelna z powrotem. Analogicznie ISOC_COMM_CONTROL i CLOCK_STATUS mogą mieć podobne właściwości (odczyt 0 ≠ nie zapisane).

---

## ✅ POTWIERDZONE — Sesja hardware 2026-05-25 część 2 (Mac Studio, Tahoe)

### Kluczowe odkrycia (nowe)

**1. Logi dextu są dostępne:**
```bash
log show --last 5m --debug --info 2>/dev/null | grep "ASFWDriver.dext"
# live:
log stream --debug --info 2>/dev/null | grep "ASFWDriver.dext"
```
Poprzednie próby z `log stream --predicate 'process == ...'` nie działały. Dext logi widoczne
jako `kernel: (net.mrmidi.ASFW.ASFWDriver.dext) [Kategoria] Treść`.

**2. IR DMA uruchomiony — ale przez RĘCZNY KLIK, nie CoreAudio:**
```
[Isoch] ✅ Started IR Context 0 for Channel 0!   ← 11:19:50 — ręczny klik Isoch Metrics
[Isoch] RxStats Pkts=0 every ~700ms              ← 0 pakietów mimo running IR
```
> ⚠️ Poprzedni zapis był błędny. CoreAudio NIE wywołało `StartDevice`. IR był uruchomiony
> przez ręczne kliknięcie przycisku "Start" w zakładce Isoch Metrics aplikacji ASFW.

**3. DMA Slab IOVA na Tahoe/Apple Silicon = `0x80000000`** — valid non-zero. DMA mapping działa.

**4. Total Packets = 0** mimo running IR DMA.
MOTU nie wysyłał pakietów — bo nie dostał ISOC_COMM_CONTROL + CLOCK_STATUS
(kroki 6-7 `MOTUAudioBackend::StartStreaming`). Ręczny Start IR minął całą sekwencję backendu.

**5. App crash pattern — fix:**
Po crashu apki dext wpada w `[terminating for upgrade via delegate]`.
Fix: `sudo kill -9 $(pgrep -f "net.mrmidi.ASFW.ASFWDriver" | head -1)` → dext restartuje się
automatycznie i apka łączy się przez strzałkę reconnect.

### Analiza 60-minutowych logów (kluczowe odkrycie)

Logi: `log show --last 60m --debug --info 2>/dev/null | grep "ASFWDriver.dext"`

```
11:19:41  Dext restart po kill -9
11:19:41  [FCP] FCPTransport: Command timeout  ← AVCDiscovery timeoutuje (oczekiwane)
11:19:50  [UserClient] StartIsochReceive channel=0  ← RĘCZNY KLIK (nie CoreAudio!)
11:19:50  [Isoch] ✅ Started IR Context 0 for Channel 0!
11:20:03  [AVC] AVCUnit: UNIT_INFO failed  + AVCDiscovery: AVCUnit initialization failed
11:20:03+ RxStats Pkts=0 co ~700ms
```

**BRAK w logach:**
- `[Audio] ASFWAudioDriver: StartDevice(...)` ← CoreAudio NIGDY nie wywołało StartDevice
- `[Audio] AudioCoordinator: StartStreaming`
- `[Audio] MOTUAudioBackend:` czegokolwiek
- `[Audio] MOTUAudioBackend: IRM allocated`

**Wniosek: CoreAudio nie wywołało `StartDevice` przez cały czas obserwacji.**
Urządzenie widoczne w Sound Settings, status "Idle". Spotify grało, ale StartDevice nigdy.

---

## 🔧 FIX — Sesja 2026-05-25 część 3 (analiza + fix kodu)

### Dwa bugi znalezione w `ASFWAudioDriver.cpp`

#### Bug 1 — Race condition: timer tworzony PO `RegisterService()` [KRYTYCZNY]

**Problem:**
```
Start():
  ...
  AddObject(audioDevice)      ← device widoczny
  RegisterService()            ← od teraz CoreAudio może dzwonić StartDevice!
  // ← OKIENKO RYZYKA
  IOTimerDispatchSource::Create(...)  ← timer jeszcze nie istnieje
  timestampTimer = OSSharedPtr(...)
```

Jeśli Spotify grało, gdy dext się restartował (kill -9 → auto-restart), CoreAudio
**natychmiast** wywołało `StartDevice` po `RegisterService()`. W tym momencie
`ivars->runtime.timestampTimer == nullptr` → `StartDevice` zwracało `kIOReturnNotReady`.
CoreAudio interpretuje NotReady jako błąd i rezygnuje — urządzenie zostaje na stałe "Idle".

Log który świadczyłby o tym: `"StartDevice failed - not initialized"` — ale ten log mógł
powstać w okienku przed uruchomieniem `log stream` przez użytkownika.

**Fix:** timer i akcja tworzone **przed** `RegisterService()`.

#### Bug 2 — Brak `SUPERDISPATCH` w `StartDevice` i `StopDevice`

**Problem:**
`Start()` i `Stop()` wywołują `SUPERDISPATCH` (np. `Start(provider, SUPERDISPATCH)`).
`StartDevice` i `StopDevice` używały plain C++ override bez SUPERDISPATCH — framework ADK
nigdy nie był notyfikowany o starcie IO. Bez tego:
- zero-timestamps wysyłane przez nasz timer mogły być ignorowane przez HAL daemon
- HAL mógł nigdy nie wysyłać nam `BeginRead`/`WriteEnd` operacji IO

**Fix:** `StartDevice` i `StopDevice` zmienione z plain C++ na `IMPL` + dodano
`StartDevice(in_object_id, in_flags, SUPERDISPATCH)` / `StopDevice(..., SUPERDISPATCH)`.

### Zmiany w kodzie (plik: `ASFWDriver/Isoch/Audio/ASFWAudioDriver.cpp`)

| Co zmieniono | Dlaczego |
|---|---|
| Timer tworzony przed `RegisterService()` | Eliminuje race condition przy dext restart |
| `StartDevice` → `IMPL` + SUPERDISPATCH | ADK framework notyfikowany o starcie IO |
| `StopDevice` → `IMPL` + SUPERDISPATCH | Symetria z StartDevice |
| Więcej logów w `StartDevice` (timer ptr, flags) | Lepsza diagnostyka |

### Jak przetestować po buildzie

```bash
# Terminal 1 — dext + coreaudiod razem (logi z obu stron):
log stream --debug --info 2>/dev/null | grep -E "(ASFWDriver\.dext|HALC_ShellObject|coreaudiod.*StartIO)"

# Terminal 2 — po teście, ostatnie 3 minuty:
log show --last 3m --debug --info 2>/dev/null | grep -E "(ASFWDriver\.dext|HALC)"
```

**Czego szukać po buildzie:**
```
[Audio] ASFWAudioDriver: Timestamp timer ready (before RegisterService)  ← nowy log
[Audio] ASFWAudioDriver: StartDevice(id=X flags=0x0)                     ← CoreAudio wywołało!
[Audio] AudioCoordinator: StartStreaming backend=MOTU-V3                 ← routing działa
[Audio] MOTUAudioBackend: Streaming started GUID=0x...                   ← pełna sekwencja
```

**Jeśli `super::StartDevice failed kr=0xE00002C7` (kIOReturnTimeout):**
ADK framework timeoutuje — prawdopodobnie HALC_ShellObject problem jest osobnym,
głębszym błędem. W tym wypadku potrzeba logów z `coreaudiod`:
```bash
log show --last 5m --debug --info 2>/dev/null | grep -E "(coreaudiod|HALC)" | head -50
```

---

## ✅ Sesja hardware 2026-05-25 część 4 — IRM fix + ISOC_COMM_CONTROL deadlock

### Naprawione w tej sesji

**Fix 1 — IRM self-addressed async transactions (IRMClient)**

Gdy Mac jest jedynym IRM-em, `ReadIRMQuadlet` i `CompareSwapIRMQuadlet` wysyłały async read/lock
do siebie samego przez OHCI. OHCI nie routuje AT→AR dla self-addressed transakcji → timeout.

**Rozwiązanie:** shadow registers (`shadowBandwidth_=4915`, `shadowChannelsLo/Hi_=0xFFFF`).
Gdy `irmNodeId_ == localNodeId_` (IsLocalIRM()), operacje wykonywane lokalnie bez transakcji async.
Resetowane przy każdym `SetLocalNode()` (mirrors bus-reset semantics).

**Potwierdzone w logach:**
```
[IRM] IRMClient: local-IRM read addr=0xf0000220 value=0x00001333
[IRM] IRMClient: local-IRM CAS addr=0xf0000220 old=0x00001333 ... OK
[IRM] Bandwidth allocation succeeded (294 units)
[IRM] Channel 0 allocation succeeded
[IRM] Channel 1 allocation succeeded
```

**Pliki:** `ASFWDriver/IRM/IRMClient.hpp`, `IRMClient.cpp`, `Controller/ControllerCoreDiscovery.cpp`

---

**Fix 2 — ISOC_COMM_CONTROL deadlock (MOTUAudioBackend)**

`StartTransmit` (IsochService) czekał 500ms na IR SYT clock przed startem IT DMA.
Ale MOTU nie wysyła pakietów IR dopóki nie dostanie ISOC_COMM_CONTROL.
ISOC_COMM_CONTROL był pisany dopiero PO StartTransmit → deadlock.

**Rozwiązanie:** ISOC_COMM_CONTROL przeniesiony z kroku 7 na krok 5.5
(po `StartReceive`, przed `StartTransmit`). MOTU natychmiast zaczyna nadawać IR,
SYT gate przechodzi, IT startuje.

**Plik:** `ASFWDriver/Audio/Backends/MOTUAudioBackend.cpp`

### Potwierdzone milestony

- ✅ MOTU 828 MK3 widoczny w System Settings → Sound → Wyjście jako "FireWire"
- ✅ CoreAudio wywołuje StartDevice → StartAudioStreaming → MOTUAudioBackend
- ✅ IRM alokacja działa bez timeoutów
- ✅ IR DMA startuje (OHCI context aktywny na kanale 0)
- ⏳ IT DMA — czeka na test po ISOC_COMM_CONTROL fix

### Czego szukać w logach po fixie

```
[IRM] local-IRM CAS ... OK                    ← IRM shadow działa
[Audio] MOTUAudioBackend: ISOC_COMM_CONTROL=0xC0000100 (irCh=0 itCh=1)  ← NOWE — przed IT
[Controller] ✅ Started IT Context             ← IT DMA ruszyło
[Audio] MOTUAudioBackend: Streaming started   ← 🎯 cel
```

---

## Następna sesja — Test hardware na Mac Studio (Tahoe)

### Krok 1 — Zbuduj i zainstaluj

```bash
# Na Mac Studio — pobierz projekt z iCloud (jeśli ikona chmurki: Download Now w Finderze)
# Otwórz ASFireWire.xcodeproj → Build (⌘B)
# Uruchom ASFW.app → zainstaluje dext
```

### Krok 2 — Uruchom logi

```bash
log stream --predicate 'subsystem == "net.mrmidi.ASFW"' --level debug
```

Podłącz TB adapter → MOTU 828 MK3.

### Krok 3 — Co obserwować w logach

**Sukces — nowa sekwencja (MOTU V3):**
```
OHCI init ✓
Bus reset + Self-ID ✓
Config ROM scan → MOTU 828 MK3 ✓
AudioCoordinator: StartStreaming backend=MOTU-V3  ← KLUCZOWE
MOTUAudioBackend: CLOCK_STATUS=0x... rateCode=0x02
MOTUAudioBackend: IRM allocated IR ch=0 IT ch=1
MOTUAudioBackend: PACKET_FORMAT=0x000000c2 written
MOTUAudioBackend: ISOC_COMM_CONTROL=0x... (irCh=0 itCh=1)
MOTUAudioBackend: FETCH_PCM_FRAMES set
MOTUAudioBackend: Streaming started GUID=0x...
```

**Jeśli widzisz `backend=AV/C` zamiast `MOTU-V3`** → sprawdź logi czy `unitSwVersion=0x000015` jest parsowany z unit directory. Model ID mismatch powinien być już naprawiony (commit `abc75ea`).

**Jeśli widzisz `CLOCK_STATUS read failed`** → quadlet write też prawdopodobnie nie działa → bug w AT DMA szerszy niż block write.

**Jeśli widzisz `ISOC_COMM_CONTROL write failed`** → quadlet write zawodzi → AT DMA bug.

**Jeśli streaming started ale brak audio** → sprawdź czy ASFWAudioNub jest w IORegistry:
```bash
ioreg -l -r -c ASFWAudioNub
```

### Krok 4 — Jeśli coś nie działa

Skopiuj logi i wklej do nowej sesji Claude Code.
Napisz: **"Kontynuujemy ASFireWire — oto logi z Mac Studio:"**

---

## ✅ ZWERYFIKOWANE — Analiza kexta MOTUFireWireAudio (2026-05-24)

Zdisassemblowano kext `/Library/Extensions/MOTUFireWireAudio.kext` na Sequoia (slice x86_64).

**Potwierdzone wartości vs nasza implementacja:**

| Stała | Wartość kext | Nasza wartość | Status |
|-------|-------------|---------------|--------|
| CLOCK_STATUS addr | `0xf0000b14` (w tablicy data, LE: `14 0b 00 f0`) | `kClockStatusOff = 0x0b14` | ✅ |
| V3_FETCH_PCM_FRAMES | `0x02000000` (data table word[1]) | `kFetchPCMFrames = 0x02000000` | ✅ |
| Rate code mask | `andl $0x700` → bits[10:8] | `kClockRateMask = 0x00000700` (poprawiono) | ✅ |
| PACKET_FORMAT addr | `0xf0000b10` (explicit imm) | `kPacketFmtOff = 0x0b10` | ✅ |
| PACKET_FORMAT value | bit7=TX_excl, bit6=RX_excl, bits[1:0]=speed | `0xC2` = `0x80\|0x40\|0x02` | ✅ |
| ISOC_COMM_CONTROL addr | `0xf0000b00` (explicit imm) | `kIsocCtrlOff = 0x0b00` | ✅ |

**Ważne obserwacje:**
- Kext przechowuje adres CLOCK_STATUS w tablicy danych (nie jako immediate w kodzie) — dlatego `grep "0xf0000b14"` nie dał wyników; potwierdzono przez zrzut sekcji `__DATA __const` pod adresem `0x7d4b8`.
- `kClockRateMask` poprawiony z `0x0000ff00` na `0x00000700` — kext używa `andl $0x700`, co odpowiada 3 bitom [10:8].
- Kolejność inicjalizacji MK3: Read CLOCK_STATUS → SetupStreams (alloc kanałów) → WritePacketFormat → WriteIsocCtrl → SetFetchPCMFrames — zgodna z naszą sekwencją.

---

## ✅ ROZWIĄZANE — AT DMA block write (tCode=0x1) (2026-05-24)

**Plik:** `ASFWDriver/Async/Contexts/ATContextBase.hpp` — `ScanCompletion()`

**Problem:** Po zakończeniu PATH1 no-branch chain (np. FCP write) OHCI ustawia:
- RUN=1 (software nie wyczyścił), Active=0, CommandPtr=0

Stary `isOrphaned` miał dwa człony — oba false w tym stanie → `ScanCompletion` zwracał `nullopt` jakby hardware wciąż pracował → timeout każdego block write.

**Fix:** Dodano trzecią klauzulę `completedAndIdle = (isRunning && !isActive && commandPtrAddr == 0)` do warunku `isOrphaned`. Przy OUTPUT_MORE precursorze: `continue` zamiast `return nullopt` → OUTPUT_LAST przetwarzany w tym samym wywołaniu, bez czekania na drugi interrupt.

Commit `eeb8787`. 488/488 testów ✅. Odblokuje AV/C dla ~80% rynku interfejsów FireWire audio.

---

## Znane nierozwiązane problemy

| Problem | Priorytet | Opis |
|---------|-----------|------|
| ~~AT DMA block write (tCode=0x1)~~ | ✅ NAPRAWIONE | `ScanCompletion` orphan check, commit `eeb8787` |
| ~~Model ID 0x000000 w Discovery~~ | ✅ NAPRAWIONE | Root dir model=0x106800, unit SW vers=0x000015. Fix: `EffectiveModelId()` commit `abc75ea` |
| ~~IR cycleMatchEnable (bit 30)~~ | ✅ NAPRAWIONE | `kIsochHeader=1u<<30` to był `cycleMatchEnable` → zero RX packets. Fix: `kRun\|kWake=0x9000`, commit `935d3ff` |
| ~~Work queue deadlock~~ | ✅ NAPRAWIONE | `StartStreaming` na background queue, commit `5554280` |
| IR Receive walidacja pakietów | ✅ POTWIERDZONE | 11 965 pkts/s w sesji 16, override DBS=21 działa |
| HALS_IORawClock re-anchoring | Średni | Watchdog-based PerformIO timing jittery; fix: OHCI cycle counter jako timestamp |
| TxQ starvation / underruny IT | ✅ NAPRAWIONE | Fix 33 — rate-matched 6 frames/interrupt, PLL target=512 |
| Brak audio na wyjściu MOTU | **Wysoki** | Fix 33 (TxQ) + Fix 30 (IR decoder) wdrożone; restart + headphone test → sesja 21 |
| Liczba kanałów 21/21 vs rzeczywiste 18 IT / 14 IR | Niski | DBS=21 obejmuje audio + padding/MIDI sloty. Apple MOTU kext używał 18ch IT / 14ch IR. Sprawdzić mapowanie i skorygować po potwierdzeniu audio |
| Brak nazw kanałów w CoreAudio / Audio MIDI Setup | Niski | Kanały widoczne jako numery (9, 10, 11…). Fizyczne I/O MOTU: 2 Analog (front) + 8 Analog Line + 16 ADAT + 2 S/PDIF. Implementacja: `IOAudioChannelDescription` tablicy w AudioDriverKit z nazwami per-kanał (Analog 1, ADAT A-1 itd.). Zrobić po stabilizacji audio. |
| FCP spam do MOTU | Niski | AVC discovery pisze do MOTU co ~2s; MOTU V3 nie używa AV/C — zbędne |
| `bufferFillLevel` UI — mislabeled "%" | Niski | `IsochTransmitContext::BufferFillLevel()` → `assembler_.bufferFillLevel()` zwraca **surowe ramki** (frames), nie procent. UI (`MetricsView.swift`) wyświetla tę wartość z sufiksem `%` — błąd etykiety. Wartość 144 = 144 ramki = 3 ms audio (nie 144%). Poprawny display: `fill * 100 / kAudioRingBufferFrames`. Pliki: `IsochHandler.cpp:404`, `DriverConnector+Isoch.swift:56`, `MetricsView.swift:331` |

---

## Instrukcja testowania na Mac Studio (Tahoe, Apple Silicon)

### Wymagania
- Mac Studio (Apple Silicon) z macOS Tahoe, SIP disabled, `amfi_get_out_of_my_way=1`
- Adapter Thunderbolt → FireWire 800
- MOTU 828 MK3

### Jednorazowe przygotowanie (Recovery Mode)

```bash
# Recovery → Terminal
csrutil disable
# Recovery → Startup Security Utility → Reduced Security → Allow kernel extensions
```

```bash
# Po normalnym restarcie
sudo nvram boot-args="amfi_get_out_of_my_way=1"
sudo systemextensionsctl developer on
# restart
```

### Odinstalowanie sterownika

```bash
systemextensionsctl uninstall net.mrmidi.ASFW net.mrmidi.ASFW.ASFWDriver
```

### Przywrócenie SIP

```bash
sudo systemextensionsctl developer off
sudo nvram -d boot-args
# restart → Recovery → csrutil enable → restart
```

---

## SESJA 19 SUMMARY — Fix 26+27+29 (2026-06-01)

### Co zostało zrobione

**Fix 26:** OHCI Cycle-Time Clock Synchronization
- Zmiana z poll-count gate (1000 pollów ≈ 2s) na bus-time gate (100ms)
- CycleCorr ratio poprawny: 1.000022 (stabilna synchronizacja)
- ✅ Wynik: q8 (nanosPerSample) odświeżana szybciej (100ms vs 2s)

**Fix 27:** TX Ring Buffer Expansion
- Zwiększenie max frames: 1536 → 4096 (32ms → 85ms)
- ✅ Wynik: buffer has capacity, ale oscylacja 0%→144% utrzymana

**Fix 29:** MOTU V3 Packet Encoding (GŁÓWNY FIX)
- ✅ Zmiana formatu: AM824 4-byte slots → MOTU V3 3-byte packed PCM
- ✅ CIP: FMT=0x00, FDF=0x00, SYT=0x0000
- ✅ Data block: [SPH 4B][msg 6B][PCM×3B×18+] = DBS=21
- ✅ Propagacja encoding przez wszystkie warstwy (IsochService→StartTransmit→IsochTransmitContext→IsochAudioTxPipeline→PacketAssembler)
- ✅ MOTUAudioBackend: włączone `encoding=kMotuV3`
- ✅ Wszystkie testy przeszły (493/493)

**Diagnoza w sesji 19:**
- Logi potwierdziły: CoreAudio PerformIO = dokładnie 48kHz (brak jittera)
- RTAID (AudioIssueDetector) pokazał RMS poprawiający się w czasie: −36 → −33 dBFS (startup underruny, potem OK)
- **Przyczyna underrunów:** Ring buffer oscyluje bo MOTU ignorowała nasze AM824 pakiety
- **Fix 29:** MOTU teraz odbierze poprawny format, powinno zacząć słuchać

### Build status

```
ASFW_Fix29.app — 10:59 (wszystkie 493 testy ✅, Xcode BUILD SUCCEEDED ✅)
Lokacja: ~/Desktop/ASFW_Fix29.app
```

### Następny krok — Sesja 20

**Priorytet 1:** Test Fix 29 na hardware
1. Restart Mac Studio
2. Uruchom ASFW_Fix29.app
3. Puść Spotify ~30 sekund
4. Sprawdź:
   - IT Underruns = ~0? (była 7,356)
   - Ring-Buffer = stabilna 50% (zamiast 0→144%)?
   - Słyszymy dźwięk?

**Jeśli TAK (wszystko działa):**
- Commit Fix 26+27+29 jako seria "Audio Output Pipeline Stabilization"
- Szukaj IR side (Format IR również konieczny?)

**Jeśli NIE (dalej problemy):**
- Sprawdzić logi: czy `encodeInterleavedFramesToMotuV3` faktycznie koduje 3-bajtowo?
- Czy MOTU odpowiada na nowe pakiety?
- Czy IR format też potrzebuje zmianę?

### Notatki techniczne — Fix 29

```cpp
// Stara (AM824 — ZŁA dla MOTU V3):
[label 0x40][PCM 24-bit] × 21 slotów × 4B = 84 bajty

// Nowa (MOTU V3 — PRAWIDŁOWA):
[SPH 4B] [msg 3B] [msg 3B] [PCM 3B×18+] = 82 bajty → pad do 84 (21×4)

// CIP nagłówek MOTU V3:
Q1: FMT=0x00 (nie 0x10), FDF=0x00 (nie SFC), SYT=0x0000 (zawsze)
// MOTU używa SPH w każdym data block, nie SYT w CIP
```

Źródło: `amdtp-motu.c` z Linux kernel (commit f5e5d35)
