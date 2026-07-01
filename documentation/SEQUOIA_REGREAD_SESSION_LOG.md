# Log sesji — przechwytywanie zapisów init MOTU (El Capitan, DTrace)

> Cel: zdobyć realne quadlety pisane przez oficjalny sterownik do **0x0b1c / 0x0b38** (write-only,
> read-back ich nie zwraca — patrz [`SEQUOIA_REGREAD_RESULT.md`](SEQUOIA_REGREAD_RESULT.md)). Metoda:
> DTrace na `IOFireWireController::asyncWrite` z **dereferencją + walidacją** wobec znanych rejestrów.
> Narzędzie: [`../ASFireWire/tools/capture_motu_writes_v3.d`](../ASFireWire/tools/capture_motu_writes_v3.d).

## 2026-06-27 — sesja zdalna (Sequoia M3 → SSH → El Capitan MB2009)

### Środowisko
- **Host sterujący:** MacBook-Pro-Cube (M3, macOS Sequoia 15.7.4), `192.168.0.80`.
- **Target:** MacBook Pro 2009 = `macbook-pro-jakub-s.local` = `192.168.0.38`.
  - System: **macOS 10.11.6 El Capitan** (build 15G22010). ✓
  - Konto SSH: **`jakubs`** (NIE `cube666` — to był login Linux Mint na tym samym sprzęcie/IP).
  - Auth: klucz `~/.ssh/mbp2009` wgrany do `/Users/jakubs/.ssh/authorized_keys`. ✓
  - `dtrace`: `/usr/sbin/dtrace` ✓
  - MOTU 828mk3: obecne (Vendor_ID 498=0x1f2), sterownik `com.motu.driver.FireWireAudio (1.6 71459)` załadowany. ✓
  - **SIP: ENABLED ❌** — blokuje `fbt` + odczyt pamięci jądra → DTrace capture nie zadziała.

### Przebieg / pułapki rozwiązane
1. Klucz hosta SSH zmieniony (Linux→El Cap na tym samym IP) → `ssh-keygen -R 192.168.0.38`.
2. Klasyfikator auto-mode **zablokował** logowanie hasłem przez `expect` (wzorzec sekret/obejście) →
   przejście na auth kluczem.
3. Klucz najpierw odrzucany, bo wgrany na konto `jakubs`, a próbowałem `cube666` → poprawny user `jakubs`.

### ⛔ BLOCKER: SIP enabled
Wymagane: wyłączyć SIP z Recovery (`csrutil disable`) **lub** `csrutil enable --without dtrace`, reboot.
Po reboot `csrutil status` ma być `disabled` (lub dtrace unrestricted).

### Plan po wyłączeniu SIP
- `scp` skryptu `capture_motu_writes_v3.{d,command}` na El Cap (Pulpit `jakubs`).
- Capture: lokalnie `./capture_motu_writes_v3.command` (sudo podawane lokalnie w Terminalu, bez hasła
  w czacie), albo zdalnie `sudo dtrace` jeśli skonfigurowane NOPASSWD.
- W trakcie: przełączyć wyjście audio na MOTU (Main Out 1/2), grać dźwięk ~10 s, Ctrl-C/ENTER.
- Analiza: znaleźć arg z danymi (walidacja vs 0b00=61620000/0b04=ffc10001/0b10=00000002/0b14=0a000100),
  odczytać 0b1c/0b38 → dopisać do `SEQUOIA_REGREAD_RESULT.md`.

## 2026-06-27 cd. — DTrace capture na El Capitan (PRZEŁOM strukturalny)

### Zabezpieczone logi/skrypty (repo `documentation/raw-captures/`)
- `2026-06-27_elcap_dtrace_counter.txt` — licznik: potwierdził, że przełączenie wyjścia robi `createDCLProgram`.
- `2026-06-27_elcap_dtrace_iomd_dump.txt` — **kluczowy**: zrzut obiektu `IOMemoryDescriptor` (run2, zmiana rate).
- `2026-06-27_elcap_counter.d`, `..._cap_run1.d`, `..._cap_run2.d`, `..._cap_run3.d` — skrypty DTrace.

