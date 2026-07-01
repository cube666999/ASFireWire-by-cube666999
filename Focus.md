# Focus.md — ASFireWire-dice work plan

> **Translation note:** this file is normally maintained in Polish on the working
> branch (`integrate-dice-c2bdf11`). This English version is a snapshot translation
> for the `motu-v3-showcase` branch only — it will not auto-update; check the
> working branch for the current source of truth.

Goal: MOTU 828 MK3 working through the DICE driver (new architecture, AudioDriverKit-native).

Archive of completed sessions → `DevLog.md`

---

## 🧭 FIRST: which system are you on? (M3 has two OSes)

> ## ⏸️ HANDOFF 2026-06-28 (user back WED 2026-07-01) — WIRE + REGISTERS EXHAUSTED, misframe = OPERATIONAL layer
> **Full day of tight-loop tests. HARD RULED OUT:** (a) **wire byte-for-byte = official** (diff: PCM only on
> slot 12/13, block-byte 40-45, 3B BE, SPH@quad2, rest zero → **slot 12/13 = Main, correct**); (b) **register set
> identical** — official writes EXACTLY `0b00 0b04 0b08 0b10 0b14 0b1c 0b38`, we write that plus an extra 0c04;
> (c) **SID** (=node 1); (d) **SPH lead** measured on the wire `−258→−63→+26→+79` (v141-v144) → LED pattern
> IDENTICAL → **lead is DEAD as a cause**.
> The official stack plays cleanly on the SAME M3+TB→FW adapter+MOTU with identical wire+registers →
> **the difference is OPERATIONAL** (IRM/bandwidth, cycle-master, start ORDER/TIMING: IT-DMA vs FETCH vs
> device IR, handshake), invisible on the wire.
>
> **➡️ PLAN (choose on return):**
> 1. **mrmidi** (lead dev) — his territory (clock/IRM/coordinator). We have an ironclad, narrowed proof:
>    "wire+registers identical, MOTU still misframes → operational, not wire". Fastest path.
> 2. **Bus-layer deep-dive** — compare our DiceDuplexBringup/coordinator start-sequence + IRM + cycle-master
>    vs the official one (DTrace lifecycle on Sequoia/El Cap: createDCLProgram / isoch-start order /
>    IRM AllocateResources) vs our logs.
> 3. **Linux clock (SPH-echo) = SECOND-CHOICE TOOL, not now.** Decision 2026-06-28: the wire is already
>    byte-perfect vs El Cap (a better oracle than Linux), so echoing SPH won't change bytes that already
>    match → unlikely to fix it. Linux as a *mechanism* is fine (see local project memory), but only fire
>    it **if** step 2 shows a real **clock drift over time** (symptom: "LEDs after a few minutes" =
>    possible convergence/drift signature). Try 1/2 first.
>
> **Build: v144** (`kMotuSphPresentationLeadTicks=305` → on-wire +26). Lead is MOOT (ruled out), init
> writes STAY (faithful to the official sequence).
> **Setup:** MOTU on the M3 (⚠️ a loose FireWire cable caused "MOTU not visible" — check the connection).
> MBP2009=Linux PASSIVE (`snd_firewire_motu` unloaded, quirks=0x10, `/dev/fw0`+fw1=MOTU, snoop tool in
> `/tmp/` ch1). SSH + sudo access details are kept in local (non-repo) notes. Lead-tuning loop: change
> lead→build→play→snoop ch1→parse.
> (Older 0b38/regread handoffs = DONE, archived.)

---

## 🟢 START HERE — first fresh session (run from the dice directory!)

1. **Run `claude` from `ASFireWire-dice/`** (not from `ASFireWire/`) — this CLAUDE.md + the dice
   CodeGraph index load automatically. Approve the "codegraph" MCP (option 2) if prompted.
