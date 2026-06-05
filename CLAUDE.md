# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ASFW is a macOS DriverKit-based FireWire (IEEE 1394) driver restoring FireWire functionality removed in macOS Tahoe (26). It uses PCIDriverKit for user-space OHCI controller access and AudioDriverKit for CoreAudio integration.

Two components:
- **ASFWDriver/** — C++23 DriverKit driver extension (dext)
- **ASFW/** — Swift 6 control app and installer (required to install the dext)

## Compact Instructions

When compacting this conversation, **preserve**:
- Aktualny numer fixa i co zmienia (np. Fix 31: kIsochHeaderSize=0, DBS IR=16)
- Aktualny stan wersji dextu: `CURRENT_PROJECT_VERSION` w `project.pbxproj`, wynik `systemextensionsctl list`
- Aktywne ustalenia debugowania: co działa (IT running, IR decode status, buffer fill %, underruns)
- Cel bieżącej sesji — co próbujemy naprawić i dlaczego
- Wszelkie potwierdzone fakty sprzętowe (MOTU 828 MK3: DBS IR=16, DBS IT=21, 18ch IT / 14ch IR)
- Zasady git: zawsze `git push cube666 main`, nigdy `git push` bez remote
- Wnioski z disassembly/logów które potwierdziły działanie fixa

**Odrzuć**:
- Surowe logi systemowe (zawartość plików `.txt` z pulpitu)
- Wielokrotnie czytane duże pliki (project.pbxproj, bump.sh, build.sh) — są w repo
- Stare iteracje debugowania które już zostały rozwiązane
- Długie outputy bash które nie wnoszą nowych informacji

## Build Commands

**Primary build (Xcode — required for signing and producing `.dext`):**
```bash
./build.sh                        # Quiet build (errors/warnings only)
./build.sh --verbose              # Full xcodebuild output
./build.sh --no-bump              # Skip version bump
./build.sh --config Release       # Release build
./build.sh --clean                # Delete DerivedData before build (forces full rebuild)
./build.sh --deploy               # After build: sign + copy to ~/Desktop/ASFW_vNN.app
./build.sh --no-bump --clean --deploy  # Typical hardware-test workflow (version already bumped)
```

**⚠️ DerivedData path:** `build.sh` uses `./build/DerivedData` (project-local), **not** `~/Library/Developer/Xcode/DerivedData`. The built app lives at `./build/DerivedData/Build/Products/Debug/ASFW.app`. Xcode GUI uses the standard path — these are separate DerivedData trees.

**⚠️ iCloud Drive + codesign:** Files on iCloud Drive carry `com.apple.quarantine` and other xattrs that break `codesign`. `--deploy` handles this by copying to `/tmp` first and stripping xattrs before signing. Sign dext first (with `ASFWDriver/ASFWDriver.entitlements`), then app with `--deep` (with `ASFW/App.entitlements`).

**⚠️ iCloud Drive + DerivedData — ZAWSZE używaj `--derived /tmp/ASFWBuild`:**
iCloud File Provider (`com.apple.fileprovider.fpfs#P`) odtwarza xattry na plikach w `./build/DerivedData`
**natychmiast** po ich usunięciu. Xcode's własna faza codesign failuje z:
`resource fork, Finder information, or similar detritus not allowed`.
`xattr -cr` na lokalnym DerivedData NIE pomaga — iCloud re-dodaje je w ułamku sekundy.

**Jedyne działające rozwiązanie — build poza iCloud Drive:**
```bash
./build.sh --no-bump --derived /tmp/ASFWBuild --deploy   # ← hardware test build
./build.sh --derived /tmp/ASFWBuild                      # ← zwykły build
./build.sh --derived /tmp/ASFWBuild --clean              # ← full rebuild
```
`/tmp` jest poza iCloud Drive → zero xattr problemów → codesign przechodzi.

**Generate `compile_commands.json`** (for clangd, static analysis):
```bash
./build.sh --commands
# or via CMake:
cmake -S . -B build && cmake --build build --target compile_commands
```

**C++ unit tests** (no hardware/DriverKit needed):
```bash
./build.sh --test-only                        # Build + run all C++ tests
./build.sh --test-only --test-filter Pattern  # Run tests matching a regex

# Or directly with CMake/CTest:
cmake -S tests -B build/tests_build
cmake --build build/tests_build -- -j$(sysctl -n hw.ncpu)
ctest --test-dir build/tests_build -V
ctest --test-dir build/tests_build -V -R TopologyManager   # single test suite
```

**Swift/XCTest tests:**
```bash
./build.sh --swift-test-only
./build.sh --swift-coverage       # With LCOV export for SonarCloud
```

**Version management:**
```bash
./bump.sh patch     # Bump patch version, sync Xcode project, auto-commit
./bump.sh refresh   # Regenerate version header only (NO version bump, NO commit)
```

**⚠️ CRITICAL — Version bump gotchas (fixed 2026-06-01):**

`./build.sh` calls `./bump.sh patch` automatically (unless `--no-bump`). Two bugs existed that caused the dext to silently ship with the old version, making macOS refuse to upgrade the system extension:

1. **Bug (fixed):** `build.sh` was calling `./bump.sh` without arguments → `refresh` mode → version never incremented. Fix: now calls `./bump.sh patch`.

2. **Bug (fixed):** `bump.sh` modified `VERSION.txt` and `project.pbxproj` but didn't commit them. On **iCloud Drive**, the sync from `cube666` (dev machine) would overwrite those files back to the committed version **before xcodebuild started**, silently reverting the bump. Fix: `bump.sh patch/major/minor` now immediately `git commit`s `VERSION.txt`, `DriverVersion.hpp`, and `project.pbxproj` after bumping.

**Consequence of old bugs:** Multiple sessions where we compiled Fix N but the running dext was still Fix N-1 because macOS saw `CFBundleVersion=21` on both old and new dext and skipped the upgrade. Always verify with:
```bash
systemextensionsctl list  # should show new version after install
log stream ... | grep "IR override wire DBS"  # should show new value
```

**NEVER use `./build.sh --no-bump` for a hardware test build** unless you manually bumped the version first and the new `CURRENT_PROJECT_VERSION` is committed to git.

## Czytanie logów dextu (Tahoe / Mac Studio)

### ⚠️ Dwie pułapki — zanim zaczniesz

**Pułapka 1 — `log` w zsh to wbudowana funkcja matematyczna:**
```bash
log stream ...    # ← uruchamia zsh-builtin, NIE /usr/bin/log — zero logów!
/usr/bin/log stream ...  # ✅ poprawne
```
Zawsze używaj pełnej ścieżki lub alias: `alias log=/usr/bin/log`.

**Pułapka 2 — predykat `process == "ASFWDriver"` nie działa:**
Dext nie uruchamia się jako osobny proces o tej nazwie — logi trafiają przez kernel. Predykat po procesie nie łapie nic. Dext używa też `OS_LOG_DEFAULT` (bez subsystemu), więc `subsystem == "net.mrmidi.ASFW"` też nie działa.

### Metoda, która działa (potwierdzona na Tahoe 2026-05-25)

Logi dextu pojawiają się jako:
```
kernel: (net.mrmidi.ASFW.ASFWDriver.dext) [Kategoria] Treść
```
Dlatego `grep "ASFWDriver.dext"` jest właściwym filtrem.

```bash
# Live stream — Terminal 1 podczas testu hardware:
log stream --debug --info 2>/dev/null | grep "ASFWDriver.dext"

# Po zdarzeniu — ostatnie N minut:
log show --last 10m --debug --info 2>/dev/null | grep "ASFWDriver.dext"

# Filtrowanie po kategorii (Isoch, IR, IT, SYT itp.):
log stream --debug --info 2>/dev/null | grep "ASFWDriver.dext" | grep -E "(Isoch|IR|IT|syt|Streaming|Started|Poll|CIP|Underrun)"

# Dext + coreaudiod razem (pełny obraz AudioDriverKit):
log stream --debug --info 2>/dev/null | grep -E "(ASFWDriver\.dext|coreaudiod.*StartIO|HALC)"
```

### Dlaczego `--predicate` z subsystemem nie działa

Dext używa `OS_LOG_DEFAULT` (bez nazwanego subsystemu). Gdyby używał `os_log_create("net.mrmidi.ASFW", "Isoch")`, działałoby:
```bash
log stream --predicate 'subsystem == "net.mrmidi.ASFW"' --level debug  # ← nie działa teraz
```
Zmiana wymaga refaktoru logowania w całym driverze — TODO, niezrobione.

## Architecture

### ASFWDriver Subsystems

The driver is organized around these functional layers (all under `ASFWDriver/`):

| Directory | Responsibility |
|-----------|---------------|
| `Hardware/` | OHCI MMIO register layout, interrupt/event definitions |
| `Bus/` | Bus reset handling, Self-ID decode, topology, gap count optimization, generation tracking |
| `Async/` | Full async TX/RX pipeline: commands, DMA contexts (AT/AR), descriptor rings, label allocation, transaction tracking |
| `Isoch/` | Isochronous TX (working) and RX (WIP): OHCI DMA descriptors, AM824/CIP encoding, SYT timestamps, AudioDriverKit integration |
| `ConfigROM/` | Config ROM build, staging, reading/scanning from devices |
| `Discovery/` | FireWire device and unit enumeration (`FWDevice`, `FWUnit`) |
| `IRM/` | Isochronous Resource Manager: bandwidth and channel allocation |
| `Protocols/AVC/` | AV/C command layer: FCP transport, Music Subunit, stream formats, PCR space |
| `Controller/` | Controller state machine and lifecycle |
| `UserClient/` | DriverKit user-client interface (`.iig`), request handlers, wire serialization formats |
| `Shared/` | Shared rings, DMA memory manager, payload handles |
| `Common/` | `FWCommon.hpp`, barrier utilities |
| `Logging/` | Structured logging |

Key entry points:
- `ASFWDriver/ASFWDriver.iig` / `ASFWDriver.cpp` — driver class and `Start`/`Stop`
- `ASFWDriver/UserClient/Core/ASFWDriverUserClient.iig` — user-client interface
- `ASFWDriver/Isoch/Audio/ASFWAudioDriver.iig` — AudioDriverKit engine

### Isochronous Audio Pipeline

```
CoreAudio → AudioRingBuffer → PacketAssembler → IsochTransmitContext → OHCI IT DMA → FireWire Bus
FireWire Bus → OHCI IR DMA → IsochReceiveContext → StreamProcessor → AM824Decoder → CoreAudio
```

Audio device publication: `AVCDiscovery` creates `ASFWAudioNub` with discovered capabilities; `ASFWAudioDriver` matches on the nub and registers `IOUserAudioDevice` with CoreAudio HAL.

### Async Transaction Flow

Command → `ATContextBase` → descriptor builder → `DescriptorRing` → OHCI DMA → interrupt → `ARPacketParser` → `PacketRouter` → `TransactionManager` completion callback (via `LabelAllocator` tLabel matching).

### Config ROM Subsystem (`ASFWDriver/ConfigROM/`)

The Config ROM pipeline is split into small, single-purpose components:

| Component | Role |
|-----------|------|
| `ConfigROMBuilder` | Builds the local node's ROM image (quadlet array + CRC) |
| `ConfigROMStager` | Programs OHCI shadow registers and stages the local ROM (casts isolated in `MemoryMapView`) |
| `ROMReader` | Issues async **quadlet** reads against Config ROM address space at `0xFFFFF0000400` |
| `ROMScanner` | FSM-driven multi-node discovery; callback-based `Start()` completes once per generation request |
| `ConfigROMParser` | Pure parsing helpers (`ParseBIB`, `ParseTextDescriptorLeaf`, bounded scans) |
| `ConfigROMStore` | Thread-safe cache of discovered ROMs |

**Bus Info Block quadlet layout (TA 1999027 + IEEE 1212):**
```
Quadlet 0 (header):    [31:24] bus_info_length  [23:16] crc_length  [15:0] crc
Quadlet 1 (bus name):  0x31333934 ("1394")
Quadlet 2 (bus opts):  [31]irmc [30]cmc [29]isc [28]bmc [27]pmc
                       [23:16]cyc_clk_acc  [15:12]max_rec
                       [11:10]reserved  [9:8]max_ROM  [7:4]generation  [3]reserved  [2:0]link_spd
Quadlets 3–4:          GUID (hi, lo)
```

Use `ASFW::FW::DecodeBusOptions(q2)` / `EncodeBusOptions(d)` / `SetGeneration(q2, gen)` from `FWCommon.hpp`. **Never** access bus options bits directly. The old `BIBFields` namespace had every position wrong (it read from quadlet 0 instead of quadlet 2).

**Text descriptor leaf layout (IEEE 1212-2001 Figure 28):**
```
+0: [leaf_length:16][crc:16]
+1: [descriptor_type:8][specifier_ID:24]  — must be 0x00000000 for minimal ASCII
+2: [width:8][character_set:8][language:16] — must be 0x00000000 for minimal ASCII
+3..: ASCII characters, big-endian packed, NUL-terminated
```
`typeSpec` is at `+1`, **not** `+2`. Stop parsing at the first NUL byte.

**`ROMScanner` one-shot completion guard:**
`CheckAndNotifyCompletion()` is called from async callback sites. It fires the per-scan completion exactly once when all nodes reach `Complete`/`Failed` and `InflightCount() == 0`. It uses the `ROMScannerCompletionManager` latch (reset by `Start()` / `Abort()`) to prevent double-firing: queued `ScheduleAdvanceFSM()` dispatches can arrive after the first completion, see the same terminal state, and try to signal again.

**`EnsurePrefix` pattern:**
When `OnRootDirComplete` needs data beyond the root directory (leaves, unit dirs), it calls `EnsurePrefix(nodeId, requiredTotalQuadlets, completionCallback)` which transparently grows `node.partialROM.rawQuadlets` via additional async reads. The completion lambda chains further `EnsurePrefix` calls for nested structures (text leaves, descriptor directories, unit directory entries). Always call `ScheduleAdvanceFSM()` at the end of `EnsurePrefix` callbacks, never `AdvanceFSM()` directly (re-entrancy guard).

**`ROMReader` header-first mode:**
Pass `count=0` to `ReadRootDirQuadlets()` to enable autosize: the reader issues a 4-byte header read first, extracts `entry_count` from bits **[31:16]** of the directory header (not `[15:0]` — that's the CRC field), then reads the exact number of entries. Capped at 64 entries.

### Reference Material (internal, not public)

- `docs/linux/` — Linux `firewire-ohci` driver (authoritative for descriptor layout)
- `docs/linux/motu/` — Linux MOTU FireWire driver (snapshot `f5e5d35`): `motu-protocol-v3.c`, `amdtp-motu.c`, `motu.h`. **Autorytatywne źródło dla kanałów MOTU 828 MK3 i DBS.** Patrz `docs/linux/motu/README.md`.
- `docs/IOFireWireFamily/` — Apple's original FireWire kext source
- `docs/IOFireWireAVC/` — Apple's AV/C protocol implementation
- `docs/ohci/` — OHCI specification

## Critical Rules and Gotchas

**Endianness:** OHCI descriptor headers are little-endian; IEEE 1394 wire payloads are big-endian. Use `ToBusOrder`/`FromBusOrder` (defined in `FWCommon.hpp`) explicitly. Never assume.

**IT descriptor layout:** The `OUTPUT_MORE_IMMEDIATE` skip address lives at offset `0x08` (Branch Word), **not** `0x04`, despite some OHCI 1.1 diagrams. Follow Linux `firewire-ohci` + Apple validated behavior. See `ASFWDriver/Isoch/README.md`.

**Constants:** All OHCI hardware register constants go in `ASFWDriver/Hardware/OHCIConstants.hpp` (single source of truth). Never define them in `.cpp` files or class headers.

**OHCI timing:** Context stop/quiesce requires polling with timeout and escalating delays (5µs → 255µs). Do not assume immediate hardware response.

**DMA coherency:** Call `OSSynchronizeIO`/`IoBarrier` after writing descriptors before waking hardware. Read descriptor status fields before acting on completion data.

**IIG files:** `.iig` interface files require Xcode's IIG preprocessor to generate `.iig.cpp`. CMake builds exclude these; production builds must use Xcode.

**IIG subclassing rules:**
- `Create()` and `init()` are **not inherited** — each subclass must declare its own as `LOCALONLY`.
- `IMPL(ClassName, Method)` is only for methods **defined in that class's own `.iig`** — it generates `ClassName_Method_Args` which IIG only emits for the defining class.
- **Overriding a parent's method** uses plain C++ (`kern_return_t ClassName::Method(...)`), never `IMPL`.
- **No `SUPERDISPATCH`** in child overrides — call the equivalent setter directly (e.g. `SetSampleRate()`, `SetControlValue()`) to confirm the change to the HAL.
- **No `virtual` keyword** in override declarations in `.iig` — use `kern_return_t Foo() override;` not `virtual kern_return_t Foo() override;`.
- **`init()` first param type matters** — check the SDK header: `IOUserAudioDevice::init()` takes `IOUserAudioDriver*`, not `IOService*`. Wrong type → compile error in `super::init()` call.
- **Forward declarations are not enough** — if `ControllerCore.hpp` only forward-declares `IAVCDiscovery`, calling any method on it requires the full `#include "Protocols/AVC/IAVCDiscovery.hpp"` in the `.cpp`.

**Test isolation:** All C++ tests compile with `ASFW_HOST_TEST` defined, which stubs out DriverKit APIs. Logic tested this way cannot cover actual hardware interaction.

**DriverKit SDK strictness vs. local CMake:** CMake builds use the system clang which includes many standard headers transitively (e.g. `<algorithm>`, `<iterator>`). The DriverKit SDK target in Xcode is stricter — if you don't `#include` it explicitly, it's not available. Always add explicit includes for anything you use from `<algorithm>`, `<iterator>`, `<utility>`, etc. when touching files compiled in the DriverKit target.

**`FW::Generation` has no implicit conversion to `uint32_t`:** The struct has `explicit` constructor and no `operator uint32_t()`. `static_cast<uint32_t>(someGeneration)` is a compile error on DriverKit25.4. Use `.value` directly or copy: `const FW::Generation gen = record->gen;`.

**`CODE_SIGNING_ALLOWED=NO` does NOT suppress scheme PostActions:** Passing this flag to `xcodebuild` disables Xcode's built-in code signing phases, but custom shell scripts in scheme PostActions still run unconditionally. If a PostAction calls `codesign`, it will fail on CI runners without the developer certificate. Guard the script with `security find-identity` to detect missing identities and exit cleanly.

## Code Patterns

- **Error handling:** `std::expected<T, E>` — no exceptions in driver code. Mark all error-returning functions `[[nodiscard]]`.
- **CRTP** for compile-time context role enforcement (AT Request vs AT Response, etc.).
- **RAII** for all resources — IOLock wrappers, DMA buffers, etc.
- **`std::span`** for non-owning array views; no raw pointer arithmetic unless interfacing with C APIs.
- **`constexpr`/`static_assert`** for compile-time invariant checking — one wrong bit shift causes silent bus errors.
- Reference OHCI spec sections in comments, e.g., `// OHCI §7.2.3`.

## Swift App (ASFW/)

Uses Swift 6 strict concurrency. All cross-actor data must be `Sendable`. Use `actor` isolation correctly. The app is required to install DriverKit extensions via `systemextensionsctl`.

## Development Environment

**MacBook Pro (M3 Max) — dwie partycje:**
- **Wewnętrzny SSD:** macOS Sequoia — dev machine (build, testy jednostkowe, Ghidra/DTrace na MOTU kext)
- **Zewnętrzny SSD:** macOS Tahoe 26.5.1 — środowisko hardware testów z MOTU 828mk3

**Od sesji 29 (2026-06-05): hardware testy na MacBooku z Tahoe (zewnętrzny SSD), nie na Mac Studio.**

Boot-args na Tahoe (MacBook): `amfi_get_out_of_my_way=1 cs_enforcement_disable=1`

Cert na Tahoe: `Apple Development: j.slipiec@gmail.com (239NB3LFDQ)` (MacBook Pro, team `4MJNRC8SW5`).
Po świeżej instalacji wymagane pobranie Apple intermediate CA:
```bash
curl -s https://www.apple.com/certificateauthority/AppleWWDRCAG3.cer | security import /dev/stdin -k ~/Library/Keychains/login.keychain-db
```

**Mac Studio** (macOS Tahoe, wewnętrzny) — backup, nieużywany aktywnie od sesji 29.

**cmake** is installed via Homebrew and NOT on the default PATH. Always prefix or export:
```bash
export PATH="/opt/homebrew/bin:$PATH"
cmake -S tests -B build/tests_build
cmake --build build/tests_build -- -j$(sysctl -n hw.ncpu)
ctest --test-dir build/tests_build
```

**Running a single test suite:**
```bash
ctest --test-dir build/tests_build -V -R IsochRxDmaRing
```

## CodeGraph MCP

The project is indexed with CodeGraph (local SQLite graph of all symbols). Index lives in `.codegraph/codegraph.db`. The MCP server is configured in `../.mcp.json` (one level above ASFireWire, in the FireWire project root).

## ⛔ BEZWZGLĘDNY ZAKAZ: grep / find / Bash do szukania kodu

**NIGDY nie używaj `grep`, `find`, ani Bash do eksploracji kodu.** To jest bezwzględna reguła bez wyjątków.

Jedyne dozwolone narzędzia do lokalizacji symboli i plików:
- `codegraph_search` — znajdź klasy, metody, pliki po nazwie
- `codegraph_context` — kontekst dla zadania (entry points + related symbols)
- `codegraph_callers` — kto wywołuje dany symbol
- `codegraph_callees` — co wywołuje dany symbol
- `codegraph_node` — pełny kod konkretnego węzła
- `codegraph_impact` — co zostanie dotknięte zmianą symbolu

`grep`/`find`/Bash są dopuszczalne **wyłącznie** gdy CodeGraph nie zwróci wyniku po 2 próbach, i tylko z wyraźną adnotacją `# fallback: CodeGraph nie znalazł`.

**ZAWSZE CodeGraph jako pierwszy krok.** Czytanie pliku bez wcześniejszego `codegraph_search` / `codegraph_context` = błąd procesu.

**WAŻNE — zawsze przekazuj `projectPath`:** MCP serwer jest skonfigurowany w `../.mcp.json` (katalog `FireWire/`), więc jego CWD to `FireWire/`, nie `ASFireWire/`. Bez explicit `projectPath` CodeGraph szuka bazy w złym katalogu i zwraca "not initialized". Każde wywołanie musi mieć:
```
# Mac Studio (kuba, hardware test):
projectPath: "/Users/kuba/Library/Mobile Documents/com~apple~CloudDocs/FireWire/ASFireWire"

# Dev machine (cube666, development):
projectPath: "/Users/cube666/Library/Mobile Documents/com~apple~CloudDocs/FireWire/ASFireWire"
```

**Re-index after adding/moving files:**
```bash
export PATH="$HOME/.npm-global/bin:/opt/homebrew/opt/node@22/bin:$PATH"
NODE_OPTIONS="--max-old-space-size=4096" codegraph build --no-incremental .
```

**Current index stats:** 624 files · 18 300 nodes · 26 342 edges (codegraph 3.10.0, graph.db).


Wytyczne Behawioralne
Kompromis: Niniejsze wytyczne stawiają ostrożność i precyzję ponad szybkość. Przy trywialnych zadaniach — kieruj się własnym osądem.

1. Pomyśl, zanim zaczniesz kodować

Nie zakładaj. Nie ukrywaj dezorientacji. Przed implementacją: - Jasno określ założenia. Jeśli masz wątpliwości — zapytaj. - Jeśli istnieje wiele interpretacji — przedstaw je, nie dokonuj wyboru po cichu. - Jeśli istnieje prostsze podejście — powiedz o tym. Sprzeciwiaj się, gdy jest to uzasadnione. - Jeśli coś jest niejasne — zatrzymaj się. Nazwij to. Zapytaj.

2. Prostota przede wszystkim

Minimalna ilość kodu, która rozwiązuje problem. Żadnych spekulacji: - Brak funkcji wykraczających poza to, o co proszono - Brak abstrakcji dla kodu jednorazowego użytku - Żadnej „elastyczności" ani „konfigurowalności", o którą nie proszono - Żadnej obsługi błędów dla niemożliwych scenariuszy - Jeśli napisałeś 200 linii, a wystarczyłoby 50 — napisz od nowa

Zadaj sobie pytanie: „Czy doświadczony inżynier (senior) uznałby to za zbyt skomplikowane?" — jeśli tak, uprość.

3. Zmiany chirurgiczne

Dotykaj tylko tego, co musisz: - Nie „poprawiaj" sąsiedniego kodu bez pytania - Dopasuj się do istniejącego stylu - Niepowiązany martwy kod: wspomnij — nie usuwaj

Gdy Twoje zmiany tworzą „osierocone" elementy: - Usuń importy/zmienne/funkcje, które stały się nieużywane przez Twoje zmiany - Nie usuwaj sam wcześniej istniejącego martwego kodu (Poinformuj o nim wyraźnie), usuń wtedy gdy zostaniesz o to poproszony

## Git — zasady push

Repozytorium ma dwa remote:
- `origin` = `mrmidi/ASFireWire.git` — **NIE należy do użytkownika**, brak uprawnień do push
- `cube666` = `cube666999/ASFireWire-by-cube666999.git` — **fork użytkownika**, tu pushujemy

**ZAWSZE przed pushem:**
1. Sprawdź `git remote -v`
2. Używaj jawnie: `git push cube666 main`
3. Nigdy nie używaj samego `git push` (domyślnie trafia na `origin` = brak uprawnień)


## ✅ DEXT DZIAŁA — Status po sesjach debugowania (2026-05-18)

### Co zostało naprawione (w kolejności)

1. **ENOEXEC przy exec dextu** — Apple Silicon wymaga **Apple Developer cert** (nie ad-hoc) do uruchomienia DriverKit dextu. `amfi_get_out_of_my_way=1` NIE pomaga przy arm64e exec.

2. **Brak arm64e slice** — `ONLY_ACTIVE_ARCH = YES` na poziomie projektu + Apple Silicon = Xcode budował tylko `arm64`. DriverKit dexty na Apple Silicon **wymagają `arm64e`**. Fix: dodano do `project.pbxproj` dla ASFWDriver (Debug i Release):
   ```
   ARCHS = "arm64e x86_64";
   ONLY_ACTIVE_ARCH = NO;
   ```

3. **teamID mismatch** — app musiała być podpisana tym samym teamID (4MJNRC8SW5) co dext.

4. **Provisioning errors Xcode 26** — `com.apple.developer.driverkit.userclient-access` i `system-extension.install` wymagają provisioning profile nawet przy ad-hoc. Fix: `App_build.entitlements` (build-time, bez restricted entitlements), `App.entitlements` (post-build re-sign).

### Aktualny stan budowania

- **ASFWDriver**: `CODE_SIGN_IDENTITY = "-"` (ad-hoc), `AD_HOC_CODE_SIGNING_ALLOWED = YES`, `ARCHS = "arm64e x86_64"`, `ONLY_ACTIVE_ARCH = NO`
- **ASFW app**: `CODE_SIGN_ENTITLEMENTS = ASFW/App_build.entitlements` (bez userclient-access), ad-hoc
- **Post-build scheme action**: re-signs: (1) app z `App.entitlements`, (2) dext z `ASFWDriver.entitlements` — oba z `"Apple Development: j.slipiec@gmail.com (239NB3LFDQ)"`
- **Uwaga**: post-action czasem nie podpisuje app. Fallback — ręczny codesign z Terminala po każdym buildzie:
  ```bash
  codesign --force --options runtime --sign "Apple Development: j.slipiec@gmail.com (239NB3LFDQ)" \
    --entitlements "/Users/kuba/Library/Mobile Documents/com~apple~CloudDocs/FireWire/ASFireWire/ASFW/App.entitlements" \
    --timestamp=none \
    /Users/kuba/Library/Developer/Xcode/DerivedData/ASFW-*/Build/Products/Debug/ASFW.app
  ```

### Aktualny stan dextu (potwierdzony 2026-05-18 ~13:00)

```
systemextensionsctl list:
  net.mrmidi.ASFW.ASFWDriver  [activated enabled]  teamID=4MJNRC8SW5

