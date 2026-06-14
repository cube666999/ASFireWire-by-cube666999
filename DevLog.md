# DevLog.md — Archiwum sesji i ukończonych etapów

Plik archiwizuje szczegóły ukończonych etapów i sesji debugowania.
Aktywny plan pracy → `Focus.md`

---

## Ukończone etapy (Etap 1–9)

### Etap 1 — Testy IR Receive ✅
**Pliki:** `tests/IsochReceiveContextTests.cpp`, `tests/IsochRxDmaRingTests.cpp`
- `Poll()` przetwarza pakiety gdy deskryptory mają `xferStatus != 0`
- `Stop()` poprawnie wyłącza kontekst i zeruje stan
- `Configure()` → `Start()` → `Stop()` → `Configure()` (re-use bez OOM)
- `DrainCompleted` prawidłowo re-arms deskryptory po przetworzeniu
**Wynik:** +12 testów

### Etap 2 — Brakujące komendy Async ✅
**Pliki:** `ASFWDriver/Async/Commands/`, `tests/`
Block read/write i lock commands zaimplementowane i pokryte testami.
**Wynik:** +11 testów (AsyncCommandBuilder) + PayloadContextStub

### Etap 3 — Testy StreamProcessor / AM824 ✅
**Pliki:** `ASFWDriver/Isoch/Receive/StreamProcessor.hpp`, `ASFWDriver/Isoch/Audio/AM824Decoder.hpp`
- Syntetyczne pakiety IEC 61883-6 (stereo 48kHz)
- Walidacja DBC continuity (wykrywanie dropped packets)
- Dekodowanie AM824 → PCM S32
**Wynik:** +17 testów

### Etap 4 — Analiza ścieżki MOTU 828 MK3 ✅
Szczegóły w `MOTU_828_MK3_BringUp.md`. Zidentyfikowane 2 krytyczne gapy (GAP 1: ConnectOPCR bez kanału, GAP 2: brak IRM w AVCAudioBackend).

### Etap 5 — IRM + ConnectOPCR fix ✅
- `ConnectOPCR(plug, channel, callback)` — dodano parametr channel
- `AVCAudioBackend::SetIRMClient` + `AllocateResources` przed `StartReceive`
**Wynik:** +16 testów

### Etap 6 — Bring-up hardening ✅
- **6a** oPCR read-back po ConnectOPCR (+3 testy)
- **6b** Bus reset recovery w AudioCoordinator (suspend/resume)
- **6c** rescanAttempts_ reset na OnUnitResumed
- **6d** IOPCIClassMatch zamiast IOPCIMatch (TB adapter support)

### Etap 7 — RX queue wiring fix ✅
`MapRxQueueFromNub` wywoływana ponownie w `StartDevice` gdy `!rxQueueValid`.

### Etap 8 — TX queue wiring fix ✅
`HandleWriteEnd` pisał do local ring buffer zamiast shared TX queue.

### Etap 9 — HandleChangeSampleRate ✅
- `ASFWIOUserAudioDevice` — podklasa `IOUserAudioDevice` z overridem `HandleChangeSampleRate`
- `AVCDiscovery::SendSampleRateCommand` — AV/C opcode 0x19, SFC encoding per IEC 61883-6
- Runtime sample rate switching przez HAL (Audio MIDI Setup)

---

## Sesja 10 — 2026-05-27 (Fix III + hipoteza workQueue deadlock)

### Fix III — Allow requestedChannels > queueChannels w IsochAudioTxPipeline (`3241bd2`)

**Objaw v15 (po Fix II):** IT configure nadal failowało:
```
IT: Configure failed - requestedChannels=18 queueChannels=2 mismatch
```
MOTU 828 MK3 wymaga `DBS=18` w CIP (18 quadletów na blok danych), ale CoreAudio
dostarcza stereo (2 kanały PCM). Poprzedni kod sprawdzał `requestedChannels != queueChannels`
→ fail za każdym razem.

**Fix:** Zmieniono `!=` na `<` — extra AM824 sloty są silence-padded.
Fail tylko gdy `requestedChannels < queueChannels` (audio byłoby obcięte).

```cpp
// PRZED (commit 3241bd2):
if (requestedChannels != 0 && requestedChannels != queueChannels) { ... fail }

// PO:
// Allow requestedChannels > queueChannels: extra AM824 slots are silence-padded.
if (requestedChannels != 0 && requestedChannels < queueChannels) { ... fail }
```

**Wynik:** IT poprawnie skonfigurowane — DBS=18, pcm=2, midiSlots=16:
```
IT: Channel geometry resolved pcm=2 dbs=18 midiSlots=16 framesPerData=8 payloadBytes=576
IT: Cadence resolved mode=blocking dbs=18 framesPerData=8 cadence=NDDD
✅ Started IT Context for Channel 1!
IT: Stopped. Stats: 4644 pkts (3483D/1161N) IRQs=556
```

Commit: `3241bd2`. Wersja: `0.2.15-audio` (ten sam build co Fix II).

### .mcp.json w katalogu projektu (`0d56712`)

CodeGraph nie ładował się w sesjach Claude Code CLI — `.mcp.json` był tylko w katalogu
nadrzędnym `FireWire/` (CWD serwera MCP). Claude Code CLI ładuje `.mcp.json` z katalogu
projektu. Stworzono `/ASFireWire/.mcp.json` z tą samą konfiguracją.

### Analiza: MOTU wciąż seq=0 mimo Fix II + Fix III

**Stan:** IT nadaje 4644 pakietów w ~580ms, ISOC_COMM_CONTROL=0xC1C00000 ✅,
FETCH_PCM_FRAMES set ✅, IR DMA aktywne (active=1) — ale `seq=0 ageMs=0`.

**Wykluczone:**
- Z=1 w IR CommandPtr — poprawne dla INPUT_LAST (Linux i Apple używają Z=1)
- IR cycleMatchEnable — już naprawione w Fix E (`935d3ff`)
- Kolejność MOTUAudioBackend — ISOC_COMM_CONTROL + FETCH_PCM_FRAMES są przed StartTransmit ✅
- Błędne kanały — ISOC_COMM_CONTROL=0xC1C00000 (MOTU TX=ch0=nasz IR, MOTU RX=ch1=nasz IT) ✅

**Hipoteza workQueue deadlock (niezweryfikowana):**

`Poll()` jest wywoływany z dwóch miejsc:
1. `InterruptDispatcher.cpp:32` — `workQueue.DispatchAsync(^{ isoch.ReceiveContext()->Poll(); })`
2. `WatchdogCoordinator.cpp:110` — `isochReceiveContext->Poll()` (co 1ms)

`workQueue` = "Default" dispatch queue (serial), uzyskiwany przez `DriverWiring::PrepareQueue`
→ `CopyDispatchQueue("Default")`.

