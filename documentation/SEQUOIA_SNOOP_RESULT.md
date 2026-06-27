# SEQUOIA SNOOP RESULT — zmierzony lead SPH oficjalnego sterownika MOTU

> Pomiar wykonany **2026-06-26** na żywym, czysto grającym stosie: **M3/Sequoia (oficjalny
> sterownik MOTU) gra muzykę → MBP2009/Linux Mint pasywnie snoopuje szynę.** Cel: skalibrować
> `kMotuSphPresentationLeadTicks` (była zgadnięta = `2*3072`). Handoff: `SEQUOIA_SNOOP_HANDOFF.md`.

## ⭐ WYNIK (do skonsumowania przez sesję Tahoe)

| metryka | wartość |
|---|---|
| **median lead** | **3 cykle** (mean=3.000, min=3, max=3, mode=3 — **zero rozrzutu**) |
| **lead w tickach** | **9216** (= `3 * 3072`) |
| pakiety DATA użyte | 192 (histogram leada: `{3: 192}`) |
| **SID nadawany przez oficjalny sterownik** | **3** (stały na wszystkich pakietach) |
| DBS | `0x0d` = 13 |
| CIP Q0 byte2 | `0x04` |
| CIP Q1 | `8222ffff` (SYT=0xFFFF) |
| kanał IT (host→device) | **ch 33** |

### ➡️ Akcja dla Tahoe
- Ustaw `kMotuSphPresentationLeadTicks` w `ASFWAudioDriverZts.cpp` z **`2*3072` (6144) → `3*3072` (9216)**.
  Brakowało **jednego cyklu** leada — prowadzący podejrzany o pisk (faza karmienia mastera).
- **SID=3 potwierdzony jako wartość oficjalnego stosu** (zgodny z El Cap). Nasz drut nadaje SID=0,
  bo jesteśmy node 0 na naszej szynie. SID = source node-id, więc 0 jest poprawne *jeśli* jesteśmy
  węzłem 0 — to NIE jest dowolnie ustawialna stała. Najpierw zestrój lead; SID traktuj jako osobny,
  niski priorytet (jak dotąd w Focus.md).

## Metoda / definicja leada

Dla każdego DATA-pakietu (`len=424`, Q1=`8222ffff`):
- `sph = quadlet[2]` (SPH pierwszego bloku), `sph_cyc = (sph>>12)&0x1FFF`
- `arr_cyc = cyc & 0x1FFF` (pole `cyc=` = cykl odbioru, `ev->cycle` z firewire-cdev)
- `lead = ((sph_cyc - arr_cyc) + 8000) % 8000`, korekta `>4000 → -8000`

`arr_cyc` (cyc) rośnie monotonicznie ~o 1 na pakiet (zgodnie z odstępem cykli) — pole skali jest
poprawne, lead nie jest artefaktem złej jednostki `cyc`.

## Dowód — surowe pakiety (cyc, Q0, Q1, SPH, sph_cyc, sph_off, lead)

```
(50719, 030d04a0, 8222ffff, 00622bbf, sph_cyc=1570, sph_off=3007, lead=3)
(50721, 030d04a8, 8222ffff, 006243bf, sph_cyc=1572, sph_off= 959, lead=3)
(50722, 030d04b0, 8222ffff, 006257bf, sph_cyc=1573, sph_off=1983, lead=3)
(50723, 030d04b8, 8222ffff, 00626bbf, sph_cyc=1574, sph_off=3007, lead=3)
(50725, 030d04c0, 8222ffff, 006283bf, sph_cyc=1576, sph_off= 959, lead=3)
(50726, 030d04c8, 8222ffff, 006297bf, sph_cyc=1577, sph_off=1983, lead=3)
```

Obserwacje pomocnicze:
- **SPH offset cadence** cyklicznie `3007 → 959 → 1983 → 3007 …` (+1024 ticków/pakiet, zawijanie co
  3072 = długość cyklu). DBC `030d04a0/a8/b0/b8` = +8/pakiet (data). Kadencja D,D,N (no-data = `len=8`).
- Pełny capture (256 pakietów, w tym no-data) → `documentation/raw-captures/2026-06-26_sequoia_official_it_cyc.txt`.

## ⭐⭐ DRUGA ATOMOWA PRAWDA — mapa slotów PCM (Main Out 1/2 → slot 12/13)

