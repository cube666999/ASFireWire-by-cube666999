# Build, Deploy & Verify — hardware test (pełne instrukcje)

> Przeniesione z `CLAUDE.md` (odchudzenie kontekstu, 2026-07-01). Skrócona wersja (złota
> komenda + dwie żelazne reguły) zostaje w `CLAUDE.md` na stałe — to jest rozwinięcie z dowodami.

## Czytanie logów dextu (Tahoe)

### ⚠️ Dwie pułapki — zanim zaczniesz

**Pułapka 1 — `log` w zsh to wbudowana funkcja matematyczna:**
```bash
log stream ...    # ← uruchamia zsh-builtin, NIE /usr/bin/log — zero logów!
/usr/bin/log stream ...  # ✅ poprawne
```

**Pułapka 2 — predykat `process == "ASFWDriver"` nie działa.**
Dext loguje przez kernel. Właściwy predykat:

```bash
# Live stream podczas testu hardware:
/usr/bin/log stream --predicate 'senderImagePath CONTAINS "ASFWDriver"' --level debug

# Po zdarzeniu — ostatnie N minut:
/usr/bin/log show --last 10m --predicate 'senderImagePath CONTAINS "ASFWDriver"' --level debug

# Filtrowanie po kategorii:
/usr/bin/log stream --predicate 'senderImagePath CONTAINS "ASFWDriver"' --level debug 2>/dev/null | grep -E "(ZTS|IR|IT|DMA|Arming|DICE|timeout)"
```

## Build, Deploy & Verify — hardware test

> **Jedna sekcja-źródło dla całej ścieżki hardware-test (build → deploy → weryfikacja).** Komendy
> dev/test (CMake, unit testy, Swift) są w `CLAUDE.md` sekcja `## Build Commands`.

### Złota komenda
```bash
./build.sh --derived /tmp/ASFWBuild --clean --deploy   # HARDWARE TEST: ZAWSZE --clean + deploy
./build.sh --derived /tmp/ASFWBuild                     # tylko sprawdzenie kompilacji (bez deploy)
```
Wynik deployu: `~/Desktop/ASFW_dice_vNN.app` (podpisany `Apple Development`, dext + app, signature zweryfikowana).

### 🛑 Dwie żelazne reguły hardware-buildu

**1. ZAWSZE `--derived /tmp/ASFWBuild` (poza iCloud).** iCloud File Provider odtwarza xattry na plikach
w lokalnym DerivedData natychmiast po ich usunięciu → Xcode codesign failuje z `resource fork, Finder
information, or similar detritus not allowed`. Budowanie w `/tmp` to jedyne działające obejście.

**2. ZAWSZE `--clean`. Bez wyjątków.** Źródła leżą w iCloud → mtime niewiarygodne → build inkrementalny
pomija rekompilację zmienionego `.cpp` i linkuje **stary `.o`**. Bump wersji i tak robi relink, więc
`systemextensionsctl` pokaże nową wersję **z kodem ze starego pliku** — testujesz ducha. Dotyczy KAŻDEJ
edycji źródła (nie tylko headerów/constexpr). `--clean` (rm DerivedData) to jedyny pewny sposób. Build
inkrementalny — wyłącznie do sprawdzenia, czy się kompiluje; NIGDY do testu hardware.
> *Dowód (2026-06-15): v12 zbudowane bez `--clean` linkowało stary `IsochReceiveContext.o` —*
> *`systemextensionsctl`=12, ale kod ze starej wersji. Brak nowych bezwarunkowych logów*
> *`Start: Readback` zdemaskował ducha; v13 z `--clean` naprawiło.*

**NIGDY `./build.sh --no-bump`** na hardware test — macOS pomija upgrade dextu, jeśli `CFBundleVersion`
się nie zmienił.

### Version bump (naprawiony jak w main, 2026-06-15 + amend-into-previous 2026-07-01)
Bump dzieje się **automatycznie** w `build.sh`. Trzy usprawnienia:
1. `build.sh` woła `./bump.sh patch` (linia ~267) — wcześniej `./bump.sh` bez argumentu → `refresh` →
   wersja nie rosła → macOS pomijał upgrade. Teraz `patch` faktycznie inkrementuje.
