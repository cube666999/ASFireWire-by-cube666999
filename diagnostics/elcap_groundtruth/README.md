# El Capitan → MOTU 828 MK3 — wire ground-truth (2026-06-14)

Pasywny snoop strumienia IT (host→device) generowanego przez **oficjalny sterownik MOTU
na macOS El Capitan** (MBP 2009), przechwycony na M3/Tahoe snoop dextem **v113**
(naprawiony framing + pomiar SPH-ahead). MOTU aktywnie grał muzykę.

Plik: `elcap_motu_snoop_v113_2026-06-14.txt` (kanał ch=33, ~9500 linii).

## Co ten capture rozstrzygnął (twarde fakty)

**Format na drucie — El Cap, potwierdzony:**
- CIP: `DBS=13 FMT=0x02 FDF=0x22 SYT=0xFFFF b2=0x04`, DBC +8
- **PCM wyłącznie w slot 10 + slot 11** (block byte 40/43), reszta slotów + MSG = ZERO
- Próbka: czyste 24-bit signed big-endian, high-aligned (np. `s10=0037a6 s11=ffcf77`)
- → **Nasz enkoder (Fix 74, slot 10/11) jest 1:1 z El Cap.** Pojedynczy pakiet DATA nieodróżnialny.

**SPH timing:**
- El Cap stempluje SPH ≈ bieżący cykl magistrali (`aheadHw` -7…+2, śr ~-3).
  `sphCyc` wiernie podąża za `hwCyc`. NIE stempluje daleko do przodu.
- Nasz SPH ≈ -2 → **to samo**. SPH-ahead NIE jest brakującą różnicą.
- Sugeruje, że MOTU jest cycle masterem → nasze free-running z CycleTimer jest poprawne.

**⚠️ Pole `aheadRx` w logu = ŚMIEĆ.** `xfer=0x8451` jest stałe → `xferStatus` deskryptora IR
to słowo statusu, NIE per-pakietowy timestamp odbioru. Ignoruj `aheadRx`/`rxCyc`; ufaj `aheadHw`.

## Parsowanie

Layout bufora (BEZ nagłówka OHCI): `[0..3]`=CIP Q0, `[4..7]`=CIP Q1, `[8..]`=blok danych
(SPH[4] + MSG×2[6] + slot[0..12] po 3B; slot N PCM @ block byte 10+N×3).

## Dual-capture v114 — El Cap (ch=33) + MOTU TX (ch=34) jednocześnie

Plik: `dual_elcap+motu_snoop_v114_2026-06-14.txt` (~8400 linii).
Snoop z dwoma kontekstami IR (ctx=1 host→device, ctx=2 device→host). Kanały odczytane
z MOTU ISOC_COMM_CONTROL: Rx@shift24, Tx@shift16.

**MOTU device→host (ch=34) — pierwszy raz scharakteryzowany:**
- len=520 (vs 424 host→device), CIP `0d040400 22ffffff` (inny format), własny SPH w Q2.

**Clock-sync — DEFINITYWNIE zamknięty:**
- Oba strumienie stempllują SPH ≈ bieżący cykl magistrali (`aheadHw` obu ±10, śr ~-3).
  Pary w tym samym momencie: ElCap sphCyc≈hwCyc, MOTU sphCyc≈hwCyc.
- → **MOTU = cycle master** (jego kwarc = zegar magistrali). El Cap i my stempllujemy SPH
  z tego wspólnego zegara. **Nasze free-running z OHCI CycleTimer (-2) jest POPRAWNE.**
- Hipoteza "host wylicza SPH z tego co MOTU wysyła" — OBALONA. Brak osobnej derywacji.

## Bilans — wykluczone na danych (wszystko po stronie referencji zgadza się z naszym enkoderem)

slot 10/11 ✓ · format próbki/CIP/DBC ✓ · SPH-ahead ✓ · clock sync ✓ · głodzenie (encoded 6/8 = normalny blocking) ✓.
**Pojedynczy pakiet od nas = nieodróżnialny od El Capa.** A jednak pisk + Main Out gaśnie.

## Otwarty wątek — jedyna niezbadana rzecz: NASZ strumień na drucie

Sprawdziliśmy referencję (El Cap) + logikę enkodera, ale NIGDY co faktycznie wychodzi z M3.
Różnica musi być na poziomie STRUMIENIA (NO-DATA len=8, ciągłość SPH/DBC przez ring-wrap, priming).
**Następny krok: Linux Snoop** — MB2009 `fw_isoch_snoop` przechwytuje nasz strumień M3,
diff bajt-po-bajt z `elcap_motu_snoop_v113_2026-06-14.txt`. (M3 nie może podsłuchać sam siebie.)