`StartTransmit` blokuje workQueue przez do 600ms (100ms fill wait + 500ms SYT gate) używając
`IOSleep`. Jeśli workQueue jest serial i `IOSleep` nie zwalnia kolejki, to `DispatchAsync(Poll)`
z IR interrupt jest kolejkowane za blokującym `StartTransmit` → Poll() nie uruchamia się
podczas okna SYT gate → bridge nigdy nie dostaje aktualizacji → `seq=0` → timeout.

**Kontrargument:** WatchdogCoordinator też wywołuje Poll(). Jeśli watchdog działa na
osobnej kolejce (nie workQueue), Poll() jest wywołane co 1ms niezależnie od blokady workQueue.

**Następny krok:** Czytać `WatchdogCoordinator.cpp` — na jakiej kolejce działa?
Jeśli watchdog = workQueue → deadlock hipoteza potwierdzona → fix potrzebny.
Jeśli watchdog = osobna kolejka → Poll() działa, problem jest gdzie indziej.

### Stan po sesji 10

| Element | Stan |
|---------|------|
| Fix III (requestedChannels > queueChannels) | ✅ `3241bd2` |
| .mcp.json w katalogu projektu | ✅ `0d56712` (aktywny od następnej sesji) |
| IT nadaje 4644 pakietów | ✅ potwierdzone |
| MOTU seq=0 (brak IR) | 🔍 hipoteza: workQueue deadlock z IOSleep w SYT gate |
| WatchdogCoordinator — kolejka? | ⏳ do zbadania w następnej sesji |

---

## Sesja 15 — 2026-05-28 (Fix 19 hardware test + Fix 20 — override wire DBS)

### Hardware test v18 / Fix 19 — wyniki (potwierdzone)

Fix 19 (dwuetapowy ISOC_COMM_CONTROL + SYT gate 3000ms) **potwierdził się na sprzęcie**.
IR pakiety zaczęły płynąć:

```
RxStats: Data=0 IRQ=0 Polls=100 (raw=100)   ← startowa próbka
RxStats: Data=456 IRQ=234 Polls=100 (raw=200)  ← pakiety napływają ✅
ExternalSyncBridge: seq>0 syt_established=1   ← SYT clock locked ✅
MOTUAudioBackend: Streaming started GUID=0x0001F20000087236 ✅
```

SYT gate przeszedł w ciągu 3s. Stary timeout 500ms był za krótki na lock PLL MOTU.
Deactivate-before-activate wyeliminował problem stale state z poprzedniej sesji.

### Nowy problem odkryty: cycling CIP DBS

Mimo że IR pakiety napływały, `StreamProcessor` odrzucał każdy:

```
Unsupported wire DBS=117 (max AM824 slots=32, queueCh=2) - skipping decode
Unsupported wire DBS=33  (max AM824 slots=32, queueCh=2) - skipping decode
Unsupported wire DBS=245 (max AM824 slots=32, queueCh=2) - skipping decode
```

Obserwacja: wartości DBS (9, 33, 53, 117, 245…) są **cyklicznym licznikiem urządzenia** —
MOTU V3 używa pola DBS w nagłówku CIP jako własnego counter-a, zamiast faktycznego rozmiaru
bloku danych. To naruszenie IEC 61883-1.

### Wyprowadzenie prawdziwego DBS=21

Znane dane z diagnostyki Sequoia:
- Pakiet IR = 504 bajty payload
- 8000 cykli/s (1394 isochronous rate)
- Target: 48 000 Hz

```
DBS × 4B × eventCount × 8000 = 48000 Hz
504B / (DBS × 4B) = eventCount
504 / (21 × 4) = 6 ✓   → 6 × 8000 = 48000 ✅
```

Jedyna liczba całkowita dająca standardową częstotliwość próbkowania to DBS=21.
Potwierdzone przez: tabelę danych kext MOTU (Box828mk3 format word) i Linux
`sound/firewire/motu/motu-stream.c` (motu_stream_get_pcm_channels, 14 PCM + MIDI + overhead).

### Fix 20 — override wire DBS=21 (`597f3c8`)

Dodano pole `overrideWireDbs_` w `StreamProcessor`. Gdy != 0:
- zastępuje pole DBS z nagłówka CIP dla `dbsBytes`, `wireSlotsPerEvent`, `summary.dbs`
- check `kMaxAmdtpDbs` jest **całkowicie pomijany** (MOTU osiąga wartości do 245)
- pole jest konfiguracyjne — `Reset()` go nie czyści

Łańcuch propagacji: `StreamProcessor → IsochAudioRxPipeline → IsochReceiveContext → IsochService`

W `MOTUAudioBackend::StartStreaming` po `StartReceive`:
```cpp
isoch_.SetRxOverrideWireDbs(kMOTUV3WireDbs48k);  // = 21
```

### Stan po sesji 15

| Element | Stan |
|---------|------|
| Fix 19 (deactivate+3s SYT gate) | ✅ hardware potwierdzone |
| IR pakiety napływają | ✅ ~456/100 polls |
| Fix 20 (override wire DBS=21) | ✅ `597f3c8` — zbudowane, nie testowane na HW |
| AM824 decode z DBS=21 | ⏳ hardware test v19 |
| IR decode z 2 kanalami PCM | ⏳ queueChannels=2, MOTU ma 14 — cisza dla kanałów 3–14 |

Następny hardware test: zainstalować `ASFW_v19_Fix20.app` na Mac Studio.
Oczekiwane: brak `Unsupported wire DBS`, `RxStats.Data` rosnące z sensownymi wartościami.

---

## Sesja 9 — 2026-05-27 (IT DMA deadlock — Fix II)

### Fix II — SYT wait przeniesiony po Start() w IsochService (`2dc6600`)

**Objaw v13/v14:** `StartTransmit timeout: missing established IR SYT clock`
`seq=0 syt=0x0000 active=1 established=0` — IT OHCI DMA nigdy nie startowało.
Brak logu `✅ Started IT Context` mimo że IR i ISOC_COMM_CONTROL + FETCH_PCM_FRAMES były OK.

**Przyczyna:** `IsochService::StartTransmit` miało SYT wait PRZED `isochTransmitContext_->Start()`:
```
1. Provision IT context
2. ⛔ WAIT 500ms for IR SYT  ← bug! IT jeszcze nie startuje
3. Configure IT context
4. Wait for TX fill
5. Start IT context          ← za późno, timeout już nastąpił
```
IT DMA nigdy nie uruchamiało rejestrów OHCI → MOTU nie dostało IT pakietów → nie odpowiadało IR
→ permanentny 500ms timeout. Deadlock niezależny od Fix I.

**Dowód z logów (09:41:35 — test historyczny na v13 / Fix I):**
```
09:41:35.405: ✅ provisioned IT Context with Dedicated Memory
09:41:35.810: FETCH_PCM_FRAMES set ✅
              (brak: "✅ Started IT Context")
09:41:36.377: ❌ StartTransmit timeout: seq=0 syt=0x0000 active=1 established=0
```
Przerwa 555ms = 500ms SYT timeout + overhead. IT context nigdy nie startowało.

