# MOTU 828 MK3 — wire ground-truth z Linux snd-firewire-motu

> 📌 Kanoniczne fakty (kanały/DBS/rate/sloty) → **`MOTU_828_MK3_FACTS.md`**. Ten plik = szczegóły wire host→device.

**Data:** 2026-06-12. Źródło: ALSA tracepoint `snd_firewire_lib:amdtp_packet` na MacBooku 2009
(Linux Mint 22.3, kernel 6.14) z **działającym** sterownikiem referencyjnym, podczas odtwarzania
audio (YouTube) przez MOTU 828 MK3 (GUID 0001f20000087236, S400, IT host→device).

Pełny zrzut: `diagnostics/motu_amdtp_linux_groundtruth.txt` (14 949 pakietów).
Sink PipeWire: `s32le 14ch 48000Hz` (uwaga: **14 kanałów**, nie 18, nie 2).

## Nagłówek CIP (IT, host→device, 48kHz)

Ostatnie pole tracepointu = 8 bajtów nagłówka CIP, big-endian. Przykład:
```
{0x01, 0x0d, 0x04, 0x68,  0x82, 0x22, 0xff, 0xff}
```

| Bajt | Wartość | Znaczenie |
|------|---------|-----------|
| 0 | 0x01 | SID (source node id), bits[7:6]=00 CIP |
| 1 | **0x0d = 13** | **DBS = 13** (data block size w quadletach) |
| 2 | 0x04 | FN=0, **QPC=1** (1 quadlet timestamp per blok), **SPH=0** (CIP SPH bit NIE ustawiony) |
| 3 | 0x68 | **DBC** (data block counter), inkrementowany |
| 4 | 0x82 | bits[7:6]=10 (CIP fmt), bits[5:0]=**FMT=0x02** |
| 5 | 0x22 | **FDF = 0x22** |
| 6-7 | 0xffff | **SYT = 0xFFFF** (NO_INFO — brak timestampu) |

## Potwierdzone fakty (obalają wcześniejsze fixy)

| Pole | Ground truth | Nasza historia |
|------|-------------|----------------|
| **DBS** | **13** | Fix 66 zmienił 21→16 **BŁĘDNIE**; Fix 69 wrócił 16→13 ✅ |
| Kanały PCM | **14** (DBS=13 = 14 PCM + 2 MSG) | `1+DIV_ROUND_UP((2+14)×3,4)=13` |
| **SYT** | **0xFFFF** zawsze | mrmidi: "send 0xFFFF SYT" — potwierdzone ✅ |
| **SPH (CIP bit)** | **0** — CIP bit SPH=0, ale pierwszy quadlet każdego bloku = MOTU timestamp | ⚠️ Poprzedni doc miał błąd "SPH=1" |
| FMT/FDF | 0x02 / 0x22 | ✅ |

**Wniosek: DBS=13 jest prawidłowe. Fix 66 (DBS→16) był błędem.** Należy zostać przy DBS=13.

## Struktura strumienia (blocking transmission)

Z pól `payload_quadlets` / `data_blocks` w tracepoincie:
- **DATA packet:** 106 quadletów payload, **8 data blocks** (8×13 + 2 CIP = 106)
- **NO-DATA packet:** 2 quadlety payload, 0 data blocks (sam nagłówek CIP)
- Przeplatane DATA/NO-DATA — standardowy blocking mode @ 48kHz

Data block = 13 quadletów = 52 bajty = SPH(4) + MSG/PCM(48 = 16 chunków × 3 bajty: 2 MSG + 14 PCM).

## Co to wyjaśnia w naszym sterowniku (bug "świeci tylko ch7")

Prawidłowy sterownik wysyła **pełny 14-kanałowy strumień** → wszystkie wyjścia "obecne"
(8 diod ANALOG OUT świeci na panelu). Stereo jest wmiksowane w odpowiednie sloty kanałów.

Nasz sterownik świecił **tylko ch7** bo wkładaliśmy 2 kanały PCM w **zły offset** w data blocku —
MOTU czytało je na pozycji ch7 zamiast Main L/R. To problem **mapowania kanałów / byte offset**,
nie timingu (SPH/DBC).

## ✅ MAPA KANAŁÓW — z CueMix "MOTU Channel Names" (El Capitan, 2026-06-12)

