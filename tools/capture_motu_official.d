#!/usr/sbin/dtrace -s
/*
 * capture_motu_official.d
 *
 * Ground-truth capture of the OFFICIAL MOTU FireWire driver's behaviour, to find
 * what register/command writes and isoch-transmit configuration it performs that
 * OUR DriverKit dext does not (suspected cause of "all audio routes to Analog 7"
 * and the high-pitched squeak on the MOTU 828mk3).
 *
 * TARGET PLATFORM (recommended): MacBook Pro 2009, macOS El Capitan, NATIVE
 * FireWire 800 port, official MOTU driver (com.motu.driver.FireWireAudio).
 *   - El Capitan DTrace permits kernel-memory reads (newer macOS restricts them).
 *   - Native OHCI controller (no Thunderbolt-FireWire bridging).
 *   - Same IOFireWireFamily version as docs/IOFireWireFamily/ reference.
 *
 * PREREQUISITES on the target machine:
 *   1. SIP disabled (csrutil disable from Recovery) — required for fbt + memory reads.
 *   2. Official MOTU driver installed, MOTU 828mk3 connected and working.
 *   3. Run as root:   sudo dtrace -w -s capture_motu_official.d | tee motu_official.txt
 *   4. While it runs: start audio playback to the MOTU (e.g. iTunes / system sound),
 *      let it play ~15 s, then stop the script (Ctrl-C).
 *
 * This is a DISCOVERY script: it hooks IOFireWireFamily's async-write and isoch
 * paths broadly and prints function + arguments. From the output we identify the
 * exact register offsets + data the official driver writes (CLOCK_STATUS 0x0b14,
 * packet format, OPT_IFACE 0x0c94, and any Command-DSP / routing writes), then
 * refine. IOFireWireFamily HAS symbols; the MOTU kext does NOT (LTO) — so we hook
 * the framework the kext calls into, never the kext itself.
 */

#pragma D option quiet
#pragma D option bufsize=8m
#pragma D option dynvarsize=4m

dtrace:::BEGIN
{
    printf("=== MOTU official-driver capture started %Y ===\n", walltimestamp);
    printf("Play audio to the MOTU now; Ctrl-C after ~15s.\n\n");
}

/*
 * (A) ASYNC WRITE PATH — register/command writes host->device.
 * Hook every IOFireWireFamily function whose name contains "rite" (Write/write).
 * Args are printed raw; FWAddress (nodeID + 48-bit offset) and data live in these.
 * Refine once we see which function actually carries offset+data.
 */
fbt:com.apple.iokit.IOFireWireFamily::entry
/ strstr(probefunc, "rite") != NULL /
{
    printf("[WR] %Y %s\n      a0=0x%llx a1=0x%llx a2=0x%llx a3=0x%llx a4=0x%llx a5=0x%llx\n",
           walltimestamp, probefunc,
           (uint64_t)arg0, (uint64_t)arg1, (uint64_t)arg2,
           (uint64_t)arg3, (uint64_t)arg4, (uint64_t)arg5);
}

/*
 * (B) ASYNC REQUEST TRANSMIT — the controller's outgoing async path.
 * Catches the actual AT (async transmit) of write-request packets, where the
 * destination offset and quadlet/block data are assembled.
 */
fbt:com.apple.iokit.IOFireWireFamily::entry
/ strstr(probefunc, "syncWrite") != NULL ||
  strstr(probefunc, "AsyncReq") != NULL ||
  strstr(probefunc, "TransmitReq") != NULL /
{
    printf("[ATREQ] %Y %s a0=0x%llx a1=0x%llx a2=0x%llx a3=0x%llx a4=0x%llx\n",
           walltimestamp, probefunc,
           (uint64_t)arg0, (uint64_t)arg1, (uint64_t)arg2,
           (uint64_t)arg3, (uint64_t)arg4);
}

/*
 * (C) ISOCH TRANSMIT SETUP — confirms channel, speed, DCL program, packet size.
 * These symbols are PROVEN to fire (see diagnostics/.../dtrace_fw_isoch.txt).
 * createLocalIsochPort(bool talking, DCLCommandStruct* opcodes, DCLTaskInfo*,
 *                      UInt32 startEvent, UInt32 startState, UInt32 startMask)
 *   -> arg1=talking, arg2=DCL program head (defines packet layout / buffers).
 */
fbt:com.apple.iokit.IOFireWireFamily:*createLocalIsochPort*:entry
{
    printf("[ISOCH] %Y createLocalIsochPort talking=%lld DCL=0x%llx taskInfo=0x%llx startEvent=0x%llx startState=0x%llx startMask=0x%llx\n",
           walltimestamp, (int64_t)arg1, (uint64_t)arg2, (uint64_t)arg3,
           (uint64_t)arg4, (uint64_t)arg5, (uint64_t)arg6);
}

fbt:com.apple.iokit.IOFireWireFamily:*IOFWIsochChannel*init*:entry
{
    printf("[ISOCH] %Y IOFWIsochChannel::init speed=arg? a2=0x%llx a3=0x%llx a4=0x%llx\n",
           walltimestamp, (uint64_t)arg2, (uint64_t)arg3, (uint64_t)arg4);
}

fbt:com.apple.iokit.IOFireWireFamily:*allocateChannelBegin*:entry
{
    printf("[ISOCH] %Y allocateChannelBegin speed=0x%llx chanMaskPtr=0x%llx\n",
           walltimestamp, (uint64_t)arg1, (uint64_t)arg2);
}

dtrace:::END
{
    printf("\n=== capture ended %Y ===\n", walltimestamp);
}