Z tego samego capture (Sequoia grało **Default Stereo Output = Main Out 1-2**), analiza blokowa
payloadu IT (DBS=13 → blok = SPH(1 quad) + 12 quad PCM = 16 slotów 3-bajtowych BE):

- **Aktywne TYLKO sloty 12 i 13** (0-indeksowane w regionie PCM po SPH); reszta = `000000`.
- `slot12 max≈1.98M`, `slot13 max≈2.00M`, active=100% bloków — czyste stereo (muzyka).
- Framing zweryfikowany: każdy blok startuje SPH z **Δ=512/ramkę** (off 3007→447→959→…→3007, +512),
  sloty 12/13 zmieniają się per ramkę = realne PCM. (1536 bloków / 192 pakiety × 8.)

**Przykład (pakiet cyc=50719, blok 0): slot12=`01b9c9` slot13=`1380d5`.**
Overall offset w bloku: SPH=bajty 0-3, slot12=bajty 4+12·3=40-42, slot13=43-45 (w obrębie 52-bajtowego bloku).

### ➡️ Akcja dla Tahoe (ROUTING — osobny trop od leada)
**Nasz enkoder kładzie Main na slot 0/1; oficjalny sterownik — na slot 12/13.** To prawdopodobnie
tłumaczy „Main cichy + diody wędrują (Analog 7 / S/PDIF / Main R)": nasze PCM trafia w złe fizyczne
wyjścia, a prawdziwy Main Out dostaje ciszę. Przenieś stereo na **slot 12/13** w enkoderze IT
(`AmdtpPayloadWriter` ścieżka MOTU, `kMotuV3PcmByteOffset`/slot — Focus „Fix 74"). 
> ⚠️ **Konflikt z wcześniejszą notatką:** Focus.md mówił „nasz WIRE16 = slot 0/1, identyczny z El Cap".
> Ten pomiar (oficjalny Sequoia, byte-perfect lead) pokazuje Main = **12/13**. Pomiar > hand-written
> notatka — zweryfikuj który kanał logiczny mapuje się na 12/13 PRZED zmianą (pełna mapa 14 wyjść
> jeszcze niezebrana — patrz niżej).

## ⭐⭐⭐ TRZECIA ATOMOWA PRAWDA — PEŁNA mapa 14 wyjść IT (CoreAudio out → wire slot)

Zmierzone 2026-06-26: PortAudio (sounddevice) zagrał **prawdziwe 14 kanałów** na MOTU (każdy kanał
inna częstotliwość 1000+i·211 Hz), snoop ch33 + FFT per slot. Wszystkie peaki dopasowane < bin (~15 Hz).
(`afplay` i `sox` **downmiksują do stereo** — `sox: can't set 14 channels; using 2` — dlatego PortAudio.)

| CoreAudio out (1-based) | wire slot | poziom | uwaga |
|:---:|:---:|:---:|---|
| **1 (Main L)** | **12** | −3 dB | Default Stereo Output |
| **2 (Main R)** | **13** | −3 dB | Default Stereo Output |
| 3 | 4 | −37 dB | stłumione w mikserze MOTU |
| 4 | 5 | −37 dB | stłumione |
| 5 | 6 | 0 dB | |
| 6 | 7 | 0 dB | |
| 7 | 8 | 0 dB | |
| 8 | 9 | 0 dB | |
| 9 | 10 | 0 dB | |
| 10 | 11 | 0 dB | |
| 11 | 2 | 0 dB | |
| 12 | 3 | 0 dB | |
| 13 | 14 | 0 dB | |
| 14 | 15 | 0 dB | |

**Odwrotnie (wire slot → CoreAudio out):** `0,1`=**NIEUŻYWANE (padding)**; `2`→11, `3`→12, `4`→3,
`5`→4, `6`→5, `7`→6, `8`→7, `9`→8, `10`→9, `11`→10, `12`→**1 (Main L)**, `13`→**2 (Main R)**, `14`→13, `15`→14.

### ➡️ Akcja dla Tahoe (ROUTING — pełna prawda)
- **14 PCM zajmuje sloty 2–15; sloty 0,1 są PUSTE.** Nasz enkoder MOTU musi mapować CoreAudio
  output ch k → wire slot wg tabeli (Main 1/2 → 12/13), a NIE k→slot k od zera.
- To prawie na pewno współ-przyczyna „Main cichy + diody wędrują": słaliśmy PCM na slot 0/1 (padding)
  i przesunięte → MOTU zapalał złe wyjścia, Main milczał.
- Poziom (−3 dB Main, −37 dB out3/4) to mikser MOTU — NIE replikować, to nie część kodowania drutu.
- Surowy capture: `documentation/raw-captures/2026-06-26_sequoia_it_sweep14_pa.txt`
  (+ `..._sweep14.txt`/`_sox.txt` = downmix afplay/sox, dla referencji że downmiksują).

## ⭐⭐⭐⭐ CZWARTA ATOMOWA PRAWDA — strumień IR (device→host) + kotwica mapy wejść

Zmierzone 2026-06-26: sygnał ~250 Hz podany na **Analog In 1**, snoop **ch34** (IR device→host).

### Struktura pakietu IR (potwierdzona empirycznie)
- **Kanał iso: ch34**, `len=520`, `tag=1 sy=0`.
- **CIP stały: `0d040400 22ffffff`** (EOH=0, nagłówek NIE-standardowy MOTU V3 — zgodne z Focus v117).
- Payload po CIP (8 B) = 512 B = **8 bloków × 64 B** (DBS=16, 16 quadletów/blok).
- Blok 64 B: **SPH @ offset 0** (`008c34b1`→`008c36b1`→`008c38b1`, **Δ=512/ramkę**), bajty 4–9 =
  6-bajtowy region nagłówka/markerów (`00000080` stały @ q3, licznik `00000301`++ w okolicy q16),
  **PCM @ offset 10: 18 kanałów × 3 bajty BE** (= 54 B; 10+54=64 ✓).

### Mapa wejść (potwierdzone sygnałem)
| IR ch (0-based) | fizyczne wejście | dowód |
|:---:|---|---|
| **0** | **Mic/Instrument 1** | 250 Hz, max≈2.06M (preamp gain) (`ir_mic1.txt`) |
| **2** | **Analog In 1** | 250 Hz (`ir_analog1.txt`) |
| **3** | **Analog In 2** | 250 Hz (`ir_analog2.txt`) |
| **4** | **Analog In 3** | 250 Hz (`ir_analog3.txt`) |
| **5** | **Analog In 4** | 250 Hz (`ir_analog4.txt`) |
| **6** | **Analog In 5** | 250 Hz (`ir_analog5.txt`) |
| **7** | **Analog In 6** | 250 Hz (`ir_analog6.txt`) |
| **8** | **Analog In 7** | 250 Hz (`ir_analog7.txt`) |
| **9** | **Analog In 8** | 250 Hz (`ir_analog8.txt`) |

**→ CAŁY BLOK ANALOG 1-8 = IR ch2-9 POTWIERDZONY (8/8).**

Pozostałe kanały: szum tła (nic niepodłączone) lub zero (ch 10–15 = wyłączona optyka).

**ch16-17 = stereo bus MONITOR/RETURN (NIE fizyczne wejścia 1:1).** Mirror obserwowany:
Analog1→ch16, Analog2→ch17, Analog3→ch16. Czyli nieparzyste wejścia panoramują w lewo (ch16),
parzyste w prawo (ch17) — to miks CueMix/return, zależny od routingu, **nie kanał wejściowy**.
⚠️ Nie mapować ch16/17 jako wejść fizycznych.

**Porządek:** **ch0 = Mic/Instrument 1 POTWIERDZONE**, ch1 = Mic/Inst 2 (hipoteza),
**ch2-9 = Analog 1-8 POTWIERDZONE 8/8**, ch10-15 = S/PDIF+ADAT (zero przy
wyłączonej optyce), ch16-17 = bus monitor/return (powyżej).

### ➡️ Akcja dla Tahoe
- IR decoder już działa (v117, offset 10) — ten pomiar **potwierdza layout** (18×3B @ off10, SPH Δ=512).
- Mapowanie capture-channel → CoreAudio input: Analog In 1 = IR ch index 2. Uwaga na **Return mirror**
  (ch16) — to nie osobne wejście, to kopia bus-returnu (zależna od „Return Assign").
- Surowy capture: `documentation/raw-captures/2026-06-26_sequoia_ir_analog1.txt`.

## ⭐⭐⭐⭐⭐ PIĄTA PRAWDA — geometria multi-rate (48/44.1/88.2k: lead stały, SPH Δ = okres próbki)

Zmierzone 2026-06-26, Sample Rate przełączany w MOTU Setup (capture: afplay/Spotify). Surowe:
`raw-captures/2026-06-26_sequoia_{it,ir}_{44k,88k,96k,176k}.txt` (+ oryginalne 48k wyżej).
⚠️ 4× (176.4/192k) pakiety >1024 B → snoop trzeba zbudować z `MAX_PKT_BYTES=4096` (`fw_big` na MBP2009).

| metryka | 44.1k | 48k | 88.2k | 96k | 176.4k | 192k | wniosek |
|---|:--:|:--:|:--:|:--:|:--:|:--:|---|
| rodzina | 1× | 1× | 2× | 2× | 4× | 4× | |
| **lead IT** | 3 | 3 | 3 | 3 | 3 | 3 | **RATE-INDEPENDENT** (6 rate'ów) → `kMotuSphPresentationLeadTicks=9216` (3×3072) zawsze (cykl FW=3072 ticków/8000 Hz) |
| **SPH Δ/ramkę** | 557 | 512 | 279 | 256 | 139 | **128** | **Δ = okres 1 próbki @ 24.576 MHz** = `24576000/fs` (557.14/512/278.64/256/139.32/**128**). Niecałkowite → dithering |
| **DBS IT / IR** | 13/16 | 13/16 | 13/16 | 13/16 | 10/13 | **10/13** | 1×/2× pełne (14/18 PCM); **4× REDUKUJE kanały** (IT 13→10, IR 16→13) — bandwidth |
| ramki/pakiet | 8 | 8 | 16 | 16 | 32 | **32** | = 8×rodzina (1×→8, 2×→16, 4×→32) |
| len IT / IR (B) | 424/520 | 424/520 | 840/1032 | 840/1032 | 1288/1672 | 1288/1672 | |
| kadencja data | 68.8% | 75% | 68.8% | 75% | 68.8% | 75% | `8000·frac·frames = fs` |

**Wszystkie 3 rodziny domknięte po 2 punkty — prawa potwierdzone na 6 rate'ach (44.1/48/88.2/96/176.4/192).**

### ➡️ Wniosek dla Tahoe (multi-rate, na przyszłość)
- **Lead 9216 ticków poprawny dla KAŻDEGO rate** (potwierdzone 44.1/48/88.2/96/176.4k — nie skalować z fs).
- **SPH Δ = `round(24576000/fs)`** z ditheringiem reszty (NIE hardcode 512). Dla bieżącego 48k = 512 dokładnie.
- **Liczba kanałów (DBS) zależy od rodziny:** 1×/2× = 13/16 (14/18 PCM); **4× = 10/13** (mniej PCM —
  MOTU redukuje przy 176.4/192k). frames/pkt = 8×rodzina (8/16/32). Wszystkie 6 rate'ów zmierzone.
- Dla bieżącego buga (48k) liczy się tylko kolumna 48k; reszta = pełna mapa drogowa multi-rate.

## Środowisko pomiaru
- MOTU 828mk3, **firmware 1.06, boot 1.01**. MOTU Audio Setup: 48000 / Clock Internal /
  Default Stereo Input=Mic/Instrument 1-2 / Default Stereo Output=Main Out 1-2 / Main Out Assign=Main Out 1-2 /
  Return Assign=Analog 1-2 / optyka A/B Disabled / "Enable Core Audio volume controls" = OFF.
- M3/Sequoia = node-id 3 (SID=3 na drucie). MBP2009/Linux Mint = snoop. LaCie d2 quadra w daisy-chain.

## Setup / pułapki napotkane (do następnego razu)

- **LaCie d2 quadra był wyłączony** → przerywał daisy-chain → Linux widział tylko siebie (`fw0`).
  Po włączeniu LaCie szyna wyliczyła: `fw1`=M3 (Apple), `fw2`=MOTU (OUI `0001f2`), `fw3`=LaCie.
- `quirks=0x10` **nie zadziałał przez samo `modprobe`** (moduł już załadowany od bootu, `quirks 0x0`).
  Trzeba było `modprobe -r firewire_ohci firewire_core …` i załadować ponownie z `quirks=0x10`.
- Snoop kolejkuje 256 buforów per uruchomienie → max ~256 pakietów/run (wystarczy, lead bezrozrzutowy).
- **Parser z handoffa miał zły regex** — linia ma `len=424 ch=.. tag=.. sy=..:` (handoff zakładał
  `len=424:`). Poprawny regex: `cyc=(\d+) len=424 ch=\d+ tag=\d+ sy=\d+:\s+([0-9a-f ]+)`.
- SSH: `-o PreferredAuthentications=password -o PubkeyAuthentication=no` (klucz z passphrase wiesza).
