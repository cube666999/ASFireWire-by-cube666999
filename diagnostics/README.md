# diagnostics/

Hardware diagnostic captures from real MOTU 828 MK3 tests.

## Sessions

### `sequoia_20260525_003640/` — MacBook Pro, macOS Sequoia, MOTU kext running

Collected by `tools/sequoia_diagnostic.sh` with Apple MOTU kext active (SIP disabled).
Key findings documented in `MOTU_828_MK3_BringUp.md` and `CHANGES.md` (Fix 13).

| File | Content |
|------|---------|
| `fw_logstream.txt` | Rich MOTU kext log — confirms `fNumFWOutputChannels 14 fNumFWInputChannels 18`, blocking isoch lifecycle, bus reset recovery |
| `dtrace_fw_isoch.txt` | DTrace `IOFireWireFamily` — IRM alloc, `IOFWIsochChannel` init×2, `allocateChannel`×2, `start`×2, bus reset cycle |
| `dtrace_ir_packets.txt` | DTrace on MOTU kext — **FAILED** (LTO/no debug symbols, probes not found) |
| `dtrace_motu_calls.txt` | DTrace on MOTU kext — **FAILED** (same reason) |
| `dtrace_avc.txt` | DTrace AV/C probes |
| `ioreg_connected_fw.txt` | IORegistry with MOTU connected — `Unit_SW_Version=21 (0x15)`, `Vendor_ID=498 (0x1F2)`, `IOAudioSampleRate=48000` |
| `ioreg_connected_audio.txt` | IORegistry audio side with MOTU kext active |
| `ioreg_baseline_fw.txt` | IORegistry FireWire bus without MOTU |
| `ioreg_baseline_audio.txt` | IORegistry audio without MOTU |
| `ioreg_streaming.txt` | IORegistry during active streaming |
| `system_profiler_fw.txt` | `system_profiler SPFireWireDataType` — Vendor 0x1F2, Model 0x106800, S400 |
| `system_info.txt` | macOS version, hardware info |
| `kextstat.txt` | Loaded kexts including `com.motu.driver.FireWireAudio` |
| `sip_status.txt` | SIP disabled (required for MOTU kext on Sequoia) |
| `MOTUFireWireAudio_Info.plist` | MOTU kext Info.plist — bundle ID `com.motu.driver.FireWireAudio` |
| `run.log` | Script execution log |

**Critical data points from this session** (channel directions — canonical: `documentation/MOTU_828_MK3_FACTS.md`):
- `fNumFWOutputChannels 14` = host's FW output = **host→device (IT, playback, our `outputChannelCount`)**
- `fNumFWInputChannels 18` = host's FW input = **device→host (IR, capture, our `inputChannelCount`)**
- MOTU uses `FireWireBlockRWCommand` (confirms Fix 10 was necessary)
- Bus reset recovery: Apple kext does stop→release→free→reinit→allocate→start (matches our `AudioCoordinator` bus reset handler)
- `HALC_ShellObject` not an issue for Apple kext (uses IOAudio, not AudioDriverKit)

---

### Loose files (2026-05-24 session)

| File | Content |
|------|---------|
| `motu_fwunit_20260524_015913.txt` | FWUnit IORegistry entry — `Unit_SW_Version=21`, `Unit_Spec_ID=498` |
| `motu_ioreg_sequoia_20260524_015540.txt` | IORegistry MOTU overview |
| `motu_kext_plist_20260524_020147.txt` | Earlier kext plist capture |
