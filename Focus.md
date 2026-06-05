# Focus.md — Plan pracy nad ASFireWire

Cel końcowy: MOTU 828 MK3 działający na macOS Tahoe przez sterownik DriverKit.

Archiwum ukończonych etapów i sesji debugowania → `DevLog.md`

---

## ⚡ AKTUALNY STAN — Przeczytaj to na starcie

> **Stan na 2026-06-05 (sesja 30) — Fix 51+52 zaimplementowane, build v70 na pulpicie MacBooka**

### Środowisko testowe (ZMIANA od sesji 29)

| Maszyna | System | Rola |
|---------|--------|------|
| **MacBook Pro (M3 Max)** | **macOS Tahoe 26.5.1 (zewnętrzny SSD)** | **Aktywne środowisko — build + test hardware** |
| Mac Studio | macOS Tahoe (wewnętrzny) | Backup — nieużywany w tej sesji |
| MacBook Pro (M3 Max) | macOS Sequoia (wewnętrzny SSD) | Diagnostyka MOTU kext (DTrace/IORegistry) |

**Boot-args na MacBooku (Tahoe/zewnętrzny SSD):**
```
amfi_get_out_of_my_way=1 cs_enforcement_disable=1
```

**Podpisywanie na MacBooku Tahoe:**
- Cert: `Apple Development: j.slipiec@gmail.com (239NB3LFDQ)` (MacBook Pro, team `4MJNRC8SW5`)
- Po `./build.sh --no-bump --derived /tmp/ASFWBuild --deploy` wymagany ręczny re-sign:
```bash
CERT="Apple Development: j.slipiec@gmail.com (239NB3LFDQ)"
codesign --force --sign "$CERT" --entitlements "ASFWDriver/ASFWDriver.entitlements" --timestamp=none \
  "/Users/cube666/Desktop/ASFW_vNN.app/Contents/Library/SystemExtensions/net.mrmidi.ASFW.ASFWDriver.dext"
codesign --force --sign "$CERT" --entitlements "ASFW/App.entitlements" --timestamp=none \
  "/Users/cube666/Desktop/ASFW_vNN.app"
```
- Pośredni CA potrzebny po świeżej instalacji: `curl -s https://www.apple.com/certificateauthority/AppleWWDRCAG3.cer | security import /dev/stdin -k ~/Library/Keychains/login.keychain-db`

---

### Ostatnie fixy (sesja 30)

> **✅ Fix 49** (sesja 30, v65) — Wyłączenie zero-copy: `kEnableZeroCopyOutputPath = false`
> - MOTU V3 wymaga encodingu (packed 3-byte PCM), nie może zero-copy raw PCM.
>
> **✅ Fix 51** (sesja 30, v68) — Startup pump fallback + cap burst pump
> - Startup fallback: `avgFramesPerCycle * 8` = 48 frames/IRQ (było 6 → TxQ przepełniał się w 100ms)
> - Burst cap: `want_steady * 4` — burst pump nie drenuje TxQ w jednym IRQ
> - Efekt: underruns 110k→60k, Buffer Fill 160%→96%, TX Throughput ciągły
> - Plik: `ASFWDriver/Isoch/Transmit/IsochAudioTxPipeline.cpp`
>
> **✅ Fix 52** (sesja 30, v70) — Bit alignment: high-aligned int32 (GÓRNE 24 bity)
> - **Dowód:** IORegistry na Sequoia z MOTU kext: `IOAudioStreamAlignment=1` = `kIOAudioStreamAlignmentHighByte`
> - **Fix:** `FormatFlagIsAlignedHigh` w formacie ADK + `>> 8` w AM824Encoder + `(s>>24),(s>>16),(s>>8)` w PacketAssembler
> - Cofnięcie błędnego Fix 50 (który cofnął poprawny kierunek Fix 48, bo brakowało `FormatFlagIsAlignedHigh`)
> - Pliki: `ASFWAudioDriver.cpp`, `AM824Encoder.hpp`, `PacketAssembler.hpp`, `AM824EncoderTests.cpp`

