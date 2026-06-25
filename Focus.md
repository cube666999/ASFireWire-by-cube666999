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

## 🔴 AKTUALNY STAN (2026-06-25 wieczór) — DRUT BAJT-W-BAJT POPRAWNY, pisk = poziom zegara/szyny

> **Kanał naprawiony, MOTU gra (diody) — ale piszczy + diody wędrują** (Analog 7 / S/PDIF / Main R).
> Dziś wieczorem **wyczerpaliśmy zawartość pakietu** — snoop z kabla pokazał, że wszystko jest poprawne.
>
> ### ⭐ Dowód z kabla (MB2009 snoop ch1, 23:10) — NASZ DRUT JEST IDEALNY
> Pasywny snoop naszego IT na ch1 = struktura **textbook + identyczna z El Cap**:
> - `ch=1 tag=1 sy=0`, DBS=13, CIP `000d04xx/8222ffff` ✓
> - **DBC +8/data, zamrożony na no-data** (f8→00→[08 N]→08→10→18…) — `dbcDisc=0` ✓
> - **SPH +512/blok, gładki i ciągły** przez pakiety i no-data (008541be→43be→…→55be w kolejnym pkt) ✓
> - **PCM czyste stereo TYLKO na slotach 0/1 (Main L/R), reszta `000000`** — snoop = WIRE16 1:1 ✓
> - Kadencja D,D,N,D = 75% data ✓
>
> Jedyna różnica od El Cap: **SID=0** (vs El Cap SID=3) — ale to nasz poprawny node id (jesteśmy węzeł 0).
> **MOTU dostawało IDENTYCZNY strumień od El Cap i grało czysto → pisk NIE jest w naszych pakietach.**
>
> ### Co wykluczone dziś TWARDO (nie hipotezy — pomiar/kabel)
> - **DBC** — `[WIRE-DBC]` watch (v136) + snoop: ciągły, zero złamań.
> - **Slope SPH + sloty PCM** — `[WIRE16-PCM]` (v135) + snoop: idealne.
> - **Dryf SPH (slope)** — `[MotuSph]` drift-watch (v134, żywy kursor vs ct): `driftCyc` oscyluje ±40, ale
>   to **jitter `ahead` w punkcie prepare** (koreluje z ahead), znosi się przy transmisji → at-transmit +2.
>   Brak rosnącego trendu. Hipoteza „dryf slope" **OBALONA**.
> - **Lead/projekcja** — zmieniony −5→**+2** (v133, mirror main `writeMotuV3SphAndAdvance`; usunięto też
>   projekcję `packetsAhead*3072`, bo `clockPair` ct ≈ czas transmisji). Pisk został → to nie lead.
>
> ### ➡️ ZOSTAJĄ TYLKO rzeczy POZA pakietem — następne kroki (NOWA SESJA)
> 1. **KONTROLNY TEST NAJPIERW: odłącz Linux/MB2009 od FireWire, zagraj, posłuchaj.** Sporo testów
>    (w tym pisk) było z **Linuksem na szynie (4 węzły)** — niekontrolowana zmienna. Obce węzły mogą
>    psuć cycle-master/clock MOTU. Pisk **znika bez Linuksa** → to perturbacja szyny, nie nasz bug.
> 2. **Discord do mrmidi** (draft w historii czatu 2026-06-25) — żelazny dowód „wire byte-perfect, MOTU
>    squeals". To jego znany bug main *„PCM byte position / MOTU Main Out"* + warstwa zegara. Zero presji
>    (wypalony). Pytanie: jak MOTU rygluje fazę bloków, czy slavuje do naszego SPH czy gra na internal.
> 3. **Absolutny SPH vs zegar MOTU** — slope OK, ale czy absolutna wartość/relacja do zegara MOTU jest
>    OK; czy MOTU slavuje do SPH (CLOCK_STATUS 0x0b14) czy gra na internal a my musimy się ryglować do IR.
>
> **Sekwencja napraw:** STABILNOŚĆ (zatrzymać pisk/wędrowanie) → MAPOWANIE (slot) → JAKOŚĆ. Slot-sweep
> nie ma sensu dopóki faza wędruje.
>
> ### Narzędzia diagnostyczne (zostają, scommitowane)
> - `[WIRE16]`+`[WIRE16-PCM]` (`IsochTxDmaRing::GaugeWirePayload`) — 6 quad + 14 slotów PCM nadanego IT.
> - `[WIRE-DBC]` + `dbcDisc=N` — ciągłość DBC per data-pakiet.
> - `[MotuSph]` (drift-watch: curCyc/driftCyc) + `[TxPump]` (`ASFWAudioDriverZts`) — kursor vs ct + ekspozycja.
> - **MB2009 snoop:** `tools/fw_isoch_snoop.c` → `sudo /tmp/fw_isoch_snoop /dev/fw0 1 N`. ⚠️ **Pułapki:**
>   (a) SSH wymuś `-o PreferredAuthentications=password -o PubkeyAuthentication=no` (klucz z passphrase wiesza);
>   (b) FW wymaga `modprobe firewire_ohci quirks=0x10`; (c) **`modprobe -r snd_firewire_motu snd_firewire_lib`**
>   — inaczej Linux AKTYWNIE przejmuje MOTU i gasi diody; (d) Linux na szynie = obcy IRM → StartAudioStreaming
>   pada loteryjnie. Działa sekwencja: **najpierw stream up bez Linuksa, potem podepnij Linux** (był na szynie
>   podczas udanego snoopa 23:10 i grało).