Kolejność 14 kanałów wyjściowych w strumieniu IT (host→device) = sloty PCM w data blocku.
Data block = 52 bajty: SPH(4, bajty 0-3) + MSG(2 chunki, bajty 4-9) + PCM(14 chunków od bajtu 10).

| Slot PCM | Kanał fizyczny | Byte offset w data blocku |
|----------|----------------|---------------------------|
| 0 | **Main Out 1 (L)** | 10 |
| 1 | **Main Out 2 (R)** | 13 |
| 2 | Analog 1 | 16 |
| 3 | Analog 2 | 19 |
| 4 | Analog 3 | 22 |
| 5 | Analog 4 | 25 |
| 6 | Analog 5 | 28 |
| 7 | Analog 6 | 31 |
| 8 | **Analog 7** | 34 |
| 9 | Analog 8 | 37 |
| 10 | Phones 1 | 40 |
| 11 | Phones 2 | 43 |
| 12 | S/PDIF 1 | 46 |
| 13 | S/PDIF 2 | 49 |

**Mapowanie slot→wyjście jest STAŁE w sprzęcie** — nie wymaga Command DSP routingu.
Żeby stereo wyszło na Main Out: wkładaj PCM w **slot 0 (byte 10) i slot 1 (byte 13)**, resztę zeruj.

**Bug "świeci tylko ch7" WYJAŚNIONY:** nasz sterownik wkładał 2 kanały PCM na **slot 8 (Analog 7,
byte offset 34)** zamiast slot 0/1 (Main, byte 10/13). Naprawa = poprawny slot, nie timing/routing.

El Capitan MOTU Audio Setup potwierdza: **Default Stereo Output = Main Out 1-2** (macOS wysyła OS
audio na sloty 0/1 → gra). **Return Assign = Analog 1-2** (osobny bus loopback, nie dotyczy output).
firmware: 1.06, boot: 1.01.

## Potwierdzenie z snoopa El Capitan (2026-06-13) — 2. niezależne źródło

**Źródło:** pasywny IR snoop w ASFWDriver (branch `snoop-mode`, v112) na M3 MacBook Pro Tahoe.
Topologia: M3(node 0) → LaCie(node 1) → MOTU 828 MK3(node 2) → MB2009 El Cap(node 3, root/IRM).
El Capitan (IOFireWireAVC) strumieniuje IT→MOTU na ch=33. M3 przechwytuje bez IRM, pasywnie.
Dane: ponad **1.3 mln pakietów** przez wiele sesji, stabilny odbiór ~8000 pkt/s.

### CIP nagłówek z działającego sterownika Apple (El Capitan, IOFireWireAVC)

```
CIP Q0:  0x030d04xx    CIP Q1:  0x8222ffff
```

| Bajt | Wartość | Znaczenie |
|------|---------|-----------|
| 0 | 0x03 | SID = node 3 (MB2009) |
| 1 | **0x0d = 13** | **DBS = 13** — 2. niezależne potwierdzenie (Linux był 1.) |
| 2 | **0x04** | FN=0, **QPC=1**, **SPH=0** (CIP SPH bit NIE ustawiony przez MOTU V3) |
| 3 | cyklicznie | DBC (data block counter, +8 na data pkt, +0 na empty) |
| 4 | 0x82 | EOH=1, FMT=0x02 (AM824) |
| 5 | 0x22 | FDF=0x22 → SFC=1 = **48 kHz** |
| 6-7 | 0xffff | SYT = 0xFFFF (brak timestampu w CIP Q1) |

### Struktura pakietu (potwierdzona z pełnym payloadem)

| Typ | Rozmiar | Zawartość |
|-----|---------|-----------|
| DATA packet | **424 bajty** | 8 CIP + 8 bloków × 13 quad × 4 B = 8 + 416 |
| EMPTY packet | **8 bajtów** | tylko 2 CIP quadlety, 0 data blocks |

Przy 48 kHz: **~6000 data pkt/s + ~2000 empty pkt/s = 8000 pkt/s** (8 frames/data pkt × 6000 = 48000 ✅).

### Format MOTU timestamp (SPH w pierwszym quadlecie każdego bloku)

MOTU V3 zapisuje timestamp OHCI cycle-time jako **pierwszy quadlet każdego data blocku** (QPC=1).
CIP SPH bit = 0 — to jest MOTU-proprietary, nie standardowy IEC 61883 SPH.

