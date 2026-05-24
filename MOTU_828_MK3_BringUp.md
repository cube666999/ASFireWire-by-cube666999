# MOTU 828 MK3 — Bring-up audio (V3 Protocol)

> Zaktualizowano: 2026-05-24. Poprzednia wersja tego dokumentu (AV/C/FCP/CMP) jest
> **nieaktualna i błędna** — MOTU 828 MK3 nie implementuje FCP mimo deklarowania AV/C
> w Config ROM. Właściwe podejście: **V3 register protocol** (patrz niżej).

---

## Kluczowe odkrycie

MOTU 828 MK3 używa **własnego protokołu rejestrowego V3** opartego na async quadlet
read/write (tCode=0x0). Brak AV/C, FCP, CMP.

Źródła:
- Linux kernel `sound/firewire/motu/motu-protocol-v3.c`
- Linux kernel `sound/firewire/motu/motu-stream.c`
- Disassembly `/Library/Extensions/MOTUFireWireAudio.kext` (x86_64, Sequoia)

Implementacja: `ASFWDriver/Audio/Backends/MOTUAudioBackend.cpp`

---

## Mapa rejestrów V3 (baza: 0xfffff0000000)

| Offset | Rejestr | Opis |
|--------|---------|------|
| `0x0b00` | ISOC_COMM_CONTROL | Aktywacja kanałów RX/TX isoch |
| `0x0b10` | PACKET_FORMAT | Prędkość TX (S400) + exclude differed |
| `0x0b14` | CLOCK_STATUS | Rate code [10:8], FETCH_PCM_FRAMES bit27 |

### CLOCK_STATUS (0x0b14) — bit layout

| Bity | Stała | Wartość | Znaczenie |
|------|-------|---------|-----------|
| [10:8] | `kClockRateMask = 0x00000700` | 0x02 = 48kHz | Aktualny sample rate |
| [27] | `kFetchPCMFrames = 0x02000000` | 1 = aktywny | Streaming aktywny |

> ⚠️ `kClockRateMask = 0x00000700` (NIE `0x0000ff00`) — potwierdzone przez kext: `andl $0x700`

### PACKET_FORMAT (0x0b10) — bit layout

| Bit | Stała | Wartość |
|-----|-------|---------|
| 7 | `kTxExcludeDiffered = 0x00000080` | zawsze 1 |
| 6 | `kRxExcludeDiffered = 0x00000040` | zawsze 1 |
| [1:0] | `kSpeedS400 = 0x02` | S400 |

Wartość do zapisu: `0xC2` = `0x80 | 0x40 | 0x02` ✅ potwierdzono w kexcie

### ISOC_COMM_CONTROL (0x0b00) — bit layout

| Bity | Znaczenie |
|------|-----------|
| 31 | Change RX isoch state |
| 30 | RX isoch activated |
| [29:24] | RX channel number |
| 23 | Change TX isoch state |
| 22 | TX isoch activated |
| [21:16] | TX channel number |

---

## Sekwencja StartStreaming (MOTUAudioBackend)

Odpowiednik Linux `begin_session` + `switch_fetching_mode`:

```
1. ReadRegister(0x0b14)               → odczyt CLOCK_STATUS (log rate code)
2. IRM AllocateResources(irCh, itCh)  → rezerwacja kanałów + bandwidth
3. WriteRegister(0x0b10, 0xC2)        → PACKET_FORMAT: S400 + exclude differed
4. isoch_.StartReceive(irCh)          → start IR OHCI DMA
5. isoch_.StartTransmit(itCh)         → start IT OHCI DMA
6. ReadModifyWrite(0x0b00)            → ISOC_COMM_CONTROL: aktywuj oba kanały
7. ReadModifyWrite(0x0b14)            → CLOCK_STATUS: ustaw FETCH_PCM_FRAMES
```

**Wszystkie operacje = quadlet write (tCode=0x0)** — inny code path niż FCP block write
(tCode=0x1). `WriteQuad(length=4)` → `WriteCommand` automatycznie wybiera tCode=0x0.

