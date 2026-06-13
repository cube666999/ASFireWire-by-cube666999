# Plan refaktoru: push → pull (IOBufferMemoryDescriptor + ZTS)

Cel: zlikwidować underruny przy pełnym buforze (38K underrunów @ 144% fill) przez przejście
z modelu „push" (custom ring buffer + synteza ZTS) na model Apple „pull"
(`IOBufferMemoryDescriptor` + ZTS z OHCI CycleTimer).

Źródło architektury: `documentation/AUDIODRIVERKIT_PIPELINE.md` (WWDC21 + mrmidi + Apple Dev Forums).

> ⚠️ **Kolejność:** TEN refaktor robimy DOPIERO po środowym teście ground-truth (snoop+DTrace),
> który naprawia osobny bug (ch7/pisk = zły układ bajtów). Push/pull to inny problem — underruny.
> Nie myl ich: naprawa układu bajtów NIE naprawi underrunów i odwrotnie.

> ⚠️ **Najlepiej poczekać na mrmidi** — przepisuje swój stos DICE na ten sam model.
> Wzoruj się na jego implementacji zamiast wymyślać od zera (pytanie wysłane na Discord).

---

## Diagnoza — dlaczego obecny model zawodzi

Paradoks: bufor pełny (144%), a mimo to underruny. Powód: w modelu push timing rozjeżdża się
między tym co driver syntezuje (`mach_absolute_time()` w `PerformIO`) a tym czego HAL faktycznie
oczekuje. Dane SĄ w buforze, ale HAL nie wie dokładnie kiedy je czytać → underrun mimo pełnego bufora.

| Aktualnie (push — źle) | Docelowo (pull — Apple) |
|------------------------|--------------------------|
| Custom ring buffer (indirect copy) | `IOBufferMemoryDescriptor` → `IOUserAudioStream::Create()` |
| `mach_absolute_time()` w PerformIO | OHCI `CycleTimer` (reg 0x1E8) → mach time |
| Push z CoreAudio do ring buffer | DMA interrupt → update ZTS → czytaj z IO buffer |
| ZTS syntezowany z software timera | ZTS z hardware OHCI cycle countera |

---

## Etapy (rosnące ryzyko — każdy testowalny osobno)

### Etap 0 — Rekonesans kodu (środa, na początku — przez CodeGraph)
Zlokalizować i przeczytać (NIE zgadywać):
- `ASFWAudioDriver.iig` / `.cpp` — gdzie jest `StartIO`, `PerformIO`, tworzenie strumieni
- `IsochAudioTxPipeline` — `InjectNearHw`, obecny ring buffer, skąd bierze dane
- `PacketAssembler` — `encodeToWire` / `writeMotuV3...` (encoding zostaje, zmienia się ŹRÓDŁO danych)
- Gdzie obecnie wołane `UpdateCurrentZeroTimestamp` (jeśli w ogóle) i `mach_absolute_time()`
- Jak `IOUserAudioStream` jest teraz tworzony (z jakim buforem)

CodeGraph queries (z `projectPath`):
```
codegraph_search "StartIO PerformIO ASFWAudioDriver"
codegraph_search "IsochAudioTxPipeline InjectNearHw"
codegraph_callers "UpdateCurrentZeroTimestamp"
codegraph_search "IOUserAudioStream Create"
```

### Etap 1 — Wprowadź IOBufferMemoryDescriptor jako bufor strumienia
- W `StartIO` (lub gdzie tworzony jest output stream) utwórz:
  ```cpp
  IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut, buffer_size_bytes, 0, buf.attach());
  ```
  `buffer_size_bytes = zero_timestamp_period * sizeof(sample) * channels_per_frame`
- Przekaż ten bufor do `IOUserAudioStream::Create(..., io_ring_buffer.get())`
- W driverze zmapuj: `GetIOMemoryDescriptor()->CreateMapping(...)` → wskaźnik do danych
- **Test:** HAL widzi strumień, IO startuje bez crashu. Jeszcze NIE czytamy z bufora w DMA.

### Etap 2 — Przełóż źródło danych DMA na IOBufferMemoryDescriptor
- `InjectNearHw` / encoder: zamiast czytać z custom ring buffer, czytaj z mapowania
  `output_memory_map->GetAddress() + GetOffset()`
- Encoding MOTU V3 (SPH/MSG/PCM packing) **zostaje bez zmian** — zmienia się tylko skąd przychodzą
  surowe próbki PCM
- Usuń (po potwierdzeniu) custom ring buffer i ścieżkę push z CoreAudio
- **Test:** audio gra z nowego bufora. Underruny mogą jeszcze być (ZTS dalej software) — to ok na tym etapie.

### Etap 3 — ZTS z OHCI CycleTimer (sedno fixu underrunów)
- Na przerwaniu DMA (lub jego ekwiwalencie) czytaj OHCI reg `0x1E8`:
  ```cpp
  uint32_t ct = ReadOHCIRegister(0x1E8);
  uint32_t cycle  = (ct >> 12) & 0x1FFF;   // 0–7999
  uint32_t offset =  ct        & 0x0FFF;   // 0–3071
  uint64_t ticks  = cycle * 3072 + offset; // @ 24.576 MHz
  ```
- Kalibracja raz przy starcie (mach_at ↔ ohci_at), potem przeliczaj delty:
  ```cpp
  double ohci_ns_per_tick = 1e9 / 24576000.0;  // ≈ 40.690 ns
  uint64_t hw_host_time = mach_at_calibration + ohci_delta_ticks * ohci_ns_per_tick;
  ```
- Wzorzec pętli ZTS (z `AUDIODRIVERKIT_PIPELINE.md` sekcja „OHCI CycleTimer"):
  - pierwsze wywołanie: `sample_time=0, host_time=hw_host_time`
  - kolejne: `sample_time += GetZeroTimestampPeriod()`, `host_time += host_ticks_per_buffer`
  - `UpdateCurrentZeroTimestamp(sample_time, host_time)`
- **Test:** underruny spadają / znikają mimo zmieniającego się obciążenia. To jest miara sukcesu.

### Etap 4 — Czyszczenie i eksperymentalny kod
- Usuń: `kChannelSweepTest`, logi `[INJ]/[WIRE]/[DBG-PCM]`, `lastEncodedPayload_`
- Usuń osierocony custom ring buffer / push helpers (po potwierdzeniu nieużywane)
- Usuń `mach_absolute_time()` z PerformIO jeśli zastąpiony

---

## Ryzyka / pułapki

- **Apple bug Input+Output ZTS** (Dev Forums thread/771504): przy jednoczesnym IN+OUT
  `in_io_buffer_frame_size` nie synchronizuje się z `UpdateCurrentZeroTimestamp`. Objaw: clicks/pops,
  `HALC_ProxyIOContext ... Skipping loop due to overload`. Brak publicznego fixu Apple.
  → Najpierw zrób TYLKO output (playback), input dodaj później.
- **DriverKit interrupt model** — sprawdzić w Etapie 0 czy mamy realne przerwanie DMA czy poll.
  Jeśli poll, ZTS update wiąże się z pętlą poll, nie z hardware IRQ (gorsza precyzja, ale działa).
- **Priorytet wątku drivera = 63** (vs system 97) — możliwe artefakty przy obciążeniu (z Dev Forums).

---

## Definicja sukcesu

Underruny ≈ 0 przy stabilnym odtwarzaniu (obecnie ~15% encoded 6/8, 38K underrunów @ 144% fill),
bez własnej kolejki i bez syntezy timingu z `mach_absolute_time()`.
