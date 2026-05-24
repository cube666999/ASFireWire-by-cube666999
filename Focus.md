# Focus.md — Plan pracy nad ASFireWire

Cel końcowy: MOTU 828 MK3 działający na macOS Tahoe przez sterownik DriverKit.

Archiwum ukończonych etapów i sesji debugowania → `DevLog.md`

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
5. isoch_.StartTransmit(itCh)   → start IT OHCI DMA
6. ReadModifyWrite(0x0b00)      → ISOC_COMM_CONTROL: aktywuj oba kanały
7. ReadModifyWrite(0x0b14)      → CLOCK_STATUS: ustaw FETCH_PCM_FRAMES
```

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

## Znane nierozwiązane problemy

| Problem | Priorytet | Opis |
|---------|-----------|------|
| AT DMA block write (tCode=0x1) | Średni | OHCI nie dostaje ack — descriptor chain bug w `ATContextBase.hpp` offset ~870. Nie blokuje MOTU V3 (używa quadlet write), ale dotyczy innych urządzeń AV/C. |
| ~~Model ID 0x000000 w Discovery~~ | ✅ NAPRAWIONE | Root dir model=0x106800, unit SW vers=0x000015. Fix: `EffectiveModelId()` commit `abc75ea` |
| IR Receive walidacja na hardware | Wysoki | Kod istnieje, nieprzetestowany na żywym sprzęcie |

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
