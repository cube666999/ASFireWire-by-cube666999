# DevLog.md — Historia sesji ASFireWire-dice

Archiwum decyzji, bugów i fixów z sesji pracy nad gałęzią DICE.
Aktywny plan → `Focus.md`

---

## Sesja 2026-06-15 — ZTS debug (v9–v11)

### Kontekst

Gałąź DICE to nowa architektura (AudioDriverKit-native, bez manual start/stop Isoch).
ZTS (Zero Timestamp) = mechanizm AudioDriverKit: `WaitForInitialHardwareZts` musi dostać
`UpdateCurrentZeroTimestamp` w ciągu 500ms od `StartIO`. Jeśli IR DMA nie dostarczy pakietu,
ZTS timeout (0xe00002d6) i CoreAudio blokuje cały IO.

### Bug C (naprawiony v9) — Brama SYT blokowała IT

**Symptom:** muzyka przez MOTU nie grała. W logach: IT startowało, po 3000ms ciszało.

**Root cause:** `IsochReceiveContext.cpp` miał bramę SYT: sprawdzało `syt != 0xFFFF`
przed opublikowaniem ZTS. MOTU V3 **zawsze** nadaje `syt=0x0000` (nie standard 0xFFFF).
Brama nigdy nie przepuszczała → ZTS timeout → IT zatrzymywano.

**Fix (v9):** Dodano fallback SYT w `IsochReceiveContext.cpp` (~linia 488):
```cpp
if (!rxClockEstablished && hasHardwareTimestamp && packetReceiveHostTicks != 0) {
    rxClockEstablished = true;
}
```
Bypass dla MOTU V3 — jeśli jest timestamp sprzętowy, opublikuj ZTS niezależnie od SYT.

### Bug B (nienaprawiony v10, naprawiony v11) — Geometry check AM824 odrzucał IR

**Symptom:** po v9 ZTS nadal timeout. Log: `inCh=18`, ale IR nadal 0 pkt/s.

**Root cause:** `RxAudioPacketProcessor::ProcessPacket` (~linia 68):
```cpp
if (cip->dataBlockSize < channels) {  // 16 < 18 = TRUE → kInvalidRange → pakiet odrzucony
```
MOTU V3 DBS=16, ale nasza geometria `inputChannelCount=18` → KAŻDY pakiet IR odrzucany.

**v10 — FIX W ZŁYM PLIKU (brak efektu):**
Zmieniono `kHostInputPcm = 16u` w `MOTUVendorProtocol.hpp`.
Brak efektu — `inputChannelCount` pochodzi z `AudioProfileRegistry::FindProfile()→RxChannelCount()`
czyli `MOTU828Mk3Profile::kRxPcmChannels` (plik `MOTU828Mk3Profile.cpp`), NIE z `kHostInputPcm`.
Log nadal pokazywał `inCh=18`.

**v11 — FIX W PRAWIDŁOWYM PLIKU:**
Zmieniono `kRxPcmChannels = 16` (było 18) w `MOTU828Mk3Profile.cpp`:
```cpp
constexpr uint32_t kRxPcmChannels = 16; // QUICK FIX: should be 18 — see TODO above
```
Log potwierdził: `DICE DUPLEX START ... inCh=16`.
AM824 check: `16 < 16 = false` → pakiety IR mogą przejść.

**Dodatkowy problem v11 — incremental build miss:**
Xcode nie rekompilował `MOTU828Mk3Profile.cpp` — iCloud File Provider zachowuje timestamps.
Fix: `rm -rf /tmp/ASFWBuild` → pełny clean rebuild → CFBundleVersion=8 → `inCh=16` ✅.

### Stan na koniec sesji

v11 (CFBundleVersion=8): geometry check naprawiony, `inCh=16`.
**ZTS nadal timeout** — DrainCompleted() = 0 przez 500ms.
IR DMA nie dostarcza żadnych deskryptorów. Przyczyna niezbadana.

