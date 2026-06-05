# MOTUFireWireAudio.kext — Ghidra/otool Analysis
## Sesja 2026-06-04

Binarka: `/Library/Extensions/MOTUFireWireAudio.kext/Contents/MacOS/MOTUFireWireAudio`  
Universal binary: x86_64 + arm64e. Analiza x86_64 (czytelniejsza).

---

## 1. Architektura ogólna

Klasy kluczowe:
- `OutputBuffer` — bazowy bufor wyjściowy, metoda `CopyPacket(float*&, float*&, int, int, uint*,int,int,CopyArgs*)`
- `FireWireOutputBuffer` — podklasa dla FW, przechowuje DBS jako `field_0x8`
- `FireWireOutputProgram` — zarządzanie logiką IT
- `com_motu_driver_FWA_Stream` — strumień audio, zawiera `field_0x198` = pcm channel start index
- `com_motu_driver_FWA_Box828mk3` — 828 MK3 specific: `SetupStreams`, `SetupStrmFwIds`, `CreateStreams`
- `InputBuffer` / `FireWireInputBuffer` — bufor wejściowy IR

---

## 2. CIP Q0 — potwierdzone pola

Z `FireWireOutputBuffer::SetHeaders` (0x2de10):

```asm
movslq 0x8(%rdi), %rcx      ; rcx = DBS (stored in FireWireOutputBuffer.field_0x8)
movl %ecx, %eax
shll $0x10, %eax             ; DBS << 16  → bits [23:16] of CIP Q0
movzbl %sil, %esi            ; sil = isoch channel number
shll $0x18, %esi             ; channel << 24 → bits [31:24]
orl %eax, %esi               ; combine: (channel<<24) | (DBS<<16)
movl %esi, -0x58(%rbp)       ; save partial Q0
...
orl $0x400, %eax             ; SPH bit = bit 10 = 0x400 ✓
bswapl %eax                  ; big-endian conversion
movl %eax, (%rcx)            ; write CIP Q0 to DMA buffer
```

**Wnioski:**
- SPH bit = `0x400` = bit 10 ✓ (zgodne z naszym Fix 45: `sphBit = 1u<<10`)
- DBS w CIP Q0 bits [23:16] ✓ (standard)
- DBC (Data Block Counter) aktualizowane w pętli `addl $0x1000, %ebx` (inkrementacja DBC co data block)

---

## 3. PCM Sample Encoding — KRYTYCZNE

Z `OutputBuffer::CopyPacket` (0x3d320, x86_64):

### Stride w data block (frame stride)
```asm
movl 0x8(%r13), %eax         ; eax = DBS (= 21 dla 828mk3)
imull %ecx, %edx             ; edx = DBS × frame_index
leaq (%rsi,%rdx,4), %rsi     ; frame_base = buffer + DBS × frame_index × 4
shll $0x2, %eax              ; eax = DBS × 4 = 84 bytes per frame stride
```
**Frame stride = DBS × 4 = 84 bajty** (standardowy AM824, 4 bajty/kanał). ✓

### Obliczenie pcm_byte_offset w obrębie data block
```asm
movl 0x10(%rbp), %edx        ; edx = pcm_param (7th arg, from CopyArgs.field_0x10)
leal (%rdx,%rdx,2), %edx     ; edx = pcm_param × 3  ← KLUCZOWE
movslq %edx, %r11
addq %rsi, %r11              ; r11 = frame_base + pcm_param×3  (PCM start)
```
**PCM byte offset = pcm_param × 3** gdzie `pcm_param` przychodzi z `CopyArgs.field_0x10`.

### Sample write (inner loop) — stride między kanałami
```asm
; L channel (ch 0):
movss (%r9,%rax,8), %xmm1    ; load float L[frame] — stride=8 bajtów w src (interleaved L+R)
cvttss2si %xmm1, %ecx        ; convert float → int32
shrl $0x18, %ecx             ; byte[0] = bits [31:24] = MSB ← GÓRNE 24 BITY
movb %cl, (%rdx)
shrl $0x10, %ecx
movb %cl, 0x1(%rdx)          ; byte[1] = bits [23:16]
movb %ch, 0x2(%rdx)          ; byte[2] = bits [15:8]

; R channel (ch 1):
movss 0x4(%r10,%rax,8), %xmm1 ; load float R[frame]
cvttss2si %xmm1, %ebx
shrl $0x18, %ebx
movb %bl, 0x3(%rdx)          ; byte[3] = R MSB
movb %bl, 0x4(%rdx)
movb %bh, 0x5(%rdx)          ; byte[5] = R bits [15:8]

addq $0x6, %rdx              ; advance dst pointer by 6 bytes (2 channels × 3 bytes)
```