**Fix:** SYT wait przeniesiony na PO `isochTransmitContext_->Start()`:
```
1. Provision IT context
2. Configure IT context
3. Wait for TX fill
4. Start IT context           ← IT DMA aktywne, OHCI nadaje
5. ✅ WAIT 500ms for IR SYT  ← MOTU dostaje IT pakiety i odpowiada IR
6. Jeśli SYT timeout → Stop IT, return kIOReturnTimeout
```

Nowy komunikat błędu (potwierdzony przez `strings` na binarium):
`"StartTransmit SYT timeout: IT is running but MOTU not responding"`
Stary (`"missing established IR SYT clock"`) nieobecny w v15 binarium.

Commit: `2dc6600`. Wersja: `0.2.15-audio`.

### Xcode cache gotcha — zawsze czyść przed rebuildem IsochService

v14 zbudowany ze stale cache'owanym `IsochService.o` — plik źródłowy zmieniony (Fix II),
ale incremental build Xcode użył starego obiektu. Binarium v14 zawierało stary komunikat
`"missing established IR SYT clock"` mimo prawidłowego kodu źródłowego.

**Fix:** zawsze przed rebuildem po zmianach w isoch:
```bash
rm -rf /tmp/ASFWBuild && ./build.sh --derived /tmp/ASFWBuild --no-bump
```

### Zombie dext — cleanup przy upgrade

v14 zainstalowany, ale procesy `_driverkit` z UUID A001CEB4 (PIDs 10907/10908) utkwiły
w `[terminating for upgrade via delegate]`. Nowy v15 nie mógł się zainstalować.

Cleanup:
```bash
sudo kill -9 10907
sudo kill -9 10908
```
Po kill: UUID 9EB26C49 (v13) uruchomił się ponownie jako PIDs 16324/16326 — v15 mógł się zainstalować.

### Stan po sesji 9

| Element | Wersja | Stan |
|---------|--------|------|
| Fix I (ISOC_COMM_CONTROL + FETCH_PCM_FRAMES przed StartTransmit) | v13 (`662ca0d`) | ✅ potwierdzone w logach |
| Fix II (SYT wait po Start() w IsochService) | v15 (`2dc6600`) | ✅ deployed, binary zweryfikowany |
| v15 aktywny | v15 | ✅ `[activated enabled]` |
| Test streamingu (IR seq>0) | v15 | ⏳ wymagany test hardware |

---

## Sesja 8 — 2026-05-27 (Fix I potwierdzony — FETCH_PCM_FRAMES)

### Fix I potwierdzony na logach (`662ca0d`)

Logi z v13 (UUID 9EB26C49 / "v13" po `systemextensionsctl`) pokazały:
- `FETCH_PCM_FRAMES set ✅` — MOTU dostaje sygnał do nadawania
- `ISOC_COMM_CONTROL=0xC1C00000 (irCh=0 itCh=1)` — kanały prawidłowe
- Mimo to `seq=0 syt=0x0000` — IT DMA nie startowało (→ zidentyfikowane jako Fix II)

Working tree na dev machine miał uncommitted modyfikację zamieniającą kolejność kroków.
Commit `662ca0d` porządkuje zmiany i przywraca cleanup error paths.

---

## Sesja 7 — 2026-05-26 (IR cycleMatchEnable + work queue deadlock)

### Fix E — IR cycleMatchEnable bug (`935d3ff`)

**Objaw:** IR DMA uruchomione (ContextControlSet, Dead=0), ale `seq=0 syt=0x0000 ageMs=0`
— zero pakietów odebranych mimo że MOTU nadaje.

**Przyczyna:** `OHCIConstants.hpp` definiowało `kIsochHeader = 1u << 30`.
W `IsochReceiveContext.cpp` używano `kRun | kIsochHeader` przy starcie IR kontekstu.
Bit 30 w ContextControlSet to **`cycleMatchEnable`** (OHCI §10.2.2 IR) — nie „włącznik nagłówka".
Ustawienie go powoduje, że kontekst zatrzymuje się i czeka aż OHCI cycle counter
zgadza się z wartością w rejestrze ContextMatch → zero pakietów dopóki przypadkowy match.

**Fix:** `IsochReceiveContext.cpp` zmieniono na:
```cpp
hardware_->Write(registers_.ContextControlClear, 0xFFFFFFFFu);
const uint32_t ctlValue = ContextControl::kRun | ContextControl::kWake;  // 0x9000
hardware_->Write(registers_.ContextControlSet, ctlValue);
```
Usunięto `kIsochHeader` z `OHCIConstants.hpp`, dodano `kCycleMatchEnable` z pełną dokumentacją.
Nagłówek isoch w buforze: sterowany przez flagę `"i"` w polu control deskryptora, nie przez ContextControlSet.

### Fix F — Work queue serial deadlock (`5554280`)

**Objaw:** `StartStreaming` wywoływane przez CoreAudio nigdy nie kończyło.
CLOCK_STATUS read zwracało timeout — rejestry MOTU nieczytelne.

**Przyczyna:** CoreAudio wywołuje `StartDevice` na serialowej dispatch queue.
`StartStreaming` → AT async quadlet read → `IODispatchQueue::DispatchSync` czeka na completion.
Completion callbacki lądują na tej samej serialowej queue → **deadlock**.

**Fix:** `StartStreaming` wysłany przez `DispatchAsync_f` na nową `IODispatchQueue`.
Potwierdzono w v10 logach: CLOCK_STATUS=0x0a000100 (clock locked, fetch active) — rejestry czytelne.

### Fix G — Auto-aktywacja dextu przy starcie apki

`ModernContentView.onAppear` teraz wywołuje `driverVM.installDriver()`.
Dext instaluje/upgraduje się bez klikania przycisku. `DriverInstallManager` traktuje
error 4 (OSSystemExtensionErrorDomain) jako sukces (wersja już aktywna).

### Fix H — Zombie dext przy upgrade (AudioDriverKit)

Przy upgrade dextu (v9→v10), stary `_driverkit` PID nie terminował — CoreAudio HAL
trzymał aktywne `IOUserAudioDevice`. `systemextensionsctl` ugrzązło w
`terminating_for_upgrade_via_delegate`. `kill -9` nie pomogło. **Jedyne rozwiązanie: reboot.**

Lekcja: dext z aktywnym AudioDriverKit wymaga restartu systemu przy każdym upgrade.

### Stan po sesji 7

| Element | Wersja | Stan |
|---------|--------|------|
| work queue deadlock | v10 (`5554280`) | ✅ potwierdzone — rejestry OK |
| IR cycleMatchEnable | v11 (`935d3ff`) | ✅ potwierdzone — IR active=1 Ctl=0x9400 |
| MOTU StartStreaming | v11 | ✅ wywoływane przez CoreAudio |
| IR odbiera pakiety | v11 | ✅ IR DMA startuje; seq=0 → Fix II (sesja 9) |

