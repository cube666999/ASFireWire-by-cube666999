# MOTU 828 MK3 — wire ground-truth z Linux snd-firewire-motu

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

### Korekta błędu w poprzednim dokumencie

Poprzednia wersja tego pliku błędnie pisała "byte 2 bits[2]=1 → SPH=1". Poprawnie:
- `0x04` = `0000 0100` → bit1 (SPH, Q0 bit9) = **0**, bit2 (QPC LSB, Q0 bit10) = **1**
- MOTU nie deklaruje SPH w CIP, ale używa pierwszego quadletu bloku jako timestamp (QPC=1)
- Nasz driver NIE powinien ustawiać SPH=1 w CIP Q0