2. **Working branch = `integrate-dice-c2bdf11`** (NOT `dice-motu` — that's the v117 fallback). Check:
   ```bash
   git branch --show-current     # should be integrate-dice-c2bdf11
   git log --oneline -1          # 4d7927f (or newer)
   ```
3. **Current state: v119** — upstream integration + TX-exposure works, but **no sound**
   (IT encoder = AM824, MOTU wants MOTU-packed). Next step and full context → section
   "🟢 INTEGRATION OK + TX EXPOSURE FIXED" below.
4. **Hardware-test build** (when you change the IT encoder code):
   `./build.sh --derived /tmp/ASFWBuild --clean --deploy`
   (VERSION.txt is already >119; macOS will accept it). Confirm `systemextensionsctl list` = new
   version before testing.
5. Logs (⚠️ `grep "ASFWDriver.dext"`, NOT `--predicate senderImagePath`):
   ```bash
   /usr/bin/log show --last 2m --debug --info 2>/dev/null | grep "ASFWDriver.dext" | grep -E "IT WIRE|maxAbs|zeroPcm|lastQuad|StartIO|ZTS"
   ```

> ℹ️ **Infrastructure fixed 2026-06-15:** version-bump (build.sh→`bump.sh patch`, pbxproj sync,
> auto-commit), deploy (`--deploy`/`--clean`), SSH remotes. From now on `./build.sh --derived
> /tmp/ASFWBuild --deploy` produces a deterministically increasing version → no more "stale ghost
> version". Details: DevLog.md.

---

## 🔴 CURRENT STATE (2026-06-26) — official-stack measurement done → 2 fixes to deploy (lead 9216 + Main slot 12/13)

> **TL;DR for a fresh Tahoe session:** read `documentation/SEQUOIA_SNOOP_RESULT.md`, then deploy
> the two fixes from the "✅ MEASUREMENT DONE" block below (lead `6144→9216`, Main → slot `12/13`).
> The rest of this section (below) is historical context predating the measurement — some notes
> about "slot 0/1" are DISPROVEN by the measurement.

> **Channel fixed, MOTU plays (LEDs light up) — but squeals + LEDs wander** (Analog 7 / S/PDIF / Main R).
> Tonight we **exhausted packet-content analysis** — the wire snoop showed everything is correct.
>
> ### ⭐ Wire proof (MB2009 snoop ch1, 23:10) — OUR WIRE IS PERFECT
> Passive snoop of our IT on ch1 = **textbook structure, identical to El Cap**:
> - `ch=1 tag=1 sy=0`, DBS=13, CIP `000d04xx/8222ffff` ✓
> - **DBC +8/data, frozen on no-data** (f8→00→[08 N]→08→10→18…) — `dbcDisc=0` ✓
> - **SPH +512/block, smooth and continuous** across data and no-data packets
>   (008541be→43be→…→55be in the next packet) ✓
> - **PCM clean stereo ONLY on slots 0/1 (Main L/R), rest `000000`** — snoop = WIRE16 1:1 ✓
> - Cadence D,D,N,D = 75% data ✓
>
> Only difference vs El Cap: **SID=0** (vs El Cap SID=3) — but that's our correct node id (we're node 0).
> **MOTU received an IDENTICAL stream from El Cap and played cleanly → the squeal is NOT in our packets.**
>
> ### Ruled out today, HARD (not hypotheses — measurement/wire)
> - **DBC** — `[WIRE-DBC]` watch (v136) + snoop: continuous, zero breaks.
> - **SPH slope + PCM slots** — `[WIRE16-PCM]` (v135) + snoop: perfect.
> - **SPH drift (slope)** — `[MotuSph]` drift-watch (v134, live cursor vs ct): `driftCyc` oscillates ±40,
>   but that's **jitter in `ahead` at the prepare point** (correlates with ahead), cancels out by
>   transmit time → at-transmit +2. No growing trend. "Slope drift" hypothesis **DISPROVEN**.
> - **Lead/projection** — changed −5→**+2** (v133, mirroring main's `writeMotuV3SphAndAdvance`; also
>   removed the `packetsAhead*3072` projection, since `clockPair` ct ≈ transmit time). Squeal remained
>   → not the lead.
>
> ### ➡️ UPDATE 2026-06-26 — byte-compare + clock audit DONE
> - **Byte-compare vs El Cap IT snoop:** wire is byte-perfect, only differs by **SID 0 vs 3**
>   (MSG/DBS/SPH-slope/slots all match; slot map already correct). SID = scoped, low likelihood
>   (requires runtime node-id→CIP plumbing).
> - **Clock RULED OUT:** El Cap → MOTU dumps show **48k / Clock Source INTERNAL** (MOTU is master).
>   We write 0x0b04/0b10/0c04 like El Cap; rate confirmed via the health gate (duplex comes up = 48k).
> - **➡️ TOP SUSPECT: SPH LEAD** (`kMotuSphPresentationLeadTicks=2*3072` = a guess). Feeding the
>   master device with the wrong phase → under/overflow → continuous squeal.
>
> ### ✅ MEASUREMENT DONE (2026-06-26, Sequoia session) → **[`documentation/SEQUOIA_SNOOP_RESULT.md`](documentation/SEQUOIA_SNOOP_RESULT.md)**
> Measured the official MOTU driver on Sequoia (snoop via MBP2009/Linux, both streams + sweep +
> multi-rate). **Full numbers + evidence in RESULT.md — this section only lists actions. Read
> RESULT.md before coding.**
>
> **TWO HARD FIXES (both measured, not guessed) — ✅ IMPLEMENTED IN CODE 2026-06-27, awaiting hardware test:**
> 1. **LEAD:** `kMotuSphPresentationLeadTicks` = `2*3072` (6144) → **`3*3072` (9216)**. The official
>    lead is **3 cycles**, rate-independent (confirmed at 44.1/48/88.2/96/176.4/192k). We were
>    1 cycle short.
>    ✅ Changed in [`ASFWAudioDriverZts.cpp:247`](ASFWDriver/Audio/DriverKit/ASFWAudioDriverZts.cpp:247).
> 2. **MAIN SLOT + ROUTING:** the IT encoder mapped CoreAudio channel c linearly to wire slot c+2
>    (`byte 10+ch*3`) → Main ended up on the wrong slots. ✅ Implemented a **fixed table**
>    `kMotuV3CoreToWireSlot[14]={12,13,4,5,6,7,8,9,10,11,2,3,14,15}` + base
>    `kMotuV3SlotBase=4` (`byte = 4 + wireSlot*3`) in
>    [`AmdtpPayloadWriter.cpp`](ASFWDriver/Audio/Wire/AMDTP/AmdtpPayloadWriter.cpp). Main L/R (ch 0/1)
>    → slot 12/13; the 14 PCM channels partition slots 2–15; slots 0/1 = padding. This explains
>    "Main silent + LEDs wandering".
>
> **🔬 TEST — `v137` built+deployed (clean) 2026-06-27:** install `~/Desktop/ASFW_dice_v137.app`,
> confirm `systemextensionsctl list`=**137**, play through MOTU Main Out 1/2. Expected: **sound on
> Main + Main L/R LEDs, squeal gone**. Logs:
> `/usr/bin/log show --last 2m --debug --info 2>/dev/null | grep "ASFWDriver.dext" | grep -E "MotuSph|IT WIRE|maxAbs"`
> If the squeal disappeared but the wrong LEDs light up → dig further into routing (full map in
> RESULT). If the squeal is still there → the lead wasn't the only cause; check `[MotuSph]`
> seedSph vs ct.