---

## Sesja debugowania — Mac Studio Tahoe (2026-05-18)

### Problem: FCP timeout

Dext działa (`[activated enabled]`, PID 22038). MOTU 828 MK3 wykryty (Node 0, GUID `0x0001F20000087236`).

```
FCPTransport: Command timeout (interim=0, retries=0)
```

Trzy kluczowe logi NIGDY nie pojawiały się:
1. `FCPTransport: AT write ack'd by MOTU`
2. `AR Req tCode=0x1: srcID=...`
3. `RxPath: AR Request processed N buffers`

**Potwierdzone (nie są przyczyną):**
- ASReqFilter = 0xFFFFFFFF (oba rejestry) ✅
- tCode=0x1 routing w DriverContext.cpp — logika SBP2→FCP fallback poprawna
- `busOps_->WriteBlock()` zwraca non-zero handle

**Pliki zmodyfikowane w tej sesji:**
| Plik | Zmiana |
|------|--------|
| `ASFWDriver/Async/Rx/RxPath.cpp` | V1 log "AR Request processed N buffers" |
| `ASFWDriver/Protocols/AVC/FCPTransport.cpp` | V1 log "AT write ack'd by MOTU" |
| `ASFWDriver/Service/DriverContext.cpp` | V1 log "AR Req tCode=0x1: srcID=..." |
| `ASFWDriver/Bus/BusResetCoordinatorActions.cpp` | ASReqFilter readback po Write |
| `ASFWDriver/Controller/ControllerCoreLifecycle.cpp` | ASReqFilter readback po WriteAndFlush |

**Gdzie przerwano czytanie ATContextBase.hpp:** offset ~870 — obsługa OUTPUT_LAST z xferStatus≠0 (normalna completion block write) i ekstrakcja tLabel.

---

## Sesja 2026-05-24 — Diagnoza protokołu MOTU

### Odkrycie: MOTU 828 MK3 NIE używa AV/C

Analiza Linux kernel driver (`sound/firewire/motu/motu-protocol-v3.c` + `motu-stream.c`) wykazała że MOTU 828 MK3 używa **protokołu V3 opartego na własnych rejestrach** — bez AV/C, bez FCP, bez CMP.

**Dwa równoległe problemy zidentyfikowane:**
1. **AT DMA block write prawdopodobnie zepsuty** — OHCI nie dostaje ack od MOTU nawet na PHY level. Może być descriptor chain bug w `ATContextBase` (nieodczytany do końca od offset ~870).
2. **Zły protokół** — AV/C/FCP/CMP nie jest właściwe dla MOTU 828 MK3. Nawet z działającym block write, MOTU nie odpowie na AV/C.

### Mapa rejestrów MOTU V3

| Offset od 0xfffff0000000 | Nazwa | Opis |
|--------------------------|-------|------|
| `0x0b00` | ISOC_COMM_CONTROL | Włączenie isoch, numery kanałów RX/TX |
| `0x0b04` | ASYNC_ADDR_HI | Adres hosta do async notyfikacji (hi) |
| `0x0b08` | ASYNC_ADDR_LO | Adres hosta do async notyfikacji (lo) |
| `0x0b10` | PACKET_FORMAT | Format pakietów + prędkość TX |
| `0x0b14` | CLOCK_STATUS | Sample rate + źródło + FETCH_PCM_FRAMES |
| `0x0c94` | OPT_IFACE_MODE | Interfejs optyczny ADAT/SPDIF |

**ISOC_COMM_CONTROL bit layout (Linux motu-stream.c):**
```c
ISOC_COMM_CONTROL_MASK     = 0xffff0000  // bits do modyfikacji
CHANGE_RX_ISOC_COMM_STATE  = 0x80000000
RX_ISOC_COMM_IS_ACTIVATED  = 0x40000000
RX_ISOC_COMM_CHANNEL_SHIFT = 24          // 6 bitów kanału RX
CHANGE_TX_ISOC_COMM_STATE  = 0x00800000
TX_ISOC_COMM_IS_ACTIVATED  = 0x00400000
TX_ISOC_COMM_CHANNEL_SHIFT = 16          // 6 bitów kanału TX
```

**CLOCK_STATUS bit layout:**
```c
V3_FETCH_PCM_FRAMES  = 0x02000000   // włącza streaming PCM
V3_CLOCK_RATE_MASK   = 0x0000ff00   // bits [15:8] = rate code
V3_CLOCK_RATE_SHIFT  = 8
// rate codes: 32k=0x00, 44.1k=0x01, 48k=0x02, 88.2k=0x03, 96k=0x04
```

**Linux begin_session() (motu-stream.c) — cała funkcja:**
```c
data = be32_to_cpu(reg) & ~ISOC_COMM_CONTROL_MASK;
data |= CHANGE_RX_ISOC_COMM_STATE | RX_ISOC_COMM_IS_ACTIVATED |
        (motu->rx_resources.channel << RX_ISOC_COMM_CHANNEL_SHIFT) |
        CHANGE_TX_ISOC_COMM_STATE | TX_ISOC_COMM_IS_ACTIVATED |
        (motu->tx_resources.channel << TX_ISOC_COMM_CHANNEL_SHIFT);
```

### Plan MacBook Sequoia — zbieranie danych z oryginalnego sterownika MOTU

Jeśli MOTU jest dostępny na MacBooku Sequoia:

```bash
# Verbose IOFireWire logging (po reboocie)
sudo nvram boot-args="IOFireWireDebug=0xffffffff"
sudo reboot
log stream --predicate 'senderImagePath contains "FireWire"' --level debug \
  | tee ~/Desktop/fw_log_$(date +%Y%m%d_%H%M%S).txt

# DTrace na IOFireWireAVC (SIP off)
sudo dtrace -n 'fbt:com.apple.iokit.IOFireWireAVC::entry { printf("%s\n", probefunc); }' \
  | tee ~/Desktop/avc_trace_$(date +%Y%m%d_%H%M%S).txt
```

---

## Stan signing / build na Mac Studio (2026-05-18)

- **ASFWDriver**: `CODE_SIGN_IDENTITY = "-"` (ad-hoc), `ARCHS = "arm64e x86_64"`, `ONLY_ACTIVE_ARCH = NO`
- **ASFW app**: build z `App_build.entitlements`, post-build re-sign z `App.entitlements`
- **teamID**: 4MJNRC8SW5, cert: `"Apple Development: j.slipiec@gmail.com (239NB3LFDQ)"`

**Ważne gotchas:**
- ARM64e slice jest wymagany dla DriverKit na Apple Silicon — `ARCHS = "arm64e x86_64"`, `ONLY_ACTIVE_ARCH = NO`
- Post-build action czasem nie działa, fallback: ręczny codesign z Terminala
- error 4 przy re-launch apki = "already active" = OK

