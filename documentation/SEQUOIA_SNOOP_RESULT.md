# SEQUOIA SNOOP RESULT — measured SPH lead of the official MOTU driver

> **Translation note:** this file is normally maintained in Polish on the working branch
> (`integrate-dice-c2bdf11`). This English version is a snapshot translation for the
> `motu-v3-showcase` branch only — it will not auto-update.

> Measurement performed **2026-06-26** on a live, cleanly-playing stack: **M3/Sequoia (official
> MOTU driver) plays music → MBP2009/Linux Mint passively snoops the bus.** Goal: calibrate
> `kMotuSphPresentationLeadTicks` (was a guess = `2*3072`). Handoff: `SEQUOIA_SNOOP_HANDOFF.md`.

## ⭐ RESULT (to be consumed by the Tahoe session)

| metric | value |
|---|---|
| **median lead** | **3 cycles** (mean=3.000, min=3, max=3, mode=3 — **zero spread**) |
| **lead in ticks** | **9216** (= `3 * 3072`) |
| DATA packets used | 192 (lead histogram: `{3: 192}`) |
| **SID sent by the official driver** | **3** (constant across all packets) |
| DBS | `0x0d` = 13 |
| CIP Q0 byte2 | `0x04` |
| CIP Q1 | `8222ffff` (SYT=0xFFFF) |
| IT channel (host→device) | **ch 33** |

### ➡️ Action for Tahoe
- Set `kMotuSphPresentationLeadTicks` in `ASFWAudioDriverZts.cpp` from **`2*3072` (6144) → `3*3072` (9216)**.
  We were missing **one cycle** of lead — the top squeal suspect (master-feed phase).
- **SID=3 confirmed as the official stack's value** (matches El Cap). Our wire sends SID=0,
  because we are node 0 on our bus. SID = source node-id, so 0 is correct *if* we are
  node 0 — it's not a freely-settable constant. Tune lead first; treat SID as a separate,
  low-priority item (as noted so far in Focus.md).

## Method / lead definition

For every DATA packet (`len=424`, Q1=`8222ffff`):
- `sph = quadlet[2]` (SPH of the first block), `sph_cyc = (sph>>12)&0x1FFF`
- `arr_cyc = cyc & 0x1FFF` (the `cyc=` field = arrival cycle, `ev->cycle` from firewire-cdev)
- `lead = ((sph_cyc - arr_cyc) + 8000) % 8000`, corrected `>4000 → -8000`

`arr_cyc` (cyc) increases monotonically by ~1 per packet (consistent with cycle spacing) — the
scale field is correct, the lead is not an artifact of a wrong `cyc` unit.

## Evidence — raw packets (cyc, Q0, Q1, SPH, sph_cyc, sph_off, lead)

```
(50719, 030d04a0, 8222ffff, 00622bbf, sph_cyc=1570, sph_off=3007, lead=3)
(50721, 030d04a8, 8222ffff, 006243bf, sph_cyc=1572, sph_off= 959, lead=3)
(50722, 030d04b0, 8222ffff, 006257bf, sph_cyc=1573, sph_off=1983, lead=3)
(50723, 030d04b8, 8222ffff, 00626bbf, sph_cyc=1574, sph_off=3007, lead=3)
(50725, 030d04c0, 8222ffff, 006283bf, sph_cyc=1576, sph_off= 959, lead=3)
(50726, 030d04c8, 8222ffff, 006297bf, sph_cyc=1577, sph_off=1983, lead=3)
```

Supporting observations:
- **SPH offset cadence** cycles `3007 → 959 → 1983 → 3007 …` (+1024 ticks/packet, wrapping every
  3072 = cycle length). DBC `030d04a0/a8/b0/b8` = +8/packet (data). Cadence D,D,N (no-data = `len=8`).
- Full capture (256 packets, including no-data) → `documentation/raw-captures/2026-06-26_sequoia_official_it_cyc.txt`.

## ⭐⭐ SECOND ATOMIC TRUTH — PCM slot map (Main Out 1/2 → slot 12/13)

