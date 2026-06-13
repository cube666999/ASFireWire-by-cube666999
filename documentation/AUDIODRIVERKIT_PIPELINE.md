# AudioDriverKit Pipeline — Prawidłowa Architektura

Źródła: WWDC21 "Create audio drivers with DriverKit", Apple Developer Forums, odkrycie mrmidi 2026-06-04.

---

## Model prawidłowy (Apple WWDC21)

```
Hardware DMA interrupt / Timer
  → UpdateCurrentZeroTimestamp(sample_time, host_time)   ← anchor dla HAL
  → zapis do IOBufferMemoryDescriptor                    ← shared buffer
  → HAL czyta bezpośrednio z IOBufferMemoryDescriptor
```

### Model błędny (mrmidi + potencjalnie ASFW)

```
CoreAudio callback push
  → custom ring buffer / shared queue
  → synteza ZTS z mach_absolute_time()
```

**Efekt błędnego modelu:** underruny przy pełnym buforze (dane są, ale HAL nie wie kiedy je czytać).

---

## IOBufferMemoryDescriptor

Tworzenie shared IO buffer (w `StartIO`):

```cpp
OSSharedPtr<IOBufferMemoryDescriptor> io_ring_buffer;
const uint32_t buffer_size_bytes = in_zero_timestamp_period
    * sizeof(float)            // lub int32_t dla 24-bit packed
    * channels_per_frame;

IOBufferMemoryDescriptor::Create(
    kIOMemoryDirectionInOut,   // zarówno driver jak i HAL czytają/piszą
    buffer_size_bytes,
    0,                         // alignment
    io_ring_buffer.attach()
);

// Przekaż do strumienia — HAL dostaje do niego dostęp przez IOUserAudioStream
ivars->output_stream = IOUserAudioStream::Create(
    driver, IOUserAudioStreamDirection::Output, io_ring_buffer.get());
```

Dostęp do buffera z drivera:

```cpp
// Po StartIO:
auto iomd = ivars->output_stream->GetIOMemoryDescriptor();
iomd->CreateMapping(0, 0, 0, 0, 0, ivars->output_memory_map.attach());

// W callbacku DMA:
auto* buf = reinterpret_cast<float*>(
    ivars->output_memory_map->GetAddress() +
    ivars->output_memory_map->GetOffset());
```

---

## Zero Timestamp (ZTS) — synchronizacja z HAL

### Zasada działania

ZTS to para `(sample_time, host_time)` atomicznie aktualizowana przez driver.
HAL używa jej do obliczenia driftu i synchronizacji zegarów.

```
sample_time: numer sampla (inkrementowany o GetZeroTimestampPeriod() co buffer)
host_time:   czas systemowy (mach_absolute_time lub OHCI CycleTimer) dla tego sampla
```

### Implementacja z timerem (WWDC21 — virtual device)

```cpp
void StartTimers() {
    UpdateCurrentZeroTimestamp(0, 0);           // reset
    auto now = mach_absolute_time();
    m_timer->WakeAtTime(kIOTimerClockMachAbsoluteTime,
                        now + m_host_ticks_per_buffer, 0);
    m_timer->SetEnable(true);
}

void ZtsTimerOccurred(OSAction* action, uint64_t time) {
    uint64_t sample_time, host_time;
    GetCurrentZeroTimestamp(&sample_time, &host_time);

    if (host_time == 0) {
        // Pierwsze wywołanie — zakotwicz do hardware time
        sample_time = 0;
        host_time   = time;                     // mach_absolute_time z timera
    } else {
        // Kolejne — inkrementuj
        sample_time += GetZeroTimestampPeriod();
        host_time   += m_host_ticks_per_buffer;
    }

    UpdateCurrentZeroTimestamp(sample_time, host_time);

    // Wypełnij bufor audio
    FillOutputBuffer(GetZeroTimestampPeriod());

    // Zaplanuj następny timer
    m_timer->WakeAtTime(kIOTimerClockMachAbsoluteTime,
                        host_time + m_host_ticks_per_buffer, 0);
}
```