```bash
# Fallback codesign po buildzie
codesign --force --options runtime --sign "Apple Development: j.slipiec@gmail.com (239NB3LFDQ)" \
  --entitlements ".../ASFW/App.entitlements" --timestamp=none \
  ~/Library/Developer/Xcode/DerivedData/ASFW-*/Build/Products/Debug/ASFW.app
```

| Plik | Cel |
|------|-----|
| `ASFW/App_build.entitlements` | Build-time app entitlements (bez restricted) |
| `ASFW/App.entitlements` | Post-build re-sign entitlements (pełne) |
| `ASFWDriver/ASFWDriver.entitlements` | Dext entitlements (pełne) |
| `ASFW.xcodeproj/xcshareddata/xcschemes/ASFW.xcscheme` | Post-action: re-sign app → dext |

---

## Sesja 5 — 2026-05-25 (Fix A, B — FETCH_PCM_FRAMES + ISOC_COMM_CONTROL)

### Fix A — FETCH_PCM_FRAMES przed StartTransmit

MOTU V3 wymaga **obu** operacji zanim zacznie wysyłać IR:
1. `ISOC_COMM_CONTROL` — które kanały isoch
2. `CLOCK_STATUS | FETCH_PCM_FRAMES` — **to wyzwala nadawanie IR przez MOTU**

Linux robi `begin_session()` + `switch_fetching_mode(true)` oba przed startem DMA.

### Fix B — zamiana kanałów w ISOC_COMM_CONTROL

Rejestr 0x0b00 używa nazewnictwa z perspektywy MOTU (device-centric):
- bity [29:24] = "RX" = MOTU **odbiera** = host→device = nasz **IT** kanał
- bity [21:16] = "TX" = MOTU **nadaje** = device→host = nasz **IR** kanał

Poprzedni błąd: `irCh` w polu RX, `itCh` w polu TX — MOTU nadawało IR na kanale 1,
nasze IR DMA słuchało kanału 0 → zero pakietów. Fix: zamieniono miejscami.
Poprawna wartość (irCh=0, itCh=1): `0xC1C00000`.

---

## Sesja 6 — 2026-05-26 (Fix C, D — AudioClockEngine + double-start guard)

### Fix C — UpdateCurrentZeroTimestamp(0, 0) → (0, currentTime)

`AudioClockEngine.cpp` `PrepareClockEngineForStart()` ustawiał anchor na `(sampleTime=0, hostTime=0)`.
CoreAudio interpretuje to jako "sample 0 był w chwili 0" (dawn of time) → liczy zaległe IO cycles
→ chaos → "not consecutive" → IO stop po ~5s.
Fix: `UpdateCurrentZeroTimestamp(0, mach_absolute_time())` — anchor jest teraz.

### Fix D — double-start guard w StartDevice

Jeśli CoreAudio ma dwa klientów jednocześnie, wywołuje `StartDevice` dwa razy. Drugie wywołanie
resetowało anchor do `(0,0)` podczas gdy sampleTime był na ~1,7M → skok wstecz = "not consecutive".
Fix: early return gdy `isRunning == true`.

**Jak poprawnie czytać logi drivera:**
`log` w zsh to wbudowana funkcja matematyczna — zawsze używaj pełnej ścieżki `/usr/bin/log`.

---

## Etap 10 — MOTU V3 Protocol Backend (2026-05-24) ✅

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
7. isoch_.StartTransmit(itCh)   → start IT OHCI DMA  ← Fix II: SYT wait PO uruchomieniu IT DMA
```

### Routing urządzeń (DeviceProtocolFactory)

| Urządzenie | Vendor | Model | Backend |
|------------|--------|-------|---------|
| MOTU 828 MK3 FW | 0x0001F2 | 0x000015 | `motuV3_` |
| MOTU 828 MK3 Hybrid | 0x0001F2 | 0x000035 | `motuV3_` |
| MOTU 896 MK3 | 0x0001F2 | 0x000016 | `motuV3_` |
| MOTU Traveler MK3 | 0x0001F2 | 0x000017 | `motuV3_` |
| MOTU UltraLite MK3 | 0x0001F2 | 0x000019 | `motuV3_` |

---

## ROZWIĄZANE — Model ID MOTU w Config ROM (2026-05-24)

**Potwierdzono na Sequoia z System Information:**
- Root directory `Model = 0x106800` (nie `0x000015`)
- Unit directory `Unit_SW_Vers = 0x15` = `0x000015` ← właściwe pole!
- GUID = `0x1F20000087236` ✅

**Przyczyna bugu:** `BackendForGuid` używał `record->modelId` (root dir = `0x106800`) zamiast
`record->unitSwVersion` (unit dir = `0x000015`). MOTU nie wstawia modelu do root directory.

**Fix:** `DeviceProtocolFactory::EffectiveModelId()` — dla vendor `0x0001F2` zwraca `unitSwVersion`.
Commit `abc75ea`. 488/488 testów ✅.

---

## ZWERYFIKOWANE — Analiza kexta MOTUFireWireAudio (2026-05-24)

Zdisassemblowano kext `/Library/Extensions/MOTUFireWireAudio.kext` na Sequoia (slice x86_64).

| Stała | Wartość kext | Nasza wartość | Status |
|-------|-------------|---------------|--------|
| CLOCK_STATUS addr | `0xf0000b14` (w tablicy data) | `kClockStatusOff = 0x0b14` | ✅ |
| V3_FETCH_PCM_FRAMES | `0x02000000` | `kFetchPCMFrames = 0x02000000` | ✅ |
| Rate code mask | `andl $0x700` → bits[10:8] | `kClockRateMask = 0x00000700` | ✅ |
| PACKET_FORMAT addr | `0xf0000b10` | `kPacketFmtOff = 0x0b10` | ✅ |
| PACKET_FORMAT value | bit7=TX_excl, bit6=RX_excl, bits[1:0]=speed | `0xC2 = 0x80\|0x40\|0x02` | ✅ |
| ISOC_COMM_CONTROL addr | `0xf0000b00` | `kIsocCtrlOff = 0x0b00` | ✅ |

Kolejność init MK3: Read CLOCK_STATUS → SetupStreams → WritePacketFormat → WriteIsocCtrl → SetFetchPCMFrames.
`kClockRateMask` poprawiony z `0x0000ff00` na `0x00000700` (3 bity [10:8]).

---

## ROZWIĄZANE — AT DMA block write (tCode=0x1) (2026-05-24)

**Plik:** `ASFWDriver/Async/Contexts/ATContextBase.hpp` — `ScanCompletion()`

**Problem:** Po zakończeniu PATH1 no-branch chain (FCP write) OHCI ustawia RUN=1, Active=0, CommandPtr=0.
Stary `isOrphaned` miał dwa człony — oba false w tym stanie → `ScanCompletion` zwracał `nullopt`
jakby hardware wciąż pracował → timeout każdego block write.

**Fix:** Dodano trzecią klauzulę `completedAndIdle = (isRunning && !isActive && commandPtrAddr == 0)`.
Przy OUTPUT_MORE precursorze: `continue` zamiast `return nullopt` → OUTPUT_LAST przetwarzany
w tym samym wywołaniu. Commit `eeb8787`. 488/488 testów ✅.

---

## Sesja hardware 2026-05-25 część 1 — Pierwsze potwierdzenia

**Potwierdzenia:**
- Async reads (ReadQuad) na rejestrach MOTU działają ✅ — rCode=Complete
- Async writes (WriteQuad) na rejestrach MOTU działają ✅ — rCode=Complete
- ASFWAudioNub pojawia się w IORegistry ✅
- MOTU 828 MK3 pojawia się w macOS Sound settings jako "FireWire" ✅
- `MOTUAudioBackend::StartStreaming` JEST wywoływany przez ścieżkę CoreAudio ✅

**Kluczowe odkrycie — PACKET_FORMAT jest write-only:**
Rejestr `0x0b10` zwraca `0x00000000` przy odczycie niezależnie od zapisu. Analogicznie inne rejestry
MOTU mogą mieć podobne właściwości (odczyt 0 ≠ nie zapisane).

---

## Sesja hardware 2026-05-25 część 2 — IR DMA ręczny vs CoreAudio

**Kluczowe odkrycia:**

```bash
# Jedyna działająca metoda logów (potwierdzona Tahoe 2026-05-25):
/usr/bin/log stream --debug --info 2>/dev/null | grep "ASFWDriver.dext"
# Dext logi: kernel: (net.mrmidi.ASFW.ASFWDriver.dext) [Kategoria] Treść
```

**IR DMA uruchomiony przez RĘCZNY KLIK, nie CoreAudio:**
```
[Isoch] ✅ Started IR Context 0 for Channel 0!   ← 11:19:50 — ręczny klik Isoch Metrics
[Isoch] RxStats Pkts=0 every ~700ms              ← 0 pakietów mimo running IR
```
CoreAudio NIE wywołało `StartDevice` przez cały czas obserwacji — MOTU widoczne w Sound Settings,
status "Idle". Przyczyna: dext startował z `timestampTimer == nullptr`.

**DMA Slab IOVA na Tahoe/Apple Silicon = `0x80000000`** — valid non-zero. DMA mapping działa.

---

## Sesja hardware 2026-05-25 część 3 — Dwa bugi w ASFWAudioDriver

### Bug 1 — Race condition: timer tworzony PO `RegisterService()` [KRYTYCZNY]

```
Start():
  AddObject(audioDevice)      ← device widoczny
  RegisterService()            ← CoreAudio może dzwonić StartDevice!  ← OKIENKO RYZYKA
  IOTimerDispatchSource::Create(...)  ← timer jeszcze nie istnieje