2. `bump.sh` synca `CURRENT_PROJECT_VERSION` w pbxproj do komponentu patch z VERSION.txt
   (`CFBundleVersion` = patch) **i auto-commituje** VERSION.txt + pbxproj po bumpie (chroni przed
   cofnięciem przez iCloud sync). Auto-commit pomijany gdy `SRCROOT` ustawione (wywołanie z Xcode).
3. **Amend-into-previous-bump (2026-07-01):** jeśli poprzedni commit to już był `chore: bump version to …`
   **i nie jest jeszcze na żadnym remote** — kolejny bump **amenduje** ten commit zamiast tworzyć nowy.
   Efekt: 20 hardware-testów w sesji = 1 commit z najnowszą wersją, nie 20 osobnych.
   Bezpieczne, bo warunek „nie pushnięty" gwarantuje że nigdy nie przepisujemy shared history.
   Powód: 65 z 86 commitów przed origin/DICE (mrmidi) były bumpami — spam nieczytelny dla upstream;
   amend redukuje to do ~1 bump-commita per faktyczna zmiana kodu.

**Praktyczna implikacja dla codziennej pracy:**
- Nic nie zmieniasz w wywołaniu — `./build.sh --clean --deploy` dalej auto-bumpuje.
- W lokalnej historii `git log` zobaczysz **jeden** bump commit (najnowszy) między merytorycznymi zmianami, nie 20.
- Jak zrobisz merytoryczny commit (`fix(motu-v3): …`) — kolejny bump utworzy nowy commit (nie amenduje merytorycznego).
- Jak pushniesz bump — od tego momentu amend go już nie ruszy (chroni shared history). Kolejny bump utworzy świeży commit.

Ręczny bump bez buildu (np. by zsynchronizować przed czymś): `./bump.sh patch`.

> ⚠️ **Pułapka regresji wersji:** `bump.sh` synca pbxproj do **komponentu patch z VERSION.txt**. Jeśli
> VERSION.txt patch jest NIŻSZY niż aktualnie zainstalowany `CFBundleVersion` (`systemextensionsctl
> list`), build wyprodukuje niższą wersję → macOS odmówi upgrade'u. Trzymaj VERSION.txt patch powyżej
> ostatnio zainstalowanej wersji. *(2026-06-15: zainstalowane było 8, ustawiono VERSION.txt=0.2.9-audio.)*

### ✅ Weryfikacja po instalacji — DWA niezależne sprawdzenia
```bash
systemextensionsctl list   # 1) podmiana dextu: pokazuje NOWĄ wersję
# 2) świeżość kodu: znajdź w logach dextu bezwarunkowy log/marker dodany w tym fixie
```
⚠️ Sam `systemextensionsctl` NIE wystarcza — pokazuje nową wersję nawet gdy zlinkowano stary `.o`
(version bump i tak robi relink). Dopóki nie zobaczysz w logach markera z tego fixa, NIE zakładaj, że
testujesz nowy kod. Brak markera mimo nowej wersji → incremental build miss → przebuduj z `--clean`.
(Czytanie logów dextu: patrz sekcja wyżej.)

### `deploy_app()` w build.sh (dodane 2026-06-15, port z main)
Kopiuje app do `/tmp` (omija iCloud xattr), strippuje xattry, podpisuje **najpierw dext**
(`ASFWDriver/ASFWDriver.entitlements`) potem **app `--deep`** (`ASFW/App.entitlements`) tożsamością
`Apple Development` z keychain, weryfikuje podpis, kopiuje na Desktop jako `ASFW_dice_v${CFBundleVersion}.app`.

### Środowisko deweloperskie
| Maszyna | System | Rola |
|---------|--------|------|
| MacBook Pro (M3 Max) | macOS Tahoe 26.5.1 (zewnętrzny SSD) | ✅ **Aktywne** — build + hardware testy + Claude Code |
| MacBook Pro (M3 Max) | macOS Sequoia (wewnętrzny SSD) | Diagnostyka MOTU kext (DTrace/IORegistry) |

**Boot-args na Tahoe:** `amfi_get_out_of_my_way=1 cs_enforcement_disable=1`
