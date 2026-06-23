# Focus.md — Plan pracy nad ASFireWire-dice

Cel: MOTU 828 MK3 działający przez sterownik DICE (nowa architektura, AudioDriverKit-native).

Archiwum ukończonych sesji → `DevLog.md`

---

## 🟢 ZACZNIJ TU — pierwsza świeża sesja (uruchom z katalogu dice!)

1. **Uruchom `claude` z `ASFireWire-dice/`** (nie z `ASFireWire/`) — wtedy ten CLAUDE.md +
   indeks CodeGraph dice ładują się automatycznie. Zatwierdź MCP „codegraph" (opcja 2) jeśli pyta.
2. **Gałąź robocza = `integrate-dice-c2bdf11`** (NIE `dice-motu` — to fallback v117). Sprawdź:
   ```bash
   git branch --show-current     # ma być integrate-dice-c2bdf11
   git log --oneline -1          # 4d7927f (lub nowszy)
   ```
3. **Aktualny stan: v119** — integracja upstream + TX-exposure działa, ale **brak dźwięku**
   (enkoder IT = AM824, MOTU chce MOTU-packed). Następny krok i pełny kontekst → sekcja
   „🟢 INTEGRACJA OK + TX EXPOSURE NAPRAWIONY" niżej.
4. **Build hardware-test** (gdy zmienisz kod enkodera IT): `./build.sh --derived /tmp/ASFWBuild --clean --deploy`
   (VERSION.txt już >119; macOS przyjmie). Potwierdź `systemextensionsctl list` = nowa wersja przed testem.
5. Logi (⚠️ `grep "ASFWDriver.dext"`, NIE `--predicate senderImagePath`):
   ```bash
   /usr/bin/log show --last 2m --debug --info 2>/dev/null | grep "ASFWDriver.dext" | grep -E "IT WIRE|maxAbs|zeroPcm|lastQuad|StartIO|ZTS"
   ```

> ℹ️ **Infrastruktura naprawiona 2026-06-15:** version-bump (build.sh→`bump.sh patch`, sync pbxproj,
> auto-commit), deploy (`--deploy`/`--clean`), SSH remotes. Od teraz `./build.sh --derived /tmp/ASFWBuild --deploy`
> daje deterministycznie rosnącą wersję → koniec z „duchem starej wersji". Szczegóły: DevLog.md.

---

## ⚡ AKTUALNY STAN — ✅ ZTS NAPRAWIONY (v117, 2026-06-22): MOTU startuje, StartIO OK

> **PRZEŁOM.** Po v9–v117 — `ZTS timed out` zniknął, `StartIO` przechodzi, duplex wstaje.
> Fix `585ea7f` (dice-motu, pushnięty). Pełny opis → DevLog 2026-06-22.
>
> ### Dowód z hardware (v117)
> ```
> DICE DUPLEX START ... rxFmt=2 (kMotuV3Packed)   ← nowa ścieżka aktywna
> IR: ... Arming direct Rx ... inCh=18             ← 18 kanałów IR
> Core audio hardware ZTS ready sampleFrame=1536   ← ZTS PUBLIKUJE
> StartIO initial hardware ZTS ... waitMs=23       ← 23 ms (było: timeout 3000 ms)
> StartIO super::StartIO ok / DUPLEX ready rxStarted=1 txStarted=1 hasIn=1 hasOut=1
> streams input(active=1 channels=18 bits=24) output(active=1 channels=14 bits=32)
> ```
> Zero `ZTS timed out`, zero `StartIO failed`. C++ testy 1114/1114.
>
> ### Co naprawiono (root cause)
> Nagłówek IR MOTU to stałe `0d040400 22ffffff` (EOH1=0 → NIE standardowy CIP) → `CIPHeader::Decode`
> odrzucał każdy pakiet → ZTS nie publikował. Fix = osobna ścieżka `kMotuV3Packed`
> (`RxAudioPacketProcessor` + `DecodeMotuV3Frame`), stały DBS=16, 18 PCM (3-bajtowe chunki, offset 10),
> ZTS z OHCI HW timestamp (isochHeader=1). `kRxPcmChannels` 16→18. Diag v31/v35/v36 usunięte.
>
> ### 🔬 STAN PO v117 (test hardware 2026-06-22)
> - ✅ **ZTS/StartIO/duplex up** — device startuje (cel sesji osiągnięty).
> - ❌ **Playback NIE gra** — `[PayloadWriter] written=0 withoutPkt=visited maxAbs=0.0`: callback
>   wyjścia odwiedza ramki, ale TX nie eksponuje pakietów (**TX underexposure**). To NIE ZTS —
>   osobny bug ścieżki wyjścia (IT). Routing OK (callback woła), CoreAudio dostarcza, ale nic nie
>   ląduje na drucie. → naprawione upstream (`4e1dbc9`), patrz sekcja „integracja origin/DICE" niżej.
> - ⏳ **Input quality** — niezweryfikowane (do testu po naprawie TX): czy 18 slotów IR mapuje się na
>   właściwe wejścia (mapa slotów IR do zebrania znanym sygnałem, Virus TI → Analog In 1).
>
> **DECYZJA użytkownika 2026-06-22:** integrować origin/DICE (TX fix). To RE-PORT na Float32, nie
> merge — patrz sekcja „🔴 NASTĘPNY DUŻY KROK" niżej z gotowym planem.

