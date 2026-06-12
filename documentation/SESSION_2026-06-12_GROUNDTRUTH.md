# Sesja 2026-06-12 — przełom: ground-truth MOTU 828 MK3 z Linuxa + El Capitan

Sesja w której **rozwiązaliśmy zagadkę** "świeci tylko ch7 + pisk" gnębiącą projekt od tygodni.
Metoda: postawiliśmy działający sterownik referencyjny (Linux) i porównaliśmy z El Capitan.

## TL;DR — co naprawdę było zepsute

| Co podejrzewaliśmy | Werdykt |
|--------------------|---------|
| Zły DBS (21, potem 16) | ❌ — prawidłowe **DBS=13** (Fix 69 był OK, Fix 66→16 błędny) |
| Zły timing SPH/DBC | ❌ — timing był OK |
| Command DSP routing potrzebny | ❌ — NIE potrzebny, sloty mapują się na sztywno |
| **Zły slot PCM** | ✅ **TO BYŁO TO** — stereo szło na slot 8 (Analog 7) zamiast slot 0/1 (Main) |

## Jak to ustaliliśmy — 3 niezależne źródła (+4. z snoopa, 2026-06-13)

### 1. Linux snd-firewire-motu (MacBook 2009, kernel 6.14)
- ALSA tracepoint `snd_firewire_lib:amdtp_packet` → 14 949 pakietów podczas grania
- Nagłówek CIP: **DBS=13** (0x0d), **SYT=0xFFFF**, SPH=1, FMT=0x02, FDF=0x22
- DATA packet = 8 data blocks (106 quadletów); przeplatane NO-DATA (2 quadlety)
- Sink: 14 kanałów PCM. Dane: `diagnostics/motu_amdtp_linux_groundtruth.txt`

### 2. CueMix "MOTU Channel Names" (El Capitan) — MAPA KANAŁÓW
14 slotów PCM strumienia IT (data block 52B = SPH[4] + MSG[6] + PCM[od byte 10]):

| Slot | Wyjście | Byte | | Slot | Wyjście | Byte |
|------|---------|------|-|------|---------|------|
| 0 | **Main Out 1 (L)** | 10 | | 7 | Analog 6 | 31 |
| 1 | **Main Out 2 (R)** | 13 | | 8 | **Analog 7** | 34 |
| 2 | Analog 1 | 16 | | 9 | Analog 8 | 37 |
| 3 | Analog 2 | 19 | | 10 | Phones 1 | 40 |
| 4 | Analog 3 | 22 | | 11 | Phones 2 | 43 |
| 5 | Analog 4 | 25 | | 12 | S/PDIF 1 | 46 |
| 6 | Analog 5 | 28 | | 13 | S/PDIF 2 | 49 |

### 4. ✅ Snoop M3→El Cap (2026-06-13, sesja 36) — pełny payload z działającego Apple sterownika

Pasywny IR snoop w `snoop-mode` branch (v112). 1.3 mln+ pakietów, ~8000 pkt/s, auto-resume.
Potwierdzenia: DBS=13, DATA pkt=424B, EMPTY=8B, framesPerPkt=8, timestamp format per blok.
**Korekta dokumentu:** byte 2 CIP Q0 = 0x04 → **QPC=1, SPH=0** (poprzedni opis "SPH=1" był błędem).
Szczegóły → `MOTU_V3_WIRE_GROUNDTRUTH.md` sekcja "Snoop El Capitan".

### 3. El Capitan "MOTU Audio Setup" (828mk3 tab)
- Sample Rate 48000, Clock Source Internal
- **Default Stereo Output = Main Out 1-2** → macOS wysyła OS audio na sloty 0/1 → gra
- Return Assign = Analog 1-2 (osobny bus loopback, NIE dotyczy naszego output)
- Optical In/Out A/B = Disabled (→ brak +ADAT, baza 14 kanałów)
- firmware: 1.06, boot: 1.01

## ⛔ Linux to ŚLEPA ULICZKA dla czystego odtwarzania (potwierdzone na końcu sesji)

Początkowo myśleliśmy że pisk Linuxa to wina PipeWire (zła mapa kanałów). Po godzinie prób
(direct hw, PipeWire surround, PipeWire Pro Audio 1:1) — **żadna nie dała czystego dźwięku**,
zawsze pisk albo zacięcie strumienia.

