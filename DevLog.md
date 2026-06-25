# DevLog.md — Historia sesji ASFireWire-dice

Archiwum decyzji, bugów i fixów z sesji pracy nad gałęzią DICE.
Aktywny plan → `Focus.md`

---

## Sesja 2026-06-23 — integracja origin/DICE (c2bdf11): TX exposure naprawiony, zostaje enkoder IT

**Gałąź `integrate-dice-c2bdf11`** (od `dice-motu`), merge commit `fd26d6d` (2 rodziców: `2751ecf` + `c2bdf11`).

### Co zrobione
- Zbadano 66 commitów origin/DICE: 1–28 (do `c2bdf11`) = całe audio/TX/Float32/ZTS; 29–66 = SBP2+MCP
  (niezwiązane). **Audio bajt-identyczne c2bdf11↔tip** → bazowano na `c2bdf11` (zero SBP2/MCP).
- Merge: 4 konflikty (coordinator ×2: nasz `startReceiveBeforeProgram_` + ich teardown-guard; .gitignore union;
  CLAUDE.md ours). Float32 re-port: `DecodeMotuV3Frame`+MOTU path → `float` (`Detail::Signed24ToFloat32`).
- Build OK, C++ 1089/1089. Bity MOTU zachowane (enum/coordinator/profil=18/v34 Start).
- ⚠️ `bump.sh` (z test-only) domknął merge pod nazwą „bump 0.2.118" w trakcie — naprawione `git commit --amend`.

### Wynik testu hardware (v119)
- Pierwszy start: MOTU nie enumerował — `ROMScan/BIB read failed`, `OnTimeout AwaitingAR ackCode=0x4`,
  `S400→S200`. **Transient bus flake** (warstwa async/ROM identyczna z v117; merge tknął tylko
  `ControllerCoreFacades.cpp` 1+/16-). **Restart Maca** naprawił → MOTU enumeruje.
- ✅ **TX EXPOSURE NAPRAWIONY** (upstream `4e1dbc9`): `IT WIRE maxAbs24=5.5M` (v117 było 0),
  `dropouts=0`, realne PCM na drucie. Bug `written=0/withoutPkt` zniknął.
- ❌ **Cisza, brak diod MOTU**: enkoder IT = AM824 (`lastQuad=0x40000000`), MOTU V3 chce MOTU-packed.
  → następny krok: enkoder IT MOTU-packed (SPH+2 MSG+14 PCM 3B@offset 10), patrz Focus.md.