**Stride między kanałami w dst = 3 bajty** (packed, NIE 4-bajtowe AM824 words). ✓

**KRYTYCZNE — BIT SHIFT:**  
Kext wyciąga **górne 24 bity** (`shrl $0x18` = bits 31:24 jako MSB).  
Nasz `PacketAssembler` wyciąga **dolne 24 bity** (`(s >> 16) & 0xFF` = bits 23:16 jako MSB).

```
Kext:   [bits 31:24][bits 23:16][bits 15:8]   (górne 24 bity int32)
Nasz:   [bits 23:16][bits 15:8][bits 7:0]     (dolne 24 bity int32)
```

Jeśli CoreAudio dostarcza **left-aligned int32** (audio w bits 31:8, bits 7:0 = 0), to:
- kext: poprawne
- nasz: przesunięte o 8 bitów w dół → amplituda 1/256 oryginalnej

Jeśli CoreAudio dostarcza **right-aligned int32** (audio w bits 23:0):
- kext: błędne (bierze górne śmieci)
- nasz: poprawne

**Do weryfikacji:** sprawdzić format sampli dostarczanych przez `IOUserAudioDevice::performIO`.

---

## 4. pcm_param = CopyArgs.field_0x10 — skąd pochodzi

Łańcuch wywołań:
```
FWA_Stream::clipOutputSamples()
  CopyArgs.field_0x10 = this->field_0x198   ← KLUCZOWE
  → OutputBuffer::ClipOutputSamples(CopyArgs)
      -0x50(%rbp) = CopyArgs.field_0x10  (saved)
      → OutputBuffer::CopyPacket(..., pcm_param=CopyArgs.field_0x10, ...)
            pcm_byte_offset = pcm_param × 3
```

`FWA_Stream::field_0x198` jest ustawiane przez:
```cpp
// com_motu_driver_FWA_Stream::SetStartingChannelIndexInPacket(int value)
// address 0x7e80
this->field_0x198 = value;  // value = pcm_param (indeks kanału × 3 = byte offset)
```

---

## 5. Inicjalizacja pcm_param dla 828mk3

Z `Box828mk3::SetupStrmFwIds` (0x3e9b0):

```asm
callq *0x168(%rax)           ; GetFirstIsochChannel() → r12 = FC
callq *0x220(%rax)           ; GetSampleRateMultiplier() → r15 = mult (1/2/4)

; IR streams (device→host):
movl %r12d, 0x26a0(%r13)     ; IR_A.start_ch = FC
leal 0x2(%r12), %eax
movl %eax, 0x26a4(%r13)      ; IR_B.start_ch = FC+2 (np. analog + digital)

; IT streams (host→device):
leal 0xa(%r12), %ebx         ; ebx = FC+10
movl %ebx, 0x26a8(%r13)      ; IT_A.start_ch = FC+10  ← analog out
leal 0xc(%r12), %eax
movl %eax, 0x26ac(%r13)      ; IT_B.start_ch = FC+12
addl $0xe, %r12d             ; r12 = FC+14
...
```

Następnie w `SetupStreams` (0x3ed90):
```asm
movl 0x26a8(%r13), %esi      ; esi = FC+10
callq 0x7e80                  ; stream->SetStartingChannelIndexInPacket(FC+10)
```

**Kluczowy wniosek:** `pcm_param = FC + 10`. Jeśli FC=0: `pcm_param=10`, `byte_offset = 30`.

### Problem z niekompatybilnością z Linuxem (pcm_byte_offset=10)

Linux: `pcm_byte_offset = 4 (SPH) + msg_chunks × 3 = 4 + 6 = 10 bajtów`.