From the same capture (Sequoia was playing **Default Stereo Output = Main Out 1-2**), a block-level
analysis of the IT payload (DBS=13 → block = SPH(1 quad) + 12 quad PCM = 16 3-byte BE slots):

- **ONLY slots 12 and 13 are active** (0-indexed within the PCM region after SPH); the rest = `000000`.
- `slot12 max≈1.98M`, `slot13 max≈2.00M`, active=100% of blocks — clean stereo (music).
- Framing verified: every block starts SPH with **Δ=512/frame** (off 3007→447→959→…→3007, +512),
  slots 12/13 change per frame = real PCM. (1536 blocks / 192 packets × 8.)

**Example (packet cyc=50719, block 0): slot12=`01b9c9` slot13=`1380d5`.**
Overall block offset: SPH=bytes 0-3, slot12=bytes 4+12·3=40-42, slot13=43-45 (within the
52-byte block).

### ➡️ Action for Tahoe (ROUTING — separate lead from the lead-timing one)
**Our encoder puts Main on slot 0/1; the official driver puts it on slot 12/13.** This most likely
explains "Main silent + LEDs wandering (Analog 7 / S/PDIF / Main R)": our PCM lands on the wrong
physical outputs, while the real Main Out gets silence. Move stereo to **slot 12/13** in the IT
encoder (`AmdtpPayloadWriter` MOTU path, `kMotuV3PcmByteOffset`/slot — Focus "Fix 74").
> ⚠️ **Conflict with an earlier note:** Focus.md said "our WIRE16 = slot 0/1, identical to El Cap".
> This measurement (official Sequoia, byte-perfect lead) shows Main = **12/13**. Measurement beats
> a hand-written note — verify which logical channel maps to 12/13 BEFORE changing anything (the
> full map of the 14 outputs hadn't been collected yet at this point — see below).

## ⭐⭐⭐ THIRD ATOMIC TRUTH — FULL map of the 14 IT outputs (CoreAudio out → wire slot)

Measured 2026-06-26: PortAudio (sounddevice) played **real 14 channels** on the MOTU (each channel
a different frequency, 1000+i·211 Hz), snoop on ch33 + FFT per slot. All peaks matched within
< 1 bin (~15 Hz).
(`afplay` and `sox` **downmix to stereo** — `sox: can't set 14 channels; using 2` — hence PortAudio.)

| CoreAudio out (1-based) | wire slot | level | note |
|:---:|:---:|:---:|---|
| **1 (Main L)** | **12** | −3 dB | Default Stereo Output |
| **2 (Main R)** | **13** | −3 dB | Default Stereo Output |
| 3 | 4 | −37 dB | attenuated in the MOTU mixer |
| 4 | 5 | −37 dB | attenuated |
| 5 | 6 | 0 dB | |
| 6 | 7 | 0 dB | |
| 7 | 8 | 0 dB | |
| 8 | 9 | 0 dB | |
| 9 | 10 | 0 dB | |
| 10 | 11 | 0 dB | |
| 11 | 2 | 0 dB | |
| 12 | 3 | 0 dB | |
| 13 | 14 | 0 dB | |
| 14 | 15 | 0 dB | |

**Reverse (wire slot → CoreAudio out):** `0,1`=**UNUSED (padding)**; `2`→11, `3`→12, `4`→3,
`5`→4, `6`→5, `7`→6, `8`→7, `9`→8, `10`→9, `11`→10, `12`→**1 (Main L)**, `13`→**2 (Main R)**, `14`→13, `15`→14.

### ➡️ Action for Tahoe (ROUTING — full truth)
- **14 PCM occupies slots 2–15; slots 0,1 are EMPTY.** Our MOTU encoder must map CoreAudio
  output channel k → wire slot per the table (Main 1/2 → 12/13), NOT k→slot k from zero.
- This is almost certainly a co-cause of "Main silent + LEDs wandering": we were sending PCM to
  slot 0/1 (padding) and shifted → MOTU lit the wrong outputs, Main stayed silent.
- The level (−3 dB Main, −37 dB out3/4) is the MOTU mixer — do NOT replicate it, it's not part of
  wire encoding.
- Raw capture: `documentation/raw-captures/2026-06-26_sequoia_it_sweep14_pa.txt`
  (+ `..._sweep14.txt`/`_sox.txt` = afplay/sox downmix, kept for reference that they downmix).

## ⭐⭐⭐⭐ FOURTH ATOMIC TRUTH — IR stream (device→host) + input map anchor

Measured 2026-06-26: a ~250 Hz signal fed into **Analog In 1**, snoop on **ch34** (IR device→host).

### IR packet structure (confirmed empirically)
- **Iso channel: ch34**, `len=520`, `tag=1 sy=0`.
- **Fixed CIP: `0d040400 22ffffff`** (EOH=0, a NON-standard MOTU V3 header — consistent with Focus v117).
- Payload after CIP (8 B) = 512 B = **8 blocks × 64 B** (DBS=16, 16 quadlets/block).
- 64 B block: **SPH @ offset 0** (`008c34b1`→`008c36b1`→`008c38b1`, **Δ=512/frame**), bytes 4–9 =
  a 6-byte header/marker region (`00000080` constant @ q3, counter `00000301`++ nearby at q16),
  **PCM @ offset 10: 18 channels × 3 bytes BE** (= 54 B; 10+54=64 ✓).

### Input map (confirmed via signal)
| IR ch (0-based) | physical input | evidence |
|:---:|---|---|
| **0** | **Mic/Instrument 1** | 250 Hz, max≈2.06M (preamp gain) (`ir_mic1.txt`) |
| **2** | **Analog In 1** | 250 Hz (`ir_analog1.txt`) |
| **3** | **Analog In 2** | 250 Hz (`ir_analog2.txt`) |
| **4** | **Analog In 3** | 250 Hz (`ir_analog3.txt`) |
| **5** | **Analog In 4** | 250 Hz (`ir_analog4.txt`) |
| **6** | **Analog In 5** | 250 Hz (`ir_analog5.txt`) |
| **7** | **Analog In 6** | 250 Hz (`ir_analog6.txt`) |
| **8** | **Analog In 7** | 250 Hz (`ir_analog7.txt`) |
| **9** | **Analog In 8** | 250 Hz (`ir_analog8.txt`) |

**→ THE ENTIRE ANALOG 1-8 BLOCK = IR ch2-9 CONFIRMED (8/8).**

Remaining channels: background noise (nothing plugged in) or zero (ch 10–15 = optical disabled).

**ch16-17 = stereo bus MONITOR/RETURN (NOT physical inputs 1:1).** Mirror observed:
Analog1→ch16, Analog2→ch17, Analog3→ch16. So odd inputs pan left (ch16), even inputs pan
right (ch17) — this is a CueMix/return mix, dependent on routing, **not an input channel**.
⚠️ Do not map ch16/17 as physical inputs.

**Ordering:** **ch0 = Mic/Instrument 1 CONFIRMED**, ch1 = Mic/Inst 2 (hypothesis),
**ch2-9 = Analog 1-8 CONFIRMED 8/8**, ch10-15 = S/PDIF+ADAT (zero with optical
disabled), ch16-17 = bus monitor/return (above).

### ➡️ Action for Tahoe
- The IR decoder already works (v117, offset 10) — this measurement **confirms the layout**
  (18×3B @ off10, SPH Δ=512).
- Capture-channel → CoreAudio input mapping: Analog In 1 = IR ch index 2. Watch out for the
  **Return mirror** (ch16) — it's not a separate input, it's a copy of the bus return
  (dependent on "Return Assign").
- Raw capture: `documentation/raw-captures/2026-06-26_sequoia_ir_analog1.txt`.

## ⭐⭐⭐⭐⭐ FIFTH TRUTH — multi-rate geometry (48/44.1/88.2k: lead constant, SPH Δ = sample period)

Measured 2026-06-26, sample rate switched in MOTU Setup (capture: afplay/Spotify). Raw:
`raw-captures/2026-06-26_sequoia_{it,ir}_{44k,88k,96k,176k}.txt` (+ the original 48k above).
⚠️ At 4× (176.4/192k) packets exceed 1024 B → the snoop had to be built with `MAX_PKT_BYTES=4096`
(`fw_big` on the MBP2009).

| metric | 44.1k | 48k | 88.2k | 96k | 176.4k | 192k | conclusion |
|---|:--:|:--:|:--:|:--:|:--:|:--:|---|
| family | 1× | 1× | 2× | 2× | 4× | 4× | |
| **IT lead** | 3 | 3 | 3 | 3 | 3 | 3 | **RATE-INDEPENDENT** (6 rates) → `kMotuSphPresentationLeadTicks=9216` (3×3072) always (FW cycle=3072 ticks/8000 Hz) |
| **SPH Δ/frame** | 557 | 512 | 279 | 256 | 139 | **128** | **Δ = 1-sample period @ 24.576 MHz** = `24576000/fs` (557.14/512/278.64/256/139.32/**128**). Non-integer → dithering |
| **DBS IT / IR** | 13/16 | 13/16 | 13/16 | 13/16 | 10/13 | **10/13** | 1×/2× full (14/18 PCM); **4× REDUCES channels** (IT 13→10, IR 16→13) — bandwidth |
| frames/packet | 8 | 8 | 16 | 16 | 32 | **32** | = 8×family (1×→8, 2×→16, 4×→32) |
| len IT / IR (B) | 424/520 | 424/520 | 840/1032 | 840/1032 | 1288/1672 | 1288/1672 | |
| data cadence | 68.8% | 75% | 68.8% | 75% | 68.8% | 75% | `8000·frac·frames = fs` |

**All 3 families closed with 2 points each — patterns confirmed across 6 rates
(44.1/48/88.2/96/176.4/192).**

### ➡️ Conclusion for Tahoe (multi-rate, future work)
- **Lead 9216 ticks is correct for EVERY rate** (confirmed at 44.1/48/88.2/96/176.4k — do not
  scale with fs).
- **SPH Δ = `round(24576000/fs)`** with remainder dithering (do NOT hardcode 512). For the
  current 48k, that's exactly 512.
- **Channel count (DBS) depends on the family:** 1×/2× = 13/16 (14/18 PCM); **4× = 10/13**
  (fewer PCM — MOTU reduces at 176.4/192k). frames/pkt = 8×family (8/16/32). All 6 rates measured.
- For the current bug (48k) only the 48k column matters; the rest is a full multi-rate roadmap.

## Measurement environment
- MOTU 828mk3, **firmware 1.06, boot 1.01**. MOTU Audio Setup: 48000 / Clock Internal /
  Default Stereo Input=Mic/Instrument 1-2 / Default Stereo Output=Main Out 1-2 / Main Out Assign=Main Out 1-2 /
  Return Assign=Analog 1-2 / optical A/B Disabled / "Enable Core Audio volume controls" = OFF.
- M3/Sequoia = node-id 3 (SID=3 on the wire). MBP2009/Linux Mint = snoop. LaCie d2 quadra daisy-chained.

## Setup / gotchas encountered (for next time)

- **The LaCie d2 quadra was powered off** → broke the daisy-chain → Linux only saw itself (`fw0`).
  After powering the LaCie on, the bus enumerated: `fw1`=M3 (Apple), `fw2`=MOTU (OUI `0001f2`), `fw3`=LaCie.
- `quirks=0x10` **did not take effect via plain `modprobe`** (the module was already loaded at boot,
  `quirks 0x0`). Had to `modprobe -r firewire_ohci firewire_core …` and reload with `quirks=0x10`.
- The snoop queues 256 buffers per run → max ~256 packets/run (enough — the lead has zero spread).
- **The parser from the handoff had a wrong regex** — the line is `len=424 ch=.. tag=.. sy=..:`
  (the handoff assumed `len=424:`). Correct regex:
  `cyc=(\d+) len=424 ch=\d+ tag=\d+ sy=\d+:\s+([0-9a-f ]+)`.
- SSH: `-o PreferredAuthentications=password -o PubkeyAuthentication=no` (a passphrase-protected
  key hangs).
