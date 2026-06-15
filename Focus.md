# Focus.md — Plan pracy nad ASFireWire-dice

Cel: MOTU 828 MK3 działający przez sterownik DICE (nowa architektura, AudioDriverKit-native).

Archiwum ukończonych sesji → `DevLog.md`

---

## 🟢 ZACZNIJ TU — pierwsza świeża sesja (uruchom z katalogu dice!)

1. **Uruchom `claude` z `ASFireWire-dice/`** (nie z `ASFireWire/`) — wtedy ten CLAUDE.md +
   indeks CodeGraph dice ładują się automatycznie. Zatwierdź MCP „codegraph" (opcja 2) jeśli pyta.
2. **Zainstaluj świeży dext:** uruchom `~/Desktop/ASFW_dice_v12.app` (zbudowany 2026-06-15,
   CFBundleVersion=**12**, zawiera fix kWake). Stare na Desktopie (v9–v11) możesz usunąć.
3. **POTWIERDŹ że biegnie NOWY kod** (krytyczne — patrz lekcja version-bump w DevLog):
   ```bash
   systemextensionsctl list   # MUSI pokazać 1.0/12 [activated enabled]
   ```
   Jeśli pokazuje niższą wersję → upgrade się nie wykonał, NIE ufaj logom dopóki nie zobaczysz 12.
4. **Dopiero wtedy** wznów debug ZTS poniżej. Logi:
   ```bash
   /usr/bin/log stream --predicate 'senderImagePath CONTAINS "ASFWDriver"' --level debug 2>/dev/null | grep -E "(ZTS|IR|DMA|Arming|DrainCompleted|DICE|timeout)"
   ```

> ℹ️ **Infrastruktura naprawiona 2026-06-15:** version-bump (build.sh→`bump.sh patch`, sync pbxproj,
> auto-commit), deploy (`--deploy`/`--clean`), SSH remotes. Od teraz `./build.sh --derived /tmp/ASFWBuild --deploy`
> daje deterministycznie rosnącą wersję → koniec z „duchem starej wersji". Szczegóły: DevLog.md.

---

## ⚡ AKTUALNY STAN — do weryfikacji na hardware

> **Bug D (kWake) naprawiony w v12 — czeka na test hardware.**
> Przyczyna `DrainCompleted()=0` znaleziona: `IsochReceiveContext::Start()` programował kontekst
> OHCI IR bitami `kRun | kIsochHeader` zamiast `kRun | kWake` (jak działający main). Bez `kWake`
> (bit 12) DMA nigdy nie rusza od `CommandPtr` → zero deskryptorów → ZTS timeout (0xe00002d6).
> Fix: [`IsochReceiveContext.cpp:108`](ASFWDriver/Isoch/Receive/IsochReceiveContext.cpp:108).
> Szczegóły → DevLog „Bug D".

### 🔬 Następny krok — zweryfikuj v12 na hardware

1. Zainstaluj `~/Desktop/ASFW_dice_v12.app`, potwierdź `systemextensionsctl list` = **12**.
2. Odtwórz audio przez MOTU, zbierz logi:
   ```bash
   /usr/bin/log stream --predicate 'senderImagePath CONTAINS "ASFWDriver"' --level debug 2>/dev/null | grep -E "(Readback|DrainCompleted|ZTS|IR RX|Arming|timeout)"
   ```
3. **Oczekiwane:** log `Start: Readback ... Ctl=0x...` z ustawionym run/active (nie dead),
   potem wpisy IR (DrainCompleted > 0), ZTS publikowany, **brak** `ZTS timed out`.
4. Jeśli nadal timeout → kontekst może być `DEAD` (nowy dead-check to pokaże) lub MOTU nadaje na
   innym kanale isoch niż `channel_` — sprawdź `Start: Wrote Match=0x...` vs kanał IRM.

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
- ✅ **Bug D (kWake, v12)** — IR DMA nie startował (`kRun|kIsochHeader` zamiast `kRun|kWake`) → `IsochReceiveContext.cpp:108`. *(czeka na finalną weryfikację hardware)*

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
