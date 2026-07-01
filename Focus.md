# Focus.md — Plan pracy nad ASFireWire-dice

Cel: MOTU 828 MK3 działający przez sterownik DICE (nowa architektura, AudioDriverKit-native).

Archiwum ukończonych sesji → `DevLog.md`

---

## 🧭 NAJPIERW: na którym systemie jesteś? (M3 ma dwa OS-y)

> ## ⏸️ HANDOFF 2026-06-28 (user wraca ŚR 2026-07-01) — DRUT + REJESTRY WYCZERPANE, misframe = warstwa OPERACYJNA
> **Cały dzień tight-loop testów. WYKLUCZONE TWARDO:** (a) **drut bajt-w-bajt = oficjalny** (diff: PCM tylko slot 12/13,
> blok-bajt 40-45, 3B BE, SPH@quad2, reszta zera → **slot 12/13 = Main, poprawnie**); (b) **zestaw rejestrów identyczny**
> — oficjalny pisze DOKŁADNIE `0b00 0b04 0b08 0b10 0b14 0b1c 0b38`, my piszemy to + 0c04 ekstra; (c) **SID** (=node 1);
> (d) **lead SPH** zmierzony na drucie `−258→−63→+26→+79` (v141-v144) → wzór diód IDENTYCZNY → **lead MARTWY**.
> Oficjalny gra czysto na TYM SAMYM M3+adapter TB→FW+MOTU z identycznym drutem+rejestrami → **różnica jest OPERACYJNA**
> (IRM/bandwidth, cycle-master, KOLEJNOŚĆ/TIMING startu: IT-DMA vs FETCH vs IR urządzenia, handshake), niewidoczna w drucie.
>
> **➡️ PLAN (wybór po powrocie):**
> 1. **mrmidi** (lead dev) — jego działka (zegar/IRM/coordinator). Mamy żelazny, zawężony dowód: „drut+rejestry identyczne,
>    MOTU misframuje → to operacyjne, nie drut". Najszybsza droga.
> 2. **Deep-dive warstwy szyny** — porównać nasz DiceDuplexBringup/coordinator start-sequence + IRM + cycle-master vs oficjalny
>    (DTrace lifecycle na Sequoia/El Cap: createDCLProgram / isoch-start order / IRM AllocateResources) vs nasze logi.
> 3. **Zegar z Linuksa (SPH-echo) = NARZĘDZIE 2. RZUTU, nie teraz.** Decyzja 2026-06-28: drut już bajt-w-bajt = El Cap
>    (lepszy oracle niż Linux), więc echo SPH nie zmieni bajtów które już pasują → mało prawdopodobny fix. Linux jako
>    *mechanizm* OK (memory `project-motu-linux-sph-echo-fallback`), ale odpalić **tylko jeśli** pkt 2 pokaże realny
>    **dryf zegara w czasie** (objaw „diody po paru minutach" = możliwa sygnatura konwergencji/dryfu). Najpierw pkt 1/2.
>
> **Build: v144** (`kMotuSphPresentationLeadTicks=305` → on-wire +26). Lead MOOT (wykluczony), init writes ZOSTAJĄ (wierne).
> **Setup:** MOTU na M3 (⚠️ luźny kabel FW dawał „nie widać MOTU" — sprawdź wpięcie). MBP2009=Linux PASYWNY (`snd_firewire_motu`
> wyładowany, quirks=0x10, `/dev/fw0`+fw1=MOTU, tool w `/tmp/` ch1). SSH + sudo → pełne dane w lokalnej pamięci
> `project-mb2009-snoop-access` (poza repo). Pętla leadu: zmień lead→build→graj→snoop ch1→parse.
> (Stare handoffy 0b38/regread = ZROBIONE, archiwalne.)

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

## 🔴 AKTUALNY STAN (2026-06-26) — POMIAR Z OFICJALNEGO STOSU GOTOWY → 2 fixy do wdrożenia (lead 9216 + Main slot 12/13)

> **TL;DR dla świeżej sesji Tahoe:** przeczytaj `documentation/SEQUOIA_SNOOP_RESULT.md`, potem wdróż
> dwa fixy z bloku „✅ POMIAR GOTOWY" niżej (lead `6144→9216`, Main → slot `12/13`). Reszta tej sekcji
> (poniżej) to kontekst historyczny sprzed pomiaru — część notatek o „slot 0/1" jest OBALONA przez pomiar.

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
> ### ➡️ AKTUALIZACJA 2026-06-26 — byte-compare + audyt zegara ZROBIONE
> - **Byte-compare vs El Cap IT snoop:** drut byte-perfect, różni tylko **SID 0 vs 3** (MSG/DBS/SPH-slope/
>   sloty zgodne; slot map już poprawny). SID = scoped, niska szansa (wymaga runtime node-id→CIP).
> - **Zegar WYKLUCZONY:** zrzuty El Cap → MOTU = **48k / Clock Source INTERNAL** (MOTU jest masterem).
>   Piszemy 0x0b04/0b10/0c04 jak El Cap; rate potwierdzony przez bramę health (duplex wstaje = 48k).
> - **➡️ TOP PODEJRZANY: LEAD SPH** (`kMotuSphPresentationLeadTicks=2*3072` = zgadnięty). Karmienie
>   urządzenia-mastera ze złą fazą → under/overflow → ciągły pisk.
>
> ### ✅ POMIAR GOTOWY (2026-06-26, sesja Sequoia) → **[`documentation/SEQUOIA_SNOOP_RESULT.md`](documentation/SEQUOIA_SNOOP_RESULT.md)**
> Zmierzono oficjalny sterownik MOTU na Sequoia (snoop MBP2009/Linux, oba strumienie + sweep + multi-rate).
> **Pełne liczby + dowody w RESULT.md — TU tylko akcje. Przeczytaj RESULT.md przed kodowaniem.**
>
> **DWA TWARDE FIXY (oba pomierzone, nie zgadywane) — ✅ WDROŻONE W KODZIE 2026-06-27, czekają na test hardware:**
> 1. **LEAD:** `kMotuSphPresentationLeadTicks` = `2*3072` (6144) → **`3*3072` (9216)**. Oficjalny lead =
>    **3 cykle**, rate-independent (potwierdzone 44.1/48/88.2/96/176.4/192k). Brakowało nam 1 cyklu.
>    ✅ Zmienione w [`ASFWAudioDriverZts.cpp:247`](ASFWDriver/Audio/DriverKit/ASFWAudioDriverZts.cpp:247).
> 2. **SLOT MAIN + ROUTING:** enkoder IT kładł CoreAudio ch c liniowo na wire slot c+2 (`byte 10+ch*3`)
>    → Main na złych slotach. ✅ Wdrożona **stała tabela** `kMotuV3CoreToWireSlot[14]={12,13,4,5,6,7,8,9,10,11,2,3,14,15}`
>    + baza `kMotuV3SlotBase=4` (`byte = 4 + wireSlot*3`) w
>    [`AmdtpPayloadWriter.cpp`](ASFWDriver/Audio/Wire/AMDTP/AmdtpPayloadWriter.cpp). Main L/R (ch 0/1) → slot 12/13;
>    14 PCM partycjonuje sloty 2–15; sloty 0/1 = padding. To wyjaśnia „Main cichy + diody wędrują".
>
> **🔬 TEST — `v137` zbudowane+deployed (clean) 2026-06-27:** zainstaluj `~/Desktop/ASFW_dice_v137.app`,
> potwierdź `systemextensionsctl list`=**137**, zagraj przez MOTU Main Out 1/2. Oczekiwane: **dźwięk na Main
> + diody Main L/R, koniec piska**. Logi:
> `/usr/bin/log show --last 2m --debug --info 2>/dev/null | grep "ASFWDriver.dext" | grep -E "MotuSph|IT WIRE|maxAbs"`
> Jeśli pisk zniknął ale złe diody → docałować routing (pełna mapa w RESULT). Jeśli pisk został → lead
> nie był jedyny; sprawdź `[MotuSph]` seedSph vs ct.

> ### 🔬 WYNIK v137/v138 (2026-06-27 noc) — DRUT CZYSTY, ale MOTU MISFRAMUJE
> **Test v137:** Main Out CISZA, świecą Analog 3,4,7. Po przejściu na slot 12/13 (remap) Main zniknął.
> - **Pierwsza pułapka — MB2009 na szynie = underexposure.** Z Linuksem na szynie: `written` zamrożone,
>   `withoutPkt` ~42%, `WIRE16-PCM` zero (obcy IRM perturbował). **Odpięcie MB2009 + restart Tahoe → writer
>   zdrowy** (`dbcDisc=0`, `dropouts=0`, PCM na `s10/s11`). Wniosek: cisza/underexposure była od MB2009, NIE nasz kod.
> - **Drut po naprawie = czysty:** `WIRE16-PCM s10/s11` (= blok bajt 40/43 = nasz wireSlot 12/13) niezero,
>   reszta zero. Gauge czyta `bajt 10+ch*3`, więc s10 = bajt 40 = slot 12. Czyli **remap działa: Main na slotach 12/13**.
> - **MIMO TO MOTU: Main cisza, Analog 3,4,7.** 2 kanały na drucie → 3 diody (w v136 też 3) → **MISFRAMING**,
>   nie błąd slotu. Diody nie przesuwają się liniowo z naszym przesunięciem danych.
>
> **➡️ NOWA DIAGNOZA: niepełny init MOTU.** Raport diag ASFW: nasz init pisze 0x0b04/0b10/0b00/0b14/0c04,
> a oficjalny **DODATKOWO** 0x0b08, **0x0b1c**, **0x0b38** (których NIE piszemy). To prime suspect misframingu.
> - **NIE cofać remapu slotów** — Sequoia ground-truth (kanał→slot na slotach 2–15); „v136 lepszy bo Main R" było
>   czytaniem szumu misframingu. Etykieta „slot 12 = Main" do potwierdzenia DOPIERO gdy MOTU zaryglu­je.
> - **v138:** SID `0→1` (raport: jesteśmy node 1, nie 0 — obala wcześniejsze założenie; niska szansa, ale to
>   ostatnia różnica bajtowa). `MOTU828Mk3Profile.cpp:46` (hardkod 1 + TODO: plumb runtime node-id).
>
> **➡️ NASTĘPNY KROK (zaktualizowany 2026-06-27 po REGREAD):** read-back 0x0b1c/0x0b38 **WYCZERPANY** —
> oba są **write-only** (`SEQUOIA_REGREAD_RESULT.md`), nie da się ich odczytać. 0b08 read-back=0
> (command/doorbell). Żeby zdobyć wartości init: **snoop payloadu zapisu** (poprawić tracer, by deref
> `buf=` DMA-bufora — `dataBE=0x80a5211c` z traca v2 to artefakt, NIE wartość). Dopiero z realnymi danymi
> Tahoe dodaje zapisy do `MOTUVendorProtocol::PrepareDuplex`. Gdyby init nie pomógł → **SPH-echo**
> (broń ostatniej szansy, plan: pamięć `project_motu_linux_sph_echo_fallback`).
>
> ### 🔬 RUNDA 1 init — v139 DO TESTU (2026-06-27, wartości ZDOBYTE DTrace-deref)
> Wyżej „read-back wyczerpany" było aktualne na chwilę; **potem El Cap DTrace deref ZDOBYŁ wartości** u źródła
> (`SEQUOIA_REGREAD_RESULT.md` góra). **v139** dodaje 2 z 3 brakujących zapisów init MOTU (`MOTUVendorProtocol`):
> - **0x0b1c = 0x00120000** (@48k) — w `ProgramTxAndEnableDuplex` PRZED FETCH 0x0b14 (kolejność trace: 0b10→0b00→0b1c→0b14).
> - **0x0b08 doorbell** (`0xffffffff`→`0x00000000`) — klamruje 0x0b04 w `PrepareDuplex` (trace: 0b08,0b04,0b08).
> - **0x0b38 ODŁOŻONE (runda 2)** — size=8, brak 2. quadletu → `ELCAP_0B38_QUADLET2_HANDOFF.md`.
>
> **🔴 WYNIK v139 (2026-06-27, potwierdzony): = v137 — init BEZSKUTECZNY.** Diody Analog 3,4,7, Main cisza
> (początkowe „nic" to było opóźnienie zapłonu). **SID=1 + 0b1c + 0b08 = ZERO efektu** na misframe.
> **Porównanie na sucho: nasz `[WIRE16]` v139 = oficjalny bajt-w-bajt** (`q0=010d0418` SID=1=nasz node ✓,
> DBS=0d, byte2=04, `q1=8222ffff`, SPH-rampa, padding=0). → **misframe NIE jest w bajtach ani w SID/0b1c/0b08.**
> **v140** = baseline (usunięto bezskuteczny+ryzykowny doorbell 0b08; 0b1c zostaje na komplet z 0b38). NIE testuj v140 (==v139).
>
> **➡️ ZOSTAJĄ DWA tropy (init wyczerpany poza 0b38):**
> 1. **`0x0b38`** — ostatni rejestr init, El Cap **runda 2** → `ELCAP_0B38_QUADLET2_HANDOFF.md` (bez Linuksa). Niska pewność (0b1c/0b08 inert).
> 2. **TIMING / domena zegara** — zmierzyć REALNY lead NASZEGO strumienia na drucie (`SPH_cyk − cykl_przybycia`),
>    porównać z oficjalnym=3 → **[`documentation/TIMING_LEAD_CHECK_PLAN.md`](documentation/TIMING_LEAD_CHECK_PLAN.md)** (snoop MB2009 ch1).
>    Lead≠3 → tani fix `kMotuSphPresentationLeadTicks`. Dryf → uzasadnienie dla **SPH-echo**.
>
> Rekomendacja: 0b38 (runda 2, tanie) → jak też inert, init wyczerpany → timing/SPH-echo.
>
> ⚠️ **Korekta wcześniejszej notatki:** „PCM na slotach 0/1 = OK / identyczne z El Cap" (wyżej w tej
> sekcji) **było błędne** — oficjalny stos kładzie Main na **12/13**. Pomiar > notatka. NIE ufaj „slot 0/1".
>
> **Bonus dla multi-rate (na przyszłość, nie do 48k fixa):** SPH Δ = `round(24576000/fs)` (NIE hardcode
> 512); kanały 14/18 na 1×/2×, redukcja do 10/13 (DBS) na 4×; frames/pkt = 8×rodzina. Tabela w RESULT.
>
> **Mapa wejść IR (do mapowania capture):** Mic/Inst 1 = IR ch0, Analog 1-8 = IR ch2-9, ch16/17 = bus
> monitor/return (NIE wejścia). Layout IR: 18×3B @ offset 10, DBS=16, SPH Δ=512 (48k). Pełne w RESULT.
>
> **Sekwencja:** wdróż lead+slot → build `--clean --deploy` → test (diody Main + dźwięk) → STABILNOŚĆ
> → reszta MAPOWANIA → JAKOŚĆ. Fallback gdyby nie pomogło: Discord mrmidi (zero presji).
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

> 📦 **Archiwum (ZTS fix v117, integracja origin/DICE, enkoder IT v121-v124, rundy rejestrów v14/v15)
> przeniesione do [`DevLog.md`](DevLog.md) 2026-07-01** — sekcja „Sesja 2026-06-22 do 2026-06-24".

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
| `../ASFireWire/Focus.md` | Main branch (zero-copy) — aktualny stan prac na starszym sterowniku |
| `../ASFireWire/documentation/MOTU_828_MK3_FACTS.md` | KANON — fakty sprzętowe MOTU 828 MK3 |
| `../ASFireWire/documentation/MOTU_V3_WIRE_GROUNDTRUTH.md` | Ground-truth z kabla — niezbędny przy IT encoder (Bug 2) |
