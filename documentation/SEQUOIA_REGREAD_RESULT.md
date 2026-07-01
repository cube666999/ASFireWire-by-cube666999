# SEQUOIA_REGREAD_RESULT — MOTU init register read-back (result)

> **Translation note:** this file is normally maintained in Polish on the working branch
> (`integrate-dice-c2bdf11`). This English version is a snapshot translation for the
> `motu-v3-showcase` branch only — it will not auto-update.

> Result of the task from [`SEQUOIA_REGREAD_HANDOFF.md`](SEQUOIA_REGREAD_HANDOFF.md). Claude
> session on **macOS Sequoia 15.7.4 (build 24G517)**, 2026-06-27. Official MOTU driver loaded
> and playing.

## ⭐ UPDATE 2026-06-27 — VALUES OBTAINED (DTrace deref, El Capitan)

> Read-back was blind (write-only), but **DTrace on El Capitan extracted real values at the
> source.** Remote session (Sequoia → SSH → MBP2009 El Capitan 10.11.6, SIP off). Full log:
> [`SEQUOIA_REGREAD_SESSION_LOG.md`](SEQUOIA_REGREAD_SESSION_LOG.md). Raw logs:
> `raw-captures/2026-06-27_elcap_dtrace_deref_values.txt` + `..._iomd_dump.txt`.

**Mechanism:** the official driver writes via `IOFireWireController::asyncWrite(... IOMemoryDescriptor* ...)`;
data was extracted through the chain `*(u32*)(*(u64*)(*(u64*)(arg7+96)))` (descriptor → `_ranges` →
inline `IOVirtualRange.address`).
**Chain validation** (matches read-back): `0b10→0x00000002` ✓, `0b14→0x0a000100` @48k ✓, `0b04→0xffc20001` ✓.

**Values (host-order / "swapped", as in the 020402 dump):**

| register | value | size | notes |
|---|---|:---:|---|
| **0x0b08** | `0xffffffff` → `0x00000000` | 4 | command/doorbell: set→clear (read-back always 0). Written on cold start. |
| **0x0b1c** | **`0x00120000`** (@48 kHz) | 4 | rate-dependent (`0x00000a00` at a different rate; correlates with `0b14` rate-idx). |
| **0x0b38** | **`{0xffc20002, 0x00000000}`** (clear=`ffffffff/ffffffff`) | **8** | V3 stream-control stream-2 (parallel to `0b04=0xffc10001`). **2nd quadlet MEASURED (cap_run4, replug 2026-06-27): `0x00000000`.** A variant with 1st quad `0xffc10002` also seen (less often). |

**Triggers (confirmed):** `0b1c`/`0b00`/`0b10`/`0b14` fire on **stream restart** (sample rate change
or output switch). **`0b38`/`0b08` ONLY on cold start** (MOTU FireWire cable replug).

**➡️ TO DO before deploying to Tahoe:**
1. **Get the 2nd quadlet of `0x0b38`** (size=8) — extend the deref with `d1 = *(u32*)(buf+4)` (`cap_run3.d`).
2. Correlate the full `0b1c` ↔ rate table, if targeting multi-rate (for 48k `0x00120000` is enough).
3. Add writes to `MOTUVendorProtocol::PrepareDuplex` with these values (NOT guessed). Order as in the trace.

> ⚠️ **Correction to the TL;DR below:** the "NEGATIVE result" section applies ONLY to read-back.
> The DTrace deref at the source **did obtain** the values — the read-back description below stays
> as context, but the actual values are above.

---

## TL;DR for the Tahoe session (read-back — historical context)

1. **Read-back is blind to these registers — that's solid.** Registers **0x0b1c** and
   **0x0b38** are **write-only command registers**: they don't respond to async read either
   idle or during an active stream. Read-back physically cannot supply their values. The
   handoff anticipated this ("steady-state read-back might NOT catch it").
2. **`dataBE=0x80a5211c` from the `_v2.txt` trace is an ARTIFACT — do NOT write this value into
   `PrepareDuplex`.** The same "magic" constant appears for **all 40 writes** and all offsets
   (0b00, 0b04, 0b14…), and we know the real values of those offsets from read-back
   (0b00=`0x61620000`, 0b04=`0xffc10001`, 0b14=`0x0a000100`) — none of them is `0x80a5211c`.
   The v2 tracer dereferenced the wrong/a constant pointer. The data is garbage.
3. **What IS certain (from the traces):** 0x0b1c / 0x0b38 / 0x0b08 are **stream-start writes**,
   fired together with `createDCLProgram` (one 0b1c write per (re)build of the DCL = per stream
   start, **NOT a periodic heartbeat** — the apparent periodicity was actually audio-output
   restarts). So they belong to the init sequence — we just don't know their **values**.
4. **Next step to get the values: snoop the actual write payload** (fix the kernel tracer to
   deref the `buf=` DMA buffer, or capture data from the write request). Read-back cannot close
   this path.

---

## Environment (comparable to the `2026-06-08_020402` dump)

| Parameter | Value |
|---|---|
| Host OS | macOS Sequoia **15.7.4** (24G517), MacBook Pro M3 Max (internal SSD) |
| Driver | **official MOTU** (HAL: 16 in / 14 out, FireWire) |
| MOTU | 828mk3, Vendor 0x1F2, Model 0x106800, GUID 0x1F20000087236, Unit SW ver **0x15** |
| nodeID / gen | 0xffc0 / gen increases with bus resets (2→4→7 during stream start) |
| Sample Rate | **48000** |
| Clock Source | **Internal** (MOTU is master — see Focus "clock ruled out") |
| Stream | 440 Hz tone / 48k / stereo, via the default output (afplay → MOTU out 1/2) |

