# MOTU 828 MK3 (V3) — kanoniczne fakty sprzętowe

> **To jest JEDYNE źródło prawdy dla faktów sprzętowych MOTU 828 MK3.**
> Jeśli jakikolwiek inny dokument mówi co innego — ten plik wygrywa, a tamten jest do poprawy.
> Każdy fakt tu ma podaną hierarchię źródeł. **Nie kopiuj tych liczb do innych docs** —
> linkuj tu (`documentation/MOTU_828_MK3_FACTS.md`).

## Hierarcha źródeł (od najbardziej do najmniej wiarygodnego)

1. **El Capitan wire snoop** — `diagnostics/elcap_groundtruth/README.md`. Realny drut
   generowany przez oficjalny kext MOTU na macOS. **Najwyższy autorytet** dla formatu na szynie.
2. **Linux wire/spec** — `docs/linux/motu/` + capture w `documentation/MOTU_V3_WIRE_GROUNDTRUTH.md`.
   Zgadza się z El Cap co do **liczby kanałów, DBS, rate**. ⚠️ Linux NIE jest autorytetem dla
   **mapowania bajtów/slotów PCM** w bloku (patrz niżej) — tam ufaj wyłącznie El Cap.
3. **Sequoia kext diagnostic** — `diagnostics/sequoia_20260525_003640/`. Surowe liczby OK,
   ale uważaj na interpretację kierunków „input/output" (łatwo odwrócić).
4. **Ręczne streszczenia** (stare sekcje w CLAUDE.md, Focus.md, DevLog.md) — **NIE są autorytetem**.
   Kilka z nich miało odwrócone kierunki. Zawsze weryfikuj względem 1–2.

## Geometria kanałów @ 48 kHz

| kierunek | nazwy | PCM | DBS | packet len |
|----------|-------|:---:|:---:|:---:|
| **host→device** | IT · playback · output · Linux RX · `outputChannelCount` · host's FW out | **14** | **13** | 424 |
| **device→host** | IR · capture · input · Linux TX · `inputChannelCount` · host's FW in | **18** | **16** | 520 |

**Derywacja z drutu (El Cap):** `(len − 8 CIP) / 8 bloków = bajty/blok = DBS×4`;
blok = `SPH(4) + MSG×2(6) + PCM×3B`. host→device: 52 B → (52−10)/3 = **14 PCM**.
device→host: 64 B → (64−10)/3 = **18 PCM**.

**Potwierdzenie krzyżowe (4 niezależne źródła zgodne):**
- El Cap wire: host→device len=424→14, device→host len=520→18
- Linux PipeWire sink (playback): `s32le 14ch` → host→device = 14
- Linux spec `docs/linux/motu/README.md`: `rx_fixed_pcm_chunks={14,14,10}` (Mac→device=14),
  `tx_fixed_pcm_chunks={18,18,14}` (device→Mac=18)
- Sequoia kext: `fNumFWOutputChannels 14` (host's FW out = host→device = 14),
  `fNumFWInputChannels 18` (host's FW in = device→host = 18)

> ⚠️ **Najczęstszy błąd:** odwrócenie kierunków. „IT = host→device = 14", nie 18.
> Linux TX/RX są z perspektywy **urządzenia** (odwrotnie do nas). Sequoia `fNumFW*` są z
> perspektywy **hosta** (input = to co host odbiera = capture = device→host).

## Inne rate'y

MOTU V3 wspiera **6 taktowań** (`SND_MOTU_CLOCK_RATE_COUNT=6`, `docs/linux/motu/motu.h`):
indeks→Hz = `{0:44100, 1:48000, 2:88200, 3:96000, 4:176400, 5:192000}` (brak 32 kHz).
PCM/kanał maleje przy wyższych rate'ach (np. Linux RX `{14,14,10}` dla 48/96/192).
**Obecny profil ASFW ogłasza tylko 48 kHz.** Pełny multi-rate = przyszłe zadanie.

## CLOCK_STATUS — rejestr zegara (offset `0x0b14`)

Ref: `docs/linux/motu/motu-protocol-v3.c`. Ten sam rejestr niesie rate i bity rozkazów:
- bity **[15:8]** = indeks rate (`V3_CLOCK_RATE_MASK=0x0000ff00`, shift 8) → mapowanie MOTU wyżej
- bit **`0x02000000`** (`V3_FETCH_PCM_FRAMES`) = **bit ROZKAZU zapisu** (start strumienia).
  **Nie odczytuje się** — czytanie go jako statusu nigdy nie zadziała. Linux czyści go przy
  ustawianiu rate. (To był root-cause timeoutu bramki zegara w dice, 2026-06-15.)
- bity źródła zegara (internal/word/SPDIF/ADAT) — `V3_CLOCK_SRC_*`

## Format na drucie (host→device, playback)

Pełne szczegóły: `documentation/MOTU_V3_WIRE_GROUNDTRUTH.md` + `diagnostics/elcap_groundtruth/README.md`.
- CIP: `DBS=13 FMT=0x02 FDF=0x22 SYT=0xFFFF`, DBC +8, blocking (8 frames/data pkt)
- Blok 52 B: `[0..3]` SPH · `[4..9]` MSG×2 (puste u nas) · `[10..]` PCM, slot N @ byte `10+N×3`, 3B/kanał
- Próbka: 24-bit signed big-endian, high-aligned
- **Main L/R = slot 10 + slot 11 (byte 40/43)** wg El Cap — to NIE slot 0/1
- SPH ≈ bieżący cykl magistrali (MOTU = cycle master); free-running z OHCI CycleTimer poprawny

## Sekwencja StartStreaming i mapa rejestrów

Patrz `MOTU_828_MK3_BringUp.md` (protokół V3, kolejność, czego NIE robić).

---
**Powiązane:** [diagnostics/elcap_groundtruth](../diagnostics/elcap_groundtruth/README.md) ·
[MOTU_V3_WIRE_GROUNDTRUTH](MOTU_V3_WIRE_GROUNDTRUTH.md) ·
[MOTU_828_MK3_BringUp](../MOTU_828_MK3_BringUp.md) ·
[docs/linux/motu/README](../docs/linux/motu/README.md)