### ⏳ TEST AUDIO — Fix 52 (v70) na MacBooku Tahoe

- **v70** jest na pulpicie MacBooka (`ASFW_v70.app`)
- Wymagany **restart** (dext upgrade z aktywnym AudioDriverKit)
- Po restarcie: otwórz v70 → System Settings → Sound → wybierz MOTU 828mk3 → Spotify
- Oczekiwany rezultat: **muzyka** przez MOTU bez pisku
- Jeśli nadal pisk: zbadać routing kanałów (IOAudioStreamStartingChannelID) i pcm_byte_offset

---

## Następne priorytety

### Priorytet 0 — Diagnoza pisku w prawym kanale
Jeśli v61 nie naprawia pisku, następny krok: **raw IR packet logging**:
```cpp
// StreamProcessor.hpp — dodać na początku OnPacketReceived():
if (packetCount_++ < 3) {
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(payload);
    ASFW_LOG(IR, "RAW[%u]: SPH=%02x%02x%02x%02x msg=%02x%02x%02x%02x%02x%02x pcm0=%02x%02x%02x pcm1=%02x%02x%02x",
             packetCount_, raw[0],raw[1],raw[2],raw[3],
             raw[4],raw[5],raw[6],raw[7],raw[8],raw[9],
             raw[10],raw[11],raw[12], raw[13],raw[14],raw[15]);
}
```
To powie czy MOTU używa SPH, jaki DBS naprawdę wysyła, i gdzie zaczyna się PCM.

Alternatywa: **DTrace na Sequoia** (`sudo dtrace -n 'fbt::*MOTU*::entry { tracemem(arg0,128); }'`)
lub **Ghidra** na binarce `/System/Library/Extensions/MOTUAudio.kext`.

### Priorytet 1 — IR drops (168 865 dropów)
Widoczne w screenshocie sesji 28. IR context traci ~75% pakietów. Sprawdzić:
- Czy ring buffer IR jest za mały?
- Czy `overrideWireDbs_` dla IR jest poprawny?

### Priorytet 2 — HALS_IORawClock
Zastąpić `mach_absolute_time()` w `PerformIO` czytaniem OHCI `CurrentIsochronousCycleTime` (rejestr `0x1E8`). Bits[25:12]=cycleCount(0–7999), bits[11:0]=cycleOffset.

### Priorytet 3 — Rozszerzyć do 18ch IT / 14ch IR
`ASFWAudioNub` publikuje teraz tylko "2 In / 2 Out". Zmiana: `outputChannelCount=18`, `inputChannelCount=14`.

### Priorytet 4 — Kontrolki głośności (F11/F12/Mute)
`IOUserAudioLevelControl` + `IOUserAudioToggleControl` w `ASFWAudioDriver`. Szczegóły w sekcji "Planowane funkcje".

---

## Diagnostyka na macOS Sequoia (MacBook w domu)

> Sequoia ma oryginalny sterownik MOTU (`MOTUAudio.kext`) — możemy podsłuchać co on wysyła
> lub rozkodować jego binarny kod. Nie wymaga Tahoe ani Mac Studio.

### Opcja A — DTrace: podejrzyj co wysyła MOTU kext (wymaga podłączonego MOTU)

```bash
# 1. Podłącz MOTU przez TB→FW adapter, uruchom ASFW lub poczekaj aż macOS załaduje MOTUAudio.kext
# 2. Sprawdź czy kext jest załadowany:
kextstat | grep -i motu

# 3. Podejrzyj wywołania funkcji kextu (wymaga SIP disabled lub tylko w dev mode):
sudo dtrace -n '
  fbt::*MOTU*::entry {
    printf("%s\n", probefunc);
  }'

# 4. Podejrzyj payload pierwszych pakietów IT (co MOTU kext WYSYŁA do urządzenia):
sudo dtrace -n '
  fbt::*IsochChannel*::entry,
  fbt::*WriteIsoch*::entry {
    printf("fn=%s arg0=0x%x\n", probefunc, arg0);
    tracemem(arg1, 128);
  }'
```