### 🟢 ENKODER IT MOTU-PACKED ZAIMPLEMENTOWANY (v121) — DO TESTU HARDWARE

**ZACZNIJ TU w nowej sesji.** Gałąź `integrate-dice-c2bdf11` (fork `cube666`), **v121** — enkoder IT
MOTU-packed napisany, build OK, C++ 1089/1089. **Czeka na test hardware: czy MOTU gra + diody.**

**Stan bazowy (v119, hardware 2026-06-23):**
- ✅ Integracja `c2bdf11`: ZTS publikuje, StartIO OK, duplex up.
- ✅ TX exposure (upstream `4e1dbc9`): `IT WIRE maxAbs24=5.5M` → realne PCM leci na drut.
- ❌ Cisza: enkoder IT wysyłał **AM824** (`lastQuad=0x40000000`), MOTU V3 chce MOTU-packed.

**🔧 Fix v121 — enkoder IT MOTU-packed (7 plików, decyzja użytkownika: ground-truth z kabla):**
Wartości z kabla (El Cap + Linux), NIE z kodu main (main miał DBS=16/FMT=0x10/slot10 — niezsynced
z własnym ground-truthem). Profil MOTU już miał `dbs=13/pcm=14/fmt=0x02/fdf=0x22/sph=true`; dodano:
1. `AmdtpTypes.hpp` — `PcmSlotEncoding::MotuV3Packed`; pola `motuSphBaseTicks/motuSphValid` w `AmdtpTimingState`.
2. `PcmSlotCodec.cpp` — case MotuV3Packed (exhaustive switch).
3. `AmdtpPayloadWriter.cpp` — branch MOTU: PCM 3-bajtowe BE @ byte `10+ch*3` (`Float32ToSigned24`), SPH/MSG nietknięte.
4. `AmdtpTxPacketizer.{hpp,cpp}` — `WriteMotuSph` (SPH = `outputPresentationTicks`+512/ramkę, cyc<<12|off), pomija AM824 non-audio sloty dla MOTU.
5. `DiceTxStreamEngine.cpp` (Direct/Tx) — `BuildTxPolicy`: `kMotuV3Packed → MotuV3Packed`.
6. `MOTU828Mk3Profile.cpp` — `quirks.tx.hostToDevicePcmEncoding = kMotuV3Packed`.
7. `ASFWAudioDriverZts.cpp` — `timing.motuSphBaseTicks = outputPresentationTicks` (replay path).

**SPH = mirror intencji main Fix 62** (stale SPH → MOTU odrzuca ramki), ale z dice tick-anchorem
(`outputPresentationTicks` = packetAnchor + sytOffset + txDelay) zamiast free-running cursora main.

