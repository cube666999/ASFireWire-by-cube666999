# Focus.md — Plan pracy nad ASFireWire

Cel końcowy: MOTU 828 MK3 działający na macOS Tahoe przez sterownik DriverKit.

Archiwum ukończonych etapów i sesji debugowania → `DevLog.md`

---

## ⚡ SESJA NA MAC STUDIO — Przeczytaj to na starcie

> **Stan na 2026-05-27 (sesja 8) — v12 gotowy do deploy:**
> - ✅ v11 (IR cycleMatchEnable fix `935d3ff`) **przetestowany** — IR DMA active=1, Ctl=0x9400
> - ✅ IR context startuje poprawnie, ale MOTU nie nadaje IR packets
> - 🔍 **Diagnoza v11:** `ISOC_COMM_CONTROL` i `FETCH_PCM_FRAMES` były nieobecne w logach
>   → working tree na dev machine miał niezacommitowany bug: `StartTransmit` przed ISOC/FETCH_PCM
>   → deadlock: StartTransmit czekał 500ms na SYT, MOTU czekało na ISOC_COMM_CONTROL → timeout
> - ✅ **Fix I** (`662ca0d`): przywrócono prawidłową kolejność + cleanup error paths
> - 🔨 **Do zrobienia: zbuduj v12 i przetestuj**
> - ⏳ **Oczekiwany wynik v12:** `ISOC_COMM_CONTROL=0xC1C00000`, `FETCH_PCM_FRAMES set`, `seq>0`

### Fix I — ISOC_COMM_CONTROL i FETCH_PCM_FRAMES przed StartTransmit (v12, `662ca0d`)

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

### Stan po sesji 5

| Krok | Status |
|------|--------|
| MOTU pojawia się w CoreAudio / Sound Settings | ✅ **POTWIERDZONE** |
| StartDevice wywoływane przez CoreAudio | ✅ **POTWIERDZONE** |
| IRM alokacja (local-IRM shadow bypass) | ✅ **POTWIERDZONE** |
| IR DMA startuje | ✅ **POTWIERDZONE** |
| ISOC_COMM_CONTROL + FETCH_PCM_FRAMES przed StartTransmit | ✅ Fix wdrożony + potwierdzone (sesja 5) |
| AudioClockEngine timestamp anchor | ✅ Fix C+D wdrożony (sesja 6) |
| IR cycleMatchEnable bit 30 usunięty | ✅ Fix E (935d3ff) — IR active=1 Ctl=0x9400 potwierdzono v11 |
| Work queue deadlock naprawiony | ✅ Fix F (5554280) — rejestry OK, StartDevice wywoływane |
| ISOC_COMM_CONTROL + FETCH_PCM_FRAMES przed StartTransmit | ✅ Fix I (`662ca0d`) — v12 gotowy do testu |
| IR odbiera pakiety (seq>0, syt!=0) | ⏳ **Test z v12** |
| IO trwa >5s bez "not consecutive" | ⏳ Czeka na test |
| StartTransmit (IT DMA) startuje | ⏳ Czeka na IR packets + SYT clock |
| Słyszysz dźwięk z Maca przez MOTU (TX) | ⏳ Kolejny krok po IT start |
| Pełny duplex (TX + RX) | ⏳ Kolejny etap |

### Największe ryzyko które pozostało

**Isoch IR — format CIP** — MOTU V3 może nie używać standardowych nagłówków CIP (IEC 61883-1).
`StreamProcessor`/`AM824Decoder` są napisane dla zgodnych strumieni. Jeśli MOTU pomija
nagłówki CIP lub używa własnego formatu, dekodowanie PCM nie zadziała.
Objaw: IR DMA odbiera pakiety (Total Packets > 0), ale `AM824Decoder` odrzuca je z błędem DBC/CIP.

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

### Czego szukać w logach — sukces

```
AudioCoordinator: Injecting MOTU V3 config ... in=14 out=18  ← config wstrzyknięty
AudioCoordinator: StartStreaming backend=MOTU-V3              ← routing działa
MOTUAudioBackend: CLOCK_STATUS=0x... rateCode=0x01            ← quadlet read OK (0x01=48kHz)
MOTUAudioBackend: IRM allocated IR ch=X IT ch=Y               ← IRM OK
MOTUAudioBackend: PACKET_FORMAT=0x000000c2 written            ← quadlet write OK
MOTUAudioBackend: ISOC_COMM_CONTROL=0x... (irCh=X itCh=Y)    ← MOTU dostaje kanały
MOTUAudioBackend: FETCH_PCM_FRAMES set (clockStatus=0x...)    ← MOTU zaczyna nadawać IR!
MOTUAudioBackend: Streaming started GUID=0x...                ← V3 sekwencja kompletna 🎯

Start: Wrote Match=0xf000000X Cmd=0x801f0001 Ctl=0x00009000   ← IR startuje (kRun|kWake)
ExternalSyncBridge: seq=X syt=0x.... ageMs=Y                  ← IR odbiera pakiety!  (⬅️ to jest cel testu v11)
ExternalSyncBridge: SYT clock established                     ← IT może startować
```

**Po v11 — pierwsze co sprawdzić:**
```bash
log show --last 5m --debug --info 2>/dev/null | grep -E "(ASFWDriver\.dext)" | grep -E "(Isoch|IR|syt|seq|SYT|Streaming)"
```
Szukaj linii `Start: Ctl=0x00009000` (dowód że bit 30 nie jest ustawiony) i `seq>0` (pakiety odebrane).

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
| Isoch Transmit (IT) | ✅ Działa | AM824 + SYT + cadence |
| Isoch Receive (IR) | 🚧 WIP | Pipeline istnieje, wymaga walidacji na hardware |
| AudioDriverKit | 🚧 W toku | ASFWAudioDriver + ASFWAudioNub podłączone |
| **MOTU V3 Backend** | ✅ Zaimplementowany | `MOTUAudioBackend` — czeka na test hardware |

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

### Sekwencja StartStreaming (MOTUAudioBackend)

```
1. ReadRegister(0x0b14)         → odczyt CLOCK_STATUS (log sample rate)
2. IRM AllocateResources        → kanały irCh + itCh + bandwidth
3. WriteRegister(0x0b10, fmt)   → PACKET_FORMAT: speed S400 + exclude differed
4. isoch_.StartReceive(irCh)    → start IR OHCI DMA
5. WriteRegister(0x0b00, ctrl)  → ISOC_COMM_CONTROL: aktywuj oba kanały (PRZED StartTransmit!)
6. isoch_.StartTransmit(itCh)   → start IT OHCI DMA  ← SYT gate teraz przechodzi (MOTU nadaje)
7. ReadModifyWrite(0x0b14)      → CLOCK_STATUS: ustaw FETCH_PCM_FRAMES
```

> ⚠️ **Ważne:** ISOC_COMM_CONTROL musi być zapisany PRZED `StartTransmit`. `IsochService::StartTransmit`
> czeka 500ms na IR SYT clock — ale MOTU nie zacznie wysyłać pakietów IR dopóki nie dostanie
> ISOC_COMM_CONTROL. Deadlock: StartTransmit czeka na IR, IR czeka na ISOC_COMM_CONTROL.
> Fix: pisz ISOC_COMM_CONTROL po starcie IR, przed startem IT.

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
| IR Receive walidacja pakietów | Wysoki | Bit 30 naprawiony — czeka na potwierdzenie `seq>0` na hardware |
| FCP spam do MOTU | Niski | AVC discovery pisze do MOTU co ~2s; MOTU V3 nie używa AV/C — zbędne |

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
