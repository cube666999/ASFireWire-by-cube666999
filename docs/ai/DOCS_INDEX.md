# DOCS_INDEX.md — mapa dokumentacji trzech repo FireWire

> Read-on-demand: NIE ładowany na starcie sesji. Otwórz gdy szukasz dokumentu, którego
> nazwy nie pamiętasz, albo gdy podejrzewasz, że temat pokryty jest w sąsiednim repo.
> Ostatnia rewizja: 2026-07-01.

**Trzy repo, każde ze swoim CodeGraph:**
- 🟢 `ASFireWire-dice/` — **aktywne**, gałąź `integrate-dice-c2bdf11`. DriverKit + AudioDriverKit, cel MOTU 828 MK3.
- 🔵 `ASFireWire-snoop/` — narzędzie pasywnego snoopa strumienia MOTU↔host (worktree, branch `snoop-mode`).
- 🟣 `ASFireWire/` (main) — **poprzedni sterownik**, IOKit-style zero-copy driver. Referencja + kanon MOTU.

Wykluczone z indeksu: `build/`, `_deps/`, `node_modules/`, `googletest`, `tools/pydice/parity/*`,
`_CLAUDE-kopia.md`, `AGENTS*.md`, `GEMINI.md` (backupy/eksperymenty edytorskie).

---

## 🟢 ASFireWire-dice (aktywne repo)

### Root
| Plik | Zawartość | Kiedy czytać |
|------|-----------|--------------|
| [`Focus.md`](../../Focus.md) | Aktywny stan + następny krok. Handoff 2026-06-28 (misframe = warstwa operacyjna), pomiar Sequoia, v137-v144. | ⭐ **Zawsze na starcie sesji.** |
| [`DevLog.md`](../../DevLog.md) | Historia rozwiązanych bugów dice (v9→v117 ZTS, v121-v124 enkoder IT MOTU-packed, rundy rejestrów v14/v15). | Przy podejrzeniu regresji, szukając „jak to było naprawione". |
| [`CLAUDE.md`](../../CLAUDE.md) | Zakazy, złota komenda, tabela drogowskazów. | Ładowany automatycznie. |
| [`README.md`](../../README.md) | Ogólny opis projektu (upstream). | Rzadko — kontekst jest w `Focus.md`. |
| [`ISOCH_AUDIO_CLEANUP_PREP.md`](../../ISOCH_AUDIO_CLEANUP_PREP.md) | Plan przygotowawczy (2026-06-11): co usunąć z isoch stacku przed przepięciem na ADK boundary. Companion `docs/ISOCH_AUDIO_ADK.md`. | Przy sprzątaniu warstwy isoch pod nową architekturę. |

