# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Zanim zaczniesz:** przeczytaj [`Focus.md`](Focus.md) — aktywny stan + następny krok.

## ⛔ BEZWZGLĘDNE REGUŁY (złamanie = błąd procesu)

- 🔍 **Szukanie kodu:** tylko CodeGraph (`codegraph_search`/`codegraph_context`/`codegraph_callers`/
  `codegraph_callees`/`codegraph_node`/`codegraph_impact`), NIGDY `grep`/`find`/`Bash` (chyba że
  CodeGraph nie zwróci wyniku po 2 próbach — wtedy jawna adnotacja `# fallback: CodeGraph nie znalazł`).
  Zawsze przekazuj `projectPath: "/Users/cube666/.../ASFireWire-dice"`. Pełne instrukcje + instalacja
  → [`docs/ai/CODEGRAPH.md`](docs/ai/CODEGRAPH.md).
- 🛑 **Hardware-test build:** ZAWSZE `--clean`, NIGDY `--no-bump`. iCloud psuje mtime → bez `--clean`
  linkujesz starego ducha. Weryfikuj DWA razy: `systemextensionsctl list` **i** marker w logach z tego
  fixa. Pełne dowody/pułapki → [`docs/ai/BUILD_DEPLOY.md`](docs/ai/BUILD_DEPLOY.md).
- 🔧 **Regresje w warstwie współdzielonej OHCI/DMA/Isoch/Async/Bus:** najpierw porównaj z main
  (`../ASFireWire`) — patrz [`docs/ai/BEHAVIOR_GUIDELINES.md`](docs/ai/BEHAVIOR_GUIDELINES.md).
- 📤 **Push:** zawsze `git push cube666 dice-motu` (branch `dice-motu`), nigdy sam `git push`.
  Szczegóły → [`docs/ai/WORKFLOW.md`](docs/ai/WORKFLOW.md).
- 🔢 **Fakty sprzętowe MOTU:** linkuj kanon, NIE kopiuj liczb →
  [`docs/ai/MOTU_HARDWARE_CANON.md`](docs/ai/MOTU_HARDWARE_CANON.md).
- `log`/`log stream` w zsh = builtin matematyczny, zawsze `/usr/bin/log`; predykat
  `senderImagePath CONTAINS "ASFWDriver"` (NIE `process ==`).

## 🛠️ Złota komenda (hardware test + deploy)
```bash
./build.sh --derived /tmp/ASFWBuild --clean --deploy
```
Szybkie sprawdzenie kompilacji (bez deployu, bez hardware): `./build.sh --derived /tmp/ASFWBuild`.

## Project Overview

ASFW is a macOS DriverKit-based FireWire (IEEE 1394) driver restoring FireWire functionality removed in macOS Tahoe (26). It uses PCIDriverKit for user-space OHCI controller access and AudioDriverKit for CoreAudio integration.

Two components:
- **ASFWDriver/** — C++23 DriverKit driver extension (dext)
- **ASFW/** — Swift 6 control app and installer (required to install the dext)

## Build Commands

```bash
./build.sh                        # Quiet build (errors/warnings only)
./build.sh --verbose              # Full xcodebuild output
./build.sh --commands             # Generate compile_commands.json (clangd, static analysis)
./build.sh --test-only                        # C++ unit tests (no hardware needed)
./build.sh --test-only --test-filter Pattern  # C++ tests matching a regex
./build.sh --swift-test-only      # Swift/XCTest tests
./bump.sh patch                   # Bump patch version and regenerate DriverVersion.hpp
```
Or via CMake/CTest: `cmake -S tests -B build/tests_build && cmake --build build/tests_build -- -j$(sysctl -n hw.ncpu) && ctest --test-dir build/tests_build -V`

## 📖 Leniwy kontekst — przeczytaj przed zadaniem, jeśli dotyczy

| Zadanie dotyczy... | Przeczytaj |
|---|---|
| **Szukania dokumentu w 3 repo (dice/snoop/main)** — nie pamiętasz nazwy pliku | [`docs/ai/DOCS_INDEX.md`](docs/ai/DOCS_INDEX.md) |
| Aktualnego stanu prac / następnego kroku | [`Focus.md`](Focus.md) |
| Historii rozwiązanych bugów dice | [`DevLog.md`](DevLog.md) |
| MOTU 828 MK3: kanały, DBS, CLOCK_STATUS, ground-truth | [`docs/ai/MOTU_HARDWARE_CANON.md`](docs/ai/MOTU_HARDWARE_CANON.md) |
| Filozofii kodu, zasad decyzyjnych, wieloetapowych zadań | [`docs/ai/BEHAVIOR_GUIDELINES.md`](docs/ai/BEHAVIOR_GUIDELINES.md) |
| Struktury ASFWDriver, ConfigROM, Isoch/Async, gotchas (endianness, DMA) | [`docs/ai/ARCHITECTURE.md`](docs/ai/ARCHITECTURE.md) |
| CodeGraph MCP: instalacja, pełny zakaz grep/find | [`docs/ai/CODEGRAPH.md`](docs/ai/CODEGRAPH.md) |
| Build/deploy/version-bump: pełne dowody i pułapki | [`docs/ai/BUILD_DEPLOY.md`](docs/ai/BUILD_DEPLOY.md) |
| Git workflow, kiedy commitować/pushować, compact instructions | [`docs/ai/WORKFLOW.md`](docs/ai/WORKFLOW.md) |
| Bugów MOTU V3 z rozwiązaniami | [`docs/MOTU_V3_DICE_TODO.md`](docs/MOTU_V3_DICE_TODO.md) |
| Main branch (zero-copy driver, `../ASFireWire`) | [`../ASFireWire/Focus.md`](../ASFireWire/Focus.md) |

Masz prawo i obowiązek pytać / sięgać po plik z tej tabeli, zanim zaczniesz zadanie, którego dotyczy.