**Przyczyna — udokumentowany, nierozwiązany bug:**
[snd-firewire-improve issue #27](https://github.com/takaswie/snd-firewire-improve/issues/27) —
periodyczne xruny, **"Lost interrupts"**, `ohci_flush_iso_completions`, podejrzenie wewnętrznego
zegara 828mk3. Open, bez fixu. Plus kontroler LSI FW643 sam niestabilny (quirks=0x10).

**KRYTYCZNY niuans — co to NIE znaczy:** pisk Linuxa to problem **timingu/przerwań isoch**, NIE
treści bajtów. Dlatego **capture nagłówków CIP (DBS=13, SYT=0xFFFF) jest dalej ważny** — to dane
o zawartości pakietu, niezależne od tego czy strumień jest stabilny. Nie skopiowaliśmy "piszczącego
sterownika"; skopiowaliśmy poprawny FORMAT, a pisk pochodzi z osobnej warstwy (timing).

**Mój błąd procesowy:** trzeba było sprawdzić CZY 828mk3 w ogóle gra czysto na Linuxie ZANIM
obiecałem to zademonstrować. Nie sprawdziłem → strata czasu na nierozwiązywalny problem.

## Ścieżki do SŁYSZALNEGO dowodu (Linux odpada)

| Ścieżka | Status |
|---------|--------|
| **El Capitan** (oficjalny sterownik) | ✅ gra czysto — potwierdzone (CueMix, Audio Setup) |
| **Nasz DriverKit na Tahoe** | 🎯 cel — po fixie slotu (L→slot 0, R→slot 1) |
| **Snoop El Capitan→M3** (dext IR na drugim porcie MOTU) | ⭐ jedyna droga do PEŁNEGO payloadu z działającego źródła; wymaga trybu snoop w dextcie + 2 hosty |
| ~~Linux czyste audio~~ | ⛔ ślepa uliczka (issue #27) |

## 828mk3 Command DSP — co potwierdziliśmy

- 828mk3 = Command DSP model; routing/mikser konfigurowany przez Command DSP frames
- `snd-firewire-ctl-services`: "return assignment ... not operable yet" dla Command DSP
- **ALE mapowanie slot→wyjście fizyczne jest STAŁE** (slot 0 → Main Out 1 fizycznie) — porównanie
  rejestrów El Capitan vs Linux: routing regs (0x0c04, 0x0c0c, 0x0c10) IDENTYCZNE → routing to nie różnica
- Różniły się tylko 0x0b04 (0xffc10001 vs 0xffc1ffff) i 0x0b08 (0 vs 0xe0000000) — stream control,
  drugorzędne wobec głównego odkrycia (slot placement)

## FIX dla naszego DriverKit (do wdrożenia)

W encoderze MOTU V3 (`PacketAssembler` / `IsochAudioTxPipeline`):
- Kanał **L → slot PCM 0** (byte offset **10** w data blocku)
- Kanał **R → slot PCM 1** (byte offset **13**)
- Zeruj sloty 2-13
- DBS=13 zostaje (Fix 69), SYT=0xFFFF zostaje

Mapowanie slot→Main jest na sztywno w sprzęcie → po poprawnym slocie audio wyjdzie na Main Out.

## Infrastruktura postawiona w tej sesji (do reużycia)

- MacBook 2009 + Linux Mint jako referencja MOTU. Setup: `documentation/LINUX_MBP2009_SSH.md`
  (SSH `cube666@192.168.0.38` / `72044277`, quirks=0x10 dla FW643, b43 WiFi)
- `linux-firewire-utils` (firewire-request) do odczytu rejestrów MOTU z Linuxa
- Zależności `snd-firewire-ctl-services` zainstalowane (cargo, libhinawa, libhitaki) — build niedokończony
- Pełny zrzut pakietów: `diagnostics/motu_amdtp_linux_groundtruth.txt`

## Powiązane dokumenty
- `documentation/MOTU_V3_WIRE_GROUNDTRUTH.md` — szczegóły wire format + mapa kanałów
- `documentation/LINUX_MBP2009_SSH.md` — dostęp i konfiguracja maszyny referencyjnej
- `documentation/MOTU_KEXT_GHIDRA.md` — wcześniejsza analiza statyczna kextu (pcm_param=FC+10)