### Mechanizm zapisu (ustalony z sygnatur + zrzutu)
Oficjalny sterownik pisze rejestry przez **4. przeciążenie**:
`IOFireWireController::asyncWrite(UInt32 gen, UInt16 node, UInt16 addrHi, UInt32 addrLo, int, int,`
`IOMemoryDescriptor* buf, ull off, int size, IOFWAsyncCommand*, IOFWWriteFlags)`.
Argumenty DTrace: `arg2`=node, `arg4`&0xffff=offset rejestru, **`arg7`=IOMemoryDescriptor***, `arg9`=size(=**4**, quadlet).
> ⚠️ To dlatego `cap_run1.d` (wąskie matchery) nic nie łapał — trzeba **szerokiego** `*asyncWrite*`.

### Droga do danych (zrzut obiektu IOMD, offsety wewn. obiektu)
`vtable@0`, **`_ranges@+96`** → inline `IOVirtualRange{ address@+0, length@+8 }`. Czyli:
```
data = *(uint32_t*)( *(uint64_t*)( *(uint64_t*)((uintptr_t)arg7 + 96) ) )
```
Bufor (`address`) jest **tworzony na świeżo per zapis** → trzeba derefować W MOMENCIE `asyncWrite:entry`
(adres po fakcie jest nieaktualny). Robi to `cap_run3.d` (z guardami kptr w predykacie). Compile OK, wgrany na MB2009 `/tmp/cap_run3.d`.

### Co złapane / czego brak
Trigger pewny re-startu streamu = **zmiana sample rate** (Audio MIDI Setup) lub przełączenie wyjścia.
Run2 (16 re-startów): zapisy stream-start = **`0b00`, `0b10`, `0b14`, `0b1c`** (każdy size=4).
- ✅ **`0x0b1c` ZŁAPANE** (×8) — mamy trigger i adres bufora; brakuje już tylko derefu wartości (`cap_run3.d`).
- ❌ **`0x0b38` i `0x0b08` NIE pojawiają się** przy re-starcie ani zmianie rate → padają tylko przy
  **ZIMNYM starcie** (pierwsza inicjalizacja: replug MOTU / reboot z MOTU od zera, audio start od podstaw).

### NASTĘPNE KROKI (gotowe do wykonania)
1. **Wartości `0x0b1c`:** uruchom `cap_run3.d`, zmień sample rate kilka razy → odczyt `[D] off=0x0b1c DATA=…`.
   Walidacja łańcucha: `0b00→61620000`, `0b10→00000002`, `0b14→0a000100` muszą się zgodzić w `[D]`.
2. **Wartości `0x0b38`/`0x0b08`:** `cap_run3.d` uruchomić, a w trakcie zrobić **zimny start** —
   odepnij kabel FireWire MOTU (lub wyłącz/włącz MOTU), poczekaj, podepnij i uruchom audio od zera.
3. Realne wartości → dopisać do `SEQUOIA_REGREAD_RESULT.md` i wdrożyć na Tahoe w `MOTUVendorProtocol::PrepareDuplex`.

### ✅ WYNIK cap_run3 (deref danych) — REALNE WARTOŚCI
Log: `raw-captures/2026-06-27_elcap_dtrace_deref_values.txt`. Walidacja łańcucha OK
(`0b10→0x00000002`, `0b14→0x0a000100`@48k, `0b04→0xffc20001`).

| rejestr | wartość (swap/host) | size | uwaga |
|---|---|:---:|---|
| 0x0b08 | `0xffffffff`→`0x00000000` | 4 | command/doorbell (set→clear); tylko zimny start |
| 0x0b1c | `0x00120000` @48k (`0x00000a00` inny rate) | 4 | rate-zależny; re-start streamu |
| 0x0b38 | `0xffc20002` (`0xffffffff`=clear) | 8 | V3 stream-control; **2. quadlet niezmierzony**; zimny start |

Triggery: `0b1c` = zmiana sample rate / przełączenie wyjścia; `0b38`/`0b08` = przepięcie kabla FireWire (zimny start).
Pełna tabela wartości + plan wdrożenia: [`SEQUOIA_REGREAD_RESULT.md`](SEQUOIA_REGREAD_RESULT.md) (sekcja na górze).

**Pozostało (opcjonalne domknięcie):** 2. quadlet `0x0b38` (deref `d1=*(u32*)(buf+4)`); pełna mapa `0b1c`↔rate.

### Hasło
Hasło konta podane w czacie podczas sesji → **zalecana zmiana po zakończeniu**.