**🔬 TEST v121:** zainstaluj `~/Desktop/ASFW_dice_v121.app`, `systemextensionsctl list`=121, zagraj
przez MOTU. Oczekiwane: **diody ANALOG OUT/Main się zapalają + słychać** na Main Out 1/2. Logi:
```bash
/usr/bin/log show --last 2m --debug --info 2>/dev/null | grep "ASFWDriver.dext" | grep -E "IT WIRE|maxAbs|StartIO|TxPrep|sph"
```
Jeśli cisza mimo `maxAbs>0`: podejrzenie SPH (sprawdź czy `motuSphValid` ustawiane — replay path) lub
slot map (slot 0/1 vs main twierdzi 10/11 — wtedy zmień `kMotuV3OutputSlotBase` analogicznie do main Fix 74).

**Topologia / fallbacki (oba na forku `cube666`):**
- `integrate-dice-c2bdf11` @ `4d7927f` (v119) — AKTYWNA, integracja + TX-exposure, brak enkodera IT.
- `dice-motu` @ `2751ecf` (v117, ZTS fix `585ea7f`) — fallback. Powrót: `git checkout dice-motu`.
- Po dźwięku: merge `integrate-dice-c2bdf11` → `dice-motu` + opcjonalny PR MOTU do `origin/DICE`.

