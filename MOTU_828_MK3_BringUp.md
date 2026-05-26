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
| [10:8] | `kClockRateMask = 0x00000700` | 0x01 = 48kHz | Aktualny sample rate |
| [25] | `kFetchPCMFrames = 0x02000000` | 1 = aktywny | Streaming aktywny |

> ⚠️ `kClockRateMask = 0x00000700` (NIE `0x0000ff00`) — potwierdzone przez kext: `andl $0x700`
>
> ⚠️ Rate code tabela (Linux `clock_rates[]`, indeksy 0–5): 44100=0x00, **48000=0x01**, 88200=0x02, 96000=0x03, 176400=0x04, 192000=0x05.
> Potwierdzone: `CLOCK_STATUS=0x08000100 rateCode=0x01` przy MOTU ustawionym na 48kHz (hardware test 2026-05-25).
>
> ⚠️ `kFetchPCMFrames = 0x02000000` = bit 25 (NIE bit 27). Bit 27 (`0x08000000`) to inny status MOTU (clock locked / device ready) — widoczny w CLOCK_STATUS odczycie idle.

### PACKET_FORMAT (0x0b10) — bit layout

| Bit | Stała | Wartość |
|-----|-------|---------|
| 7 | `kTxExcludeDiffered = 0x00000080` | zawsze 1 |
| 6 | `kRxExcludeDiffered = 0x00000040` | zawsze 1 |
| [1:0] | `kSpeedS400 = 0x02` | S400 |

Wartość do zapisu: `0xC2` = `0x80 | 0x40 | 0x02` ✅ potwierdzono w kexcie

### ISOC_COMM_CONTROL (0x0b00) — bit layout

⚠️ **Nazewnictwo z perspektywy MOTU (device-centric), NIE hosta:**

| Bity | Znaczenie (MOTU-centric) | Odpowiada (host-centric) |
|------|--------------------------|--------------------------|
| 31 | Change "RX" isoch state | zmiana kanału IT (host→device) |
| 30 | "RX" isoch activated | IT aktywowany |
| [29:24] | "RX" channel number | numer kanału IT (host→device) |
| 23 | Change "TX" isoch state | zmiana kanału IR (device→host) |
| 22 | "TX" isoch activated | IR aktywowany |
| [21:16] | "TX" channel number | numer kanału IR (device→host) |

**Mapowanie kanałów w kodzie:**
- bity [29:24] (`kRxChannelShift=24`) ← `itChannel` (MOTU odbiera = host→device)
- bity [21:16] (`kTxChannelShift=16`) ← `irChannel` (MOTU nadaje = device→host)

Przykład irCh=0, itCh=1: `0xC1C00000` (MOTU nadaje IR na ch=0, odbiera IT z ch=1)

---

## Sekwencja StartStreaming (MOTUAudioBackend)

Odpowiednik Linux `begin_session` + `switch_fetching_mode` (obie wywołane **przed** startem DMA):

```
1. ReadRegister(0x0b14)               → odczyt CLOCK_STATUS (log rate code)
2. IRM AllocateResources(irCh, itCh)  → rezerwacja kanałów + bandwidth
3. WriteRegister(0x0b10, 0xC2)        → PACKET_FORMAT: S400 + exclude differed
4. isoch_.StartReceive(irCh)          → start IR OHCI DMA
5. WriteRegister(0x0b00, ctrl)        → ISOC_COMM_CONTROL: aktywuj oba kanały (Linux: begin_session)
6. ReadModifyWrite(0x0b14)            → CLOCK_STATUS: ustaw FETCH_PCM_FRAMES  ← PRZED IT! (Linux: switch_fetching_mode)
7. isoch_.StartTransmit(itCh)         → start IT OHCI DMA  (SYT gate przechodzi — MOTU już nadaje IR)
```

> ⚠️ **KRYTYCZNE — kolejność kroków 4→5→6→7:**
>
> `IsochService::StartTransmit` czeka 500ms na IR SYT clock. MOTU V3 NIE wyśle żadnych IR
> pakietów dopóki **obie** operacje nie zostaną wykonane:
> - Krok 5: ISOC_COMM_CONTROL — mówi MOTU które kanały isoch używać
> - Krok 6: CLOCK_STATUS FETCH_PCM_FRAMES — **to dopiero wyzwala nadawanie IR przez MOTU**
>
> Pisz **oba** rejestry po `StartReceive` ale **przed** `StartTransmit`.
>
> Poprzedni błąd (FETCH_PCM_FRAMES po StartTransmit): klasyczny deadlock —
> StartTransmit czekał na SYT, MOTU czekało na FETCH_PCM_FRAMES → timeout 500ms.
> **Potwierdzone przez hardware test 2026-05-25 (sesja 5). Fix w `MOTUAudioBackend.cpp`.**