### Implementacja z OHCI CycleTimer (dla ASFW — do zaimplementowania)

```cpp
// OHCI CurrentIsochronousCycleTime — rejestr 0x1E8
// bits[25:12] = cycleCount (0-7999)
// bits[11:0]  = cycleOffset (0-3071)
// Ticks = cycleCount * 3072 + cycleOffset  (@ 24.576 MHz)

void OnDMAInterrupt() {
    uint32_t ohci_ct = ReadOHCIRegister(0x1E8);
    uint32_t cycle   = (ohci_ct >> 12) & 0x1FFF;
    uint32_t offset  = ohci_ct & 0x0FFF;

    // Przelicz na mach_absolute_time (wymaga kalibracji raz)
    uint64_t hw_host_time = OHCITicksToMachTime(cycle, offset);

    uint64_t sample_time, host_time;
    GetCurrentZeroTimestamp(&sample_time, &host_time);

    if (host_time == 0) {
        sample_time = 0;
        host_time   = hw_host_time;
    } else {
        sample_time += GetZeroTimestampPeriod();
        host_time   += m_host_ticks_per_buffer;
    }

    UpdateCurrentZeroTimestamp(sample_time, host_time);

    // Odczytaj dane z IOBufferMemoryDescriptor (HAL je tam umieścił)
    // → zakoduj do IT DMA descriptors → OHCI wysyła na bus
}
```

---

## Przeliczenie OHCI → mach_absolute_time

OHCI CycleTimer tyka @ 24.576 MHz = 3072 ticks/cycle × 8000 cycles/s.
`mach_absolute_time` na Apple Silicon tyka @ 1 GHz (1 ns/tick).

```cpp
// Kalibracja (raz przy starcie):
uint64_t mach_at_calibration = mach_absolute_time();
uint32_t ohci_at_calibration = ReadOHCIRegister(0x1E8);

// Przeliczenie delta:
uint64_t ohci_delta_ticks = ohciTicksSince(ohci_at_calibration, current_ohci);
// 1 OHCI tick = 1/24576000 s = ~40.69 ns
// 1 mach tick (Apple Silicon) = 1 ns
double ohci_ns_per_tick = 1e9 / 24576000.0;  // ≈ 40.690 ns
uint64_t mach_delta = static_cast<uint64_t>(ohci_delta_ticks * ohci_ns_per_tick);
uint64_t hw_host_time = mach_at_calibration + mach_delta;
```

---

## Znany bug Apple Dev Forums (thread/771504)

Przy jednoczesnym Input+Output recording:
- `in_io_buffer_frame_size` jest stały i nie synchronizuje się z `UpdateCurrentZeroTimestamp`
- Write (output) działa poprawnie, Read (input) ma inną charakterystykę
- Apple nie podał publicznego rozwiązania (tylko "wyślij sysdiagnose")

**Symptomy:** clicks, pops, `HALC_ProxyIOContext::IOWorkLoop: Skipping loop due to overload`

**Priorytet wątku drivera:** 63 (vs systemowy 97) — możliwa przyczyna artefaktów przy wysokim obciążeniu.

---

## Podsumowanie: Co zmienić w ASFW

| Aktualnie | Docelowo |
|-----------|----------|
| Custom ring buffer (indirect copy) | `IOBufferMemoryDescriptor` → `IOUserAudioStream::Create()` |
| `mach_absolute_time()` w PerformIO | OHCI `CycleTimer` (reg 0x1E8) przeliczony na mach time |
| Push z CoreAudio do ring buffer | DMA interrupt → update ZTS → fill IO buffer |
| ZTS synthezowany z software timer | ZTS z hardware OHCI cycle counter |

**Priorytet implementacji:** po potwierdzeniu że diody MOTU świecą (Fix 62 / v84).