> ### 🔬 RESULT v137/v138 (night of 2026-06-27) — WIRE CLEAN, but MOTU MISFRAMES
> **Test v137:** Main Out SILENT, Analog 3,4,7 light up. After moving to slot 12/13 (remap), Main
> disappeared.
> - **First trap — MB2009 on the bus = underexposure.** With Linux on the bus: `written` frozen,
>   `withoutPkt` ~42%, `WIRE16-PCM` zero (a foreign IRM was interfering). **Unplugging MB2009 +
>   restarting Tahoe → writer healthy** (`dbcDisc=0`, `dropouts=0`, PCM on `s10/s11`). Conclusion:
>   the silence/underexposure came from MB2009, NOT our code.
> - **Wire after the fix = clean:** `WIRE16-PCM s10/s11` (= block byte 40/43 = our wireSlot 12/13)
>   is non-zero, rest zero. The gauge reads `byte 10+ch*3`, so s10 = byte 40 = slot 12. So the
>   **remap works: Main is on slots 12/13**.
> - **YET MOTU: Main silent, Analog 3,4,7.** 2 channels on the wire → 3 LEDs (also 3 in v136) →
>   **MISFRAMING**, not a slot bug. The LEDs don't shift linearly with our data shift.
>
> **➡️ NEW DIAGNOSIS: incomplete MOTU init.** ASFW diag report: our init writes 0x0b04/0b10/0b00/0b14/0c04,
> while the official one writes **ADDITIONALLY** 0x0b08, **0x0b1c**, **0x0b38** (which we do NOT write).
> Prime suspect for the misframing.
> - **Do NOT revert the slot remap** — Sequoia ground-truth (channel→slot on slots 2–15); "v136 was
>   better because Main R" was reading noise from the misframing. The "slot 12 = Main" label is to be
>   confirmed only once MOTU locks correctly.
> - **v138:** SID `0→1` (report: we are node 1, not 0 — invalidates an earlier assumption; low chance,
>   but it's the last byte-level difference). `MOTU828Mk3Profile.cpp:46` (hardcoded 1 +
>   TODO: plumb runtime node-id).
>
> **➡️ NEXT STEP (updated 2026-06-27 after REGREAD):** read-back of 0x0b1c/0x0b38 is **EXHAUSTED** —
> both are **write-only** (`SEQUOIA_REGREAD_RESULT.md`), can't be read back. 0b08 read-back=0
> (command/doorbell). To get the init values: **snoop the write payload** (fix the tracer to deref
> the `buf=` DMA buffer — `dataBE=0x80a5211c` from trace v2 is an artifact, NOT a value). Only once
> we have real data does Tahoe add writes to `MOTUVendorProtocol::PrepareDuplex`. If init doesn't
> help → **SPH-echo** (last-resort weapon, plan kept in local project memory).
>
> ### 🔬 ROUND 1 init — v139 TO TEST (2026-06-27, values OBTAINED via DTrace-deref)
> The above "read-back exhausted" was only true briefly; **then an El Cap DTrace deref OBTAINED the
> values** at the source (`SEQUOIA_REGREAD_RESULT.md`, top). **v139** adds 2 of 3 missing MOTU init
> writes (`MOTUVendorProtocol`):
> - **0x0b1c = 0x00120000** (@48k) — in `ProgramTxAndEnableDuplex` BEFORE FETCH 0x0b14 (trace order:
>   0b10→0b00→0b1c→0b14).
> - **0x0b08 doorbell** (`0xffffffff`→`0x00000000`) — brackets 0x0b04 in `PrepareDuplex`
>   (trace: 0b08,0b04,0b08).
> - **0x0b38 DEFERRED (round 2)** — size=8, missing 2nd quadlet → `ELCAP_0B38_QUADLET2_HANDOFF.md`.
>
> **🔴 RESULT v139 (2026-06-27, confirmed): = v137 — init had NO EFFECT.** Analog 3,4,7 LEDs, Main
> silent (the initial "nothing" was just startup delay). **SID=1 + 0b1c + 0b08 = ZERO effect** on the
> misframing.
> **Dry comparison: our `[WIRE16]` v139 = official byte-for-byte** (`q0=010d0418` SID=1=our node ✓,
> DBS=0d, byte2=04, `q1=8222ffff`, SPH ramp, padding=0). → **the misframe is NOT in the bytes nor in
> SID/0b1c/0b08.**
> **v140** = baseline (removed the ineffective+risky 0b08 doorbell; 0b1c stays to complete the set
> alongside 0b38). Do NOT test v140 (==v139).
>
> **➡️ TWO LEADS REMAIN (init exhausted apart from 0b38):**
> 1. **`0x0b38`** — last init register, El Cap **round 2** → `ELCAP_0B38_QUADLET2_HANDOFF.md`
>    (no Linux needed). Low confidence (0b1c/0b08 were inert).
> 2. **TIMING / clock domain** — measure the REAL lead of OUR stream on the wire
>    (`SPH_cycle − arrival_cycle`), compare to the official = 3 →
>    **[`documentation/TIMING_LEAD_CHECK_PLAN.md`](documentation/TIMING_LEAD_CHECK_PLAN.md)**
>    (MB2009 snoop ch1). Lead≠3 → cheap fix via `kMotuSphPresentationLeadTicks`. Drift →
>    justification for **SPH-echo**.
>
> Recommendation: 0b38 (round 2, cheap) → if also inert, init is exhausted → timing/SPH-echo.
>
> ⚠️ **Correction to an earlier note:** "PCM on slots 0/1 = OK / identical to El Cap" (earlier in
> this section) **was wrong** — the official stack puts Main on **12/13**. Measurement beats notes.
> Do NOT trust "slot 0/1".
>
> **Bonus for multi-rate (future work, not for the 48k fix):** SPH Δ = `round(24576000/fs)`
> (do NOT hardcode 512); 14/18 channels at 1×/2×, reduced to 10/13 (DBS) at 4×; frames/packet =
> 8× family. Table in RESULT.
>
> **IR input map (for capture mapping):** Mic/Inst 1 = IR ch0, Analog 1-8 = IR ch2-9, ch16/17 = bus
> monitor/return (NOT inputs). IR layout: 18×3B @ offset 10, DBS=16, SPH Δ=512 (48k). Full details
> in RESULT.
>
> **Sequence:** deploy lead+slot → build `--clean --deploy` → test (Main LEDs + sound) → STABILITY
> → rest of MAPPING → QUALITY. Fallback if this doesn't work: Discord mrmidi (zero pressure).
>
> ### Diagnostic tools (kept, committed)
> - `[WIRE16]`+`[WIRE16-PCM]` (`IsochTxDmaRing::GaugeWirePayload`) — 6 quads + 14 PCM slots of the
>   transmitted IT.
> - `[WIRE-DBC]` + `dbcDisc=N` — DBC continuity per data packet.
> - `[MotuSph]` (drift-watch: curCyc/driftCyc) + `[TxPump]` (`ASFWAudioDriverZts`) — cursor vs ct +
>   exposure.
> - **MB2009 snoop:** `tools/fw_isoch_snoop.c` → `sudo /tmp/fw_isoch_snoop /dev/fw0 1 N`.
>   ⚠️ **Gotchas:** (a) force SSH `-o PreferredAuthentications=password -o PubkeyAuthentication=no`
>   (a passphrase-protected key hangs); (b) FireWire needs `modprobe firewire_ohci quirks=0x10`;
>   (c) **`modprobe -r snd_firewire_motu snd_firewire_lib`** — otherwise Linux ACTIVELY takes over
>   MOTU and kills the LEDs; (d) Linux on the bus = a foreign IRM → StartAudioStreaming fails
>   randomly. Working sequence: **bring the stream up first without Linux, then plug in Linux**
>   (it was on the bus during the successful 23:10 snoop and it played).

