# Focus.md — Plan pracy nad ASFireWire-dice

Cel: MOTU 828 MK3 działający przez sterownik DICE (nowa architektura, AudioDriverKit-native).

Archiwum ukończonych sesji → `DevLog.md`

---

## 🟢 ZACZNIJ TU — pierwsza świeża sesja (uruchom z katalogu dice!)

1. **Uruchom `claude` z `ASFireWire-dice/`** (nie z `ASFireWire/`) — wtedy ten CLAUDE.md +
   indeks CodeGraph dice ładują się automatycznie. Zatwierdź MCP „codegraph" (opcja 2) jeśli pyta.
2. **Zainstaluj świeży dext:** uruchom `~/Desktop/ASFW_dice_v34.app` (CFBundleVersion=**34**,
   fix isochHeader IR — patrz „AKTUALNY STAN" niżej). Stare na Desktopie możesz usunąć.
3. **POTWIERDŹ że biegnie NOWY kod** (krytyczne — patrz lekcja version-bump w DevLog):
   ```bash
   systemextensionsctl list   # MUSI pokazać aktualną wersję [activated enabled]
   ```
   Jeśli pokazuje niższą wersję → upgrade się nie wykonał, NIE ufaj logom.
4. **Dopiero wtedy** wznów debug poniżej. Logi (⚠️ `--predicate senderImagePath` NIE działa dla
   dextu — używaj `grep "ASFWDriver.dext"`):
   ```bash
   /usr/bin/log stream --debug --info 2>/dev/null | grep "ASFWDriver.dext" | grep -E "(IR DIAG|ZTS|IR|DMA|DICE|timed out)"
   ```

> ℹ️ **Infrastruktura naprawiona 2026-06-15:** version-bump (build.sh→`bump.sh patch`, sync pbxproj,
> auto-commit), deploy (`--deploy`/`--clean`), SSH remotes. Od teraz `./build.sh --derived /tmp/ASFWBuild --deploy`
> daje deterministycznie rosnącą wersję → koniec z „duchem starej wersji". Szczegóły: DevLog.md.

---

## ⚡ AKTUALNY STAN — ROOT CAUSE ZNALEZIONY (2026-06-22): IR MOTU używa NIE-standardowego CIP

> **Przełom przez snoop działającego El Capitan.** Pełna analiza + dowody:
> `../ASFireWire/documentation/MOTU_V3_WIRE_GROUNDTRUTH.md` §„✅ IR ground truth — ZEBRANE".
> Raw: `../ASFireWire/documentation/raw-captures/2026-06-22_el_capitan_snoop_IR_ch34_device-to-host.txt`.
>
> ### 🔑 Root cause „ZTS timed out 0xe00002d6"
> Nagłówek IR (device→host) z MOTU to **stałe `0d040400 22ffffff`** — **NIE jest standardowym CIP**:
> `Q1=0x22ffffff` ma bit31 (EOH1) = **0**. Nasz `RxAudioPacketProcessor::ProcessPacket` woła
> `CIPHeader::Decode(q0,q1)` które **wymaga EOH1=1** → `nullopt` → `kInvalidRange` → **każdy pakiet
> IR odrzucony** → ZTS nigdy nie publikuje → timeout.
>
> Potwierdzone **3 niezależnymi stosami**: snoop El Capitan ch=34 (offset 0), nasz dice procesor
> (offset 8, isochHeader=1), Linux ALSA tracepoint. Wszystkie widzą ten sam stały nagłówek →
> **nasz offset `payload+8` / isochHeader=1 był poprawny cały czas** (v34 był słuszny). Bug to
> WYŁĄCZNIE walidacja standardowego CIP na niestandardowym nagłówku IR MOTU.
>
> 📌 Wcześniejsza hipoteza „DBC=0=idle, MOTU nie nadaje" była **BŁĘDNA** — MOTU nadaje żywe dane
> (SPH rośnie i synca z zegarem, PCM niesie realny sygnał/szum). Nagłówek IR jest po prostu stały.
>
> ### Struktura IR (do dekodera)
> - DATA pakiet = **520 B** = 8 (nagłówek `0d040400 22ffffff`) + **8 bloków × 16 quadletów**
> - Blok = 64 B = **SPH(4B) + 2 MSG chunki + 18 PCM chunków** (×3B); **DBS=16, 18 PCM** (NIE 16!)
> - SPH bloku = MOTU timestamp (ten sam format co IT) → źródło ZTS, jest prawdziwy ✅
>
> ### 🔧 Fix do zaimplementowania (dice)
> 1. **IR: nie walidować standardowego CIP** — nagłówek to stałe `0d040400 22ffffff`. Albo osobna
>    ścieżka dekodowania dla MOTU V3 IR, albo rozluźnić `CIPHeader::Decode` dla wireFormat MOTU V3.
> 2. **Stały DBS=16** (nie z nagłówka); pomiń 8B nagłówek; dekoduj 8 bloków.
> 3. Każdy blok: SPH(4B) → pomiń 2 MSG (6B) → 18 PCM × 3B.
> 4. SPH bloku[0] → timestamp do ZTS (jak w `ExpandReceiveTimestamp`).
> 5. **`kRxPcmChannels` 16 → 18** w `MOTU828Mk3Profile.cpp` (potwierdzone: El Capitan = 18-kanałowy IR).
>
> ### 🧹 Sprzątanie przed fixem
> - Usunąć DIAG `IR DIAG poll#…` (Poll/Stop) i `IR DIAG reject#…` + skan offsetów (v35/v36) z
>   [`IsochReceiveContext.cpp`](ASFWDriver/Isoch/Receive/IsochReceiveContext.cpp) i pole `diagRejectCount_`.
> - v34 (`kRun|kWake|kIsochHeader` w `Start()`) **zostaje** — był poprawny (offset isochHeader=1).
>
> ### ⏮️ Snoop dext na Tahoe — przywrócić dice
> Tahoe ma teraz wgrany **snoop-mode v115** (architektura main, do capture). Po zakończeniu analizy
> wgrać z powrotem dice (`~/Desktop/ASFW_dice_v36.app` lub nowy build z fixem) — ten sam bundle ID.

### ⚠️ origin/DICE (mrmidi) jest 28 commitów przed nami — NIE scalone

`git fetch origin` (2026-06-19) pokazał: `dice-motu` rozjechał się od `origin/DICE` w `d200603`,
mrmidi zrobił od tego momentu 28 commitów (`git log dice-motu..origin/DICE`). **Nie rozwiązują**
naszego buga MOTU V3 CIP — `RxAudioPacketProcessor.cpp`/`kIsochHeaderSize=8` identyczne, `Poll()`'s
ZTS-publish wciąż wymaga `framesDecoded!=0` (czyli udanego CIP decode). Żaden commit message nie
wspomina MOTU — prawdopodobnie testowane na innym DICE-sprzęcie ze "podręcznikowym" CIP.

**Warte integracji później (decyzja użytkownika 2026-06-19: dokończyć v36 najpierw):**
- `0950319` FW-62 — use-after-free/TOCTOU guard w `Poll()` (DOKŁADNIE plik który edytujemy!)
- `5a0b306` HAL input safety offset 624→128 frames (~10.7ms phantom latency fix)
- `fc3f46b` RX capture path → Float32
- `191302b` ZTS publish rework (`ztsProjector_` usunięty, periodic-boundary publish)
- Upstream IR `Start()` używa `kRun|kIsochHeader` **bez `kWake`** — nasz "Bug D"/kWake (v12,
  `2c17a1a`) nigdy nie istniał upstream, jest lokalnym fixem tylko w `dice-motu`.

Pełna lista: `git log --oneline dice-motu..origin/DICE`. Branch `DICE` (lokalny) trackuje
`origin/DICE`, jest 28 commitów za nim.

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
- ⚠️ **Bug D (kWake, v12) — NIEPEŁNY, dokończony w v34** — Bug D dodał `kWake` (IR DMA startuje) ale
  błędnie wyrzucił `kIsochHeader` → OHCI format niespójny z procesorem. v34: `kRun|kWake|kIsochHeader`
  (`IsochReceiveContext.cpp:108`). Patrz AKTUALNY STAN.

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