```

Jeśli Spotify grało gdy dext się restartował (kill -9 → auto-restart), CoreAudio natychmiast
wywołało `StartDevice` po `RegisterService()`. W tym momencie `timestampTimer == nullptr`
→ `StartDevice` zwracało `kIOReturnNotReady` → CoreAudio rezygnuje → urządzenie na stałe "Idle".

**Fix:** timer i akcja tworzone **przed** `RegisterService()`.

### Bug 2 — Brak `SUPERDISPATCH` w `StartDevice` i `StopDevice`

`StartDevice` i `StopDevice` używały plain C++ override bez SUPERDISPATCH — framework ADK
nigdy nie był notyfikowany o starcie IO. Fix: `StartDevice`/`StopDevice` → `IMPL` + `SUPERDISPATCH`.

---

## Sesja hardware 2026-05-25 część 4 — IRM fix + ISOC_COMM_CONTROL deadlock

### Fix — IRM self-addressed async transactions (IRMClient)

Gdy Mac jest jedynym IRM-em, `ReadIRMQuadlet` i `CompareSwapIRMQuadlet` wysyłały async transakcje
do siebie samego przez OHCI. OHCI nie routuje AT→AR dla self-addressed transakcji → timeout.

**Rozwiązanie:** shadow registers (`shadowBandwidth_=4915`, `shadowChannelsLo/Hi_=0xFFFF`).
Gdy `IsLocalIRM()`, operacje wykonywane lokalnie bez transakcji async.

**Potwierdzone:**
```
[IRM] IRMClient: local-IRM CAS addr=0xf0000220 old=0x00001333 ... OK
[IRM] Bandwidth allocation succeeded (294 units)
[IRM] Channel 0 allocation succeeded
[IRM] Channel 1 allocation succeeded
```

**Pliki:** `ASFWDriver/IRM/IRMClient.hpp`, `IRMClient.cpp`, `Controller/ControllerCoreDiscovery.cpp`

### Potwierdzone milestony po sesji 2026-05-25

- ✅ MOTU 828 MK3 widoczny w System Settings → Sound jako "FireWire"
- ✅ CoreAudio wywołuje StartDevice → StartAudioStreaming → MOTUAudioBackend
- ✅ IRM alokacja działa bez timeoutów
- ✅ IR DMA startuje (OHCI context aktywny na kanale 0)

---

## Sesja 14 — Fix 19 blocker: SYT gate + deactivate-before-activate

**Potwierdzony objaw:** `Streaming stopped` ale nigdy `Streaming started`.
→ `StartTransmit` zwraca `kIOReturnTimeout` bo MOTU nie nadaje IR na ch=0.

**Fix 19 (commit `68823bf`):**
1. **Deactivate przed activate** — jeśli MOTU jest w stale state (lower bits `0x1900` zamiast idle `0x3000`),
   bezpośredni activate może być zignorowany. Two-step: deactivate (20ms) → activate.
2. **SYT gate: 500ms → 3000ms** — MOTU może potrzebować więcej czasu na lock PLL po odebraniu
   pierwszych IT pakietów.

**Diagnostyka po naprawie SYT:**
```
seq=0  → MOTU NIE nadaje IR wcale (problem rejestrowy lub hardware)
seq>0, established=0 → MOTU nadaje IR, ale CIPHeader::Decode odrzuca (format CIP)
```

```
IR Poll[0] ch=0: 0 pkts in last 500 polls        ← MOTU milczy (scenariusz A)
IR Poll[0] ch=0: 47 pkts in last 500 polls       ← MOTU wysyła!
IR HW[0] ch=0: ctl=0x9400 run=1 active=1 dead=0 ← context żywy (dobry stan)
IR HW[0] ch=0: ctl=0x0800 run=0 active=0 dead=1 ← context DEAD — problem deskryptorów (scenariusz C)
```

---

## Sesja 16 — 2026-05-28 (Fix 21, AudioDeviceStart ✅, HALS_IORawClock)

### Fix 21 — IT wire DBS override (MOTUAudioBackend.cpp)

**Objaw:** Brak audio mimo że IT DMA nadaje. MOTU 828 MK3 milczy.

**Przyczyna:** `StartTransmit` używał `config.outputChannelCount` (=2) dla `requestedAm824Slots`.
CIP header miał `DBS=2`. MOTU oczekuje większego DBS. Pakiety z DBS=2 były ignorowane.

**Fix:** `requestedAm824Slots` zmieniony na `kMOTUV3WireDbs48k`. Commit po potwierdzeniu audio.

> ⚠️ **SUPERSEDED (zapis historyczny):** wartość DBS=21 z tej notatki była BŁĘDNA. Realny
> wire DBS dla host→device (IT) = **13** (14 PCM, potwierdzone El Cap + Linux). Aktualne fakty:
> `documentation/MOTU_828_MK3_FACTS.md`. Zostawione dla kontekstu debugowania.

### HALS_IORawClock re-anchoring — zidentyfikowane, nienaprawione

**Objaw:**
```
HALS_IORawClock::Update: Re-anchoring IO timeline.
Sample time is consecutive, host time is not consecutive.
```
Co ~2-3s na początku, potem co 20-50s+ (CoreAudio adaptuje się). System **nie crashuje**.

**Przyczyna:** `PerformIO` wyzwalany przez `IOTimerDispatchSource` z 1ms interwałem.
Kernel może opóźniać timery do ~1.5ms+. CoreAudio widzi: sampleTime rośnie równo, hostTime skacze.

**Fix (TODO):** Zastąpić `mach_absolute_time()` czytaniem OHCI `CurrentIsochronousCycleTime` (offset `0x1E8`):
```cpp
// bits[25:12] = cycleCount (0-7999), bits[11:0] = cycleOffset
// Convert: cycleCount/8000 × timebaseFreq + cycleOffset × (timebaseFreq/24576000)
```

### Czego NIE robi front panel MOTU 828 MK3

- **Metry poziomów** na panelu przednim = tylko analog hardware inputs. Nie pokazują poziomu FireWire IT.
- **Test definitywny:** Słuchawki do gniazda `PHONES` (6.35mm, przód) — zawiera mix z FireWire IT.
- **Isoch Transmit zakładka w ASFW:** Szara gdy IT jest zarządzany przez CoreAudio — normalny stan.
- **Przycisk Stop nie działa w Isoch Metrics:** IR zarządzany przez CoreAudio — to dobry znak.

### Potwierdzenia sesji 16

- `AudioDeviceStart (err 0)` — CoreAudio HAL uruchamia urządzenie ✅
- IO aktywne przez **3+ minuty** bez `AudioDeviceStop` ✅
- IR pakiety: **11 965 pkts/s** ✅

---

## Sesja 17 — 2026-05-28 (Fix 22 — SYT gate bypass dla MOTU V3)

### Fix 22 — SYT gate bypass (IsochService.cpp + MOTUAudioBackend.cpp)

**Objaw:** Chwilowy pisk na słuchawkach przez ~3 sekundy, potem cisza:
```
IT: run=1 active=1 pkts=26644
❌ StartTransmit SYT timeout: IT is running but MOTU not responding
   (waited 3000ms seq=10 syt=0x0000 fdf=0x02 dbs=21 ageMs=2046 active=1 established=0)