### Opcja B — Ghidra: rozkoduj MOTU kext bez podłączania sprzętu

```bash
# Znajdź binarny kext na Sequoia:
find /System/Library/Extensions /Library/Extensions -name "*.kext" 2>/dev/null | xargs -I{} find {} -name "MOTUAudio" -o -name "MOTUFireWire" 2>/dev/null

# Lub przez kextstat:
kextstat | grep -i motu
# → pokaże ścieżkę, np. /Library/Extensions/MOTUAudio.kext/Contents/MacOS/MOTUAudio

# Wyexportuj symbole (bez Ghidry):
nm -u /Library/Extensions/MOTUAudio.kext/Contents/MacOS/MOTUAudio | grep -iE "(isoch|packet|write|channel|sph|dbs)"
otool -tv /Library/Extensions/MOTUAudio.kext/Contents/MacOS/MOTUAudio > motu_disasm.txt
```

Następnie wrzuć binarny plik do **Ghidra** (https://ghidra-sre.org, darmowe):
- `File → Import File` → binarka MOTUAudio
- Szukaj funkcji: `buildITPacket`, `writePayload`, `setDBS`, `setSPH`
- Sprawdź jak buduje data block (byte offsets, PCM packing)

### Opcja C — IORegistry: sprawdź parametry aktywnego streamingu

```bash
# Podłącz MOTU, uruchom streaming (np. odtwórz audio przez MOTU),
# potem sprawdź co kext zarejestrował w IORegistry:
ioreg -l -r -c IOFireWireUnit | grep -A 20 -i motu
ioreg -l -r -c IOAudioDevice  | grep -A 40 -i motu

# Szukaj: sampleRate, channelCount, DBS, streamFormat, packetSize
```

### Opcja D — log stream: logi MOTU kext podczas streamingu

```bash
# Uruchom streaming przez MOTU na Sequoia, zbierz logi:
/usr/bin/log stream --debug --info 2>/dev/null | grep -iE "(motu|firewire|isoch|1394)" | head -100

# Po zatrzymaniu:
/usr/bin/log show --last 5m --debug --info 2>/dev/null | grep -iE "(motu|firewire)" > ~/Desktop/motu_sequoia.txt
```

### Co chcemy znaleźć

| Pytanie | Gdzie szukać |
|---------|-------------|
| Jaki DBS wysyła kext do MOTU 828 MK3? | Ghidra / DTrace payload |
| Jaki `pcm_byte_offset` (10 czy inny)? | Ghidra: `buildPayload` / `writePCM` |
| Jak buduje SPH (czy używa CycleTimer)? | Ghidra: `setSPH` / `buildDataBlock` |
| Ile kanałów PCM koduje do IT stream? | IORegistry: channelCount |
| Co słychać na prawym kanale przy streamingu? | Test audio na Sequoia z MOTU |

---

## Stan implementacji (maj 2026)

| Subsystem | Status | Uwagi |
|-----------|--------|-------|
| OHCI init & bus reset | ✅ Działa | Self-ID, topology, gap count |
| Async TX/RX | ✅ Działa | Block read/write, lock, PHY — częściowo |
| Config ROM reading | ✅ Działa | Pełny scanner z FSM multi-node |
| AV/C / FCP | ✅ Działa (kod) | Nie używane dla MOTU V3 |
| IRM | ✅ Działa | Alokacja kanału + bandwidth |
| Isoch Transmit (IT) | ✅ Działa | MOTU V3 DBS=21, SYT gate bypass, Zero-Copy |
| Isoch Receive (IR) | ✅ Odbiera | 8001 pkts/s od MOTU, DBS override=21 |
| AudioDriverKit | ✅ AudioDeviceStart | IO 3+ min; clock jitter (HALS_IORawClock) |
| **MOTU V3 Backend** | ⏳ Pisk prawy kanał | Fix 45+46+47 wdrożone (v61). Lewy gra ✓, prawy pisk. Diagnoza w toku. |

---

## Status etapów

| Etap | Status | Testy |
|------|--------|-------|
| 1–9 — Szczegóły w DevLog.md | ✅ Zrobione | 488/488 ✅ |
| 10 — MOTU V3 Protocol Backend | ✅ Zaimplementowany | 493/493 ✅ |

---

## Znane nierozwiązane problemy

| Problem | Priorytet | Opis |
|---------|-----------|------|
| ~~AT DMA block write (tCode=0x1)~~ | ✅ NAPRAWIONE | `ScanCompletion` orphan check, commit `eeb8787` |
| ~~Model ID 0x000000 w Discovery~~ | ✅ NAPRAWIONE | `EffectiveModelId()` commit `abc75ea` |
| ~~IR cycleMatchEnable (bit 30)~~ | ✅ NAPRAWIONE | `kRun\|kWake=0x9000`, commit `935d3ff` |
| ~~Work queue deadlock~~ | ✅ NAPRAWIONE | `StartStreaming` na background queue, commit `5554280` |
| ~~TxQ starvation / underruny IT~~ | ✅ NAPRAWIONE | Fix 33 — rate-matched 6 frames/interrupt |
| ~~IT pump oscillation~~ | ✅ NAPRAWIONE | Fix 36b/36c — adaptive pump (985 Hz IRQ coalescing) |
| ~~Podwójny dext po restarcie~~ | ✅ NAPRAWIONE | Fix 37 — `.cancel` dla tej samej wersji dextu |
| ~~Zero-copy output nieaktywne~~ | ✅ NAPRAWIONE | Fix 38c — `kEnableZeroCopyOutputPath=true` |
| ~~SetZeroCopyOutputBuffer przed Configure()~~ | ✅ NAPRAWIONE | Fix 39 (`3fad643`) — 33 759 underrunów → 0 |
| ~~InjectNearHw: AM824 encoder dla MOTU V3~~ | ✅ NAPRAWIONE | Fix 40 (`5049c19`) |
| ~~EXC_ARM_DA_ALIGN przy MOTU V3 encoding~~ | ✅ NAPRAWIONE | Fix 41 (`5049c19`) — uint32_t zero-fill |
| Pisk w prawym kanale MOTU | ⏳ **FIX 47 (v61) — do testu** | Fix 45: SPH bit. Fix 46: OHCI CycleTimer. Fix 47: poprawna formuła SPH (ct & 0x01FFFFFF). Przyczyna asymetryczna (prawy) nieustalona. |
| IR drops (168 865) | ⏳ Zbadać | IR context traci ~75% pakietów — ring buffer? overrideWireDbs_? |
| HALS_IORawClock re-anchoring | Średni | `mach_absolute_time()` zamiast OHCI cycle counter jako hostTime |
| Liczba kanałów 2/2 vs 18 IT / 14 IR | Niski | Po potwierdzeniu audio: `outputChannelCount=18`, `inputChannelCount=14` |
| Brak nazw kanałów w CoreAudio | Niski | `IOAudioChannelDescription` per-kanał (Analog 1, ADAT A-1 itd.) |
| Brak obsługi klawiszy głośności (F11/F12/Mute) | Średni | Brak `IOUserAudioLevelControl` / `IOUserAudioToggleControl` |
| FCP spam do MOTU | Niski | AVC discovery pisze do MOTU co ~2s; MOTU V3 nie używa AV/C |
| `bufferFillLevel` UI — mislabeled "%" | Niski | Zwraca surowe ramki, nie %. Fix: `fill * 100 / kAudioRingBufferFrames` |

---

## Planowane funkcje — kontrolki głośności (F11/F12/Mute)

**Problem:** Gdy MOTU 828 MK3 jest wyjściem systemowym, klawisze głośności nie działają — `ASFWAudioDriver` nie deklaruje żadnych kontrolek.

**Implementacja (3 kroki, ~100–150 linii w `ASFWAudioDriver`):**

```cpp
// 1. Zadeklarować w ASFWAudioDriver::Start():
auto* volumeCtrl = IOUserAudioLevelControl::Create(
    this, kIOUserAudioObjectPropertyScopeOutput,
    kIOUserAudioObjectPropertyElementMain, 0.0f, -96.0f, 0.0f);
auto* muteCtrl = IOUserAudioToggleControl::Create(
    this, kIOUserAudioControlSubTypeMute,
    kIOUserAudioObjectPropertyScopeOutput,
    kIOUserAudioObjectPropertyElementMain);
AddControl(volumeCtrl); AddControl(muteCtrl);

// 2. Obsłużyć callback (SetControlValue override):
//    zapisać gain_ / mute_ jako std::atomic<float> / std::atomic<bool>

// 3. Zastosować w PerformIO przed IT shared queue:
const float gain = mute_.load() ? 0.0f : dBToLinear(volumeDb_.load());
if (gain != 1.0f) {
    for (auto& sample : outputSamples) sample = static_cast<int32_t>(sample * gain);
}
```

**MOTU V3 nie ma hardware volume register** — gain stosujemy software-side. Przy gain=1.0f koszt zerowy.

---

## Instrukcja testowania na Mac Studio (Tahoe, Apple Silicon)

### Wymagania
- Mac Studio (Apple Silicon) z macOS Tahoe, SIP disabled, `amfi_get_out_of_my_way=1`
- Adapter Thunderbolt → FireWire 800 · MOTU 828 MK3

### Jednorazowe przygotowanie (Recovery Mode)
```bash
csrutil disable   # Recovery → Terminal
sudo nvram boot-args="amfi_get_out_of_my_way=1"
sudo systemextensionsctl developer on
# restart
```

### Logi dextu (właściwa metoda — Tahoe)
```bash
# Live stream:
/usr/bin/log stream --debug --info 2>/dev/null | grep "ASFWDriver.dext"

# Po zdarzeniu:
/usr/bin/log show --last 10m --debug --info 2>/dev/null | grep "ASFWDriver.dext"

# Filtrowanie audio/isoch:
/usr/bin/log stream --debug --info 2>/dev/null | grep "ASFWDriver.dext" | grep -E "(Isoch|IR|IT|syt|Streaming|Started|Underrun)"
```

> ⚠️ **Pułapka zsh:** `log` w zsh = wbudowana funkcja matematyczna. Zawsze używaj `/usr/bin/log`.

### Czego szukać przy starcie streamingu (sukces)
```
AudioCoordinator: Injecting MOTU V3 config ... in=14 out=18
MOTUAudioBackend: ISOC_COMM_CONTROL deactivate=0x808019xx
MOTUAudioBackend: ISOC_COMM_CONTROL activate=0xC1C019xx (irCh=0 itCh=1)
[Isoch] SYT gate bypassed (device uses syt=0x0000 — MOTU V3 mode)
[Isoch] ✅ Started IT Context for Channel 1!
MOTUAudioBackend: Streaming started GUID=0x0001F20000087236
```

### Jeśli coś nie działa
```bash
ioreg -l -r -c ASFWAudioNub   # brak wpisu = problem AudioCoordinator; wpis bez audio = problem ADK/HALC
```
Napisz na starcie sesji: **"Kontynuujemy ASFireWire — oto logi z Mac Studio:"** i wklej output z `log stream`.

### Odinstalowanie sterownika
```bash
systemextensionsctl uninstall net.mrmidi.ASFW net.mrmidi.ASFW.ASFWDriver
```