Kext: `pcm_byte_offset = (FC+10) × 3`. Dla `pcm_byte_offset=10` wymagane byłoby FC+10 = 10/3 = 3.33 — NIE CAŁKOWITE.

Możliwe interpretacje:
1. `frame_base` w kexcie wskazuje na `data_block_start + 4` (pomija SPH). Wtedy: `actual_byte_offset = 4 + FC × 3`. Dla FC=2: `4 + 6 = 10`. ✓
2. Kext używa innego byte_offset (np. 12 dla FC=4), a Linux i kext mają inne układy pakietów.

**Nie udało się jednoznacznie ustalić** bez podłączonego hardware. FC=2 (2 msg kanały) daje zgodność z Linuxem jeśli interpretacja (1) jest poprawna.

---

## 6. OutputBuffer::Init — mapa pól obiektu

```
OutputBuffer::Init(int arg1, int arg2, int arg3, int arg4, int arg5,
                   int arg6, int arg7, ushort arg8, ushort arg9)

this->0x8  = arg3 (rcx)   = DBS (data block size in quadlets)
this->0xc  = arg1 (rsi)   = num_output_channels (?)
this->0x10 = arg2 (rdx)   = frames_per_packet (?)
this->0x24 = arg2 (rdx)   = same as 0x10
this->0x28 = arg5 (r9)    = pcm_param default (2 lub 3 w zależności od sample rate)
this->0x2c = arg6 (stack) = r10 (0 lub 4)
this->0x30 = arg7 (stack) = r15 (0 lub 2)
this->0x34 = arg8 (ushort)= r11w (0x4000 lub 0x2000)
this->0x36 = arg9 (ushort)= r10w (0x8000 lub 0x1000)
this->0x14 = computed     = buffer_length ≈ 0xFA00 × arg2 / LCM
```

**Ważne:** `this->0x28` (= r9d = 2 lub 3) to NIE jest pcm_byte_offset! To osobne pole. Pcm_byte_offset pochodzi z `CopyArgs.field_0x10` → `FWA_Stream.field_0x198`.

---

## 7. InputBuffer828::InitHook — pcm_byte_offset dla starego 828

```asm
; InputBuffer828::InitHook (0x2d3c0) — dla oryginalnego 828 (nie 828mk3!)
movl 0x14(%rdi), %eax       ; field_0x14 = arg2 z InitInputBuffer (msg chunks?)
leal 0xa(,%rax,4), %eax     ; eax = field_0x14 × 4 + 10
movl %eax, 0x70(%rdi)       ; pcm_byte_offset = field_0x14 × 4 + 10
```

Dla `field_0x14 = 0`: pcm_byte_offset = 10. Zgodne z Linuxem dla starszych urządzeń.

---

## 8. Konkluzje dla naszego kodu

### ✅ Zgodne z kextem
- SPH bit 10 w CIP Q0 (Fix 45) ✓
- 3 bajty na kanał, big-endian ✓
- Frame stride = DBS × 4 ✓
- Kanał R po kanale L w odległości 3 bajtów ✓

### ⚠️ Do zbadania — bit shift
Kext używa górnych 24 bitów int32. Nasz kod używa dolnych 24 bitów.  
Jeśli ADK dostarcza left-aligned int32 → nasz kod jest 8 bitów przesunięty → amplituda 1/256.  
**Sprawdzić:** format sampli z `IOUserAudioDevice::performIO` i `IOUserAudioStream`.

### ❓ Niejasne — pcm_byte_offset (10 vs inne)
Nie udało się z pewnością ustalić czy kext używa byte offset 10 (jak Linux) czy inną wartością (9, 12).  
Nasz kod używa `block + 10u` (zgodnie z Linux). Wymagany test sprzętowy.

### ❓ IR drops (168 865)
Prawdopodobnie niezwiązane z pcm_byte_offset. Sprawdzić ring buffer IR size vs packet rate.

---

## 9. Metodologia

Narzędzia: `otool -arch x86_64 -tv` + `nm -a` + `c++filt` + `strings`.  
Disassembly zapisane w `/tmp/motu_disasm.txt` (115 166 linii, ~7MB).  
Analiza x86_64 (arm64e dała puste wyjście z otool).
