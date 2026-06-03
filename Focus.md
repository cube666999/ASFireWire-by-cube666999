# Focus.md — Plan pracy nad ASFireWire

Cel końcowy: MOTU 828 MK3 działający na macOS Tahoe przez sterownik DriverKit.

Archiwum ukończonych etapów i sesji debugowania → `DevLog.md`

---

## ⚡ SESJA NA MAC STUDIO — Przeczytaj to na starcie

> **Stan na 2026-06-03 (sesja 27) — Fix 45+46 zaimplementowane, build v60 na pulpicie:**
>
> **✅ Fix 45** (sesja 25/26, commit `fb5425f`) — CIP header: SPH bit + FMT/FDF dla MOTU V3:
> - `kCIPFormatMotuV3=0x02`, `kFDFMotuV3=0x22`, `sphBit=(1u<<10)` w `CIPHeaderBuilder::build()`.
> - Bez SPH=1 w Q0: MOTU interpretuje SPH bytes jako PCM ch0 (zero) → cisza.
>
> **✅ Fix 46** (sesja 27, commit `e517882`, v60) — OHCI CycleTimer → MOTU V3 SPH timestamps:
> - **Root cause pisku:** SPH bytes 0-3 w każdym data blocku = 0x00000000 (timestamp "cycle 0 = way in the past") → timing artifacts.
> - **Fix:** `DoRefillOnce()` czyta `hardware_->ReadCycleTime()` (rejestr 0x0F0) przed `OnRefillTickPreHW()`.
> - Konwersja per Linux `amdtp-motu.c`: `sph = ((ct & 0x0e000000) >> 13) | ((ct & 0x01fff000) >> 12)`.
> - Zapis big-endian do bytes 0-3 każdego data blocku w `encodeInterleavedFramesToMotuV3`.
> - Pliki: `PacketAssembler.hpp`, `IsochAudioTxPipeline.hpp/.cpp`, `IsochTransmitContext.cpp`.
>
> **⏳ TEST AUDIO — Fix 45+46 (v60) na Mac Studio:**
> - Restart Mac Studio (wymagany dla dext upgrade z aktywnym AudioDriverKit)
> - Uruchom ASFW.app v60 z pulpitu → Apple Music → słuchaj na PHONES/MAIN
> - Szukaj w logach: `[Isoch] SYT gate bypassed` + brak `❌ StartTransmit SYT timeout`
> - **Oczekiwany wynik:** Czyste audio z MOTU (bez pisku) 🎵

---

## Następne priorytety (po potwierdzeniu audio)

1. **HALS_IORawClock** — zastąpić `mach_absolute_time()` w `PerformIO` czytaniem OHCI `CurrentIsochronousCycleTime` (rejestr `0x1E8`). Bits[25:12]=cycleCount(0–7999), bits[11:0]=cycleOffset.
2. **Rozszerzyć do 18ch IT / 14ch IR** — `ASFWAudioNub` publikuje teraz tylko "2 In / 2 Out". Zmiana: `outputChannelCount=18`, `inputChannelCount=14`.
3. **Kontrolki głośności (F11/F12/Mute)** — `IOUserAudioLevelControl` + `IOUserAudioToggleControl` w `ASFWAudioDriver`. Szczegóły w sekcji "Planowane funkcje".

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
| **MOTU V3 Backend** | ⏳ Audio pending | Fix 45 (SPH bit) → rebuild → test audio |

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
| Brak/zniekształcone audio na wyjściu MOTU | ⏳ **FIX 45+46 (v60) — do testu** | Fix 45: SPH=1, FMT=0x02, FDF=0x22. Fix 46: real CycleTimer SPH timestamps → czyste audio |
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