---

> 📦 **Archive (ZTS fix v117, origin/DICE integration, IT encoder v121-v124, register rounds v14/v15)
> moved to [`DevLog.md`](DevLog.md) 2026-07-01** — section "Session 2026-06-22 to 2026-06-24".

---

## ✅ Done (archive — details in DevLog.md)

> **System:** once an item from "CURRENT STATE" is resolved and verified, move it here as a
> one-liner (with version number + file), and the full write-up (root cause + fix + log evidence)
> goes to `DevLog.md`. Focus.md holds ONLY the active state + next step; it does not grow with history.

- ✅ **Bug A/C (SYT, v9)** — the SYT gate was killing IT (MOTU V3 sends `syt=0x0000`) → fallback in
  `IsochReceiveContext.cpp`.
- ✅ **Bug B (geometry, v11)** — AM824 check `16<18` rejected IR → `kRxPcmChannels=16` in
  `MOTU828Mk3Profile.cpp`.
- ✅ **Bug D (kWake+isochHeader, v34)** — `Start()` = `kRun|kWake|kIsochHeader` (`IsochReceiveContext.cpp`).
- ✅ **ZTS timeout / IR CIP (v117, `585ea7f`)** — MOTU IR has a non-standard header `0d040400 22ffffff`
  (EOH1=0); separate `kMotuV3Packed` path (DBS=16, 18 PCM, 3-byte chunks) → ZTS publishes (23 ms),
  StartIO OK, duplex comes up. Hardware-verified. Full write-up → DevLog 2026-06-22.