### `docs/ai/` — leniwy kontekst dla Claude
| Plik | Zawartość | Kiedy czytać |
|------|-----------|--------------|
| [`BEHAVIOR_GUIDELINES.md`](BEHAVIOR_GUIDELINES.md) | Filozofia kodu (prostota, chirurgiczność), zasady decyzyjne, „najpierw main". | Przed refaktorem / większym zadaniem. |
| [`MOTU_HARDWARE_CANON.md`](MOTU_HARDWARE_CANON.md) | Kanoniczne fakty MOTU (kanały 14/18, DBS 13/16, CLOCK_STATUS 0x0b14) + tabela źródeł ground-truth (poziom zaufania). | Zadania dot. `MOTUVendorProtocol`, `MOTU828Mk3Profile`, enkoder/dekoder. |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Struktura ASFWDriver, ConfigROM pipeline, Async/Isoch flow, gotchas (endianness, IT descriptor offset 0x08, DMA coherency). | Przy pracy nad Bus/Async/ConfigROM lub gdy potrzebujesz mapy modułów. |
| [`CODEGRAPH.md`](CODEGRAPH.md) | Pełny zakaz grep/find + instalacja indeksu. | Świeża maszyna albo CodeGraph nie działa. |
| [`BUILD_DEPLOY.md`](BUILD_DEPLOY.md) | Pełna ścieżka hardware-test: `--clean`/`--derived`, weryfikacja markerem, `deploy_app()`, historia bug'ów bump.sh. | Gdy build/deploy się psuje, świeża maszyna, wątpliwości „testuję nowy kod?". |
| [`WORKFLOW.md`](WORKFLOW.md) | Git (dwa remote'y, ostrzeżenie o tokenie), kiedy commitować/pushować, compact instructions. | Przed pushem albo przed kompaktowaniem. |

### `documentation/` — architektura specyficzna dla DICE
| Plik | Zawartość | Kiedy czytać |
|------|-----------|--------------|
| [`ZTS_AND_SYT.md`](../../documentation/ZTS_AND_SYT.md) | ⭐ ZTS i SYT timing w AudioDriverKit — kluczowe dla bringup'u. | Bug'i dot. `DrainCompleted=0`, ZTS timeout, StartIO. |
| [`ZTS_BUFFERS_AND_IRS.md`](../../documentation/ZTS_BUFFERS_AND_IRS.md) | Analiza `Saffire.kext` (DICE/TCAT/IOAudioFamily): jak Focusrite anchoruje ZTS, sizeuje bufory, konstruuje DCL. Reference tylko, bez zmian kodu. | Kalibracja naszej pipeline przeciw działającemu DICE. |
| [`FWOHCI_IR.md`](../../documentation/FWOHCI_IR.md) | Architektura IR (Isoch Receive) z dekompilacji Apple — jak działa DMA ring. | Bugi w IR DMA, `IsochReceiveContext`. |
| [`IRM_EXPLAINED.md`](../../documentation/IRM_EXPLAINED.md) | IRM protocol (Isoch Resource Manager) — rezerwacja kanału + bandwidth. | Bugi handshake IRM (`AllocateResources`, kolizja z obcym IRM). |
| [`ASYNC_COMPARE_SWAP.md`](../../documentation/ASYNC_COMPARE_SWAP.md) | Async compare-swap w OHCI. | Rzadko — użyj gdy piszesz atomic ops na config space. |
| [`TRANSFER_DELAY_AND_OTHER.md`](../../documentation/TRANSFER_DELAY_AND_OTHER.md) | TX payload, SYT transfer delay, „85/15 silence" (Saffire Pro 24 DSP @ 48k). Dowody z FireBug + `libffado-2.5.0` + IEC 61883-6. | Analiza timing SYT i lead. |
| [`SEQUOIA_SNOOP_RESULT.md`](../../documentation/SEQUOIA_SNOOP_RESULT.md) | ⭐ **Pomiar oficjalnego sterownika MOTU na Sequoia** — lead=3 cykle, mapa slotów, multi-rate. Base dla fixów v137. | Przed dotknięciem lead/slot mapping. |
| [`SEQUOIA_SNOOP_HANDOFF.md`](../../documentation/SEQUOIA_SNOOP_HANDOFF.md) | Setup snoopa na Sequoia (przygotowanie sesji). | Powtarzasz pomiar. |
| [`SEQUOIA_REGREAD_RESULT.md`](../../documentation/SEQUOIA_REGREAD_RESULT.md) | Read-back rejestrów oficjalnego stosu (0x0b1c/0x0b38 write-only) + wartości init DTrace-deref. | Prace nad `MOTUVendorProtocol::PrepareDuplex`. |
| [`SEQUOIA_REGREAD_HANDOFF.md`](../../documentation/SEQUOIA_REGREAD_HANDOFF.md) | Setup DTrace regread. | Powtarzasz sesję regread. |
| [`SEQUOIA_REGREAD_SESSION_LOG.md`](../../documentation/SEQUOIA_REGREAD_SESSION_LOG.md) | Surowy log sesji regread. | Rzadko — archiwum. |
| [`ELCAP_0B38_QUADLET2_HANDOFF.md`](../../documentation/ELCAP_0B38_QUADLET2_HANDOFF.md) | Runda 2 dla rejestru 0x0b38 (brak 2. quadletu — do domknięcia El Cap). | Wznowienie kwestii 0b38. |
| [`TIMING_LEAD_CHECK_PLAN.md`](../../documentation/TIMING_LEAD_CHECK_PLAN.md) | Plan pomiaru realnego leadu naszego strumienia na drucie (MB2009 snoop ch1). | Weryfikacja leadu SPH. |

### `docs/` — plany produktowe / kolejne kroki
| Plik | Zawartość | Kiedy czytać |
|------|-----------|--------------|
| [`MOTU_V3_DICE_TODO.md`](../../docs/MOTU_V3_DICE_TODO.md) | Lista bugów MOTU V3 z rozwiązaniami (Bug 1: IR geometry, Bug 2: IT encoder, Bug 3: SYT). | Planowanie kolejnych kroków MOTU. |
| [`ISOCH_AUDIO_ADK.md`](../../docs/ISOCH_AUDIO_ADK.md) | Design reference (2026-06-10): jak spiąć ADK audio stack z FireWire transport przez czysty boundary. Protokół już zwalidowany (`ADKVirtualAudioLab/`). | Migracja isoch↔ADK. |
| [`CUEMIX_FX_TAHOE.md`](../../docs/CUEMIX_FX_TAHOE.md) | Notatka odłożona (2026-06-25): CueMix FX na Tahoe — pomysł po tym jak audio zagra. | Nie teraz. Wróć gdy audio stabilne. |

---

## 🔵 ASFireWire-snoop (narzędzie snoopa)

Osobny worktree, izolowany build. Cel: pasywny IR capture strumienia IT oficjalnego sterownika
(El Capitan / Sequoia MOTU) z drugiego Maca — pełny payload pakietów.

### Root
| Plik | Zawartość | Kiedy czytać |
|------|-----------|--------------|
| [`../../../ASFireWire-snoop/Focus.md`](../../../ASFireWire-snoop/Focus.md) | Aktywny plan snoop repo. | Pracujesz w snoop worktree. |
| [`../../../ASFireWire-snoop/DevLog.md`](../../../ASFireWire-snoop/DevLog.md) | Historia sesji snoop. | Regresja w narzędziach snoop. |
| [`../../../ASFireWire-snoop/CLAUDE.md`](../../../ASFireWire-snoop/CLAUDE.md) | Guidance snoop repo. | Uruchamiasz `claude` z tego katalogu. |
| [`../../../ASFireWire-snoop/SNOOP_IMPLEMENTATION_NOTES.md`](../../../ASFireWire-snoop/SNOOP_IMPLEMENTATION_NOTES.md) | ⭐ Notatki implementacyjne: klasa `IsochSnoopContext`, pipeline capture. | Modyfikacja/debug samego snoopa. |
| [`../../../ASFireWire-snoop/MOTU_828_MK3_BringUp.md`](../../../ASFireWire-snoop/MOTU_828_MK3_BringUp.md) | Sekwencja StartStreaming + mapa rejestrów V3 (kopia sprzed rozdzielenia repo). | Referencja bringup — nowsza kopia w main. |
| [`../../../ASFireWire-snoop/CHANGES.md`](../../../ASFireWire-snoop/CHANGES.md) | Historyczny fix log (do Fix 41, v51). | Rzadko — archiwum. |
| [`../../../ASFireWire-snoop/REFACTOR_THOUGHTS.md`](../../../ASFireWire-snoop/REFACTOR_THOUGHTS.md) | Propozycja konsolidacji audio constants/config. | Refaktor konfiguracji audio. |
| [`../../../ASFireWire-snoop/README.md`](../../../ASFireWire-snoop/README.md) | Opis snoop tool. | Wprowadzenie. |

### `documentation/` (snoop) — WSZYSTKO poza `MOTU_KEXT_GHIDRA.md` to kopie z main
| Plik | Zawartość | Kiedy czytać |
|------|-----------|--------------|
| [`../../../ASFireWire-snoop/documentation/MOTU_KEXT_GHIDRA.md`](../../../ASFireWire-snoop/documentation/MOTU_KEXT_GHIDRA.md) | ⭐ Disasm `MOTUFireWireAudio.kext` (mechanizm SPH=bit 10, encoding). ⚠️ DBS=21 to własny kekst MOTU, NIE ścieżka El Cap. | Analiza mechaniki MOTU-kekstu. |
| Reszta (`FWOHCI_IR`, `IRM_EXPLAINED`, `ASYNC_*`, `COMPLETION_STRATEGIES`, `PHY_COMMAND_CONTRACTS`, `ieee1394_*`) | Kopie z main. | Używaj wersji z main (aktualniejsze). |

---

## 🟣 ASFireWire (main, poprzedni sterownik) — referencja + kanon

**Zero-copy IOKit-style driver.** Nie aktywny cel prac dice, ale krytyczna referencja:
- Kod współdzielony (OHCI/DMA/Isoch/Async/Bus) — najpierw sprawdź jak działa tam.
- Kanoniczne fakty MOTU żyją tutaj (dice tylko linkuje).

### Root
| Plik | Zawartość | Kiedy czytać |
|------|-----------|--------------|
| [`../../../ASFireWire/Focus.md`](../../../ASFireWire/Focus.md) | Aktywny plan main — obecnie PCM byte position bug (MOTU Main Out). | Bugi w main lub porównanie stanu. |
| [`../../../ASFireWire/DevLog.md`](../../../ASFireWire/DevLog.md) | Historia sesji main — bugi/fixy/decyzje. | Szukasz jak coś było naprawione w main. |
| [`../../../ASFireWire/CLAUDE.md`](../../../ASFireWire/CLAUDE.md) | Guidance main repo. | Uruchamiasz `claude` z main. |
| [`../../../ASFireWire/MOTU_828_MK3_BringUp.md`](../../../ASFireWire/MOTU_828_MK3_BringUp.md) | ⭐ Sekwencja StartStreaming + mapa rejestrów V3. NIE FCP/AV/C — V3 register protocol. | Bringup MOTU V3 (canonical). |
| [`../../../ASFireWire/CHANGES.md`](../../../ASFireWire/CHANGES.md) | Fix log main (do Fix 41+). | Archiwum fixów. |
| [`../../../ASFireWire/REFACTOR_THOUGHTS.md`](../../../ASFireWire/REFACTOR_THOUGHTS.md) | Propozycja konsolidacji audio constants. | Refaktor. |
| [`../../../ASFireWire/README.md`](../../../ASFireWire/README.md) | Opis projektu main. | Wprowadzenie. |

### `documentation/` (main) — KANON
| Plik | Zawartość | Kiedy czytać |
|------|-----------|--------------|
| [`../../../ASFireWire/documentation/MOTU_828_MK3_FACTS.md`](../../../ASFireWire/documentation/MOTU_828_MK3_FACTS.md) | ⭐⭐ **KANON MOTU 828 MK3** — jedyne źródło prawdy: kanały, DBS, rate, CLOCK_STATUS, slot map, hierarchia źródeł. NIE kopiuj liczb — linkuj. | Cokolwiek dotyka faktów sprzętowych MOTU. |
| [`../../../ASFireWire/documentation/MOTU_V3_WIRE_GROUNDTRUTH.md`](../../../ASFireWire/documentation/MOTU_V3_WIRE_GROUNDTRUTH.md) | ⭐ Ground-truth z kabla (El Cap snoop + Linux): DBS=13/IT, 16/IR, CIP, **SPH Δ=512**, PCM slot map. | Enkoder IT / dekoder IR / SPH. |
| [`../../../ASFireWire/documentation/MOTU_KEXT_GHIDRA.md`](../../../ASFireWire/documentation/MOTU_KEXT_GHIDRA.md) | Disasm oficjalnego kekstu (SPH bit=0x400, DBC, encoding). ⚠️ DBS=21 to jego własna ścieżka. | Mechanika MOTU kekstu. |
| [`../../../ASFireWire/documentation/LINUX_MBP2009_SSH.md`](../../../ASFireWire/documentation/LINUX_MBP2009_SSH.md) | Setup Linux Mint MBP2009 (`snd-firewire-motu` + ALSA tracepoint). ⛔ Linux NIE gra MOTU — tylko struktura CIP. | Snoop/parse Linux ALSA, NIE oracle audio. |
| [`../../../ASFireWire/documentation/AUDIODRIVERKIT_PIPELINE.md`](../../../ASFireWire/documentation/AUDIODRIVERKIT_PIPELINE.md) | ⭐ Prawidłowa architektura ADK (WWDC21 + odkrycie mrmidi 2026-06-04): model pull, IOBufferMemoryDescriptor, ZTS z OHCI CycleTimer. | Migracja push→pull, refaktor ZTS. |
| [`../../../ASFireWire/documentation/REFACTOR_PLAN_IOBUFFER_ZTS.md`](../../../ASFireWire/documentation/REFACTOR_PLAN_IOBUFFER_ZTS.md) | Plan refaktoru: 38K underrunów @ 144% fill → `IOBufferMemoryDescriptor` + ZTS z CycleTimer. | Implementacja pull model. |
| [`../../../ASFireWire/documentation/SESSION_2026-06-12_GROUNDTRUTH.md`](../../../ASFireWire/documentation/SESSION_2026-06-12_GROUNDTRUTH.md) | Przełomowa sesja: diagnoza „ch7 + pisk" — slot 8 (byte 34), NIE 0/1. Metoda: Linux + El Cap. | Kontekst mapowania kanałów. |
| [`../../../ASFireWire/documentation/FWOHCI_IR.md`](../../../ASFireWire/documentation/FWOHCI_IR.md) | Architektura IR z dekompilacji Apple. | Bugi IR DMA. |
| [`../../../ASFireWire/documentation/IRM_EXPLAINED.md`](../../../ASFireWire/documentation/IRM_EXPLAINED.md) | IRM protocol. | Bugi IRM. |
| [`../../../ASFireWire/documentation/ASYNC_COMPARE_SWAP.md`](../../../ASFireWire/documentation/ASYNC_COMPARE_SWAP.md) | Async compare-swap OHCI. | Atomic ops config space. |
| [`../../../ASFireWire/documentation/ASYNC_READ_API.md`](../../../ASFireWire/documentation/ASYNC_READ_API.md) | Async read API. | Praca nad async R/W. |
| [`../../../ASFireWire/documentation/COMPLETION_STRATEGIES.md`](../../../ASFireWire/documentation/COMPLETION_STRATEGIES.md) | Completion strategies OHCI. | Callback design. |
| [`../../../ASFireWire/documentation/PHY_COMMAND_CONTRACTS.md`](../../../ASFireWire/documentation/PHY_COMMAND_CONTRACTS.md) | PHY command contracts. | Bugi bus reset/PHY. |
| [`../../../ASFireWire/documentation/ieee1394_bus_reset.md`](../../../ASFireWire/documentation/ieee1394_bus_reset.md) | IEEE 1394 bus reset spec. | Bugi resetu. |
| [`../../../ASFireWire/documentation/ieee1394_tree_identification.md`](../../../ASFireWire/documentation/ieee1394_tree_identification.md) | IEEE 1394 tree identification. | Topology. |
| `raw-captures/` | El Cap wire snoop dumps (surowe bajty z drutu). | Weryfikacja on-wire. |

### `docs/linux/` (main) — referencje spoza repo
Linux `firewire-ohci` + `snd-firewire-motu` — autorytatywne dla OHCI mechanism + kolejności
StartStreaming. Sięgaj gdy dice `documentation/*` nie ma odpowiedzi.

---

## 🧭 Reguły utrzymania tego indeksu

- Nowy plik w `documentation/` któregoś repo → dopisz wiersz od razu (jednozdaniowy opis + kiedy czytać).
- Plik nieaktualny/porzucony → oznacz `~~przekreślone~~` z krótkim wyjaśnieniem, nie usuwaj (żeby stare linki się broniły).
- NIE indeksuj: `build/`, `_deps/`, `node_modules/`, `googletest`, `pydice/parity/*`, backupów (`_CLAUDE-kopia*`, `AGENTS*`, `GEMINI*`).
- Ten index NIE jest ładowany na starcie — linkowany z tabeli w [`CLAUDE.md`](../../CLAUDE.md) („leniwy kontekst").