IT: Stopped. Stats: 26908 pkts (20181D/6727N)
```

**Analiza:** IT wysłało **20 181 data packets** z prawdziwym dźwiękiem Spotify — pisk to był prawdziwy dźwięk.
`IsochService::StartTransmit` czekało 3000ms na `externalSyncBridge_.clockEstablished`. Ta flaga wymaga
**16 kolejnych pakietów IR** z `fdf == 0x02 AND syt != 0x0000 AND syt != 0xFFFF`.
MOTU 828 MK3 **zawsze wysyła `syt=0x0000`** — nigdy nie osadza IEEE 1394 SYT timestamps.
Linux `snd-firewire-motu` w ogóle nie sprawdza SYT → `clockEstablished` nigdy nie mogła być ustawiona.

**Rozwiązanie:**
```cpp
// IsochService.hpp
kern_return_t StartTransmit(..., bool skipSYTGate = false);

// IsochService.cpp
if (skipSYTGate) {
    ASFW_LOG(Controller, "[Isoch] SYT gate bypassed (device uses syt=0x0000 — MOTU V3 mode)");
} else { /* polling loop 3000ms */ }

// MOTUAudioBackend.cpp
const kern_return_t kr = isoch_.StartTransmit(
    itChannel, hardware_, sid, streamModeRaw,
    config.outputChannelCount, kMOTUV3WireDbs48k,
    txMem, txBytes, nullptr, 0, 0,
    /*skipSYTGate=*/true);  // ← Fix 22
```

---

## Sesja 18 — 2026-05-28 (Fix 21+22+23 — IT DBS + SYT bypass + TX Profile B)

### Diagnoza: przyczyna pisku

Fix 22 pozwolił nadawać przez 56s → **49 632 underrunów** = 14,77% data packets dostaje ciszę.
Underrun rate: 886/s → co ~1ms ring buffer pusty → 14.77% ciszy moduluje dźwięk → pisk.
**Przyczyna:** Profile A target=512 frames = 10,67ms — zerowy margines jittera.

### Fix 23 — TX Profile B (AudioTxProfiles.hpp)

Ring buffer target 512→1024, max 768→1536 frames (~21ms marginu na jitter CoreAudio).
`kTxTuningProfileRaw = 1`, pre-prime unbounded.

### Potwierdzony stan

- ✅ Fix 19 (`68823bf`): deactivate+activate + SYT gate 3000ms
- ✅ Fix 20 (`597f3c8`): override wire DBS=21 dla MOTU V3
- ✅ Fix 21 (uncommitted): IT DBS=21 override — `requestedAm824Slots = kMOTUV3WireDbs48k`
- ✅ Fix 22 (uncommitted): SYT gate bypass — `skipSYTGate=true`
- ✅ Fix 23 (uncommitted): TX Profile B — target=1024, max=1536 frames

---

## Sesja 19 SUMMARY — Fix 26+27+29 (2026-06-01)

### Fix 26 — OHCI Cycle-Time Clock Synchronization

Zmiana z poll-count gate (1000 pollów ≈ 2s) na bus-time gate (100ms).
CycleCorr ratio: 1.000022 (stabilna synchronizacja z MOTU crystal).

### Fix 27 — TX Ring Buffer Expansion

Zwiększenie max frames: 1536 → 4096 (32ms → 85ms).

### Fix 29 — MOTU V3 Packet Encoding (GŁÓWNY FIX sesji 19)

Zmiana formatu IT z AM824 (4B/slot z label 0x40) na MOTU V3 (3-byte packed PCM).

**Diagnoza — dlaczego AM824 nie działał:**
- Wysyłaliśmy: `[label 0x40][PCM 24-bit] × 21 slotów`
- MOTU V3 oczekiwała: `[SPH][msg][msg][PCM×3B×18+]`
- MOTU odbierała nasze pakiety jako śmieci → ignorowała IT → ring buffer zawsze pusty

**Rozwiązanie:**
```cpp
// Stara (AM824 — ZŁA dla MOTU V3):
[label 0x40][PCM 24-bit] × 21 slotów × 4B = 84 bajty