- ✅ **IT on the correct channel (2026-06-25)** — `isoChannel` = `hostToDeviceIsoChannel` (ch1)
  instead of `sid` (ch0), [`ASFWAudioDevice.cpp:205`](ASFWDriver/Audio/DriverKit/ASFWAudioDevice.cpp:205).
  Hardware-verified: MOTU's LEDs blink (locked onto the stream). SPH drift remains → see
  "CURRENT STATE".

---

## DICE architecture — how ZTS works (vs. the main branch)

**On the DICE branch:** Isoch RX/TX is driven exclusively by AudioDriverKit. There is no manual
Start/Stop for IR/IT. `Start` in the UI is disabled — that's correct behavior.

**ZTS flow:**
1. CoreAudio calls `StartIO` → `BeginDirectIo` → `ArmDirectRx(irChannel=0)`
2. `IsochReceiveContext::Start()` arms IR DMA on isoch channel 0 (the IRM channel)
3. MOTU transmits IT on the bus → OHCI IR DMA receives it → `DrainCompleted()` → n > 0
4. A packet is received with a timestamp → `UpdateCurrentZeroTimestamp()` → ZTS published
5. CoreAudio OK, `StartIO` granted → IT starts with the ZTS delay

**If step 3 never happens:** ZTS times out after 500ms, CoreAudio blocks IO.