Następny krok: zbadać `IsochReceiveContext.cpp` — jak zbraja OHCI IR DMA, czy kontekst
startuje, czy MOTU nadaje na oczekiwanym kanale.

---

## Infrastruktura build

### ✅ Version-bump bug — naprawiony 2026-06-15 (port z main)

`build.sh:267` wołał `./bump.sh` **bez argumentu** → `refresh` → wersja nie rosła → macOS pomijał
upgrade dextu. Dodatkowo `bump.sh` nie syncował pbxproj ani nie commitował → iCloud sync mógł cofnąć bump.
Dowód: pbxproj `CURRENT_PROJECT_VERSION=4`, VERSION.txt=0.2.0, a zainstalowany dext = 8.

**Naprawa (3 zmiany, zwalidowane na żywo — `./bump.sh patch` → 0.2.9, pbxproj=9, auto-commit):**
1. `build.sh:267` → `./bump.sh patch` (było `./bump.sh`).
2. `bump.sh` `generate_version_header()` → sync `CURRENT_PROJECT_VERSION` w pbxproj do patch z VERSION.txt.
3. `bump.sh` `main()` → auto-commit VERSION.txt+pbxproj po bumpie (skip gdy `SRCROOT` = wywołanie z Xcode).

VERSION.txt podniesiony do 0.2.9-audio (zainstalowane było 8) by uniknąć regresji wersji.
Uwaga: `bump.sh` synca pbxproj do patch z VERSION.txt — trzymaj VERSION.txt patch > zainstalowana wersja.

### iCloud Drive + DerivedData + deploy (port z main, 2026-06-15)

Build zawsze poza iCloud: `--derived /tmp/ASFWBuild`. Dodano flagi `--clean` (rm DerivedData) i
`--deploy` (funkcja `deploy_app()`: kopia do /tmp → strip xattr → sign dext+app → verify →
Desktop/`ASFW_dice_vNN.app`) — przeniesione 1:1 z main, nazwa pliku z prefiksem `_dice_`.

### Remote SSH (2026-06-15)

Przełączono dice remote z HTTPS+token (skompromitowany, usunięty z GitHub) na SSH:
`cube666 = git@github.com:cube666999/ASFireWire-by-cube666999.git`, `origin = git@github.com:mrmidi/ASFireWire.git`.
Push: `git push cube666 dice-motu`.

---

## Decyzje architektoniczne (dice vs. main)

| Aspekt | main branch (zero-copy) | dice branch |
|--------|------------------------|-------------|
| Start/Stop IR/IT | Ręczny (UI) | AudioDriverKit automatyczny |
| ZTS | Symulowany przez timer | Hardware (IR DMA → UpdateCurrentZeroTimestamp) |
| Encoder | PacketAssembler.hpp (MOTU V3 native) | AM824 (TODO: wymienić na MOTU V3) |
| Geometry | kRxPcmChannels=18, bypassed check | kRxPcmChannels=16 (quick fix), 18 cel |
| Model danych | Ring-buffer indirect copy | IOBufferMemoryDescriptor (cel po ZTS) |

---

## Pliki kluczowe (dice branch)

| Plik | Rola w ZTS debug |
|------|-----------------|
| `ASFWDriver/Audio/DriverKit/Config/DICE/Isoch/Profiles/MOTU828Mk3Profile.cpp` | `kRxPcmChannels` = źródło `inputChannelCount` |
| `ASFWDriver/Audio/DriverKit/ASFWAudioDriverGraph.cpp` linia ~166 | `profile->RxChannelCount()` override |
| `ASFWDriver/Isoch/Receive/IsochReceiveContext.cpp` | IR DMA, `DrainCompleted()`, `Poll()` |
| `ASFWDriver/Audio/Engine/Direct/Rx/RxAudioPacketProcessor.cpp` linia ~68 | AM824 geometry check |
| `ASFWDriver/Audio/Protocols/Vendor/MOTU/MOTUVendorProtocol.hpp` | `kHostInputPcm` (nieistotny dla geometrii) |