---

## Identyfikacja urządzenia w Config ROM

MOTU NIE wstawia modelu do root directory. Prawidłowe pole:

| Pole ROM | Klucz | Wartość dla 828 MK3 |
|----------|-------|---------------------|
| Root dir `Model` | 0x17 | `0x106800` (ignoruj!) |
| Unit dir `Unit_SW_Vers` | 0x13 | `0x000015` ← to jest model ID |
| GUID | — | `0x1F20000087236` |

Fix: `DeviceProtocolFactory::EffectiveModelId()` dla vendor `0x0001F2` zwraca
`unitSwVersion` zamiast `rootModelId`. Commit `abc75ea`.

### Obsługiwane modele MOTU (vendor 0x0001F2)

| Model | Unit_SW_Vers | Backend |
|-------|-------------|---------|
| 828 MK3 FW | 0x000015 | `kMOTUV3` |
| 828 MK3 Hybrid | 0x000035 | `kMOTUV3` |
| 896 MK3 | 0x000016 | `kMOTUV3` |
| Traveler MK3 | 0x000017 | `kMOTUV3` |
| UltraLite MK3 | 0x000019 | `kMOTUV3` |

---

## Weryfikacja vs kext MOTU (Sequoia, x86_64)

Wszystkie stałe potwierdzone przez disassembly `MOTUFireWireAudio.kext`:

| Stała | Wartość kext | Nasza wartość | Status |
|-------|-------------|---------------|--------|
| CLOCK_STATUS addr | `0xf0000b14` (LE w `__DATA __const`) | `kClockStatusOff = 0x0b14` | ✅ |
| FETCH_PCM_FRAMES | `0x02000000` (data table word[1]) | `kFetchPCMFrames = 0x02000000` | ✅ |
| Rate mask | `andl $0x700` | `kClockRateMask = 0x00000700` | ✅ |
| PACKET_FORMAT addr | `0xf0000b10` | `kPacketFmtOff = 0x0b10` | ✅ |
| PACKET_FORMAT value | `0xC2` | `0xC2` | ✅ |
| ISOC_COMM_CONTROL addr | `0xf0000b00` | `kIsocCtrlOff = 0x0b00` | ✅ |

---

## Isoch Receive — uwagi dla MOTU V3

`IsochReceiveContext` → `StreamProcessor` → `AM824Decoder` istnieje w kodzie, ale **nigdy
nie był przetestowany na żywym sprzęcie**. Dodatkowe ryzyko specyficzne dla MOTU:

MOTU V3 może nie używać standardowych nagłówków CIP (IEC 61883-1). `StreamProcessor`
i `AM824Decoder` są napisane dla zgodnych strumieni IEC 61883-6. Jeśli MOTU pomija
nagłówki CIP lub używa własnego formatu, oba komponenty będą wymagały modyfikacji.

**Co sprawdzić podczas hardware testu:**
- Czy IR DMA ring odbiera jakiekolwiek pakiety (logi `IsochReceiveContext`)
- Czy `StreamProcessor` nie odrzuca pakietów z błędem DBC/CIP
- Czy zdekodowane próbki PCM mają właściwą wartość (nie szum)

Jeśli IR nie działa od razu → zbadaj format pakietu przez Linux
`sound/firewire/motu/motu-protocol-v3.c` (funkcja `motu_stream_get_pcm_channels`).

---

## Czego NIE robić z MOTU

- ❌ NIE wysyłaj FCP commands (MOTU ignoruje FCP mimo AV/C w Config ROM)
- ❌ NIE używaj CMP (`ConnectOPCR`/`ConnectIPCR`) — zbędne dla V3
- ❌ NIE próbuj AV/C UNIT INFO / SUBUNIT INFO — timeout
- ❌ NIE używaj `AVCAudioBackend` dla MOTU — to jest backend dla innych urządzeń