**IR channel:** `irChannel=0` = the IRM channel assigned by `IsochResourceManager`.
Check: does MOTU actually transmit on channel 0? Did the IRM assign the right channel?

---

## Environment

| Machine | System | Role |
|---------|--------|------|
| MacBook Pro (M3 Max) | macOS Tahoe 26.5.1 (external SSD) | ✅ Active — build + test |
| MacBook Pro (M3 Max) | macOS Sequoia (internal SSD) | Diagnostics (DTrace, IORegistry) |

Build (dice branch):
```bash
./build.sh --derived /tmp/ASFWBuild --deploy   # build + sign + Desktop/ASFW_dice_vNN.app
./build.sh --derived /tmp/ASFWBuild --clean    # full rebuild
```
Version bump + deploy work automatically (ported from main, 2026-06-15): `bump.sh patch` syncs
pbxproj + auto-commits; `deploy_app()` signs the dext+app and copies it to the Desktop.

Dext logs (Tahoe):
```bash
/usr/bin/log stream --predicate 'senderImagePath CONTAINS "ASFWDriver"' --level debug
```

**git push:** always `git push cube666 dice-motu` (working branch = `dice-motu`, NOT `main`;
NOT plain `git push` — no permission on origin)

---

## Links to related documents

| Document | Contents |
|----------|-----------|
| `DevLog.md` (this repo) | dice session history — bug v9/v10/v11, architecture decisions |
| `documentation/ZTS_AND_SYT.md` | ⭐ ZTS and SYT timing — key for the current problem (DrainCompleted=0) |
| `documentation/FWOHCI_IR.md` | IR (Isoch Receive) architecture from Apple decompilation — how the DMA ring works |
| `docs/MOTU_V3_DICE_TODO.md` | List of MOTU V3 bugs with the correct fix for each |
| `../ASFireWire/Focus.md` | Main branch (zero-copy) — current state of work on the older driver |
| `../ASFireWire/documentation/MOTU_828_MK3_FACTS.md` | CANON — MOTU 828 MK3 hardware facts |
| `../ASFireWire/documentation/MOTU_V3_WIRE_GROUNDTRUTH.md` | Wire ground-truth — required for the IT encoder (Bug 2) |