### ISOC_COMM_CONTROL — odczyt zwraca niezerową wartość

MOTU zwraca `0x3000` z odczytu 0x0b00 w stanie idle (bity dolne [15:0]).
Kod wykonuje read-modify-write: `ctrl &= ~kIsocMask; ctrl |= nasze_bity`.
Bity `0x3000` z odczytu są zachowywane — MOTU je ustawił, niech pozostaną.

Poprawna wartość zapisu (irCh=0, itCh=1): `0xC1C00000`
- bity[31:24]=0xC1 → Change=1, Activated=1, RX-channel=1 (IT=1, host→device)
- bity[23:16]=0xC0 → Change=1, Activated=1, TX-channel=0 (IR=0, device→host)

**Błąd wykryty 2026-05-25 sesja 5:** poprzedni kod miał kanały zamienione miejscami
(`irCh` w polu RX, `itCh` w polu TX), co powodowało że MOTU nadawało IR na kanale 1,
a nasze IR DMA słuchało kanału 0 → zero pakietów odebranych.

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

## ✅ Logi dext — dostępne przez kernel log (Tahoe, 2026-05-25 update)

`log stream` z predykatami `process ==` / `processID ==` nie działa. Ale logi dextu są widoczne
jako **komunikaty kernela** przypisane do dext bundle. Właściwy sposób:

```bash
# Logi z ostatnich N minut (po zdarzeniu):
log show --last 5m --debug --info 2>/dev/null | grep "ASFWDriver.dext"

# Live stream (uruchom przed testem):
log stream --debug --info 2>/dev/null | grep "ASFWDriver.dext"
```

Format wyjścia: `kernel: (net.mrmidi.ASFW.ASFWDriver.dext) [Kategoria] Treść`

**Potwierdzone na Tahoe (2026-05-25):** wszystkie ASFW_LOG wiadomości są widoczne tą metodą.

**DMA Slab IOVA na Tahoe/Apple Silicon:** IOVA Base = `0x80000000` (non-zero, valid). Poprzedni
debug snapshot pokazywał `0x0` — był to artefakt ze starej instancji dextu.

**Diagnostykę należy prowadzić przez:**
1. `log show --last Xm 2>/dev/null | grep "ASFWDriver.dext"` ← **GŁÓWNA METODA**
2. `log show --last Xm 2>/dev/null | grep -E "(ASFWDriver\.dext|HALC)"` — razem z coreaudiod
3. Async Commands UI (write/read rejestrów)
4. Isoch Metrics (licznik pakietów)
5. IORegistry (`ioreg -l -r -c ASFWAudioNub`)

**⚠️ Ważne:** Ręczny Start IR przez Isoch Metrics UI **omija całą sekwencję `MOTUAudioBackend`**.
IR uruchomiony ręcznie nie wysyła ISOC_COMM_CONTROL ani CLOCK_STATUS do MOTU —
dlatego Total Packets = 0. Jedyna poprawna ścieżka: CoreAudio → `StartDevice` → `StartAudioStreaming`
→ `AudioCoordinator::StartStreaming` → `MOTUAudioBackend::StartStreaming`.

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

## ⚠️ WAŻNE — Diagnostyka przez odczyt rejestrów jest zawodna (2026-05-25)

**PACKET_FORMAT (0x0b10) jest write-only.** Zapis: rCode=Complete ✅. Odczyt: zawsze `0x00000000` — niezależnie od zapisanej wartości. Potwierdzone na hardware (Mac Studio, Tahoe, MOTU 828 MK3 FW).

**Konsekwencja:** nie możemy wnioskować o stanie StartStreaming na podstawie odczytu 0x00000000 z ISOC_COMM_CONTROL lub CLOCK_STATUS. Te rejestry mogą mieć podobną właściwość (zapis momentary / trigger, odczyt zawsze 0 w stanie idle).

**Prawidłowa diagnostyka stanu streamingu:**
- Obserwuj Isoch Metrics → Total Packets (jeśli > 0 — IR DMA odbiera dane od MOTU)
- Sprawdź stan OHCI IR context przez `ReceiveContext()->GetState()` (nie dostępne z UI)
- Jeśli przycisk Start w Isoch Metrics → natychmiast Stop: `isoch_.StartReceive` było już wywołane

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

## 🔧 Naprawione bugi AudioDriverKit (2026-05-25, sesja 3)

