# Focus.md — Plan pracy nad ASFireWire-dice

Cel: MOTU 828 MK3 działający przez sterownik DICE (nowa architektura, AudioDriverKit-native).

Archiwum ukończonych sesji → `DevLog.md`

---

## ⚡ AKTUALNY STAN — Przeczytaj to na starcie

> **Stan na 2026-06-15 (v11 / CFBundleVersion=8) — ZTS timeout:**
> Geometry check naprawiony (kRxPcmChannels=16 → inCh=16 ✅).
> CoreAudio widzi 16 wejść, IR zbrojony na właściwym kanale.
> **Problem: DrainCompleted() zwraca 0 ukończonych deskryptorów w ciągu 500ms.**
> ZTS (Zero Timestamp) nie jest publikowany → `WaitForInitialHardwareZts` timeout (0xe00002d6).
> MOTU albo nie nadaje na oczekiwanym kanale isoch, albo OHCI IR kontekst nie odbiera.

### Potwierdzone fakty z logów (v11 / CFBundleVersion=8)

```
DICE DUPLEX START guid=0x0001f20000087236 ir=0 it=1 inCh=16 outCh=14 inSlots=16 outSlots=13
IR: direct audio binding changed (gen 0 -> 2). Arming direct Rx inBase=... inCh=16 ... rate=48000
[500ms przerwy — ZERO wpisów IR]
ASFWAudioDevice: initial hardware ZTS timed out after 500 ms (err 0xe00002d6)
```

Zero wpisów IR między "Arming" a timeout = `Poll()→DrainCompleted()` = 0 przez cały czas.

### Co jest NAPRAWIONE

- ✅ **v9 — SYT cadence (Bug C):** `IsochReceiveContext.cpp` — SYT fallback gdy `syt=0x0000`.
  MOTU V3 zawsze wysyła `syt=0x0000`; brama SYT zabijała IT po 3000ms. Fix: bypass dla MOTU V3.
- ✅ **v11 — Geometry check (Bug B):** `MOTU828Mk3Profile.cpp` — `kRxPcmChannels=16` (było 18).
  `RxAudioPacketProcessor` ma sprawdzenie AM824: `cip->dataBlockSize < channels` → 16 < 18 → każdy
  pakiet IR był odrzucany. Fix: 16 < 16 = false → pakiety przechodzą.
  Źródło `inputChannelCount=16`: `AudioProfileRegistry::FindProfile()→RxChannelCount()→kRxPcmChannels`
  (NIE `kHostInputPcm` w `MOTUVendorProtocol.hpp` — ten jest nieistotny dla tej ścieżki).

### Co czeka na rozwiązanie

**🔴 Główny problem: OHCI IR DMA nie dostarcza żadnych deskryptorów**

`Poll()` w `IsochReceiveContext.cpp` linia ~160 → `DrainCompleted()` = 0 przez 500ms.
Możliwe przyczyny (niezbadane):
1. MOTU nie nadaje na kanale isoch którego oczekujemy (zły kanał IR).
2. OHCI IR DMA context nie jest uruchamiany lub zatrzymuje się natychmiast.
3. DMA ring nie jest prawidłowo skonfigurowany (deskryptory nie spełniają OHCI).
4. Brakuje wznowienia OHCI kontekstu po konfiguracji.

**Następny krok: przejrzeć `IsochReceiveContext.cpp` — jak zbroi IR, uruchamia OHCI i jak DMA ring
dostarcza pakiety. Porównać z main branch (`IsochReceiveContext.cpp` w ASFireWire/).**

### TODO per `docs/MOTU_V3_DICE_TODO.md`

| Bug | Opis | Status |
|-----|------|--------|
| Bug A (SYT) | Brama SYT blokowała IT | ✅ Naprawiony v9 |
| Bug B (geometry) | AM824 check 16 < 18 odrzucał IR | ✅ Naprawiony v11 |
| Bug C (kRxDbs) | kRxPcmChannels=18 zamiast 16 | ✅ Naprawiony v11 |
| Bug 1 (MOTU V3 decoder) | `kRxPcmChannels` powinien =18, wymaga `DecodeMOTUV3Frame` | ⏳ Post-ZTS |
| Bug 2 (IT encoder) | `Quirks()` zwraca `kAM824` zamiast MOTU V3 dla TX | ⏳ Post-ZTS |

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