---

## ⚡ (archiwum) ZTS NAPRAWIONY (v117, 2026-06-22): MOTU startuje, StartIO OK

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

**🔬 TEST v121 (2026-06-23): cisza, ZERO diod MOTU → MOTU ODRZUCA pakiety.** Logi: dext OK, duplex
zdrowy, zero fatal, `IT WIRE maxAbs24=6.4M` (realne PCM na drucie!), `lastQuad` zmienny (ścieżka
MOTU-packed aktywna). Brak diod = odrzucenie, nie zły slot. **Root cause: SPH=0.**

**🔧 Fix v124 — SPH free-run z zegara OHCI (NIE z replay/SYT):**
v121 brał SPH z `outputPresentationTicks` w ścieżce replay, a `motuSphValid` ustawiał tylko gdy
replay ustanowiony. Ale `ComputeReplaySytOffset` zwraca `kNoInfo` dla `syt==0xffff`, a **MOTU IR ma
SYT=0xffff** → replay nigdy się nie ustanawia dla MOTU → `motuSphValid=false` → **SPH=0** → MOTU
odrzuca (main Fix 62: stale SPH → reject). Porównanie z El Cap: wszystko zgadza się bajt-w-bajt
(CIP Q0=030d04xx, Q1=8222ffff, blok) PRÓCZ SPH (El Cap: rosnący Δ=512, my: 0).
Fix (2 pliki, MOTU-only):
- `AmdtpTxPacketizer` — kursor SPH **seed-once-free-run** (mirror main): seed RAZ z anchora, potem
  +512/ramkę (8×512=4096=odstęp data-pakietów → stały lead). Wymusza SYT=0xffff dla MOTU.
- `ASFWAudioDriverZts` — seed z `txExecutionTimeline.AnchorForPacket` (anchor z completion-stampów
  OHCI, **niezależny od SYT/replay**) + lead 2 cykle. Diag `[MotuSph]` loguje SPH ~1/s.

**🔬 TEST v124:** zainstaluj `~/Desktop/ASFW_dice_v124.app`, `systemextensionsctl list`=124, zagraj
przez MOTU (Spotify). Logi:
```bash
/usr/bin/log show --last 2m --debug --info 2>/dev/null | grep "ASFWDriver.dext" | grep -E "IT WIRE|maxAbs|MotuSph"
```
Oczekiwane: `[MotuSph] sph=0x...` **rosnący, niezerowy** (cyc/off rosną); **diody MOTU się świecą + słychać** na Main Out 1/2.
- Diody świecą ale cisza na Main → root-cause=slot; przenieś stereo na slot 10/11 (main Fix 74, `kMotuV3PcmByteOffset`+slot).
- Nadal zero diod ale SPH rośnie → stroić lead (`kMotuSphPresentationLeadTicks`) lub domenę anchora.
- `[MotuSph]` nie pojawia się → anchor niedostępny (brak completion-stampów); sprawdzić `txExecutionTimeline.controlBlock`.

**📐 Dwie dźwignie ground-truth do audytu SPH (analiza na sucho PRZED testem w domu):**
Blocker = SPH (MOTU odrzuca ramki ze stale/zerowym SPH). Zanim odpalisz hardware, zestaw nasz
`WriteMotuSph` (`AmdtpTxPacketizer`) + seed w `ASFWAudioDriverZts` z **realnie grającymi** źródłami:
1. **`MOTU_KEXT_GHIDRA.md`** (`../ASFireWire/documentation/`) — disasm działającego kekstu: **SPH bit =
   0x400 (bit 10)** w CIP Q0, DBC, encoding → potwierdza GDZIE SPH siedzi w nagłówku.