Tool: `../ASFireWire/tools/read_motu_regs.c` (async `ReadQuadlet` via `IOFireWireLib`, range
0x0b00–0x0c98 every 4 B). **On Sequoia, `Open(fw)` does NOT collide with the playing driver** —
reads work freely even during an active stream (unlike the tool's El Capitan-era header warning).

---

## Table — IDLE vs STREAMING (host-order "swapped", as in the 020402 dump)

> "IDLE" = MOTU is the default device, no active IO. "STREAMING" = a tone is actively playing
> through the MOTU (read during playback, gen=4, clean — no bus-reset race). **The columns are
> IDENTICAL** for every readable register → starting a stream did not change ANY readable
> register in this range. The entire startup config lives in the **write-only** 0b1c/0b38 (below).

| offset | IDLE (swapped) | STREAMING (swapped) | note |
|---|---|---|---|
| 0x0b00 | 0x61620000 | 0x61620000 | |
| 0x0b04 | 0xffc10001 | 0xffc10001 | V3 stream control |
| **0x0b08** | **0x00000000** | **0x00000000** | written on start, read-back = 0 (command/doorbell?) |
| 0x0b10 | 0x00000002 | 0x00000002 | PACKET_FORMAT |
| 0x0b14 | 0x0a000100 | 0x0a000100 | CLOCK_STATUS (rate idx) |
| **0x0b1c** | **— (no response)** | **— (no response)** | ⭐ **write-only, UNreadable** |
| 0x0b28 | 0x00101800 | 0x00101800 | |
| **0x0b38** | **— (no response)** | **— (no response)** | ⭐ **write-only, UNreadable** |
| 0x0b68 | 0x00080061 | 0x00080061 | |
| 0x0c04 | 0x00000100 | 0x00000100 | ROUTE_PORT_CONF |
| 0x0c0c | 0x00000080 | 0x00000080 | |
| 0x0c10 | 0x00000067 | 0x00000067 | counter/status (020402 had 0x63 — drift, not relevant) |
| 0x0c40–0x0c4c | "Pres et 1 " (ASCII) | — || port name |
| 0x0c60–0x0c6c | "Internal " (ASCII) | — || clock source name |

> **Confirmation #2 (2026-06-27, Spotify):** a repeat read during real music playback from
> Spotify (not a synthetic tone, nodeID=0xffc0 gen=12) gave an **identical** table — 0b08=0,
> 0b1c/0b38 still absent. Result is independent of the audio source and of whether the exact
> start moment is caught.

Offsets **absent from the table** (0b0c, 0b18, 0b1c, 0b20, 0b24, 0b2c, 0b30, 0b34, **0b38**,
0b3c) returned a read error (no response) and are skipped by the tool — the same in both states
and the same as in the old 020402 dump. This is NOT "zero" — it's "the device does not respond
to a read at this address."

---

## Proof that 0x0b1c / 0x0b38 are write-only (not "we just didn't catch the moment")

- Read while **idle**, read **during an active stream**, and the historical `020402` dump — **in
  all three**, 0b1c and 0b38 are absent. The handoff suggested they might appear "only while
  playing" — **they don't appear then either**.
- In the write trace (`_v2.txt`), 0b1c/0b38 **are written** multiple times, so the device does
  accept them — it just doesn't return them on read. A classic write-only command register
  (consistent with `buf=` = the data buffer in v1).

## Proof that `dataBE=0x80a5211c` is an artifact (do NOT use it)

`grep -oE "dataBE=0x[0-9a-f]+" ..._v2.txt | sort | uniq -c` → **the same value `0x80a5211c` 40×**,
across offsets 0b00/0b04/0b08/0b10/0b14/0b1c/0b38. Since 0b00/0b04/0b14 actually have
`0x61620000`/`0xffc10001`/`0x0a000100` (from read-back), the constant `0x80a5211c` cannot be their
data → the v2 tracer was logging the wrong/a constant pointer. **The values of 0b1c/0b38 remain
unknown.**

## Write timing pattern (this IS solid and useful)

From `_004331_...` and `_010315_..._v2.txt`, correlated with `createDCLProgram`:

- **0x0b1c** — 1 write per `createDCLProgram` (talking=1, i.e. IT/output DCL). 4 writes in v2
  correspond to 4 DCL rebuilds (output restarts) — **per-stream-start, not a heartbeat**.
- **0x0b38** — written at stream start (right next to createDCL), 1–2×.
- **0x0b08** — a few writes tightly grouped at start; read-back = 0 (command/doorbell).

Conclusion: this is a **stream initialization** sequence. The right place for it on our side is
`MOTUVendorProtocol::PrepareDuplex` (alongside the existing 0b04/0b10/0b14/0c04), fired once at
duplex start. We're only missing the **values**.

---

## Recommendation for Tahoe

1. **Do NOT add guessed values** to `PrepareDuplex` (especially NOT `0x80a5211c`). Without real
   data, this step is blind.
2. **Get the values by snooping the write payload** (the only remaining path — read-back is
   exhausted):
   - fix the kernel tracer to log the **contents** of `buf=` (deref the write-request DMA
     buffer), not the pointer/a constant — then 0b1c/0b38/0b08 will yield real quadlets;
   - or dump write-request data from DTrace on the `IOFireWireUserClient`/AVC write path.
3. **In parallel, consider that this might not be static config.** 0b08 read-back=0 suggests a
   command/doorbell, not a held value. If, after obtaining the values and writing them, the
   misframing remains → move to **SPH-echo** (fallback from `Focus.md` / local project memory).

> Bottom line: read-back done, environment (48k/Internal/Main) confirmed, the mechanism works on
> Sequoia without colliding with the driver. But **0b1c/0b38 are write-only and their values do
> not exist in any material collected so far** (v1 = pointer, v2 = artifact constant). The next
> step is snooping write data, not another read.
