# Snoop mode — notatki implementacyjne

Branch: `snoop-mode`. Worktree izolowany. Cel: pasywny IR capture strumienia IT oficjalnego
sterownika macOS (El Capitan/MOTU) z drugiego Maca (M3 Tahoe) — pełny payload pakietów.

## ✅ STATUS (2026-06-12): scaffold napisany i kompiluje się

Powstała klasa **`IsochSnoopContext`** (`ASFWDriver/Isoch/Receive/IsochSnoopContext.{hpp,cpp}`).
Build `./build.sh --no-bump --derived /tmp/ASFWBuild` → **BUILD SUCCEEDED** (dext v107).
Pliki auto-włączone do targetu ASFWDriver przez synchronized root group (zero edycji pbxproj).

**Co robi `IsochSnoopContext`:**
- Samodzielna klasa `OSObject` (NIE dziedziczy `DmaContextManagerBase`, NIE ma `IsochAudioRxPipeline`).
- Reużywa `Rx::IsochRxDmaRing` (generyczny silnik OHCI IR DMA, bez semantyki audio).
- `Configure(channel, ctxIndex)` → `rxRing_.SetupRings` (bez `ConfigureFor48k` — brak dekodera).
- `Start()` → programuje rejestry IR identycznie jak `IsochReceiveContext::Start()`:
  `ContextMatch = 0xF0000000 | channel` (pasywny match — NIC nie alokuje na magistrali),
  `CommandPtr`, `ContextControlSet = kRun|kWake`, `kIsoRecvIntMaskSet`. Dead-check na readback.
- `Poll()` → `rxRing_.DrainCompleted(...)` → zamiast `audio_.OnPacket()` woła `HexDumpPacket()`.
- `HexDumpPacket()` → ręczna konwersja nibble→hex (bez snprintf), `kHexDumpBytes=64` (16 quadletów:
  CIP[2q] + pierwszy data block MOTU V3 52B). Throttle: pierwsze 8 pakietów + co 2000-ny.
- Logi pod grep `ASFWDriver.dext` z prefiksem `[Snoop]`.

**Twarde wymagania zakresu — spełnione:** ZERO IRM/discovery/Config-ROM/sterowania MOTU/AV-C/
dekodera AM824/CoreAudio. Czysto pasywny liść (read-only listening na magistrali).

## ⏭️ NASTĘPNY KROK (wymaga użytkownika + 2 hosty — NIE zrobione w scaffoldzie)

Scaffold kompiluje się, ale **nie jest jeszcze podpięty** do żadnego entry pointa (świadomie —
integracja wymaga sprzętu i decyzji o numerze kanału). Aby uruchomić snoop trzeba
(wzór: `IsochService::StartReceive` w `ASFWDriver/Isoch/IsochService.cpp:18`):
1. Stworzyć `IsochDMAMemoryManager` (config jak w `StartReceive`: numDescriptors=512,
   packetSize=4096, descAlign=16, payloadAlign=16384), `Initialize(hardware)`.
2. `IsochSnoopContext::Create(&hw, isochMem)` → `Configure(SNOOP_CHANNEL, 0)` → `Start()`.
3. Pompować `Poll()` z work queue (jak IR — watchdog ~1kHz). Najprościej: osobny IODispatchSource
   albo hook w istniejącym pompowaniu. **To jest praca do zrobienia przy teście sprzętowym.**
4. Ustalić `SNOOP_CHANNEL` — numer kanału isoch oficjalnego sterownika (skan 0–63 albo `read_motu_regs`).

⚠️ Integracja celowo pominięta w scaffoldzie — żeby nie ruszać ścieżki audio/discovery bez testu.

## Zakres (twardy — patrz zaplanowane zadanie)
Napisz szkielet → skompiluj (`./build.sh --derived /tmp/ASFWBuild`) → commit → STOP.
Bez bump/deploy/testu sprzętowego (wymaga użytkownika).

