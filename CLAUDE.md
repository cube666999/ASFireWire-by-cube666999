# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ASFW is a macOS DriverKit-based FireWire (IEEE 1394) driver restoring FireWire functionality removed in macOS Tahoe (26). It uses PCIDriverKit for user-space OHCI controller access and AudioDriverKit for CoreAudio integration.

Two components:
- **ASFWDriver/** — C++23 DriverKit driver extension (dext)
- **ASFW/** — Swift 6 control app and installer (required to install the dext)

## MOTU 828 MK3 (V3) — canonical hardware facts

> **Single source of truth lives in the `main` branch:** `documentation/MOTU_828_MK3_FACTS.md`
> (channels, DBS, rates, CLOCK_STATUS register, slot map, source hierarchy). Link there; do
> not copy numbers into other docs. Hard sources (El Capitan wire snoop + Linux) are authoritative
> over any hand-written summary — a CLAUDE.md summary once had these inverted and misled a fix.

Channel geometry @ 48 kHz (confirmed by El Cap wire + Linux spec + Sequoia kext):

| direction | roles | PCM | DBS |
|-----------|-------|:---:|:---:|
| host→device | IT · playback · `outputChannelCount` | **14** | 13 |
| device→host | IR · capture · `inputChannelCount` | **18** | 16 |

These must stay consistent in two places: `MOTUVendorProtocol::BuildRuntimeCaps` (drives the
wire) and `MOTU828Mk3Profile` (drives CoreAudio geometry + graph validation). `Tx` == host→device,
`Rx` == device→host. The MOTU clock register is `CLOCK_STATUS` (0x0b14): rate index in bits [15:8],
`0x02000000` is a write-only FETCH-PCM command bit (never read it back as status).

## Build Commands

**Primary build (Xcode — required for signing and producing `.dext`):**
```bash
./build.sh                        # Quiet build (errors/warnings only)
./build.sh --verbose              # Full xcodebuild output
./build.sh --no-bump              # Skip version bump
./build.sh --config Release       # Release build
```

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
./bump.sh patch     # Bump patch version and regenerate DriverVersion.hpp
./bump.sh refresh   # Regenerate version header only
```

## Wytyczne Behawioralne
**Kompromis:** Niniejsze wytyczne stawiają ostrożność i precyzję ponad szybkość. Przy trywialnych zadaniach — kieruj się własnym osądem.

### 1. Pomyśl, zanim zaczniesz kodować
Nie zakładaj. Nie ukrywaj dezorientacji. Przed implementacją:
- Jasno określ założenia. Jeśli masz wątpliwości — zapytaj.
- Jeśli istnieje wiele interpretacji — przedstaw je, nie dokonuj wyboru po cichu.
- Jeśli istnieje prostsze podejście — powiedz o tym. Sprzeciwiaj się, gdy jest to uzasadnione.
- Jeśli coś jest niejasne — zatrzymaj się. Nazwij to. Zapytaj.

### 2. Prostota przede wszystkim
Minimalna ilość kodu, która rozwiązuje problem. Żadnych spekulacji:
- Brak funkcji wykraczających poza to, o co proszono
- Brak abstrakcji dla kodu jednorazowego użytku
- Żadnej „elastyczności" ani „konfigurowalności", o którą nie proszono
- Żadnej obsługi błędów dla niemożliwych scenariuszy
- Jeśli napisałeś 200 linii, a wystarczyłoby 50 — napisz od nowa

Zadaj sobie pytanie: „Czy doświadczony inżynier (senior) uznałby to za zbyt skomplikowane?" — jeśli tak, uprość.

### 3. Zmiany chirurgiczne
Dotykaj tylko tego, co musisz:
- Nie „poprawiaj" sąsiedniego kodu bez pytania
- Dopasuj się do istniejącego stylu
- Niepowiązany martwy kod: wspomnij — nie usuwaj

Gdy Twoje zmiany tworzą „osierocone" elementy:
- Usuń importy/zmienne/funkcje, które stały się nieużywane przez **Twoje** zmiany
- Nie usuwaj sam wcześniej istniejącego martwego kodu (Poinformuj o nim wyraźnie), usuń wtedy gdy zostaniesz o to poproszony 

**Test:** Każda zmieniona linia powinna bezpośrednio wynikać z prośby użytkownika.

### 4. Wykonanie ukierunkowane na cel
| Zamiast | Zrób |
|---------|------|
| „Napraw bug" | Przeczytaj `[[Docs/context/bug-history.md]]` → napisz test który go odtwarza, potem spraw żeby przechodził |
| „Refaktoryzuj X" | Upewnij się że testy przechodzą przed i po |
| Wieloetapowe | Krótki plan: 1. [Krok] → weryfikacja: [Sprawdzenie] |

## Zasady Decyzyjne
- **Fazy:** Nie zaczynaj USB/komunikacji (Faza 2) dopóki Faza 1.5 nie zamknięta
- **Konflikt danych:** Nigdy nie zgaduj — pokaż rozbieżność z nazwami plików i wartościami
- **Hierarchia źródeł:** → [[Docs/BVERHUE-REFERENCE]]

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

⚠️ **Materiał referencyjny Linux/Apple/OHCI NIE jest w tym repo (dice)** — żyje w main branch
(`../ASFireWire/`). Linkuj tam, nie kopiuj:

- `../ASFireWire/docs/linux/` — Linux `firewire-ohci` driver (autorytatywny dla descriptor layout)
- `../ASFireWire/docs/linux/motu/` — Linux MOTU driver (kanały 828 MK3, DBS, sekwencja StartStreaming)
- `../ASFireWire/docs/IOFireWireFamily/` — Apple's original FireWire kext source
- `../ASFireWire/docs/IOFireWireAVC/` — Apple's AV/C protocol implementation
- `../ASFireWire/docs/ohci/` — OHCI specification

**Dice ma własną `documentation/`** (architektura specyficzna dla DICE/AudioDriverKit):
- `documentation/ZTS_AND_SYT.md` — ⭐ **kluczowy dla bieżącej pracy** — ZTS i SYT timing
- `documentation/FWOHCI_IR.md` — architektura IR (Isoch Receive) z dekompilacji Apple
- `documentation/IRM_EXPLAINED.md` — IRM protocol (rezerwacja kanału + bandwidth)
- `documentation/ASYNC_COMPARE_SWAP.md`, `ASYNC_READ_API.md`, `COMPLETION_STRATEGIES.md`,
  `PHY_COMMAND_CONTRACTS.md`, `ieee1394_bus_reset.md`, `ieee1394_tree_identification.md`

## Critical Rules and Gotchas

**Endianness:** OHCI descriptor headers are little-endian; IEEE 1394 wire payloads are big-endian. Use `ToBusOrder`/`FromBusOrder` (defined in `FWCommon.hpp`) explicitly. Never assume.

**IT descriptor layout:** The `OUTPUT_MORE_IMMEDIATE` skip address lives at offset `0x08` (Branch Word), **not** `0x04`, despite some OHCI 1.1 diagrams. Follow Linux `firewire-ohci` + Apple validated behavior. See `ASFWDriver/Isoch/README.md`.

**Constants:** All OHCI hardware register constants go in `ASFWDriver/Hardware/OHCIConstants.hpp` (single source of truth). Never define them in `.cpp` files or class headers.

**OHCI timing:** Context stop/quiesce requires polling with timeout and escalating delays (5µs → 255µs). Do not assume immediate hardware response.

**DMA coherency:** Call `OSSynchronizeIO`/`IoBarrier` after writing descriptors before waking hardware. Read descriptor status fields before acting on completion data.

**IIG files:** `.iig` interface files require Xcode's IIG preprocessor to generate `.iig.cpp`. CMake builds exclude these; production builds must use Xcode.

**Test isolation:** All C++ tests compile with `ASFW_HOST_TEST` defined, which stubs out DriverKit APIs. Logic tested this way cannot cover actual hardware interaction.

**Wire compatibility is the correctness bar.** ASFW is general-purpose but typically tested only against audio hardware. For untestable device classes/topologies, "correct" means *behaves like the in-tree reference stacks* (`firewire/` = Linux, authoritative for OHCI mechanism; `IOFireWireFamily.kmodproj/` = Apple, authoritative for policy/ordering). Spec is the floor, the references are the ceiling. Internal architecture is free; observable **bus behavior must conform**. Only deviate from the references with hardware in hand — "cleaner than the reference" is an untested behavior. This is sharpest at the bus-policy layer (root/cycle-master/reset/gap), which is global state affecting every device at once.

## Code Patterns

- **Error handling:** `std::expected<T, E>` — no exceptions in driver code. Mark all error-returning functions `[[nodiscard]]`.
- **CRTP** for compile-time context role enforcement (AT Request vs AT Response, etc.).
- **RAII** for all resources — IOLock wrappers, DMA buffers, etc.
- **`std::span`** for non-owning array views; no raw pointer arithmetic unless interfacing with C APIs.
- **`constexpr`/`static_assert`** for compile-time invariant checking — one wrong bit shift causes silent bus errors.
- Reference OHCI spec sections in comments, e.g., `// OHCI §7.2.3`.

## Swift App (ASFW/)

Uses Swift 6 strict concurrency. All cross-actor data must be `Sendable`. Use `actor` isolation correctly. The app is required to install DriverKit extensions via `systemextensionsctl`.

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

**WAŻNE — zawsze przekazuj `projectPath`:** MCP serwer jest skonfigurowany w `ASFireWire-dice/.mcp.json`, ale jego CWD może być inne. Bez explicit `projectPath` CodeGraph może zwrócić "not initialized". Każde wywołanie musi mieć:
```
projectPath: "/Users/cube666/Library/Mobile Documents/com~apple~CloudDocs/FireWire/ASFireWire-dice"
```

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

## ⚠️ iCloud Drive + DerivedData — ZAWSZE używaj `--derived /tmp/ASFWBuild`

iCloud File Provider odtwarza xattry na plikach w lokalnym DerivedData natychmiast po ich usunięciu.
Xcode's codesign failuje z `resource fork, Finder information, or similar detritus not allowed`.

**Jedyne działające rozwiązanie — zawsze buduj poza iCloud (`/tmp`):**
```bash
./build.sh --derived /tmp/ASFWBuild --deploy   # ← hardware test build + deploy na Desktop
./build.sh --derived /tmp/ASFWBuild            # ← zwykły build
./build.sh --derived /tmp/ASFWBuild --clean    # ← full rebuild (usuwa DerivedData)
```

**NEVER use `./build.sh --no-bump`** dla hardware test build — macOS pomija upgrade dextu jeśli CFBundleVersion się nie zmienił.

## Środowisko deweloperskie

| Maszyna | System | Rola |
|---------|--------|------|
| MacBook Pro (M3 Max) | macOS Tahoe 26.5.1 (zewnętrzny SSD) | ✅ **Aktywne** — build + hardware testy + Claude Code |
| MacBook Pro (M3 Max) | macOS Sequoia (wewnętrzny SSD) | Diagnostyka MOTU kext (DTrace/IORegistry) |

**Boot-args na Tahoe:** `amfi_get_out_of_my_way=1 cs_enforcement_disable=1`

**Build hardware-test (z deployem — tak jak w main):**
```bash
./build.sh --derived /tmp/ASFWBuild --deploy
```
Wynik: `~/Desktop/ASFW_dice_vNN.app` (podpisany `Apple Development`, dext + app, signature zweryfikowana).
Sam build bez deployu: `./build.sh --derived /tmp/ASFWBuild` → `/tmp/ASFWBuild/Build/Products/Debug/ASFW.app`.
Pełny rebuild: dodaj `--clean` (usuwa DerivedData przed buildem).

> **`deploy_app()` w build.sh** (dodane 2026-06-15, port z main): kopiuje app do `/tmp` (omija iCloud
> xattr), strippuje xattry, podpisuje **najpierw dext** (`ASFWDriver/ASFWDriver.entitlements`) potem
> **app `--deep`** (`ASFW/App.entitlements`) tożsamością `Apple Development` z keychain, weryfikuje
> podpis, kopiuje na Desktop jako `ASFW_dice_v${CFBundleVersion}.app`.

**Logi dextu (Tahoe):**
```bash
/usr/bin/log stream --predicate 'senderImagePath CONTAINS "ASFWDriver"' --level debug
```

**git push:** zawsze `git push cube666 dice-motu` (patrz sekcja `## Git — zasady push` niżej)

## Dokumenty projektowe

| Dokument | Zawartość |
|----------|-----------|
| **[`Focus.md`](Focus.md)** | ⭐ **Aktywny plan pracy — dice branch.** Bieżący stan ZTS debug, ostatnie fixy, następne kroki. Przeczytaj tu zanim zaczniesz. |
| **[`DevLog.md`](DevLog.md)** | Historia sesji dice — bugi, fixy, decyzje architektoniczne (v9–v11 i dalej). |
| **[`docs/MOTU_V3_DICE_TODO.md`](docs/MOTU_V3_DICE_TODO.md)** | Lista bugów MOTU V3 z właściwymi rozwiązaniami (Bug 1: IR geometry, Bug 2: IT encoder, Bug 3: SYT). |
| **[`../ASFireWire/Focus.md`](../ASFireWire/Focus.md)** | Plan pracy nad **main branch (zero-copy driver)** — inny cel: PCM byte position bug (MOTU Main Out). |
| **[`../ASFireWire/documentation/MOTU_828_MK3_FACTS.md`](../ASFireWire/documentation/MOTU_828_MK3_FACTS.md)** | KANON — jedyne źródło prawdy dla faktów sprzętowych MOTU 828 MK3 (kanały, DBS, rate, sloty). |
| **[`../ASFireWire/documentation/MOTU_V3_WIRE_GROUNDTRUTH.md`](../ASFireWire/documentation/MOTU_V3_WIRE_GROUNDTRUTH.md)** | ⭐ **Ground-truth z kabla (El Cap snoop + Linux)** — DBS=13/IT, DBS=16/IR, CIP format, SPH Δ=512, PCM slot map. Niezbędne przy implementacji IT encodera (Bug 2) i IR decodera (Bug 1). |
| **[`../ASFireWire/documentation/SESSION_2026-06-12_GROUNDTRUTH.md`](../ASFireWire/documentation/SESSION_2026-06-12_GROUNDTRUTH.md)** | Sesja przełomowa — diagnoza slotu PCM: slot 8 (byte 34) zamiast 0/1 (byte 10/13). Kontekst dla mapowania kanałów MOTU. |
| **[`../ASFireWire/tools/fw_isoch_snoop.c`](../ASFireWire/tools/fw_isoch_snoop.c)** | Naprawiony snoop tool (fix: `__builtin_bswap32` na nagłówku). Zbudowany na MB2009 `/tmp/fw_isoch_snoop`. Używany do przechwytywania IT z kabla. |

## Git — zasady push

To repo (dice) ma dwa remote:
- `origin` = `mrmidi/ASFireWire.git` — **NIE należy do użytkownika**, brak uprawnień do push
- `cube666` = `cube666999/ASFireWire-by-cube666999.git` — **fork użytkownika**, tu pushujemy

**Branch roboczy dice = `dice-motu`** (NIE `main` — `main`/`dice-motu` to różne branche; main branch zero-copy
żyje w osobnym repo `../ASFireWire`).

**ZAWSZE przed pushem:**
1. Sprawdź `git remote -v` i `git branch` (potwierdź że jesteś na `dice-motu`)
2. Używaj jawnie: `git push cube666 dice-motu`
3. Nigdy nie używaj samego `git push` (domyślnie trafia na `origin` = brak uprawnień)

> ⚠️ **Uwaga bezpieczeństwa:** URL remote `cube666` ma wbudowany token GitHub (`ghp_…`) w `.git/config`.
> To pre-existing stan. Nie wypisuj tokena w logach/outpucie ani nie commituj go do śledzonych plików.
> Jeśli token wycieknie — użytkownik powinien go zrewokować na GitHub i przestawić remote na SSH.

## System pracy — kiedy notować, commitować, pushować

Prowadź dokumentację i historię **sam, bez przypominania** — to część zadania, nie dodatek.

**Notuj na bieżąco (`Focus.md` / `DevLog.md`):**
- Po każdym fixie: numer wersji (CFBundleVersion), co zmienia, w którym pliku, wynik testu hardware.
- `Focus.md` = ZAWSZE aktualny stan + następny krok. `DevLog.md` = archiwum rozwiązanych bugów.
- Potwierdzone fakty sprzętowe → **NIE kopiuj liczb**, linkuj kanon `../ASFireWire/documentation/MOTU_828_MK3_FACTS.md`.
- Aktualizuj memory (`MEMORY.md` + plik faktu) gdy ustalisz coś nieoczywistego i trwałego.

**Commituj gdy:**
- Fix jest kompletny i zweryfikowany (build przeszedł / test hardware potwierdził), LUB
- Kończysz spójny etap dokumentacji/refaktoru. Nie commituj w połowie niedziałającej zmiany.
- Commit message po **angielsku** (mrmidi czyta commity dice). Format `fix(motu-v3): …`, `docs: …`, `chore: …`.

**Pushuj gdy:** commit reprezentuje stabilny punkt który warto mieć w zdalnym repo (działający fix,
ukończona dokumentacja). `git push cube666 dice-motu`. Nie pushuj eksperymentalnych WIP bez powodu.

**Czego NIE robić (lekcje z pierwszego sterownika):**
- ❌ NIE `./build.sh --no-bump` na hardware test (macOS pominie upgrade dextu — patrz sekcja version bump).
- ❌ NIE buduj w iCloud DerivedData (xattr breakage — zawsze `--derived /tmp/ASFWBuild`; pełny rebuild = `--clean`).
- ❌ NIE używaj `grep`/`find`/`Bash` do szukania kodu (CodeGraph first — patrz zakaz wyżej).
- ❌ NIE `log stream` bez `/usr/bin/` (zsh builtin) ani z predykatem po procesie (użyj `senderImagePath`).
- ❌ NIE zakładaj że incremental build rekompiluje zmieniony plik (iCloud zachowuje timestamps →
  `rm -rf /tmp/ASFWBuild` dla pewności po edycji constexpr/header).

## ✅ Version bump — naprawiony (jak w main, 2026-06-15)

W pierwszym sterowniku (main) naprawiliśmy dwa bugi version-bump. **Dice ma teraz oba naprawione:**

1. **`build.sh` woła `./bump.sh patch`** (linia ~267). Wcześniej wołał `./bump.sh` bez argumentu →
   `refresh` → wersja nie rosła → macOS pomijał upgrade dextu. Teraz `patch` faktycznie inkrementuje.
2. **`bump.sh` synca `CURRENT_PROJECT_VERSION` w pbxproj** do komponentu patch z VERSION.txt
   (`CFBundleVersion` = patch) **i auto-commituje** VERSION.txt + pbxproj po bumpie (chroni przed
   cofnięciem przez iCloud sync). Auto-commit pomijany gdy `SRCROOT` ustawione (wywołanie z Xcode).

**Normalny workflow (bump dzieje się automatycznie w build.sh):**
```bash
./build.sh --derived /tmp/ASFWBuild     # ← bumpuje patch, synca pbxproj, commituje, buduje
```
Ręczny bump bez buildu (np. żeby zsynchronizować przed czymś): `./bump.sh patch`.

> ⚠️ **Pułapka regresji wersji:** `bump.sh` synca pbxproj do **komponentu patch z VERSION.txt**.
> Jeśli VERSION.txt patch jest NIŻSZY niż aktualnie zainstalowany `CFBundleVersion`
> (`systemextensionsctl list`), build wyprodukuje niższą wersję → macOS odmówi upgrade'u.
> Trzymaj VERSION.txt patch powyżej ostatnio zainstalowanej wersji.
> *(2026-06-15: zainstalowane było 8, ustawiono VERSION.txt=0.2.9-audio.)*

**ZAWSZE weryfikuj po instalacji:**
```bash
systemextensionsctl list   # powinien pokazać NOWĄ wersję
# + sprawdź w logach że nowy fix faktycznie działa (nie stary kod)
```
Jeśli zmieniłeś `constexpr`/header a log pokazuje starą wartość → incremental build miss →
`rm -rf /tmp/ASFWBuild` + pełny clean rebuild.

## CodeGraph MCP — instalacja na świeżej maszynie

Index dice żyje w `.codegraph/`. Serwer skonfigurowany w `ASFireWire-dice/.mcp.json`
(`--path` wskazuje na dice). Komenda to `serve --mcp`, NIE `mcp`.

```bash
# Build/refresh indeksu dice:
export PATH="/opt/homebrew/bin:/opt/homebrew/opt/node@22/bin:$PATH"
cd "/Users/cube666/Library/Mobile Documents/com~apple~CloudDocs/FireWire/ASFireWire-dice"
NODE_OPTIONS="--max-old-space-size=4096" codegraph index .

# Zatwierdź MCP raz po instalacji: uruchom `claude` z katalogu dice →
#   "New MCP server found: codegraph" → opcja 2 (Use this and all future) →
#   /mcp powinien pokazać "codegraph · ✓ connected"
```

⚠️ **`.mcp.json` jest per-katalog:** uruchamiając `claude` z `ASFireWire-dice/` dostajesz indeks dice;
z `ASFireWire/` — indeks main. Każde wywołanie CodeGraph przekazuj `projectPath` (patrz zakaz grep wyżej).

## Compact Instructions

Przy kompaktowaniu konwersacji **zachowaj:**
- Aktualny numer wersji dextu (CFBundleVersion) i co ostatni fix zmienia + w którym pliku.
- Stan ZTS debug: czy `DrainCompleted()` > 0, czy ZTS publikowany, gdzie IR DMA się zatrzymuje.
- Cel bieżącej sesji — co naprawiamy i dlaczego.
- Potwierdzone fakty sprzętowe → linkuj kanon `../ASFireWire/documentation/MOTU_828_MK3_FACTS.md`, NIE kopiuj liczb.
- Zasady git: `git push cube666 dice-motu`, branch `dice-motu`.
- Wnioski z logów/disassembly które potwierdziły działanie fixa.

**Odrzuć:** surowe logi systemowe, wielokrotnie czytane duże pliki (project.pbxproj, build.sh — są w repo),
stare iteracje debugowania już rozwiązane, długie outputy bash bez nowych informacji.
