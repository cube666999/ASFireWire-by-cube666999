# MOTU 828 MK3 (V3) — canonical hardware facts

> Przeniesione z `CLAUDE.md` (odchudzenie kontekstu, 2026-07-01). Czytaj przy: pracy nad
> `MOTUVendorProtocol`, `MOTU828Mk3Profile`, enkoderem/dekoderem IT/IR, rejestrem CLOCK_STATUS.

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

## Źródła ground-truth MOTU — skąd liczby i czemu ufać

> Liczby MOTU pochodzą z **obserwacji działających stosów**, nie z domysłów. Część stosów gra,
> część nie — pomyłka tutaj kosztowała już cofnięte fixy. Linkuj te pliki, **NIE kopiuj z nich liczb**.

| Źródło | Co to jest | Wartość / zaufanie |
|--------|-----------|--------------------|
| [El Capitan→M3 snoop](../../../ASFireWire/documentation/raw-captures/) | Pasywny snoop ASFWDriver (M3 Tahoe) łapiący **El Capitan↔MOTU** — realne bajty z drutu | ⭐ **wire oracle**: CIP, **SPH Δ=512**, payload, slot map |
| [`MOTU_V3_WIRE_GROUNDTRUTH.md`](../../../ASFireWire/documentation/MOTU_V3_WIRE_GROUNDTRUTH.md) | Analiza snoopów El Cap + Linux tracepoint | ⭐ **KANON wire** — DBS=13/IT, 16/IR, CIP, SPH Δ=512, mapa slotów |
| [`MOTU_KEXT_GHIDRA.md`](../../../ASFireWire/documentation/MOTU_KEXT_GHIDRA.md) | Disasm działającego `MOTUFireWireAudio.kext` | ✅ mechanizm: **SPH bit=0x400 (bit 10)**, DBC, encoding. ⚠️ jego **DBS=21** = własny kekst MOTU, NIE ścieżka El Cap IOFireWireAVC (13/16) — do *wartości* używaj WIRE_GROUNDTRUTH |
| [Sequoia DTrace bundle](../../../ASFireWire/diagnostics/sequoia_20260525_003640/) | macOS Sequoia + oficjalny kekst MOTU, DTrace lifecycle + ioreg | ✅ kolejność IRM/isoch lifecycle. ❌ **brak danych pakietowych/SPH** (sondy MOTU/IR padły) |
| [`LINUX_MBP2009_SSH.md`](../../../ASFireWire/documentation/LINUX_MBP2009_SSH.md) | Linux Mint (MacBook 2009) z własnym `snd-firewire-motu` | ✅ TYLKO struktura CIP (DBS=13, SYT=0xFFFF) via ALSA tracepoint. ⛔ **NIE gra (pisk, issue #27), NIE oracle audio/bit-order** |

**Reguła zaufania:** dla *grania* oracle = **El Capitan** (snoop lub oficjalny stos). **Linux NIE gra
MOTU** — ślepa uliczka, daje wyłącznie strukturę bajtów. main `MOTU_828_MK3_BringUp.md` oraz
`MOTU_KEXT_GHIDRA.md` mają miejscami **stałe sprzed wire-snoopa** (DBS=21, PACKET_FORMAT 0xC2) — przy
konflikcie wygrywa El Cap wire / `MOTU_V3_WIRE_GROUNDTRUTH.md`, nie hand-written summary ani disasm.