// Nowa (MOTU V3 — PRAWIDŁOWA):
[SPH 4B] [msg 3B] [msg 3B] [PCM 3B×18+] = 82 bajty → pad do 84 (21×4)

// CIP nagłówek MOTU V3 (Fix 29 — przed Fix 45):
Q1: FMT=0x00 (nie 0x10), FDF=0x00 (nie SFC), SYT=0x0000 (zawsze)
```

Nowe funkcje: `encodeInterleavedFramesToMotuV3()`, `fillSilentMotuV3Frames()`.
Propagacja: `PacketEncoding::kMotuV3` przez wszystkie warstwy.
Źródło: `amdtp-motu.c` z Linux kernel (commit f5e5d35). Wszystkie 493 testy ✅.

---

## Sesja 20 — 2026-06-01 (Fix 30 — IR MOTU V3 Decoder)

### Fix 30 — IR MOTU V3 Decoder

**Diagnoza — dlaczego IR miało 215K błędów:**
IR packets zawierały MOTU V3 format `[SPH][msg][PCM×3B]` ale kod dekodował jako AM824 `[label][PCM×24bit]`.
Co 4-byte slot przechodził do next channel offset → kompletny misalignment.

**Fix:**
- Nowy plik: `MotuV3Decoder.hpp` — dekodowanie 3-byte packed PCM bez label bytes
- `StreamProcessor::ProcessPacket()`: check `FDF==0x00` → MOTU V3 mode
- `DecodeDataBlock()`: czyta `[SPH 4B][msg 6B][PCM 3B×N]`, zwraca PCM samples

Override DBS=21 już ustawiony w MOTUAudioBackend.cpp.
IT encoding (Fix 29) ✅ + IR decoding (Fix 30) = pełna duplex MOTU V3 ✓.

---

## Sesja 24 — 2026-06-02 (Fix 40+41 — MOTU V3 encoder + EXC_ARM_DA_ALIGN)

### Fix 40 — InjectNearHw używał AM824 dla MOTU V3

**Bug:** `PacketAssembler::encodeToWire()` zawsze wywoływała `EncodePcmFramesWithAm824Placeholders`,
niezależnie od `encoding_`. MOTU dostawało AM824 payload zamiast V3 → cisza.

**Fix:** `encodeToWire()` — dispatcher: `encodeInterleavedFramesToMotuV3` lub `encodeInterleavedFramesToAm824`.
Commit `5049c19`.

### Fix 41 — EXC_ARM_DA_ALIGN kernel panic

**Bug:** `std::memset(block, 0, 84)` gdzie `block = payloadVirt + kCIPHeaderSize (8 bytes)`.
Payload buffer jest page-aligned → `payloadVirt + 8` = tylko **8-byte aligned**.
`_platform_memset` w DriverKit używa ARM64 `stnp` (non-temporal store pair) = wymaga **16-byte** → `EXC_ARM_DA_ALIGN` → crash po ~10 razach → kernel panic.

**Fix:** zastąpiono `std::memset` pętlą `uint32_t` zero-fill (zawsze 4-byte aligned).
Dotyczy `encodeInterleavedFramesToMotuV3` i `fillSilentMotuV3Frames`.
Crash report: `/Library/Logs/DiagnosticReports/net.mrmidi.ASFW.ASFWDriver-2026-06-02-163054.ips`
Commit `5049c19`. Testy: 493/493 ✅.

---

## Stan po sesjach 9–24 — tabela kompletnych fixów

| Fix | Commit | Opis | Status |
|-----|--------|------|--------|
| Fix A | — | FETCH_PCM_FRAMES przed StartTransmit | ✅ |
| Fix B | — | Zamiana kanałów IT/IR w ISOC_COMM_CONTROL | ✅ |
| Fix C | — | AudioClockEngine anchor = (0, currentTime) | ✅ |
| Fix D | — | double-start guard w StartDevice | ✅ |
| Fix E | `935d3ff` | IR cycleMatchEnable bit 30 usunięty | ✅ |
| Fix F | `5554280` | Work queue deadlock — StartStreaming na background queue | ✅ |
| Fix G | — | Auto-aktywacja dextu przy starcie apki | ✅ |
| Fix H | — | Zombie dext przy upgrade — reboot wymagany | ✅ |
| Fix I | `662ca0d` | ISOC_COMM_CONTROL + FETCH_PCM_FRAMES przed StartTransmit | ✅ |
| Fix II | `2dc6600` | SYT wait przeniesiony po Start() w IsochService | ✅ |
| Fix III | `3241bd2` | Allow requestedChannels > queueChannels | ✅ |
| Fix 17 | `c13132b` | `rawPollCount_` pre-lock | ✅ |
| Fix 18 | `c13132b` | CIPHeader OHCI double-swap usunięty | ✅ |
| Fix 19 | `68823bf` | Deactivate+activate ISOC_COMM_CONTROL + SYT gate 3000ms | ✅ |
| Fix 20 | `597f3c8` | Override wire DBS=21 dla MOTU V3 (StreamProcessor) | ✅ |
| Fix 21 | (w `5049c19`) | IT DBS=21 override — `requestedAm824Slots=kMOTUV3WireDbs48k` | ✅ |
| Fix 22 | (w `5049c19`) | SYT gate bypass — `skipSYTGate=true` | ✅ |
| Fix 23 | (w `5049c19`) | TX Profile B — target=1024, max=1536 frames | ✅ |
| Fix 26 | — | OHCI cycle-time gate zamiast poll-count (100ms) | ✅ |
| Fix 27 | — | TX Ring Buffer Expansion (max 4096 frames) | ✅ |
| Fix 29 | — | MOTU V3 Packet Encoding (3-byte packed PCM) | ✅ |
| Fix 30 | — | IR MOTU V3 Decoder (MotuV3Decoder.hpp) | ✅ |
| Fix 33 | `50417e9` | TxQ rate-matched 6 frames/interrupt, PLL target=512 | ✅ |
| Fix 36b/c | `80729a5`,`42d7334` | Adaptive pump + guard kMaxRbFillFrames | ✅ |
| Fix 37 | — | Podwójny dext — `.cancel` dla tej samej wersji | ✅ |
| Fix 38c | `6fa3de4`,`3af2591` | Zero-copy output path — `kEnableZeroCopyOutputPath=true` | ✅ |
| Fix 39 | `3fad643` | SetZeroCopyOutputBuffer — `reconfigureAM824()` reset guard | ✅ |
| Fix 40 | `5049c19` | InjectNearHw encoder dispatcher (MOTU V3 vs AM824) | ✅ |
| Fix 41 | `5049c19` | EXC_ARM_DA_ALIGN — uint32_t zero-fill zamiast memset | ✅ |
