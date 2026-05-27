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