```
Bit layout (big-endian, 32 bits):
  bits[31:28] = 0    (zawsze zero, 4 bity)
  bits[27:25] = seconds  (3 bity, 0–7)
  bits[24:12] = cycles   (13 bitów, 0–7999)
  bits[11:0]  = offset   (12 bitów, 0–3071, jednostki ~326 ns = 1/3072 cyklu 8kHz)
```

Przykłady z snoopa (dwa kolejne bloki w tym samym pakiecie — ten sam cykl, rosnący offset):
```
blok 0: 0x00304622  → secs=0, cycles=772, offset=0x622=1570
blok 1: 0x00304837  → secs=0, cycles=772, offset=0x837=2103
                      różnica offset = 533 ≈ 512 = 24.576 MHz / 48000 ✅
```

Kolejne pakiety ze snoopa (DBC cycling co 8):
```
0x030d0478  0x030d0480  0x030d0488  0x030d0490 ...  (DBC +8 per data pkt)
```

### Bit ordering — górne czy dolne bity int32? ⚠️ NIEPOTWIERDZONE

Z danych snoopa nie da się rozstrzygnąć na poziomie wire — oba warianty produkują identyczne
3 bajty na drucie:
```
int32 left-justified  (górne): 0x38B52400 → [38][B5][24]  ✓
int32 right-justified (dolne): 0x0038B524 → [38][B5][24]  ✓
```