**Discord (kontekst zespołu, 2026-06-23):** mrmidi **wypalony** („burned out, no motivation", #coding 19.06)
— zespół (Chris Izatt/Alesis, lychzord/Midas Venice ma dźwięk!, alicankaralar/Venice PR#32) wspiera bez
presji. Wysłaliśmy mrmidi wiadomość (#coding) z dobrymi wieściami (jego TX fix u nas zadziałał) + ofertą
PR MOTU bez pośpiechu. **Ton wobec mrmidi: zero presji, wsparcie.**

#### (kontekst) Dlaczego integracja — TX underexposure

**Dlaczego:** playback (wyjście IT) NIE gra mimo działającego ZTS. `[PayloadWriter]` log:
`written=0 withoutPkt=visited maxAbs=0.0` → callback wyjścia odwiedza ramki, ale nie znajduje
pakietu IT (TX **underexposure**). To NIE ZTS. mrmidi naprawił tę klasę upstream:
`4e1dbc9 fix(audio): enforce tx frame exposure lead`, `440a297 surface tx underexposure telemetry`.

**Stan rozjazdu (`git fetch origin`, 2026-06-22):** merge-base `d200603`. **origin/DICE jest 66
commitów przed nami** (rośnie — był 28), my 50 przed nim. ⚠️ **origin/DICE NIE MA ŻADNYCH plików
MOTU** — całe wsparcie MOTU to wyłącznie nasz dodatek.

**❗ Próba `git merge origin/DICE` (2026-06-22) — przerwana, to RE-PORT:**
- Konflikty tekstowe małe (4): `.gitignore`, `DiceDuplexRestartCoordinator.cpp/.hpp`, `CLAUDE.md`.
- **ALE upstream przepisał RX na Float32** (`fc3f46b`): `DecodeDirectRxFrame(... float* out)`,
  `writer_.Frame()` zwraca **`float*`**. Nasz `DecodeMotuV3Frame(int32_t*)` i ścieżka MOTU
  (`int32_t* frameOut`) **nie skompilują się** — trzeba przerobić dekoder MOTU na **float**
  (`Detail::Signed24ToFloat32` na 3-bajtowych próbkach; konwencja jest już w upstream
  `DirectRxPacketDecoder.hpp`).
- Auto-merge „przeszedł" w `IsochReceiveContext.cpp` ale to NIE znaczy semantycznie poprawnie —
  trzeba zweryfikować nasz v34 `kRun|kWake|kIsochHeader` Start() vs upstream ZTS rework (`191302b`
  usunął `ztsProjector_`).

**Plan re-portu (do świeżej sesji, hardware iteration):**
1. Gałąź integracyjna z dice-motu (v117 zostaje fallbackiem — zweryfikowane, pushnięte).
2. `git merge origin/DICE`; rozwiąż 4 konflikty (coordinator = nasz `kMotuV3Packed` branch + ich zmiany).
3. **Przerób ścieżkę MOTU IR na Float32**: `DecodeMotuV3Frame` → float, `RxAudioPacketProcessor`
   MOTU path `float* frameOut`.
4. Zweryfikuj że nasze pliki MOTU (profile/protocol — nowe, nie konfliktują) grają z API upstream
   (sygnatura `Configure`, writer, AudioGraphBinding).
5. Build (spodziewaj się błędów kompilacji float/API) → test hardware: czy `written>0` / playback gra.

**Korzyści integracji:** TX exposure fix (playback), FW-62 use-after-free guard, HAL offset 624→128,
RX Float32, ZTS rework — plus alignment z mrmidi (łatwiejszy PR MOTU).

**Alternatywa:** oddać wsparcie MOTU mrmidiemu (origin/DICE go nie ma) — on zna swój TX rework,
zintegruje nasz device-facing MOTU + ground-truth na swojej bazie. Nasz fork + docs już pushnięte.

Pełna lista: `git log --oneline dice-motu..origin/DICE`.

### (archiwum) v15 — dwuetapowy ISOC_COMM_CONTROL + ZTS 3000ms

> **v15 — dwuetapowy ISOC_COMM_CONTROL deactivate→activate + ZTS timeout 3000ms (port Fix 19 z main).**
> v14 (klaster rejestrów 0x0b04/PACKET_FORMAT/0x0c04) NIE pomógł → szukaliśmy w historii main.
> **Znalezione (main DevLog sesja 9+15): MOTU milczało `seq=0` nawet z poprawnym activate
> `0xC1C00000` — IR pakiety popłynęły dopiero po dodaniu deactivate-PRZED-activate (Fix 19).**
> To NIE jest tylko o stale-state (power-cycle dice nie pomógł — zgodne z tym, że potrzebne jest
> samo *przejście* deactivate→activate). dice robił pojedynczy activate (rozbity ProgramRx/ProgramTx)
> bez deactivate → dokładnie objaw seq=0.
>
> **Fix v15:**
> - [`MOTUVendorProtocol::ProgramRx`](ASFWDriver/Audio/Protocols/Vendor/MOTU/MOTUVendorProtocol.cpp):
>   pełne ISOC_COMM_CONTROL w jednym miejscu — deactivate both (Change=1,Act=0) + IOSleep(20) →
>   activate both (RX+TX z kanałami). `ProgramTxAndEnableDuplex` zostawia tylko FETCH_PCM.
> - [`ASFWAudioDevice.cpp:302`](ASFWDriver/Audio/DriverKit/ASFWAudioDevice.cpp:302): ZTS timeout
>   500ms → **3000ms** (MOTU PLL lock do ~3s, jak main).
> Wszystko device-facing + timing; host data path (DMA) nietknięty.
>
> ### 🔬 Test v15
> 1. Odpal `~/Desktop/ASFW_dice_v15.app`, zostaw appkę otwartą, `systemextensionsctl list` = **15**.
> 2. Zagraj przez MOTU, zbierz logi:
>    ```bash
>    /usr/bin/log show --last 5m --predicate 'senderImagePath CONTAINS "ASFWDriver"' 2>/dev/null | grep -E "(ProgramRx deactivate|ProgramTx FETCH|DrainCompleted|IR interrupt|Wrote Match|timed out|StartIO failed)"
>    ```
> 3. **Oczekiwane:** `ProgramRx deactivate=0x... activate=0x...` w logu, potem przerwania IR,
>    `ZTS timed out` znika, audio gra.
> 4. Jeśli nadal cisza → zostaje #6 (IRM AllocateResources — main go potrzebował) i kolejność arm-IR.

### (archiwum) v14 — klaster rejestrów (nie był root-cause)

> **Bug D (kWake) POTWIERDZONY działający w v13** — readback `Ctl=0x9400` (run+wake+active),
> kontekst IR DMA biegnie i jest aktywny. Ale ZTS nadal timeout → diagnoza poszła dalej.
>
> **Nowy root-cause (v14): MOTU nie nadaje IR** — zero przerwań IR przez 567 ms mimo poprawnie
> uzbrojonego kontekstu. Power-cycle MOTU **nie pomógł** → wykluczono stale-state. Porównanie z
> **działającym main** (`MOTUAudioBackend::StartStreaming`) wykazało, że dice **nie pisze rejestrów
> stream-control, które El Cap pisze przy każdym init**:
> - `0x0b04 = 0xffc10001` (brak w dice) — V3 stream control
> - `PACKET_FORMAT 0x0b10 = 0x00000002` (dice miał błędne `0xc2` z exclude-differed)
> - `ROUTE_PORT_CONF 0x0c04 = 0x00000100` (brak w dice)
>
> **Fix v14:** dodane wszystkie trzy zapisy w
> [`MOTUVendorProtocol::PrepareDuplex`](ASFWDriver/Audio/Protocols/Vendor/MOTU/MOTUVendorProtocol.cpp).
> Wszystko device-facing (ground-truth z El Cap, mirror main) — host data path (DMA) nietknięty.

### 🔬 Następny krok — zweryfikuj v14 na hardware

1. Zainstaluj `~/Desktop/ASFW_dice_v14.app`, potwierdź `systemextensionsctl list` = **14**.
   (Build był `--clean` → świeży kod gwarantowany, stary `.o` wykluczony.)
2. Odtwórz audio przez MOTU, zbierz logi:
   ```bash
   /usr/bin/log show --last 5m --predicate 'senderImagePath CONTAINS "ASFWDriver"' 2>/dev/null | grep -E "(PrepareDuplex|0x0b04|ROUTE_PORT|DrainCompleted|IR interrupt|Wrote Match|timed out|StartIO failed)"
   ```
3. **Oczekiwane jeśli fix trafiony:** przerwania IR padają, `ZTS timed out` znika, audio gra.
4. **Jeśli nadal timeout** → root-cause leży w pozostałych różnicach vs main:
   #5 kolejność (IR DMA przed ISOC_COMM_CONTROL) lub #6 IRM AllocateResources — oba strukturalne
   (DICE coordinator). Szczegóły porównania → DevLog „v14".

### Co czeka po ZTS (post-ZTS TODO)

| Bug | Opis | Status |
|-----|------|--------|
| Bug 1 (MOTU V3 decoder) | `kRxPcmChannels` powinien =18, wymaga `DecodeMOTUV3Frame` | ⏳ Post-ZTS |
| Bug 2 (IT encoder) | `Quirks()` zwraca `kAM824` zamiast MOTU V3 dla TX | ⏳ Post-ZTS |

---

## ✅ Ukończone (archiwum — szczegóły w DevLog.md)

> **System:** gdy element z „AKTUALNY STAN" zostaje rozwiązany i zweryfikowany, przenoś go tutaj
> jednolinijkowo (z numerem wersji + plikiem), a pełny opis (root cause + fix) wędruje do `DevLog.md`.
> Focus.md trzyma TYLKO aktywny stan + następny krok; nie rośnie historią.

- ✅ **Bug A/C (SYT, v9)** — brama SYT zabijała IT (MOTU V3 wysyła `syt=0x0000`) → fallback w `IsochReceiveContext.cpp`.
- ✅ **Bug B (geometry, v11)** — AM824 check `16<18` odrzucał IR → `kRxPcmChannels=16` w `MOTU828Mk3Profile.cpp`.
- ✅ **Bug D (kWake+isochHeader, v34)** — `Start()` = `kRun|kWake|kIsochHeader` (`IsochReceiveContext.cpp`).
- ✅ **ZTS timeout / IR CIP (v117, `585ea7f`)** — IR MOTU ma niestandardowy nagłówek `0d040400 22ffffff`
  (EOH1=0); osobna ścieżka `kMotuV3Packed` (DBS=16, 18 PCM, 3-bajtowe chunki) → ZTS publikuje (23 ms),
  StartIO OK, duplex wstaje. Hardware-verified. Pełny opis → DevLog 2026-06-22.

---

## Architektura DICE — jak działa ZTS (vs. main branch)

**W gałęzi DICE:** Isoch RX/TX sterowane wyłącznie przez AudioDriverKit. Nie ma ręcznego
Start/Stop IR/IT. `Start` w UI jest zablokowany — poprawne zachowanie.

**Przepływ ZTS:**
1. CoreAudio wywołuje `StartIO` → `BeginDirectIo` → `ArmDirectRx(irChannel=0)`
2. `IsochReceiveContext::Start()` zbroi IR DMA na kanale isoch 0 (kanał IRM)
3. MOTU nadaje IT na magistrali → OHCI IR DMA odbiera → `DrainCompleted()` → n > 0
4. Pakiet odebrany z timestamp → `UpdateCurrentZeroTimestamp()` → ZTS opublikowany
5. CoreAudio OK, `StartIO` udzielony → IT startuje z opóźnieniem ZTS

**Jeśli krok 3 się nie zdarzy:** ZTS timeout po 500ms, CoreAudio blokuje IO.

**Kanał IR:** `irChannel=0` = kanał IRM przydzielony przez `IsochResourceManager`.
Sprawdzić: czy MOTU faktycznie nadaje na kanale 0? Czy IRM przydzielił prawidłowy kanał?

---

## Środowisko

| Maszyna | System | Rola |
|---------|--------|------|
| MacBook Pro (M3 Max) | macOS Tahoe 26.5.1 (zewnętrzny SSD) | ✅ Aktywne — build + test |
| MacBook Pro (M3 Max) | macOS Sequoia (wewnętrzny SSD) | Diagnostyka (DTrace, IORegistry) |

Build (dice branch):
```bash
./build.sh --derived /tmp/ASFWBuild --deploy   # build + sign + Desktop/ASFW_dice_vNN.app
./build.sh --derived /tmp/ASFWBuild --clean    # pełny rebuild
```
Version bump + deploy działają automatycznie (port z main, 2026-06-15): `bump.sh patch` synca pbxproj
+ auto-commit; `deploy_app()` podpisuje dext+app i kopiuje na Desktop.

Logi dextu (Tahoe):
```bash
/usr/bin/log stream --predicate 'senderImagePath CONTAINS "ASFWDriver"' --level debug
```

**git push:** zawsze `git push cube666 dice-motu` (branch roboczy = `dice-motu`, NIE `main`; NIE `git push` — brak uprawnień do origin)

---

## Linki do powiązanych dokumentów

| Dokument | Zawartość |
|----------|-----------|
| `DevLog.md` (ten repo) | Historia sesji dice — bug v9/v10/v11, decyzje architektoniczne |
| `documentation/ZTS_AND_SYT.md` | ⭐ ZTS i SYT timing — kluczowy dla bieżącego problemu (DrainCompleted=0) |
| `documentation/FWOHCI_IR.md` | Architektura IR (Isoch Receive) z dekompilacji Apple — jak działa DMA ring |
| `docs/MOTU_V3_DICE_TODO.md` | Lista bugów MOTU V3 z poprawnym rozwiązaniem każdego |
| `../ASFireWire/Focus.md` | Main branch (zero-copy) — aktualny stan PCM byte position bug |
| `../ASFireWire/documentation/MOTU_828_MK3_FACTS.md` | KANON — fakty sprzętowe MOTU 828 MK3 |
| `../ASFireWire/documentation/MOTU_V3_WIRE_GROUNDTRUTH.md` | Ground-truth z kabla — niezbędny przy IT encoder (Bug 2) |
