# CLAUDE.md

> **Translation note:** this file is normally maintained in Polish on the working
> branch (`integrate-dice-c2bdf11`). This English version is a snapshot translation
> for the `motu-v3-showcase` branch only.

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Before you start:** read [`Focus.md`](Focus.md) — active state + next step.

## ⛔ HARD RULES (breaking these = a process error)

- 🔍 **Code search:** CodeGraph only (`codegraph_search`/`codegraph_context`/`codegraph_callers`/
  `codegraph_callees`/`codegraph_node`/`codegraph_impact`), NEVER `grep`/`find`/`Bash` (unless
  CodeGraph returns nothing after 2 attempts — then an explicit `# fallback: CodeGraph found nothing`
  annotation). Always pass `projectPath: "/Users/cube666/.../ASFireWire-dice"`. Full instructions +
  install → [`docs/ai/CODEGRAPH.md`](docs/ai/CODEGRAPH.md).
- 🛑 **Hardware-test build:** ALWAYS `--clean`, NEVER `--no-bump`. iCloud corrupts mtimes → without
  `--clean` you link a stale ghost. Verify twice: `systemextensionsctl list` **and** a log marker
  from this fix. Full evidence/pitfalls → [`docs/ai/BUILD_DEPLOY.md`](docs/ai/BUILD_DEPLOY.md).
- 🔧 **Regressions in the shared OHCI/DMA/Isoch/Async/Bus layer:** compare against main
  (`../ASFireWire`) first — see [`docs/ai/BEHAVIOR_GUIDELINES.md`](docs/ai/BEHAVIOR_GUIDELINES.md).
- 📤 **Push:** always `git push cube666 dice-motu` (branch `dice-motu`), never plain `git push`.
  Details → [`docs/ai/WORKFLOW.md`](docs/ai/WORKFLOW.md).
- 🔢 **MOTU hardware facts:** link the canon, do NOT copy numbers →
  [`docs/ai/MOTU_HARDWARE_CANON.md`](docs/ai/MOTU_HARDWARE_CANON.md).
- `log`/`log stream` in zsh is a math builtin — always use `/usr/bin/log`; predicate
  `senderImagePath CONTAINS "ASFWDriver"` (NOT `process ==`).

## 🛠️ Golden command (hardware test + deploy)
```bash
./build.sh --derived /tmp/ASFWBuild --clean --deploy
```
Quick compile check (no deploy, no hardware): `./build.sh --derived /tmp/ASFWBuild`.

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

## 📖 Lazy context — read before a task, if relevant

| If the task involves... | Read |
|---|---|
| **Finding a doc across 3 repos (dice/snoop/main)** — can't remember the filename | [`docs/ai/DOCS_INDEX.md`](docs/ai/DOCS_INDEX.md) |
| Current work state / next step | [`Focus.md`](Focus.md) |
| History of resolved dice bugs | [`DevLog.md`](DevLog.md) |
| MOTU 828 MK3: channels, DBS, CLOCK_STATUS, ground-truth | [`docs/ai/MOTU_HARDWARE_CANON.md`](docs/ai/MOTU_HARDWARE_CANON.md) |
| Code philosophy, decision rules, multi-step tasks | [`docs/ai/BEHAVIOR_GUIDELINES.md`](docs/ai/BEHAVIOR_GUIDELINES.md) |
| ASFWDriver structure, ConfigROM, Isoch/Async, gotchas (endianness, DMA) | [`docs/ai/ARCHITECTURE.md`](docs/ai/ARCHITECTURE.md) |
| CodeGraph MCP: install, the full grep/find ban | [`docs/ai/CODEGRAPH.md`](docs/ai/CODEGRAPH.md) |
| Build/deploy/version-bump: full evidence and pitfalls | [`docs/ai/BUILD_DEPLOY.md`](docs/ai/BUILD_DEPLOY.md) |
| Git workflow, when to commit/push, compact instructions | [`docs/ai/WORKFLOW.md`](docs/ai/WORKFLOW.md) |
| MOTU V3 bugs with solutions | [`docs/MOTU_V3_DICE_TODO.md`](docs/MOTU_V3_DICE_TODO.md) |
| Main branch (zero-copy driver, `../ASFireWire`) | [`../ASFireWire/Focus.md`](../ASFireWire/Focus.md) |

You have the right and the obligation to ask / reach for a file from this table before starting a task it relates to.
