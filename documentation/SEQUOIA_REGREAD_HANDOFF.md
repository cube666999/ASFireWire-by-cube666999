# HANDOFF dla sesji Claude na **Sequoia** — odczyt rejestrów init MOTU (jutro)

> Świeży Claude na Sequoia: Twoje zadanie to **odczytać kilka rejestrów MOTU**, gdy oficjalny
> sterownik gra. Pełny kontekst niżej, ale samo zadanie jest krótkie. Wynik zapisz do repo (iCloud,
> widoczne z Tahoe) — sesja dev na Tahoe go skonsumuje.

---

## Cel w jednym zdaniu

Zdobyć **wartości**, które oficjalny sterownik MOTU pisze do rejestrów init, których **nasz sterownik
NIE pisze** — żeby zreplikować pełną inicjalizację i (hipoteza) naprawić **misframing** (MOTU dostaje
nasz czysty strumień, ale rozmazuje go po wyjściach + pisk).

## Dlaczego (kontekst — opcjonalny)

Raport diagnostyczny ASFW na Tahoe pokazał, że nasz init MOTU to **podzbiór** oficjalnego. Porównanie
trace transakcji (nasz) vs ślad oficjalny (`raw-captures/2026-06-08_004331_official_driver_dcl_and_regwrites.txt`):

| rejestr | oficjalny pisze | my piszemy |
|---|:---:|:---:|
| 0x0b00, 0x0b04, 0x0b10, 0x0b14 | ✅ | ✅ |
| **0x0b08** | ✅ | ❌ |
| **0x0b1c** (z buforem danych) | ✅ | ❌ |
| **0x0b38** | ✅ | ❌ |

Ślad oficjalny pokazuje **adresy**, nie dane (`buf=` wskaźnik). Dump steady-state
(`raw-captures/2026-06-08_020402_motu_register_dump.txt`) ma 0x0b08=`0x00000000`, ale **0x0b1c i 0x0b38
POMINĄŁ**. Brakuje nam więc realnych wartości tych dwóch. To prime suspect misframingu.

## Zadanie

### Krok A — narzędzie
Na pulpicie Sequoia jest **`read_motu_regs.command`** (tym zrobiono dump 020402). Najpierw **przeczytaj
ten plik**, zrozum jak czyta rejestry MOTU (jaki mechanizm FireWire/IOKit, jaką listę offsetów). Potem
**rozszerz listę offsetów** o pełny zakres 0x0b00–0x0b3c co 4 bajty:

```
0x0b00 0x0b04 0x0b08 0x0b0c 0x0b10 0x0b14 0x0b18 0x0b1c
0x0b20 0x0b24 0x0b28 0x0b2c 0x0b30 0x0b34 0x0b38 0x0b3c
```

(Krytyczne: **0x0b1c** i **0x0b38**. Reszta zakresu — dla pewności, że nic nie pomijamy.)

> Jeśli `read_motu_regs.command` już czyta zakres / przyjmuje offsety argumentem — po prostu go użyj.
> Jeśli czyta sztywną listę — dodaj brakujące offsety. NIE wymyślaj nowego mechanizmu FireWire jeśli
> ten działa; bazuj na nim.

### Krok B — DWA odczyty: IDLE vs STREAMING (to najważniejsza część)

Oficjalny sterownik pisze te rejestry **przy starcie streamu** (w śladzie zapisy 0x0b1c/0b38 padają
koło `createDCLProgram`). Dlatego skonfigurowane wartości pojawiają się **dopiero gdy gra**. Zrób:

1. **IDLE** — MOTU podłączone, **NIC nie gra**. Odczytaj cały zakres.
2. **STREAMING** — **muzyka/ton REALNIE leci na Main Out 1/2** (sprawdź, że gra na Main, nie na Phones
   ani wyciszone!). Odczytaj cały zakres.

**Różnica IDLE → STREAMING = dokładnie to, co start skonfigurował, a czego nam brakuje. To jest złoto.**

**Czy moment startu jest istotny?** Dla samego ODCZYTU — **nie**. Czytaj **kilka sekund po starcie**, w
ustabilizowanym graniu; narzędzie czyta bieżący stan rejestru (nie łapie transientów), a skonfigurowane
wartości się utrzymują. Nie musisz trafić w moment startu.

**Środowisko (musi być jak przy dumpie 020402, żeby było porównywalne):** Sample Rate **48000**,
Clock Source **Internal**, **Default Stereo Output = Main Out 1-2**. Treść nieważna (ton lub muzyka).

⚠️ **Caveat 0x0b1c:** w śladzie miało **bufor danych** → może być *komendą* (zapis i natychmiastowy
powrót). Jeśli tak, read-back steady-state może go NIE złapać (diff pokaże brak zmiany). To OK na pierwszy
pass — zanotuj wtedy w wyniku „0x0b1c bez zmiany idle→stream (możliwy transient)". Snoop *zapisów* to
trudniejszy następny krok, nie rób go teraz.

### Krok C — zapis wyniku
Dopisz do nowego pliku `documentation/SEQUOIA_REGREAD_RESULT.md` w repo:
- tabela: offset | wartość IDLE | wartość STREAMING (swapped/host-order, jak w dumpie 020402),
- wyraźnie zaznacz **0x0b1c** i **0x0b38**,
- środowisko (firmware MOTU, Sample Rate, Clock Source, Default Stereo Output — jak w dumpie),
- (jeśli się da) podejrzyj też wartość pisaną do 0x0b1c „na żywo" — w trace miała bufor danych, więc
  read-back może różnić się od write; jeśli `read_motu_regs.command` umie tylko read, to OK, read-back
  wystarczy na start.

## Pułapki
- Oficjalny sterownik **owns** urządzenie; współbieżny async read może kolidować. Dump 020402 jednak
  się udał przy żywym sterowniku, więc mechanizm działa — użyj tego samego.
- SSH/Linux **nie pomoże** — to async read point-to-point do MOTU, nie isoch broadcast; snoop tego nie
  złapie. Musi być z hosta, który może adresować MOTU (Sequoia z istniejącym narzędziem).
- Wartości w dumpie 020402 są „swapped" (host-order) w drugiej kolumnie — trzymaj tę samą konwencję.

## Co zrobi Tahoe z wynikiem
Doda brakujące zapisy do `MOTUVendorProtocol::PrepareDuplex` (mirror oficjalnej sekwencji) z realnymi
wartościami — bez zgadywania. Jeśli to ucisza misframe → przełom. Jeśli nie → następny krok to SPH-echo
(plan w pamięci `project_motu_linux_sph_echo_fallback`).

---
### Kontekst pełny (opcjonalnie)
`Focus.md` sekcja „AKTUALNY STAN" + `SEQUOIA_SNOOP_RESULT.md`. Skrót stanu: drut byte-perfect (PCM na
slotach 2–15, lead SPH=3, DBC ciągły), ale MOTU **misframuje** (2 kanały → 3 diody). Branch dev:
`integrate-dice-c2bdf11`, dext v138 (SID=1 fix).