### Bug 1 — Race condition: timer po `RegisterService()` [NAPRAWIONY]

**Plik:** `ASFWDriver/Isoch/Audio/ASFWAudioDriver.cpp` — `IMPL(ASFWAudioDriver, Start)`

**Problem:** `IOTimerDispatchSource` był tworzony **po** `RegisterService()`. Przy restarcie
dextu gdy Spotify grało, CoreAudio natychmiast wywoływało `StartDevice` — timer był null,
`StartDevice` zwracało `kIOReturnNotReady`. CoreAudio nie ponawiało i urządzenie zostawało
"Idle" na zawsze.

**Fix:** Timer i OSAction tworzone **przed** `RegisterService()`. Nowy log potwierdzający:
`[Audio] ASFWAudioDriver: Timestamp timer ready (before RegisterService)`

### Bug 2 — Brak SUPERDISPATCH w StartDevice/StopDevice [NAPRAWIONY]

**Plik:** `ASFWDriver/Isoch/Audio/ASFWAudioDriver.cpp`

**Problem:** `StartDevice` i `StopDevice` były plain C++ overrides bez wywołania
`StartDevice(in_object_id, in_flags, SUPERDISPATCH)`. Porównaj z `Start()`/`Stop()`
które poprawnie wywołują SUPERDISPATCH. Bez tego AudioDriverKit framework nie notyfikował
HAL daemon (`audiodriverkit_server`) o starcie IO — zero-timestamps mogły być ignorowane.

**Fix:** Zmieniono z plain C++ na `IMPL(ASFWAudioDriver, StartDevice/StopDevice)`
i dodano wywołanie SUPERDISPATCH jako pierwszy krok.

### Diagnoza jeśli problem nadal istnieje

Jeśli po tych fixach `StartDevice` wciąż nie pojawia się w logach:
```bash
# Sprawdź coreaudiod — błąd HALC to osobny problem
log show --last 5m --debug --info 2>/dev/null | grep -E "(coreaudiod|HALC|ShellObject)"
```

Jeśli `super::StartDevice failed kr=0x...` — ADK framework odmawia startu IO.
Możliwa przyczyna: nieprawidłowe stream formats lub device UID conflict.

---

## 🛠️ Troubleshooting: dext utknął w `[terminating for upgrade via delegate]`

**Symptom:** Po rebuildzie `systemextensionsctl list` pokazuje dwa wpisy — stary
`[terminating for upgrade via delegate]` i nowy `[activated enabled]`, ale nowy
proces dextu nie startuje (puste logi). Driver Status w ASFW app = szary.

**Przyczyna:** Stary proces dextu (może być zombie — od nieudanego poprzedniego
deployu) blokuje starty. sysextd czeka na delegate callback (ASFW app) który nigdy
nie przychodzi.

**Fix (szybki — 30 sekund):**
```bash
# 1. Zamknij ASFW app + zabij stary proces dextu
killall ASFW 2>/dev/null; sudo kill -9 <PID_starego_dextu> 2>/dev/null

# PID znajdź przez: ps aux | grep "net.mrmidi" | grep -v grep
# Przykład: sudo kill -9 82946

# 2. Poczekaj 2s i otwórz świeżo ASFW z DerivedData
sleep 2
open "/Users/kuba/Library/Developer/Xcode/DerivedData/ASFW-cnsxxyhtbpewiqgxvpywrevoashk/Build/Products/Debug/ASFW.app"

# 3. Weryfikacja — nowy PID z nowym UUID powinien być widoczny
ps aux | grep "net.mrmidi" | grep -v grep
systemextensionsctl list   # powinno być tylko 1 wpis: [activated enabled]
```

**Uwaga:** Jeśli `systemextensionsctl list` pokazuje `[terminating for upgrade via delegate]`
po rebuildzie i pojawia się szary status — ZAWSZE najpierw sprawdź `ps aux | grep net.mrmidi`
i pozbądź się starego procesu. Nie czekaj, nie rebuilduj — to nie pomoże.

---

## Czego NIE robić z MOTU

- ❌ NIE wysyłaj FCP commands (MOTU ignoruje FCP mimo AV/C w Config ROM)
- ❌ NIE używaj CMP (`ConnectOPCR`/`ConnectIPCR`) — zbędne dla V3
- ❌ NIE próbuj AV/C UNIT INFO / SUBUNIT INFO — timeout
- ❌ NIE używaj `AVCAudioBackend` dla MOTU — to jest backend dla innych urządzeń
- ❌ NIE używaj ręcznego Start IR w Isoch Metrics jako testu — omija całą sekwencję backendu