IORegistry:
  ASFWDriver <class IOUserService, registered, matched, active, CurrentPowerState=3>
  IOUserServer(net.mrmidi.ASFW.ASFWDriver) <registered, matched, active>
  ASFWDriverUserClient <IOUserUserClient> — tworzone przy połączeniu apki

ps aux:
  _driverkit  3458  /Library/SystemExtensions/F5E94065-.../net.mrmidi.ASFW.ASFWDriver.dext/...
```

Dext przeżywa reboot. Instalacja w `/Library/SystemExtensions/`.

### ✅ Potwierdzony stan (2026-05-18 ~14:12)

**Podłączone urządzenie: MOTU 828 MK3** (Vendor ID: 0x0001F2, GUID: 0x0001F20000087236)

Device Discovery pokazuje:
- Node 0 • Generation 3 • **Ready**
- Unit: Spec ID: 0x0001F2, SW Version: 0x000015, ROM Offset: 5 quadlets
- Model ID: `EffectiveModelId()` zwraca `unitSwVersion=0x000015` → `kMOTUV3` ✅ (Fix 11)

Sequoia diagnostic (2026-05-25) potwierdził:
- `fNumFWOutputChannels 14` (IR, device→host) · `fNumFWInputChannels 18` (IT, host→device)
- MOTU kext używa `FireWireBlockRWCommand` (potwierdza Fix 10)
- Bus reset recovery działa tak samo jak nasz `AudioCoordinator`

### Następne kroki (stan 2026-05-28, sesja 17 — Fix 22 wdrożony)

**✅ Osiągnięte (sesja 16):**
- `AudioDeviceStart (err 0)` potwierdzony ✅
- IO aktywne 3+ minuty bez crashu ✅
- IR: 11 965 pkts/s od MOTU ✅
- IT: DBS=21 na wire (Fix 21) ✅
- CoreAudio: `HALS_IORawClock` re-anchoring adaptuje się (system działa) ✅

**✅ Osiągnięte (sesja 17):**
- Zidentyfikowana przyczyna "piszczenia przez 3s → cisza": SYT gate timeout ✅
- Fix 22 (SYT gate bypass dla MOTU V3) — built 2026-05-28, uncommitted ✅
- Logi potwierdziły: IT nadawało **20 181 data packets** (Spotify dotarło do MOTU!) ✅
- Brama SYT zabijała IT po dokładnie 3000ms (MOTU V3 zawsze `syt=0x0000`) ✅

**Priorytet 1 — Test audio z Fix 22:**
- Wymagany **restart komputera** (dext upgrade z aktywnym AudioDriverKit)
- Uruchomić ASFW.app z pulpitu (lub DerivedData)
- Odtworzyć Spotify z MOTU 828mk3 jako wyjściem
- W logach szukać `[Isoch] SYT gate bypassed` (nie `❌ StartTransmit SYT timeout`)
- Jeśli słyszysz dźwięk → sukces → commit Fix 21 + Fix 22 razem → DevLog archiwum

**Priorytet 2 — Fix HALS_IORawClock re-anchoring:**
- Zastąpić `mach_absolute_time()` w `PerformIO` czytaniem OHCI `CurrentIsochronousCycleTime`
- OHCI register offset `0x1E8`: bits[25:12]=cycleCount (0-7999), bits[11:0]=cycleOffset
- Przeliczenie: `cycleCount/8000 × timebaseFreq` → hostTime zsynchronizowany z 1394 bus

**Priorytet 3 (po audio) — Rozszerzyć do 18 kanałów IT / 14 IR:**
- Teraz `ASFWAudioNub` publikuje tylko "2 In / 2 Out"
- MOTU 828 MK3 ma 18ch IT (host→device) i 14ch IR (device→host)
- Zmiana: `outputChannelCount=18`, `inputChannelCount=14` w konfiguracji nuba

4. **Disable FCP spam dla MOTU V3** — AVCDiscovery wysyła FCP co ~2s do MOTU który ignoruje AVC

### Znany problem: OSSystemExtensionErrorDomain error 4 przy re-launch

Przy ponownym uruchomieniu apki (gdy dext już jest [activated enabled]), `activate()` zwraca error 4 (ExtensionNotFound/version match). **Fix zastosowany** w `DriverInstallManager.swift`: error 4 przy aktywacji traktowany jako sukces ("Extension already active"). Rebuild wymagany żeby fix zadziałał.

### Pliki konfiguracyjne (signing)

| Plik | Cel |
|------|-----|
| `ASFW/App_build.entitlements` | Build-time app entitlements (bez restricted) |
| `ASFW/App.entitlements` | Post-build re-sign entitlements (pełne) |
| `ASFWDriver/ASFWDriver.entitlements` | Dext entitlements (pełne, używane przy buildzie i post-sign) |
| `ASFW.xcodeproj/xcshareddata/xcschemes/ASFW.xcscheme` | Post-action: re-sign app → dext |

---

## Related Documents

### Dokumenty projektowe (czytaj jako pierwsze)

| Plik | Zawartość |
|------|-----------|
| `Focus.md` | **Aktywny plan pracy** — bieżący stan, etap 10 (MOTU V3), instrukcja hardware testu, lista rozwiązanych bugów |
| `DevLog.md` | Archiwum ukończonych etapów 1–9, logi sesji debugowania, szczegóły signing/build |
| `MOTU_828_MK3_BringUp.md` | **V3 register protocol** — mapa rejestrów, sekwencja StartStreaming, weryfikacja vs kext, czego NIE robić z MOTU |
| `CHANGES.md` | Changelog forka cube666999, link do GitHub |
| `README.md` | Ogólny opis projektu |
| `AGENTS.md` | Przewodnik dla AI assistants — architektura, wzorce, zasady |
| `REFACTOR_THOUGHTS.md` | Propozycje refaktoringu (żaden niezaimplementowany — tylko notatki) |
| `diagnostics/README.md` | Indeks captured logów z hardware testów (Sequoia + MOTU kext) |

### Dokumentacja techniczna (`documentation/`)

| Plik | Zawartość |
|------|-----------|
| `documentation/FWOHCI_IR.md` | Architektura IR (Isoch Receive) z dekompilacji Apple AppleFWOHCI — jak Apple implementuje DMA ring dla IR |
| `documentation/IRM_EXPLAINED.md` | Trace IRM protocol krok po kroku — jak Mac rezerwuje kanał i bandwidth (CAS na CHANNELS_AVAILABLE) |
| `documentation/COMPLETION_STRATEGIES.md` | Strategie completion dla AT async stack — kiedy sygnalizować finalizację |
| `documentation/ASYNC_COMPARE_SWAP.md` | Pełny flow CAS (tCode=0x2) — entry points, packet construction, completion |
| `documentation/ASYNC_READ_API.md` | Dokumentacja async read API |
| `documentation/PHY_COMMAND_CONTRACTS.md` | Kontrakty dla PHY commands i alpha PHY packets |
| `documentation/ieee1394_bus_reset.md` | IEEE 1394 bus reset state machine (modernizowana dokumentacja) |
| `documentation/ieee1394_tree_identification.md` | IEEE 1394 tree identification state machine |

### Referencje Linux MOTU (`docs/linux/motu/`)

| Plik | Zawartość |
|------|-----------|
| `docs/linux/motu/README.md` | **Szybka referencja** — kanały 828 MK3 (18 IT / 14 IR fixed), wzór DBS, wyjaśnienie DBS=21 |
| `docs/linux/motu/motu-protocol-v3.c` | Rejestry V3, sekwencja StartStreaming, detekcja ADAT, definicje `snd_motu_spec_828mk3_fw` |
| `docs/linux/motu/amdtp-motu.c` | AMDTP streaming: DBS = `1 + DIV_ROUND_UP((msg+pcm)*3, 4)`, PCM byte offset=10, SPH |
| `docs/linux/motu/motu.h` | Struktury: `snd_motu_spec`, `snd_motu_packet_format`, flagi, stałe V3 |

### Narzędzia diagnostyczne (`tools/`) — od mrmidi

| Plik | Zawartość |
|------|-----------|
| `tools/DMA_PROGRAM_MODES.md` | **AT DMA approaches** — 5 trybów (PATH1/PATH2/LINUX/APPLE1/APPLE2), Z nibble bug, APPLE2 jako rekomendowany |
| `tools/itprog.md` | **IT DMA ring reference** — diagram dla 48kHz blocking, Z=3 DATA / Z=2 NO-DATA, schedule [8,8,8,8,8,8,0,0] |

### READMEs subsystemów

| Plik | Zawartość |
|------|-----------|
| `ASFWDriver/README.md` | Główny przegląd sterownika — warstwy, wejścia, zależności |
| `ASFWDriver/Bus/README.md` | Bus management — bus reset, Self-ID, topology |
| `ASFWDriver/ConfigROM/README.md` | Config ROM subsystem — builder, stager, scanner, parser |
| `ASFWDriver/Controller/README.md` | Controller Core — lifecycle, state machine |
| `ASFWDriver/Isoch/README.md` | Isoch stack — IT/IR DMA, AM824, SYT, AudioDriverKit |
| `ASFWDriver/Shared/README.md` | Shared transfer stack — rings, DMA memory manager |
| `ASFWDriver/Async/Interfaces/IFireWireBusContract.md` | Kontrakt `IFireWireBusOps` — co musi implementować każdy bus provider |
| `ASFWDriver/Bus/IEEE1394-BusReset.md` | Specyfikacja IEEE 1394 bus reset (lokalna kopia/notki) |

---

## Test Stub Quirk — DMA Alignment

`HardwareInterface::AllocateDMA` in `tests/HardwareInterfaceStub.cpp` allocates the virtual buffer with **at least 4096-byte (page) alignment** (`effectiveAlign = max(4096, requested)`). This is intentional: the mock IOVA counter starts at `0x20000000` (page-aligned), so `DMAMemoryManager::AlignCursorToIOVA(4096)` only produces correct virtual-address alignment if `slabVirt_` is also page-aligned. Without this, the IOVA cursor aligns but the VA does not, breaking `IsochDMAMemoryManagerTest.PayloadSlicingAndPageAlignment`.