## ⚠️ CodeGraph — uwaga o indeksie (przeczytaj!)
Worktree `ASFireWire-snoop` **NIE ma indeksu `.codegraph/`** (jest gitignorowany, nie wszedł do
worktree). Kod jest IDENTYCZNY na HEAD z głównym repo. Więc:
- **Czytaj kod CodeGraphem z projectPath = GŁÓWNE repo** (`.../FireWire/ASFireWire`, tam jest indeks)
- **Pisz zmiany w worktree** (`.../ASFireWire-snoop`)
- Jeśli CodeGraph w ogóle niepodpięty (serwer w `FireWire/.mcp.json`, może nie załadowany) →
  **fallback grep/find dozwolony** (CLAUDE.md: gdy CodeGraph nie zwróci wyniku). Adnotuj `# fallback`.

## Pliki IR (receive) — zmapowane, tu celuj
Strona IR jest w `ASFWDriver/Isoch/Receive/`:
- **`IsochReceiveContext.{hpp,cpp}`** — kontekst odbioru isoch (główny punkt zaczepienia snoopu)
- **`IsochRxDmaRing.{hpp,cpp}`** — DMA ring IR (deskryptory OHCI IR)
- **`IsochAudioRxPipeline.{hpp,cpp}`** — pipeline RX (dekoduje do CoreAudio — snoop ma to POMINĄĆ)
- **`StreamProcessor.hpp`** — przetwarzanie strumienia
- `ASFWDriver/Isoch/IsochReceiveContext.hpp` (uwaga: jest też duplikat w katalogu nadrzędnym Isoch/)
- `ASFWDriver/Isoch/IsochService.{hpp,cpp}` — usługa isoch (lifecycle kontekstów)
Wejścia/lifecycle: `ASFWDriver/Isoch/Audio/AM824Decoder.hpp`, `MotuV3Decoder.hpp` (dekodery — snoop omija).
OHCI IR descriptors: `ASFWDriver/Hardware/OHCIConstants.hpp`, `documentation/FWOHCI_IR.md`.

**Strategia:** zobacz jak `IsochReceiveContext` + `IsochRxDmaRing` ustawiają kontekst IR na kanale,
i dodaj tryb "snoop" który robi to samo ale zamiast `IsochAudioRxPipeline`/dekodera tylko loguje
surowy payload (hex). NIE pisz IR DMA od zera — reużyj `IsochRxDmaRing`.

## Projekt trybu snoop — twarde wymagania
1. **Czysto pasywny liść.** ZERO: IRM, discovery, config-ROM publish, sterowania MOTU,
   alokacji kanału/bandwidth. Inaczej zakłóci stream oficjalnego sterownika na magistrali.
   Tylko: otwórz kontekst IR na zadanym kanale → odbieraj → loguj surowe pakiety.
2. **Pełny payload**, nie tylko nagłówek CIP (tracepoint Linuxa dał tylko CIP — to ma dać resztę:
   bajty PCM, byte offset, amplituda/bit-shift). Zrzut hex do logów dextu.
3. **Kanał isoch jako parametr/stała** — numer kanału oficjalnego sterownika ustalimy przy teście
   (skan 0-63 albo z `read_motu_regs`). Na razie #define lub pole konfiguracyjne.
4. Reużyj istniejący stos IR jeśli jest (IsochReceiveContext) — dodaj tryb "snoop" który pomija
   dekodowanie do CoreAudio, tylko loguje. NIE pisz IR DMA od zera jeśli już istnieje.

## Build (weryfikacja przeze mnie możliwa)
`./build.sh --derived /tmp/ASFWBuild` — musi przejść (poza iCloud → bez problemów xattr/codesign).
Tryb test C++: `./build.sh --test-only` jeśli dotykasz logiki testowalnej bez DriverKit.

## Po skończeniu — podsumowanie dla użytkownika (rano)
- Co powstało (pliki/klasy), jak włączyć tryb snoop
- Czego wymaga test sprzętowy: adapter TB→FW do drugiego portu MOTU, El Capitan streaming na
  MacBooku 2009, ustalenie numeru kanału isoch, czytanie logów dextu (grep `ASFWDriver.dext`)

## Ground-truth już mamy (do walidacji co snoop zobaczy)
DBS=13, SYT=0xFFFF, 14 kanałów PCM, slot 0/1=Main (byte 10/13). Szczegóły:
`documentation/MOTU_V3_WIRE_GROUNDTRUTH.md`. Snoop ma to POTWIERDZIĆ na pełnym payloadzie.
