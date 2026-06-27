# HANDOFF — dobić 2. quadlet `0x0b38` (DTrace deref na El Capitan)

> Następna sesja Claude (orkiestrowana jak poprzednio: SSH → **MBP2009 / El Capitan 10.11.6, SIP off**,
> oficjalny sterownik MOTU + DTrace). To **mała, celowana dokrętka** poprzedniego zadania, nie nowa praca.
> Pełny kontekst i działający przepływ DTrace: **[`SEQUOIA_REGREAD_SESSION_LOG.md`](SEQUOIA_REGREAD_SESSION_LOG.md)**
> + wynik: **[`SEQUOIA_REGREAD_RESULT.md`](SEQUOIA_REGREAD_RESULT.md)**.

## Cel w jednym zdaniu

`0x0b38` to **8-bajtowy** block-write (V3 stream-control stream-2). Mamy **tylko 1. quadlet
`0xffc20002`**; brakuje **2. quadletu (bajty 4–7)**. Bez niego Tahoe nie może bezpiecznie napisać 0b38.

## Co dokładnie zrobić

Poprzednia sesja wyciągnęła 1. quadlet łańcuchem (RESULT, sekcja „WARTOŚCI ZDOBYTE"):
```
arg7 = IOMemoryDescriptor*
ranges = *(uint64_t *)(arg7 + 96)        // &_ranges → _ranges
addr   = *(uint64_t *)(ranges)           // IOVirtualRange.address (bufor danych)
d0     = *(uint32_t *)(addr)             // 1. quadlet  ← już mamy (0xffc20002 dla 0b38)
```

**Rozszerz skrypt `.d` (ten z `cap_run3.d` / przebiegu w SESSION_LOG) o drugi quadlet:**
```d
this->addr = *(uint64_t *)(*(uint64_t *)(arg7 + 96));
this->d0   = *(uint32_t *)(this->addr);
this->d1   = *(uint32_t *)(this->addr + 4);   // ← NOWE: 2. quadlet 0b38
printf("addrLo=%x d0=%08x d1=%08x len=%d\n", this->addrLo, this->d0, this->d1, this->len);
```
- Jeśli skrypt ma już `len`/`size` z deskryptora — potwierdź, że dla 0b38 `len==8` (sanity: to block-write).
- Filtruj/grepuj po **`addrLo == 0xf0000b38`** (lub łap wszystko i `grep 0b38`).

## ⚠️ Trigger — 0b38 pada TYLKO przy ZIMNYM starcie

Z RESULT: `0b38`/`0b08` lecą **wyłącznie przy cold start** (nie przy zwykłym re-starcie streamu).
Żeby je złapać, **podczas działania skryptu DTrace** wymuś zimny start MOTU:
- **odepnij i wepnij kabel FireWire MOTU** (najpewniejsze), lub
- przeładuj oficjalny sterownik / reboot z podłączonym MOTU.
Samo odpalenie odtwarzania **nie wystarczy** (to tylko re-start streamu → 0b1c, nie 0b38).

## Walidacja (że łańcuch dalej dobry)

W tym samym przebiegu złap też **znany** rejestr i potwierdź wartość (jak poprzednio):
- `0b14 → 0x0a000100` (@48k) lub `0b10 → 0x00000002`.
Jeśli się zgadza → `d1` dla 0b38 jest wiarygodny. (Uwaga na artefakt `0x80a5211c` z tracera v2 —
to była stała-śmieć; realne dane muszą różnić się per offset.)

## Gdzie zapisać wynik

Dopisz do **`SEQUOIA_REGREAD_RESULT.md`** (sekcja „WARTOŚCI ZDOBYTE", wiersz 0x0b38):
- **`0x0b38 = 0xffc20002 <d1>`** (oba quadlety, host-order/„swapped"),
- potwierdź `len==8`,
- 2–3 surowe linie dowodu (addrLo + d0 + d1) + linia walidacyjna znanego rejestru,
- surowy log → `raw-captures/2026-06-27_elcap_0b38_quadlet2.txt`.

## Co zrobi Tahoe

Doda 8-bajtowy block-write `0x0b38 = {0xffc20002, <d1>}` do `MOTUVendorProtocol` (potrzebny block-write,
nie quadlet — do dorobienia) w sekwencji init, obok 0b1c/0b08 (już wdrożone w **v139**). To **runda 2**;
runda 1 (v139 = 0b1c + 0b08) jest już w testach na Tahoe.

---
### Status (żeby było jasne)
- ✅ `0x0b08` (`0xffffffff→0`), `0x0b1c` (`0x00120000`@48k) — **wdrożone w v139** (Tahoe).
- ⏳ `0x0b38` — **to zadanie**: dobić 2. quadlet, potem Tahoe dołoży w rundzie 2.
- Branch dev: `integrate-dice-c2bdf11`. Diagnoza: MOTU misframuje czysty drut → brakujące zapisy init.