2. **`MOTU_V3_WIRE_GROUNDTRUTH.md`** (kanon) — **SPH rośnie Δ=512** → potwierdza JAK ma rosnąć (kadencja/lead).
Cel: sprawdzić czy nasza domena ticków + lead + `cyc<<12|off` zgadzają się z El Cap. (Pełna tabela
źródeł + poziom zaufania: `CLAUDE.md` → „Źródła ground-truth MOTU".)

**✅ WYNIK AUDYTU SPH na sucho (2026-06-24) — format OK, problem = wartość seeda (lead):**
Zestawiono `AmdtpTxPacketizer::WriteMotuSph` + seed w `ASFWAudioDriverZts` z El Cap wire + Ghidra.

**Zgadza się z kablem — NIE RUSZAĆ:** SPH=1. quadlet bloku, `(cyc<<12)|off` (cyc bits[24:12], off
[11:0]) `:284`; Δ=512/ramkę free-run `:285`; CIP Q0 bajt2=**0x04**; SYT=0xFFFF.
> ⚠️ **Fałszywy alarm — nie ścigać znowu:** `cipConfig.qpc=0` + `sph=true` (`AmdtpTxPacketizer.cpp:69-70`)
> *wyglądają* odwrotnie do GT, ale builder mapuje nasze `sph`→**bit 10** (`CipHeader.cpp:39` `sph?1u<<10`)
> = to co GT zwie QPC=1 → na drucie wychodzi poprawne 0x04. Mylące nazewnictwo, NIE bug.

**Podejrzani (priorytet) — to stroić w domu, NIE przepisywać formatu:**
1. **LEAD (HIGH)** — `kMotuSphPresentationLeadTicks = 2*3072` (`ASFWAudioDriverZts.cpp:237`) = zgadywane.
   IR GT pokazał SPH **~5 cykli ZA** ct (`aheadHw=-5`), my dajemy **+2 PRZED** → spróbuj 0, potem ujemny.
   Plus: seed dodaje `packetsAhead*3072` (`:247-251`) ale pakiety DATA są co **4096** ticków (6000/s) —
   sprawdź czy `nextPacketToPrepare`/`completionCursor` liczą tylko DATA (wtedy projekcja myli się ~1024/pkt).
2. **SECONDS=0 (MEDIUM)** — GT ma seconds w bits[27:25], my nigdy ich nie ustawiamy (kursor zawija co 1 s,
   seed gubi seconds z CYCLE_TIMER `:240-244`). Całkowita cisza wskazuje raczej na lead niż seconds
   (seconds dałby periodyczny ~1/s glitch). Jak lead nie pomoże a seedSph ładnie śledzi ct → dołóż seconds.
3. **Świeżość seeda (LOW)** — zweryfikuj że `clockPair.TryRead` daje **rosnący** ct (nie 0/frozen),
   inaczej `motuSphValid=false`→SPH=0→reject (awaria v121).

**🔬 RECEPTA (test w domu):** `log show --last 2m --debug --info | grep ASFWDriver.dext | grep MotuSph`,
czytaj `[MotuSph] ct=0x..(cyc off) seedSph=0x..(cyc off)`:
- ct rośnie, seedSph rośnie i blisko ct, MOTU dalej odrzuca → **strój lead** (0 → ujemny).
- seedSph mocno ≠ ct → bug kadencji projekcji (pkt 1, `packetsAhead`).
- brak `[MotuSph]` / ct=0 → seed nie wstaje (pkt 3), `clockPair` pusty.
Bottom line: problem to JEDNA liczba (lead/seed), nie format. Nie ruszaj Δ=512 ani CIP.

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

**Zakładka Isoch (Metrics) — świadomie martwa w DICE, NIE regresja.** `GetIsochRxMetrics`
zwraca same zera (`IsochHandler.cpp:342`, „zeroes out due to direct-only architecture”), reset to
no-op (`:385`), a metryki TX nie istnieją (tylko selektory RX 34/35; „Isoch Transmit” w
`MetricsView.swift:104` to martwy placeholder). Powód: w main isoch pędził UI→UserClient (właściciel
liczników), w DICE pędzi go AudioDriverKit ścieżką Direct, której UserClient nie widzi — hydraulika
telemetrii nie została przepięta. **Decyzja: odłożone, nie warto teraz** — duplikuje logi
(`drainedTotal`, `rxZtsPublishCount_`, CIP DBS/SYT/DBC są w `log stream`), a TX byłby budowany wokół
enkodera, który i tak wymieniamy (Bug 2). **Wrócić gdy:** (a) wielokrotnie grepujesz logi po tych
samych licznikach w iteracji hardware → tani RX (żywe liczniki z `IsochReceiveContext` już istnieją,
trzeba tylko akcesory + mapowanie na `IsochRxSnapshot`); (b) TX dopiero PO ustabilizowaniu MOTU V3
enkodera.

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
- ✅ **IT na właściwym kanale (2026-06-25)** — `isoChannel` = `hostToDeviceIsoChannel` (ch1) zamiast `sid`
  (ch0), [`ASFWAudioDevice.cpp:205`](ASFWDriver/Audio/DriverKit/ASFWAudioDevice.cpp:205). Hardware-verified:
  MOTU mruga diodami (lock na strumień). Pozostaje dryf SPH → patrz „AKTUALNY STAN".

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