**⚠️ Linux NIE jest referencją dla bit ordering.** Linux `snd-firewire-motu` ma znane bugi
powodujące artefakty dźwiękowe (issue #27: lost interrupts, timing), przez co:
- Odtwarzanie na Linuxie = ŚLEPA ULICZKA (piszczy, nie używać)
- Kod źródłowy (`val >> 8` w `amdtp-motu.c`) może być błędny

**Co mówi Linux source (traktować jako hipotezę, NIE jako fakt):**
```c
d[0] = (val >> 24);  // bits[31:24] — górne
d[1] = (val >> 16);
d[2] = (val >>  8);  // bits[7:0] odrzucone
```
Hipoteza: górne 24 bity int32. Zgodne ze standardem CoreAudio (left-justified).

**Jak zweryfikować niezależnie:**
IR capture z Virus TI → MOTU Analog In → sinus 440 Hz. MOTU's własne ADC packuje sampel
do 3 bajtów — pattern sinusa jednoznacznie pokaże czy MSB jest w byte[0] czy byte[2],
i czy dane są left- czy right-justified. To da bezpośrednią hardware odpowiedź bez Linux.

### Pełne 24 bity potwierdzone z payload snoopa

Analiza wszystkich niezerowych pakietów danych: **wszystkie 3 bajty sampla są aktywne**.
Próbki z Phones 1/2 (El Cap grał przez Main Out i Phones jednocześnie):
```
ch10 Phones1: [38 b5 24]  [eb ff 47]  [05 7b 75]  [ef 2f 03]  [ef 96 d2]
ch11 Phones2: [38 9e 5f]  [ea 63 df]  [03 98 49]  [ee 7c 5e]  [f2 6d 8d]
```
Brak wzorca "tylko górny bajt niezerowy" — prawdziwe 24-bitowe audio, dynamika ~-7 do -28 dBFS.
S/PDIF 1/2 = cisza. **Main Out L/R (sloty 0/1) i Phones 1/2 (sloty 10/11) aktywne** — El Cap grał muzykę na obu wyjściach. Analog 1-8 = cisza.

### Korekta błędu w poprzednim dokumencie

Poprzednia wersja tego pliku błędnie pisała "byte 2 bits[2]=1 → SPH=1". Poprawnie:
- `0x04` = `0000 0100` → bit1 (SPH, Q0 bit9) = **0**, bit2 (QPC LSB, Q0 bit10) = **1**
- MOTU nie deklaruje SPH w CIP, ale używa pierwszego quadletu bloku jako timestamp (QPC=1)
- Nasz driver NIE powinien ustawiać SPH=1 w CIP Q0

---

## Raw capture files (źródła tego dokumentu)

Pełne, nieprzycięte logi leżą w `documentation/raw-captures/`:

| Plik | Zawartość |
|------|-----------|
| `2026-06-13_el_capitan_snoop_ch33_main.txt` | Pasywny snoop ch=33 (IT) z ASFWDriver snoop-mode na M3 Tahoe podczas odtwarzania El Capitan → MOTU. Źródło przykładów CIP Q0=0x030d04xx/Q1=0x8222ffff w tym dokumencie. |
| `2026-06-13_el_capitan_snoop_ch33_connect.txt` | Ten sam snoop, sesja wokół podłączenia/handshake MOTU. |
| `2026-06-08_001235_official_driver_writerequest_trace.txt` | DTrace `IOFireWireController::processWriteRequest`/`IOFWPseudoAddressSpace::doWrite` z oficjalnego sterownika El Capitan — async register writes, **bez** danych pakietów izochronicznych. |
| `2026-06-08_004331_official_driver_dcl_and_regwrites.txt`, `2026-06-08_010315_official_driver_dcl_and_regwrites_v2.txt` | DTrace z `[OUT] off=0x0bXX` (zapisy rejestrów) + `[ISOCH] createDCLProgram talking=0/1` — potwierdza, że El Capitan tworzy DCL program **dla obu kierunków** (talking=1 TX, talking=0 RX) nawet gdy nie nagrywa — IR działa równolegle z IT do poziomów/monitoringu. |
| `2026-06-08_020402_motu_register_dump.txt` | Zrzut rejestrów MOTU 0x0b00–0x0c98 (raw + byte-swapped) w jednym punkcie czasu. |
| ✅ `2026-06-22_el_capitan_snoop_IR_ch34_device-to-host.txt` | **Pasywny snoop ctx=2 (v115) podczas gdy El Capitan AKTYWNIE NAGRYWAŁ z MOTU.** Zawiera ch=33 (IT) **oraz ch=34 (IR, device→host)** — pierwszy realny capture IR. Źródło sekcji „✅ IR ground truth — ZEBRANE" niżej. |

⚠️ Pliki z 2026-06-08/06-13 NIE zawierają bajtów pakietu IR — tylko kanał 33 (IT) lub wywołania
API/rejestry. **IR ground truth (device→host) został zebrany dopiero 2026-06-22** — patrz plik
`..._IR_ch34_...` i sekcja „✅ IR ground truth — ZEBRANE" niżej.

## ✅ IR ground truth — ZEBRANE (MOTU→host, device→host) — 2026-06-22

**Źródło:** pasywny snoop ctx=2 w ASFWDriver snoop-mode (v115) na M3 Tahoe, podczas gdy **El Capitan
(MB2009) aktywnie NAGRYWAŁ** z wejść MOTU (QuickTime audio recording). To wymusiło nadawanie IR
przez MOTU. Topologia: M3 ↔ LaCie hub ↔ MOTU ↔ MB2009/El Capitan.
Raw: `documentation/raw-captures/2026-06-22_el_capitan_snoop_IR_ch34_device-to-host.txt`
(605 linii snoop: ch=33 IT host→device + **ch=34 IR device→host**, 50 DATA + 26 EMPTY pakietów IR).

> ⚠️ **Kluczowa różnica względem IT — IR NIE używa standardowego nagłówka CIP.**

### Nagłówek IR (device→host) — STAŁY, niestandardowy

Pierwsze 2 quadlety każdego pakietu IR (DATA i EMPTY) są **stałe**:
```
Q0 = 0x0d040400    Q1 = 0x22ffffff
```
Bajtowo: `0d 04 04 00 22 ff ff ff` — **identyczne we wszystkich 50 pakietach DATA i 26 EMPTY**,
DBC-byte (Q0 byte3) zawsze `0x00`, niezależnie od czasu.

| | IT (host→device, działa) | IR (device→host) |
|--|--------------------------|-------------------|
| Q0 | `030d04xx` (SID=3, DBS=13, **DBC rośnie**) | `0d040400` (**stały**, DBC-byte=0x00) |
| Q1 | `8222ffff` (**EOH1=1**, FMT=0x02, FDF=0x22, SYT=ffff) | `22ffffff` (**EOH1=0** ❌, NIE-CIP) |

**Q1 IR = `0x22ffffff` ma bit31 (EOH1) = 0 → łamie IEC 61883 dwuquadletowy CIP.** To NIE jest błąd
odczytu — potwierdzone niezależnie przez (a) snoop El Capitan ch=34 (offset 0), (b) nasz dice
`RxAudioPacketProcessor` (offset 8, isochHeader=1), (c) Linux ALSA tracepoint (`13 4 4 0 34 255 255
255`). **Trzy niezależne stosy widzą ten sam stały nagłówek.** MOTU IR po prostu nie wstawia
poprawnego CIP — geometrię trzeba znać a-priori, NIE parsować z nagłówka.

### Struktura pakietu IR

| Typ | Rozmiar | Zawartość |
|-----|---------|-----------|
| DATA | **520 bajtów** | 8 (nagłówek) + 8 bloków × 16 quad × 4 B = 8 + 512 |
| EMPTY | **8 bajtów** | sam nagłówek `0d040400 22ffffff`, 0 bloków |

**DBS=16, 8 bloków danych/pakiet** ✅ (zgodne z kanonem `MOTU_828_MK3_FACTS.md`).

### Blok danych IR = 16 quadletów (64 bajty)

```
quad 0  (bajty 0-3)   = SPH (MOTU timestamp, ten sam format co IT — patrz wyżej)
quad 1+ (bajty 4-63)  = 60 bajtów = 20 chunków × 3 bajty = 2 MSG + 18 PCM
```
`1 + DIV_ROUND_UP((2+18)×3, 4) = 1 + 15 = 16` ✅. **18 kanałów PCM** (NIE 16 — to potwierdza,
że dice „Quick Fix" `kRxPcmChannels=16` powinien być **18**; El Capitan negocjował 18-kanałowy IR,
ALSA `arecord` zaakceptował tylko `-c 18`).

### Potwierdzenie: dane są ŻYWE (nie idle)

- **SPH rośnie i jest zsynchronizowany z zegarem**: sphCyc 1706→3706→5665→7665 (zawija na 8000),
  `aheadHw=-5` względem `ReadCycleTime()` → realny zegar urządzenia, nie placeholder.
- **Sloty PCM niosą realny sygnał**: pierwsze pakiety `s0=00001e s1=fffffa s2=fffffc …`
  (szum dna ~±0x25, 24-bit signed), potem `000000` gdy nic nie grało na wejściu. Realne nagrywanie.

> 📌 **Korekta wcześniejszej hipotezy (2026-06-19):** „DBC=0 na stałe = MOTU nie nadaje / idle"
> było **BŁĘDNE**. MOTU nadaje pełne, żywe dane IR — tyle że DBC-byte w (niestandardowym) nagłówku
> jest zawsze 0, a nagłówek jest stały. To normalne dla IR MOTU, nie oznaka idle.

### Konsekwencje dla dekodera dice (root cause „ZTS timed out")

`RxAudioPacketProcessor::ProcessPacket` woła `CIPHeader::Decode(q0,q1)` które **wymaga EOH1=1**.
Dla IR (`Q1=0x22ffffff`, EOH1=0) → `nullopt` → `kInvalidRange` → **każdy pakiet IR odrzucony** →
ZTS nigdy nie publikuje → timeout `0xe00002d6`. Offset (`payload+8`, isochHeader=1) był **poprawny
cały czas** — bug to wyłącznie walidacja standardowego CIP na niestandardowym nagłówku IR.

**Fix (dice):** dla IR nie walidować/parsować standardowego CIP. Użyć stałego DBS=16, pominąć
8-bajtowy nagłówek, dekodować 8 bloków (każdy: SPH 4B → pomiń 2 MSG chunki (6B) → 18 PCM × 3B),
wziąć SPH bloku[0] jako timestamp do ZTS.

### Mapa slotów IR (który wejście = który slot PCM) — WCIĄŻ DO ZEBRANIA

Ten capture był z **cichym/szumowym** wejściem (nic nie wpięte). Żeby zmapować 18 slotów IR na
fizyczne wejścia, potrzeba **znanego sygnału** (np. Virus TI sinus 440 Hz → MOTU Analog In 1) i
identyfikacji niezerowego slotu — analogicznie do mapy slotów IT (CueMix, wyżej). Metoda jak w
nagłówku tej sekcji, tylko z wpiętym sygnałem.

### Dlaczego IR jest priorytetem

Wejścia są **feature blokującym** — workflow = nagrywanie syntezatorów (Virus TI + inne).
Bez zmapowanych slotów IR nie można poprawnie zdekodować audio z wejść MOTU.