### Discord (kontekst)
mrmidi **wypalony** (#coding 19.06: „burned out, no motivation"). Zespół wspiera bez presji
(Chris Izatt/Alesis, lychzord/Midas Venice **ma dźwięk**, alicankaralar/Venice PR#32). Wysłano mrmidi
wiadomość (#coding): jego TX fix u nas zadziałał + oferta PR MOTU bez pośpiechu. Ton: zero presji.

---

## Sesja 2026-06-22 — ROOT CAUSE ZTS: IR MOTU używa niestandardowego nagłówka (przełom)

### ✅ ROZWIĄZANE i ZWERYFIKOWANE NA HARDWARE (v117, commit `585ea7f`)
Fix wdrożony — osobna ścieżka dekodowania `kMotuV3Packed`:
- `AudioWireFormat::kMotuV3Packed` (nowy), wybierany dla vendora MOTU w `DiceDuplexRestartCoordinator`.
- `RxAudioPacketProcessor`: pomija standardowy CIP, DBS=16, 8 bloków SPH(4B)+2 MSG+18 PCM(3B@offset 10),
  `hasValidCip=true` → ZTS z OHCI HW timestamp (MOTU syt=0xFFFF, więc fallback nie SYT-cadence).
- `DecodeMotuV3Frame`: 18× 3-bajtowe BE → right-justified sign-extended int32.
- `kRxPcmChannels` 16→18. Diag v31/v35/v36 usunięte.

**Dowód z logów v117:** `rxFmt=2`, `inCh=18`, `Core audio hardware ZTS ready sampleFrame=1536`,
`StartIO initial hardware ZTS waitMs=23` (było: timeout 3000 ms), `StartIO super::StartIO ok`,
`DUPLEX ready rxStarted=1 txStarted=1 hasIn=1 hasOut=1`, `streams input(channels=18 bits=24)
output(channels=14 bits=32)`. Zero `ZTS timed out`/`StartIO failed`. C++ 1114/1114.

**Następne:** weryfikacja słyszalności + mapa slotów IR (znany sygnał na wejściu) — patrz Focus.md.

### ❌ Playback NIE gra (osobny bug — TX underexposure) + próba integracji origin/DICE
Test v117 z Spotify: dźwięk nie wychodzi. `[PayloadWriter] written=0 withoutPkt=visited maxAbs=0.0`
→ callback wyjścia odwiedza ramki, ale TX nie eksponuje pakietów IT (underexposure). NIE ZTS.
mrmidi naprawił to upstream (`4e1dbc9 enforce tx frame exposure lead`, `440a297`).

Próba `git merge origin/DICE` (na gałęzi `integrate-dice-upstream`, dice-motu nietknięte) — przerwana,
bo to **RE-PORT nie merge**: origin/DICE jest 66 commitów przed nami, NIE ma plików MOTU, i przepisał
RX na **Float32** (`fc3f46b`) → nasz `DecodeMotuV3Frame(int32_t*)` się nie skompiluje. Merge cofnięty
(`reset --hard`), v117 zachowane jako fallback. Pełny plan re-portu → Focus.md §„🔴 NASTĘPNY DUŻY KROK".

### Ścieżka diagnostyczna (v34→v36)
- **v34**: `Start()` = `kRun|kWake|kIsochHeader`. Potwierdzone na HW: `ctl=0x40008451` (bit30
  isochHeader aktywny), `drainedTotal=54569` — OHCI odbiera mnóstwo pakietów IR. Ale ZTS nadal
  timeout, zero logów CADENCE/QUALIFIED → każdy pakiet odrzucony w `ProcessPacket`.
- **v35**: dump 24 bajtów odrzuconych pakietów. Offset 8 = `0d 04 04 00 22 ff ff ff`, **identyczny
  w każdym pakiecie** (timestampy 0x3aac→0x43ac). Podejrzenie: martwa pamięć.
- **v36**: skan offsetów 4..28 szukający poprawnego CIP (EOH0=0/EOH1=1) — **nie znalazł żadnego**
  sensownego kandydata z DBS≈16. Bajt `0x10` nie występował nigdzie. Ślepa uliczka zgadywania.

### Rozstrzygnięcie — snoop działającego El Capitan (ground-truth)
Wgrano snoop-mode v115 (architektura main) na Tahoe; El Capitan (MB2009) **aktywnie nagrywał** z
MOTU → wymuszone nadawanie IR; M3 pasywnie snoopował ctx=2 kanał TX MOTU (ch=34).

**Wynik:** nagłówek IR (device→host) to **stałe `0d040400 22ffffff`** — `Q1` ma **EOH1=0** →
NIE jest standardowym CIP. Potwierdzone 3 niezależnymi stosami (snoop El Cap, dice, Linux ALSA).
- DATA pakiet = 520 B = 8 (nagłówek) + 8 bloków × 16 quad. **DBS=16, 18 PCM** (nie 16).
- Blok = SPH(4B) + 2 MSG + 18 PCM (×3B). SPH **żywy** (rośnie, synca z `ReadCycleTime`).
- PCM niesie realny sygnał → MOTU NADAJE żywe dane (hipoteza „DBC=0=idle" była błędna).

### Root cause
`RxAudioPacketProcessor::ProcessPacket` → `CIPHeader::Decode(q0,q1)` wymaga **EOH1=1**. IR MOTU ma
EOH1=0 → `nullopt` → `kInvalidRange` → każdy pakiet odrzucony → ZTS nie publikuje → timeout
`0xe00002d6`. **Offset `payload+8`/isochHeader=1 (v34) był poprawny** — bug to wyłącznie walidacja
standardowego CIP na niestandardowym nagłówku IR.

### Fix (do implementacji) + pełne dane
Patrz `Focus.md` §AKTUALNY STAN i `../ASFireWire/documentation/MOTU_V3_WIRE_GROUNDTRUTH.md`
§„✅ IR ground truth — ZEBRANE". Raw capture:
`../ASFireWire/documentation/raw-captures/2026-06-22_el_capitan_snoop_IR_ch34_device-to-host.txt`.

### Uboczne ustalenia tej sesji
- **origin/DICE (mrmidi) 28 commitów przed nami** — `git fetch origin`. Nie rozwiązują tego buga
  (ten sam `RxAudioPacketProcessor`/CIP). Warte integracji później: FW-62 use-after-free guard
  (`0950319`), HAL safety offset 624→128 (`5a0b306`), RX Float32 (`fc3f46b`), ZTS rework (`191302b`).
- **Przeniesiono raw-captures do main** (`../ASFireWire/documentation/raw-captures/`): 6 plików z
  El Cap/Linux/DTrace + nowy IR snoop. Opisane w `MOTU_V3_WIRE_GROUNDTRUTH.md` i `MOTU_828_MK3_BringUp.md`.

---

## Sesja 2026-06-18 (cd.) — v34: fix isochHeader IR (root cause ZTS)

### Diagnoza (z v33 DIAG)
`IR DIAG drainedTotal>0` potwierdziło: **MOTU nadaje IR, OHCI odbiera pakiety.** Ale zero logów
„RX timestamp invalid" mimo 300+ pakietów → `packetAccepted=false` dla wszystkich → cała ścieżka
ZTS (cadence/replay/publish) pomijana w `IsochReceiveContext::Poll()` → `ZTS timed out`.

### Root cause — niespójność OHCI isochHeader vs procesor
`RxAudioPacketProcessor` (host data path dice) jest napisany pod format **IR packet-per-buffer
WITH header/trailer**: `kIsochHeaderSize=8` (CIP na `payload+8`), RX timestamp z `payload[0..3]`.
To wymaga `isochHeader=1` w IRContextControl. Ale „Bug D fix" (v12) ustawił `Start()` na
`kRun|kWake` (isochHeader=**0**, kopiując main) → OHCI pisze bufor **data-only** (CIP na offsecie 0):
procesor czyta offset 8 → garbage → `CIPHeader::Decode` zwraca nullopt → `kInvalidRange` →
`packetAccepted=false` → ZTS martwy.

### Rozstrzygnięcie referencją — OHCI 1.1 spec (nie sterownik!)
Pobrana oficjalna spec OHCI 1.1 (IBM `ohci092.pdf`). **Figure 10-5** (IRContextControl):
bit 31=bufferFill, **bit 30=isochHeader**, bit 29=cycleMatchEnable, bit 28=multiChanMode.
**Figure 10-11 / §10.6.2.1** (IR with header/trailer, bufferFill=0 isochHeader=1) — każdy pakiet
poprzedzony 8 bajtami:
```
quadlet 0: [xferStatus(INVALID):16][timeStamp:16]   ← per-packet HW cycle stamp (cycleSeconds[2:0]<<13 | cycleCount[12:0])
quadlet 1: [dataLength:16][tag:2][chanNum:6][tcode:4][sy:4]
quadlet 2+: isochronous data (CIP...)
```
= dokładnie `kIsochHeaderSize=8` + timestamp w `payload[0..3]` bits[15:0]. **Procesor był poprawny;
zepsuta była konfiguracja kontekstu OHCI.** §10.6.3 (isochHeader=0) = bufor data-only, CIP offset 0
(to jest main + `kIsochHeaderSize=0`, timing z CIP SYT). Deskryptor IR INPUT_LAST status word =
`[xferStatus:16][resCount:16]` (§10.2.2.1) — `DrainCompleted` czyta poprawnie; timestamp NIE jest
w deskryptorze, tylko w buforze (przy isochHeader=1).

**Korekta dla potomności:** main DevLog „Fix E" twierdził „bit 30 = cycleMatchEnable" — to BŁĄD,
bit 30 = isochHeader, bit 29 = cycleMatchEnable. main miał `kRun|kIsochHeader` + `kIsochHeaderSize=0`
(niespójne, CIP na 8 czytany jako 0) → „naprawił" przez isochHeader=0 (spójne dla *ich* offsetu 0).
Fix main był poprawny dla main, ale diagnoza bitu zła.

### Fix (v34) — `IsochReceiveContext::Start()` (~linia 108)
```cpp
const uint32_t ctlValue = kRun | kWake | kIsochHeader;   // były potrzebne WSZYSTKIE trzy
```
Bug D dodał kWake (DMA startuje) ale wyrzucił isochHeader → poprawka łączy oba. Daje **prawdziwy
per-packet hardware timestamp** (cycle-granular, omija tylko 12-bit cycleOffset — najlepsze dostępne
źródło, zgodne z designem ZTS dice). Zero zmian w procesorze. Naprawia ścieżkę RX dla **wszystkich
kart** (obecny stan isochHeader=0 + procesor offset=8 był zepsuty dla każdej, nie tylko MOTU) —
nie narusza architektury mrmidi, czyni ją spójną. Build v34 `--clean --deploy`, kompiluje czysto.

> ⚠️ Świadoma różnica vs main: dice=isochHeader=1+offset8+HW timestamp, main=isochHeader=0+offset0+
> CIP SYT. To NIE przeszczep z main — to naprawa dice pod jego własny design ZTS. MOTU V3 wysyła
> SYT=0xFFFF (brak cadence) → ścieżka SYT-only main nie zadziałałaby dla MOTU; HW timestamp tak.

### Następny krok
Test hardware v34. Jeśli `drainedTotal>0` + brak `timed out` + ZTS publikowany → fix trafiony,
usunąć DIAG z Poll()/Stop(). Jeśli CIP dekoduje ale timestamp zły → weryfikować endianness
`DecodeReceiveTimestamp` vs format OHCI (payload[0..3] LE, bits[15:0]).

---

## Sesja 2026-06-15 — ZTS debug (v9–v11)

### Kontekst

Gałąź DICE to nowa architektura (AudioDriverKit-native, bez manual start/stop Isoch).
ZTS (Zero Timestamp) = mechanizm AudioDriverKit: `WaitForInitialHardwareZts` musi dostać
`UpdateCurrentZeroTimestamp` w ciągu 500ms od `StartIO`. Jeśli IR DMA nie dostarczy pakietu,
ZTS timeout (0xe00002d6) i CoreAudio blokuje cały IO.

### Bug C (naprawiony v9) — Brama SYT blokowała IT

**Symptom:** muzyka przez MOTU nie grała. W logach: IT startowało, po 3000ms ciszało.

**Root cause:** `IsochReceiveContext.cpp` miał bramę SYT: sprawdzało `syt != 0xFFFF`
przed opublikowaniem ZTS. MOTU V3 **zawsze** nadaje `syt=0x0000` (nie standard 0xFFFF).
Brama nigdy nie przepuszczała → ZTS timeout → IT zatrzymywano.

**Fix (v9):** Dodano fallback SYT w `IsochReceiveContext.cpp` (~linia 488):
```cpp
if (!rxClockEstablished && hasHardwareTimestamp && packetReceiveHostTicks != 0) {
    rxClockEstablished = true;
}
```
Bypass dla MOTU V3 — jeśli jest timestamp sprzętowy, opublikuj ZTS niezależnie od SYT.

### Bug B (nienaprawiony v10, naprawiony v11) — Geometry check AM824 odrzucał IR

**Symptom:** po v9 ZTS nadal timeout. Log: `inCh=18`, ale IR nadal 0 pkt/s.

**Root cause:** `RxAudioPacketProcessor::ProcessPacket` (~linia 68):
```cpp
if (cip->dataBlockSize < channels) {  // 16 < 18 = TRUE → kInvalidRange → pakiet odrzucony
```
MOTU V3 DBS=16, ale nasza geometria `inputChannelCount=18` → KAŻDY pakiet IR odrzucany.

**v10 — FIX W ZŁYM PLIKU (brak efektu):**
Zmieniono `kHostInputPcm = 16u` w `MOTUVendorProtocol.hpp`.
Brak efektu — `inputChannelCount` pochodzi z `AudioProfileRegistry::FindProfile()→RxChannelCount()`
czyli `MOTU828Mk3Profile::kRxPcmChannels` (plik `MOTU828Mk3Profile.cpp`), NIE z `kHostInputPcm`.
Log nadal pokazywał `inCh=18`.

**v11 — FIX W PRAWIDŁOWYM PLIKU:**
Zmieniono `kRxPcmChannels = 16` (było 18) w `MOTU828Mk3Profile.cpp`:
```cpp
constexpr uint32_t kRxPcmChannels = 16; // QUICK FIX: should be 18 — see TODO above
```
Log potwierdził: `DICE DUPLEX START ... inCh=16`.
AM824 check: `16 < 16 = false` → pakiety IR mogą przejść.

**Dodatkowy problem v11 — incremental build miss:**
Xcode nie rekompilował `MOTU828Mk3Profile.cpp` — iCloud File Provider zachowuje timestamps.
Fix: `rm -rf /tmp/ASFWBuild` → pełny clean rebuild → CFBundleVersion=8 → `inCh=16` ✅.

### Bug D (naprawiony v12) — IR DMA nigdy nie startował (brak bitu kWake)

**Symptom:** po v11 (geometry OK, `inCh=16`) ZTS nadal timeout. Log: `Arming direct Rx`,
potem 500ms ZERO wpisów IR, `ZTS timed out (0xe00002d6)`. `DrainCompleted()` = 0 cały czas.
Potwierdzone na świeżo na **CFBundleVersion=11** (`systemextensionsctl list` = 11) — to nie był
„duch starej wersji", lecz realny stan kodu.

**Root cause:** `IsochReceiveContext::Start()` (dice) programował kontekst OHCI IR bitami
`kRun | kIsochHeader`, podczas gdy działający main pisze `kRun | kWake`:
- **Brak `kWake` (bit 12):** bez WAKE kontekst OHCI „zbroi się", ale DMA nigdy nie rusza od
  `CommandPtr` → zero ukończonych deskryptorów. Linux pisze `CONTEXT_RUN | CONTEXT_WAKE` przy starcie.
- **Ustawiony bit 30:** dla IR to `isochHeader` (dice nazywa go `kIsochHeader`, main nazywa ten sam
  bit `kCycleMatchEnable` — etykieta IT-centryczna). Ring (`IsochRxDmaRing::SetupRings`, identyczny
  z main: INPUT_LAST, reqCount=4096, Z=1) **nie rezerwuje** 4 bajtów na nagłówek isoch → przesunąłby CIP.

**Fix (v12):** `IsochReceiveContext.cpp:108` — `ctlValue = kRun | kWake` (było `kRun | kIsochHeader`),
+ port readback i dead-check z main (log `Start: Readback ...` potwierdza, że kontekst wstał;
return `kIOReturnNotPermitted` gdy `kDead`). Plik: [`IsochReceiveContext.cpp`](ASFWDriver/Isoch/Receive/IsochReceiveContext.cpp:108).

> 🔑 **Lekcja procesowa:** ta klasa istnieje też w działającym main (`../ASFireWire`). Problem był
> tam już rozwiązany (komentarz w main `Start()`: „matching Linux CONTEXT_RUN | CONTEXT_WAKE").
> Porównanie z main od razu wskazało regresję. → Reguła w CLAUDE.md: zawsze sprawdzaj main.

**Stan:** zmiana wprowadzona, build v12 (do weryfikacji hardware — czy log pokazuje
`DrainCompleted > 0` i ZTS publikowany).

---

## Sesja 2026-06-18 — IT/IR deadlock: CIP format + kolejność startu (v26–v33)

### Kontekst
v25 (IRM non-fatal) wciąż nie grał. Symptom przez całą sesję niezmienny:
`ZTS timed out after 3000 ms`, `replay=0`, `IT WIRE final data=20k+` (IT leci),
**zero odebranych pakietów IR**. MOTU nie nadaje IR (`seq=0`).

### Co zmieniono (kolejno)
- **v26** — hipoteza „IT wysyła tylko NO-DATA": wymuszono DATA z zerowym PCM w prefill +
  pre-replay (`ASFWAudioDriverZts.cpp`). Efekt: `wireDataPackets` wzrosło z 0 do ~20k. MOTU dalej cisza.
- **v27→v28** — CIP **SPH=1** + **FDF=0x22** dla MOTU (`MOTU828Mk3Profile.cpp`, nowe pole
  `sph` w `AmdtpStreamConfig`/`DiceStreamConfig`, propagacja `DiceTxStreamEngine`,
  `AmdtpTxPacketizer` używa `config.sph` zamiast hardkodu). v27 błędnie zmienił `kTxPcmChannels`
  14→12 i payload-writer offset — **cofnięte** (MOTU pakuje PCM 24-bit, 14 kanałów, SPH nie zjada
  kanału; kanon = 14). Dalej cisza.
- **v29** — **kolejność startu**: host IR DMA startuje PRZED device ProgramRx/ProgramTx (jak main
  `MOTUAudioBackend`). Quirk `startHostReceiveBeforeDeviceProgram` (default false, true tylko MOTU)
  w `DiceQuirks.hpp`; setter w `DiceDuplexRestartCoordinator` + wstrzyknięcie w `DiceAudioBackend`.
  Log potwierdził odwróconą kolejność (`Starting prepared IR` przed `ProgramRx`). Dalej cisza.
- **v30** — **CIP FMT=0x02** (był standardowy AM824 `0x10`). Po tym CIP Q1 = `0x8222ffff`
  **byte-match wire**. Dalej cisza.
- **v33** — build **diagnostyczny** (nie naprawczy): bezwarunkowe liczniki IR w
  `IsochReceiveContext::Poll()`/`Stop()` (`IR DIAG poll#... processed=... drainedTotal=... ctl=`)
  by rozstrzygnąć MOTU-silent vs dice-not-draining vs interrupt-not-firing. **Czeka na wynik.**
  (v31/v32 zjedzone przez bump z `--test-only`.)

### Ground-truth CIP host→device (zweryfikowane bajt-po-bajcie)
Wire (El Cap snoop + Linux tracepoint, kanon `../ASFireWire/.../MOTU_V3_WIRE_GROUNDTRUTH.md`):
`CIP Q0 byte2=0x04`, `CIP Q1=0x8222ffff`. Stąd: **SPH=1, QPC=0, FMT=0x02, FDF=0x22, SYT=0xFFFF, DBS=13**.
⚠️ **Tabela w `MOTU_V3_WIRE_GROUNDTRUTH.md` ma błędną interpretację bitów** (pisze „QPC=1, SPH=0") —
surowy bajt `0x04` to bit10=SPH=1, QPC=0. Drut = wyrocznia, nie tabela. v30 dice produkuje
dokładnie `0x8222ffff`.

### Co WYKLUCZONO (twardo, by nie wracać)
Device-facing protokół MOTU w dice jest **w pełni zweryfikowany jako identyczny z działającym main**:
- CIP (SPH/FMT/FDF/DBS/SYT/rozmiar 424) — byte-match wire ✅
- Rejestry MOTU: offsety + wartości + kolejność (`0x0b00/0b04/0b10/0b14/0c04`, `0xffc10001`,
  PACKET_FORMAT `0x02`, ROUTE_PORT `0x100`, FETCH `0x02000000`, shift 24/16, deactivate→activate) ✅
- Writes docierają z `rcode=Complete` (`OnARResponse ... rcode=0x0`), gen poprawna ✅
- IR DMA context zdrowy: readback `Ctl=0x9400` (Run|Wake|Active, nie Dead), `Match=0xf0000000` (ch0) ✅
- IRM: `IRMClient` współdzielony z main (local CSR), CAS success ✅

**Wniosek:** problem NIE leży w warstwie device-facing/protokołu (wyczerpana). Pozostają dwie
hipotezy, które rozstrzyga v33: (a) **host-side IR receive path** (dice-specific DMA/DirectIO,
per CLAUDE.md RÓŻNY od main) — context „aktywny" ale nie drenuje / interrupt nie strzela; lub
(b) **MOTU faktycznie nie nadaje** z powodu niewidocznego w logach dice (wtedy krok 2 =
porównanie z żywym main na tym samym MOTU, który tam realnie gra).

### Uboczne
- **Pre-existing luka testów naprawiona:** `tests/audio/CMakeLists.txt` — `DiceProfileTests`
  referował `gMotu828Mk3Profile` przez registry, ale nie linkował `MOTU828Mk3Profile.cpp`
  (undefined vtable → cały target się nie budował). Dodano brakujące źródło. Po tym **1114/1114
  testów C++ przechodzi** (potwierdza brak regresji zmian tej sesji).
- Wszystkie zmiany behawioralne są **bramkowane per-urządzenie** (nowe pola default
  `false`/`0x10`, nadpisuje tylko `MOTU828Mk3Profile`); wspólny `RunDuplexStart` dla `false`
  odtwarza 1:1 poprzedni przepływ → architektura mrmidi pod Generic/Saffire nienaruszona.

**Stan:** niezacommitowane (czeka na rozstrzygnięcie hardware). v33 na Desktopie.

---

## Infrastruktura build

### ✅ Version-bump bug — naprawiony 2026-06-15 (port z main)

`build.sh:267` wołał `./bump.sh` **bez argumentu** → `refresh` → wersja nie rosła → macOS pomijał
upgrade dextu. Dodatkowo `bump.sh` nie syncował pbxproj ani nie commitował → iCloud sync mógł cofnąć bump.
Dowód: pbxproj `CURRENT_PROJECT_VERSION=4`, VERSION.txt=0.2.0, a zainstalowany dext = 8.

**Naprawa (3 zmiany, zwalidowane na żywo — `./bump.sh patch` → 0.2.9, pbxproj=9, auto-commit):**
1. `build.sh:267` → `./bump.sh patch` (było `./bump.sh`).
2. `bump.sh` `generate_version_header()` → sync `CURRENT_PROJECT_VERSION` w pbxproj do patch z VERSION.txt.
3. `bump.sh` `main()` → auto-commit VERSION.txt+pbxproj po bumpie (skip gdy `SRCROOT` = wywołanie z Xcode).

VERSION.txt podniesiony do 0.2.9-audio (zainstalowane było 8) by uniknąć regresji wersji.
Uwaga: `bump.sh` synca pbxproj do patch z VERSION.txt — trzymaj VERSION.txt patch > zainstalowana wersja.

### iCloud Drive + DerivedData + deploy (port z main, 2026-06-15)

Build zawsze poza iCloud: `--derived /tmp/ASFWBuild`. Dodano flagi `--clean` (rm DerivedData) i
`--deploy` (funkcja `deploy_app()`: kopia do /tmp → strip xattr → sign dext+app → verify →
Desktop/`ASFW_dice_vNN.app`) — przeniesione 1:1 z main, nazwa pliku z prefiksem `_dice_`.

### Remote SSH (2026-06-15)

Przełączono dice remote z HTTPS+token (skompromitowany, usunięty z GitHub) na SSH:
`cube666 = git@github.com:cube666999/ASFireWire-by-cube666999.git`, `origin = git@github.com:mrmidi/ASFireWire.git`.
Push: `git push cube666 dice-motu`.

---

## Decyzje architektoniczne (dice vs. main)

| Aspekt | main branch (zero-copy) | dice branch |
|--------|------------------------|-------------|
| Start/Stop IR/IT | Ręczny (UI) | AudioDriverKit automatyczny |
| ZTS | Symulowany przez timer | Hardware (IR DMA → UpdateCurrentZeroTimestamp) |
| Encoder | PacketAssembler.hpp (MOTU V3 native) | AM824 (TODO: wymienić na MOTU V3) |
| Geometry | kRxPcmChannels=18, bypassed check | kRxPcmChannels=16 (quick fix), 18 cel |
| Model danych | Ring-buffer indirect copy | IOBufferMemoryDescriptor — **zaimplementowane** (jeden bufor/kierunek współdzielony nub ↔ driver ↔ `IOUserAudioStream` ↔ isoch DMA; `AudioEndpointRuntime` + `CopyDirectAudioMemory`) |

---

## Pliki kluczowe (dice branch)

| Plik | Rola w ZTS debug |
|------|-----------------|
| `ASFWDriver/Audio/DriverKit/Config/DICE/Isoch/Profiles/MOTU828Mk3Profile.cpp` | `kRxPcmChannels` = źródło `inputChannelCount` |
| `ASFWDriver/Audio/DriverKit/ASFWAudioDriverGraph.cpp` linia ~166 | `profile->RxChannelCount()` override |
| `ASFWDriver/Isoch/Receive/IsochReceiveContext.cpp` | IR DMA, `DrainCompleted()`, `Poll()` |
| `ASFWDriver/Audio/Engine/Direct/Rx/RxAudioPacketProcessor.cpp` linia ~68 | AM824 geometry check |
| `ASFWDriver/Audio/Protocols/Vendor/MOTU/MOTUVendorProtocol.hpp` | `kHostInputPcm` (nieistotny dla geometrii) |

---

## 2026-06-24 — ROOT CAUSE ciszy MOTU: IT nadaje na złym kanale (ch0 zamiast ch1)

**Objaw:** od v117 (ZTS/duplex up) MOTU we WSZYSTKICH wersjach milczy i nie mruga na wyjściu, mimo
że IR działa (ZTS żyje). Sesja: v124→v128.

**Droga diagnostyczna (co wykluczyliśmy — wszystko fałszywe tropy):**
1. **v124/v125 — SPH seed.** Hipoteza: MOTU odrzuca przez stale/zerowy SPH. v124 seed-once-free-run,
   v125 seed z żywego `clockPair`. Log `[MotuSph]`: ct rośnie, seedSph rośnie, format OK. Cisza.
2. **v125 — exposure stall.** `[PayloadWriter]`: `written` zamrożone, `withoutPkt` rośnie, deficit
   rośnie → wyglądało na under-exposure. Dodano `[TxPump]` (`ASFWAudioDriverZts::PrepareTransmitSlots`).
   v126 hardware: **exposure ZDROWA** (data/noData=0.75, realtime, demand-bound) — stall z v125 był
   stanem przejściowym starej sesji, NIE blockerem.
3. **v127 — lead SPH +2→−5** (El Cap `aheadHw=-5`). Cisza → lead to nie problem.
4. **v128 — `[WIRE16]`** (`IsochTxDmaRing::GaugeWirePayload`, zrzut 6 quadletów nadanego pakietu).
   Bajty zgodne z El Cap: q0=`000d04xx` (DBS=13, SID=0), q1=`8222ffff`, SPH free-run seconds=0,
   PCM na block byte 10 (slot 0 = Main L). Nagłówek isoch builder: tag=1/sy=0/S400 — też OK.
   **Wszystkie bajty poprawne, a cisza** → blocker o warstwę niżej.

**ROOT CAUSE (dowód z kabla, MB2009 Linux Mint `fw_isoch_snoop`, 2026-06-24):**
Snoop ch1 = **0 pakietów**. Snoop ch0 = nasz IT (DBS=13, `000d04xx 8222ffff`) **razem z** IR MOTU
(DBS=16, `0d040400`). **Nasz IT leci na ch0, MOTU słucha wejścia na ch1** → nigdy nie odbiera.

Kod: [`ASFWAudioDevice.cpp:205`](ASFWDriver/Audio/DriverKit/ASFWAudioDevice.cpp:205)
`txSlotProvider.isoChannel = txConfig.sid;` — kanał IT ustawiony na **sid (0)** zamiast przydzielonego
**hostToDeviceIsoChannel (1)**. `isoChannel` → nagłówek isoch (`ASFWAudioDriverPrivate.hpp:142`).
Pułapka: `IsochService::PrepareTransmit(channel=1)` → `IsochTransmitContext::SetChannel(1)` ustawia
`channel_` ringu, które **nie jest czytane nigdzie** — realny drut buduje ścieżka ADK `txSlotProvider`.
Stąd „Prepared IT on channel 1" w logach, a na drucie ch0.

**Fix — ZWERYFIKOWANY (v132, 2026-06-25):** kanał IT jest rezerwowany dopiero w `StartAudioStreaming`
(po `AllocateTxIsochResources`+prefill), więc czytanie przy alokacji daje 0xFF. Rozwiązanie: nowy getter
`ASFWAudioNub::GetTxIsochChannel` → `IsochService::PlaybackChannel()` (`reserved_.playbackChannel`),
wołany **po** `StartAudioStreaming`, stempluje `txSlotProvider.isoChannel` na steady-state pakiety
(prefill zostaje na placeholderze=sid, nieszkodliwy cichy lap). MOTU używa `AVCAudioBackend`
(`kDefaultItChannel=1`). **Dowód z hardware:** po włączeniu muzyki MOTU **zapala diody** (lock na
strumień na właściwym kanale) — root cause ciszy potwierdzony i naprawiony. Pliki: `IsochService.hpp`
(getter), `ASFWAudioNub.iig`+`.cpp` (GetTxIsochChannel), `ASFWAudioDevice.cpp` (stempel po starcie).

**Problem wtórny (po fixie kanału): pisk + wędrujące diody (Analog7/Digital/MainR).** Strumień płynie,
WIRE16 potwierdza PCM na slotach 0/1 (Main L/R) i SPH free-run poprawne — czyli bufor OK, problem to
timing odbioru. **Ustalenie z porównania z main (v133):** nasz seed SPH miał `clockTicks +
packetsAhead*3072 + lead` — projekcję **~300 cykli do przodu**. Main `PacketAssembler::
writeMotuV3SphAndAdvance` **NIE projektuje** — seeduje `clockTicks + 2*3072` (clockPair ct jest
odświeżany co Refill ≈ czas transmisji, jak main `currentCycleTime_`). Projekcja +300 wpychała SPH
~37 ms w przyszłość MOTU → przebuforowanie → resync (pisk) + obrót fazy bloków (wędrujące diody).
v133: usunięto projekcję, lead −5→+2 (mirror main; −5 było z El Cap **IR**, zły kierunek, ustawione
gdy kanał był zepsuty → nietestowane). **Status: v133 do testu hardware.**

**Metoda snoop MB2009 (do powtórzenia):** `tools/fw_isoch_snoop.c` (main repo) na MacBook 2009.
SSH wymaga `-o PreferredAuthentications=password -o PubkeyAuthentication=no` (klucz id_rsa ma passphrase
→ inaczej wisi). FW: `sudo modprobe firewire_ohci quirks=0x10` (LSI FW643 + MSI). Przed snoopem
`sudo modprobe -r snd_firewire_motu snd_firewire_lib`. Topologia: M3 ↔ LaCie ↔ MOTU ↔ MB2009 (jedna
szyna — MB2009 widzi fw1=M3/Apple, fw2=MOTU, fw3=LaCie). ⚠️ załadowanie modułu robi bus reset → M3
re-inicjalizuje duplex (przeżywa). Build snoopa: `gcc -O2 -o /tmp/fw_isoch_snoop /tmp/fw_isoch_snoop.c`,
uruchom `sudo /tmp/fw_isoch_snoop /dev/fw0 <kanał> <N>`.

**Lekcja:** przy „idealne bajty + zero reakcji urządzenia" sprawdzaj adresowanie (kanał/plug) ZANIM
iterujesz timing. Jeden pasywny snoop z kabla rozstrzygnął to, czego 4 buildy SPH nie tknęły.
