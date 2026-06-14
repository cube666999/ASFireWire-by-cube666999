#!/usr/bin/env python3
"""
Analyze Isochronous packet data and OHCI descriptor formats.

Endianness notes:
- OHCI descriptors: LITTLE-ENDIAN (host CPU order on x86/ARM)
- CIP headers (Q0, Q1): BIG-ENDIAN (wire order)
- AM824 audio samples: BIG-ENDIAN (wire order)
- FireBug displays everything in LITTLE-ENDIAN (confusing!)

Usage:
    python3 analyze_isoch.py
"""

import struct
from dataclasses import dataclass
from typing import Optional

# ============================================================================
# OHCI Descriptor (16 bytes, LITTLE-ENDIAN)
# ============================================================================
@dataclass
class OHCIDescriptor:
    """OHCI descriptor as seen by CPU (little-endian)."""
    control: int      # offset 0: reqCount[15:0], cmd[31:28], key[27:25], i[25:24], b[23:22], etc.
    dataAddress: int  # offset 4: DMA address of buffer
    branchWord: int   # offset 8: next descriptor address | Z
    statusWord: int   # offset 12: xferStatus[15:0] | resCount[15:0]
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'OHCIDescriptor':
        """Parse 16 bytes as OHCI descriptor (little-endian)."""
        control, dataAddr, branch, status = struct.unpack('<IIII', data[:16])
        return cls(control, dataAddr, branch, status)
    
    @property
    def reqCount(self) -> int:
        return self.control & 0xFFFF
    
    @property
    def cmd(self) -> int:
        return (self.control >> 28) & 0xF
    
    @property
    def xferStatus(self) -> int:
        return (self.statusWord >> 16) & 0xFFFF
    
    @property
    def resCount(self) -> int:
        return self.statusWord & 0xFFFF
    
    @property
    def bytesReceived(self) -> int:
        return self.reqCount - self.resCount if self.resCount <= self.reqCount else 0
    
    def __str__(self):
        cmd_names = {0: 'OUTPUT_MORE', 1: 'OUTPUT_LAST', 2: 'INPUT_MORE', 3: 'INPUT_LAST'}
        cmd_name = cmd_names.get(self.cmd, f'CMD_{self.cmd}')
        return (f"OHCI Desc: ctl=0x{self.control:08x} ({cmd_name}, req={self.reqCount}) "
                f"data=0x{self.dataAddress:08x} br=0x{self.branchWord:08x} "
                f"stat=0x{self.statusWord:08x} (xfer=0x{self.xferStatus:04x} res={self.resCount} recv={self.bytesReceived})")


# ============================================================================
# IEC 61883-1 CIP Header (8 bytes, BIG-ENDIAN)
# ============================================================================
@dataclass
class CIPHeader:
    """Common Isochronous Packet header (big-endian wire format)."""
    q0: int  # Quadlet 0: SID[5:0], DBS[7:0], FN[1:0], QPC[2:0], SPH, DBC[7:0]
    q1: int  # Quadlet 1: FMT[5:0], FDF[23:0] or FDF[7:0]+SYT[15:0]
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'CIPHeader':
        """Parse 8 bytes as CIP header (big-endian)."""
        q0, q1 = struct.unpack('>II', data[:8])
        return cls(q0, q1)
    
    @property
    def sid(self) -> int:
        """Source ID (node that created the packet)."""
        return (self.q0 >> 24) & 0x3F
    
    @property
    def dbs(self) -> int:
        """Data Block Size (in quadlets)."""
        return (self.q0 >> 16) & 0xFF
    
    @property
    def fn(self) -> int:
        """Fraction Number."""
        return (self.q0 >> 14) & 0x03
    
    @property
    def qpc(self) -> int:
        """Quadlet Padding Count."""
        return (self.q0 >> 11) & 0x07
    
    @property
    def sph(self) -> int:
        """Source Packet Header flag."""
        return (self.q0 >> 10) & 0x01
    
    @property
    def dbc(self) -> int:
        """Data Block Counter (8-bit, wraps at 256)."""
        return self.q0 & 0xFF
    
    @property
    def fmt(self) -> int:
        """Format field."""
        return (self.q1 >> 24) & 0x3F
    
    @property
    def fdf(self) -> int:
        """Format Dependent Field (for AM824: contains SFC)."""
        return (self.q1 >> 16) & 0xFF
    
    @property 
    def syt(self) -> int:
        """Synchronization timestamp."""
        return self.q1 & 0xFFFF
    
    @property
    def is_empty(self) -> bool:
        """Check if this is an empty (NO-DATA) packet."""
        return self.syt == 0xFFFF
    
    def __str__(self):
        status = "EMPTY" if self.is_empty else f"SYT=0x{self.syt:04x}"
        return (f"CIP: Q0=0x{self.q0:08x} Q1=0x{self.q1:08x} | "
                f"SID={self.sid} DBS={self.dbs} DBC=0x{self.dbc:02x} FMT=0x{self.fmt:02x} FDF=0x{self.fdf:02x} {status}")


# ============================================================================
# AM824 Audio Sample (4 bytes per channel, BIG-ENDIAN)
# ============================================================================
@dataclass
class AM824Sample:
    """AM824 audio sample (1 quadlet per channel)."""
    raw: int  # Full 32-bit quadlet
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'AM824Sample':
        """Parse 4 bytes as AM824 sample (big-endian)."""
        raw, = struct.unpack('>I', data[:4])
        return cls(raw)
    
    @property
    def label(self) -> int:
        """Label byte (bits 31:24)."""
        return (self.raw >> 24) & 0xFF
    
    @property
    def audio_24bit(self) -> int:
        """24-bit audio sample (bits 23:0), signed."""
        val = self.raw & 0xFFFFFF
        # Sign extend from 24 to 32 bits
        if val & 0x800000:
            val |= 0xFF000000
        return val - 0x100000000 if val & 0x80000000 else val
    
    @property
    def label_type(self) -> str:
        """Decode label type."""
        if self.label == 0x40:
            return "PCM"
        elif self.label == 0x00:
            return "MIDI/Empty"
        elif self.label == 0x80:
            return "Raw MIDI"
        elif (self.label & 0xF0) == 0x80:
            return f"MIDI ch{self.label & 0x0F}"
        else:
            return f"Label=0x{self.label:02x}"
    
    def __str__(self):
        return f"AM824: 0x{self.raw:08x} ({self.label_type}, audio={self.audio_24bit})"


# ============================================================================
# Analysis Functions
# ============================================================================

def parse_isoch_packet(data: bytes) -> tuple:
    """Parse a complete isochronous packet (CIP + payload)."""
    if len(data) < 8:
        return None, []
    
    cip = CIPHeader.from_bytes(data[:8])
    samples = []
    
    if not cip.is_empty and len(data) > 8:
        # Parse AM824 samples after CIP header
        payload = data[8:]
        num_samples = len(payload) // 4
        for i in range(num_samples):
            sample_data = payload[i*4:(i+1)*4]
            if len(sample_data) == 4:
                samples.append(AM824Sample.from_bytes(sample_data))
    
    return cip, samples


def hex_to_bytes(hex_str: str) -> bytes:
    """Convert hex string (with or without spaces) to bytes."""
    hex_clean = hex_str.replace(' ', '').replace('\n', '')
    return bytes.fromhex(hex_clean)


# ============================================================================
# Test with FireBug captured packets
# ============================================================================

# ============================================================================
# MOTU V3 Stream Analysis — parses fw_isoch_snoop output
# ============================================================================

MOTU_SLOT_NAMES = [
    "Main Out L", "Main Out R",
    "Analog 1",   "Analog 2",  "Analog 3", "Analog 4",
    "Analog 5",   "Analog 6",  "Analog 7", "Analog 8",
    "Phones 1",   "Phones 2",  "S/PDIF 1", "S/PDIF 2",
]
MOTU_DBS        = 13          # data block size in quadlets (DBS=13 → 52 bytes/block)
MOTU_BLOCK_BYTES = MOTU_DBS * 4   # 52 bytes
MOTU_PCM_SLOTS  = 14          # PCM slots per block
MOTU_FRAMES_PER_DATA_PKT = 8  # blocking mode @ 48 kHz
SPH_TICKS_PER_CYCLE  = 3072
SPH_CYCLES_PER_SEC   = 8000
SPH_TICKS_PER_SEC    = SPH_TICKS_PER_CYCLE * SPH_CYCLES_PER_SEC  # 24 576 000
SPH_TICKS_PER_SAMPLE = 512   # 48 kHz: 24 576 000 / 48 000


def decode_sph(sph_raw: int) -> dict:
    """Decode MOTU V3 presentation timestamp quadlet."""
    secs   = (sph_raw >> 25) & 0x07
    cycles = (sph_raw >> 12) & 0x1FFF
    offset =  sph_raw        & 0x0FFF
    ticks  = (cycles % SPH_CYCLES_PER_SEC) * SPH_TICKS_PER_CYCLE + offset
    return {'raw': sph_raw, 'secs': secs, 'cycles': cycles, 'offset': offset, 'ticks': ticks}


def pcm_signed24(data: bytes, byte_offset: int) -> int:
    """Read 3 bytes at byte_offset as big-endian signed 24-bit int."""
    if byte_offset + 3 > len(data):
        return 0
    v = (data[byte_offset] << 16) | (data[byte_offset + 1] << 8) | data[byte_offset + 2]
    return v - 0x1000000 if v & 0x800000 else v


def pcm_dbfs(val24: int) -> str:
    """Format signed 24-bit PCM as approximate dBFS string."""
    import math
    if val24 == 0:
        return "-inf dBFS"
    pk = abs(val24) / 0x7FFFFF
    return f"{20 * math.log10(pk):.1f} dBFS"


def parse_snoop_line(line: str) -> Optional[dict]:
    """Parse one fw_isoch_snoop output line.

    Format: pkt#N len=L ch=C tag=T sy=Y: <hex_quadlets...>
    Returns dict with keys: pkt, len, ch, tag, sy, raw (bytes)
    """
    line = line.strip()
    if not line.startswith('pkt#'):
        return None
    try:
        meta, _, hex_part = line.partition(':')
        fields: dict = {}
        for part in meta.split():
            if '=' in part:
                k, v = part.split('=', 1)
                fields[k] = int(v)
            elif part.startswith('pkt#'):
                fields['pkt'] = int(part[4:])
        raw = bytes.fromhex(hex_part.strip().replace(' ', ''))
        return {
            'pkt': fields.get('pkt', 0),
            'len': fields.get('len', 0),
            'ch':  fields.get('ch', 0),
            'tag': fields.get('tag', 0),
            'sy':  fields.get('sy', 0),
            'raw': raw,
        }
    except Exception:
        return None


def analyze_motu_v3_stream(source, verbose: bool = False) -> None:
    """Analyze fw_isoch_snoop output for MOTU V3 IT stream (host→device).

    source: filename string, list of strings, or file-like object.
    Prints structured report to stdout.
    """
    import math, sys

    if isinstance(source, str):
        with open(source) as f:
            lines = f.readlines()
    elif isinstance(source, list):
        lines = source
    else:
        lines = source.readlines()

    packets = [p for line in lines if (p := parse_snoop_line(line)) is not None]
    if not packets:
        print("No snoop packets found. Check file format (expected 'pkt#N len=...').")
        return

    ch_set = {p['ch'] for p in packets}
    print(f"\n{'='*70}")
    print(f"  MOTU V3 IT Stream Analysis — {len(packets)} packets on ch={ch_set}")
    print(f"{'='*70}")

    data_pkts   = [p for p in packets if p['len'] > 8]
    empty_pkts  = [p for p in packets if p['len'] <= 8]
    print(f"\n  Packets:  {len(packets)} total  |  {len(data_pkts)} DATA  |  {len(empty_pkts)} EMPTY")

    # ── CIP header verification ─────────────────────────────────────────────
    print(f"\n{'─'*70}")
    print("  CIP HEADER CHECK (first 5 DATA packets)")
    print(f"{'─'*70}")
    cip_errors = []
    for p in data_pkts[:5]:
        raw = p['raw']
        if len(raw) < 8:
            continue
        cip = CIPHeader.from_bytes(raw[:8])
        ok_dbs  = "✅" if cip.dbs  == 13   else f"❌(want 13)"
        ok_fmt  = "✅" if cip.fmt  == 0x02 else f"❌(want 0x02)"
        ok_fdf  = "✅" if cip.fdf  == 0x22 else f"❌(want 0x22)"
        ok_syt  = "✅" if cip.syt  == 0xFFFF else f"❌(want 0xFFFF)"
        ok_qpc  = "✅" if cip.qpc  == 1    else f"❌(want 1)"
        ok_sph  = "✅" if cip.sph  == 0    else f"❌(want 0)"
        print(f"  pkt#{p['pkt']:3d}  DBS={cip.dbs}{ok_dbs}  FMT=0x{cip.fmt:02x}{ok_fmt}"
              f"  FDF=0x{cip.fdf:02x}{ok_fdf}  SYT=0x{cip.syt:04x}{ok_syt}"
              f"  QPC={cip.qpc}{ok_qpc}  CIP_SPH={cip.sph}{ok_sph}  DBC=0x{cip.dbc:02x}")
        for chk, label in [(cip.dbs != 13, "DBS"), (cip.fmt != 0x02, "FMT"),
                           (cip.fdf != 0x22, "FDF"), (cip.syt != 0xFFFF, "SYT")]:
            if chk:
                cip_errors.append(label)

    if cip_errors:
        print(f"\n  ⚠️  CIP ERRORS: {set(cip_errors)}")
    else:
        print(f"\n  ✅ All CIP fields correct")

    # ── DBC continuity ───────────────────────────────────────────────────────
    print(f"\n{'─'*70}")
    print("  DBC CONTINUITY")
    print(f"{'─'*70}")
    dbc_errors = 0
    prev_dbc: Optional[int] = None
    for p in data_pkts:
        raw = p['raw']
        if len(raw) < 8:
            continue
        cip = CIPHeader.from_bytes(raw[:8])
        if prev_dbc is not None:
            expected = (prev_dbc + MOTU_FRAMES_PER_DATA_PKT) & 0xFF
            if cip.dbc != expected:
                print(f"  ❌ pkt#{p['pkt']}: DBC=0x{cip.dbc:02x} expected 0x{expected:02x}"
                      f" (gap={( cip.dbc - expected) & 0xFF})")
                dbc_errors += 1
        prev_dbc = cip.dbc
    if dbc_errors == 0:
        print(f"  ✅ DBC continuous (+{MOTU_FRAMES_PER_DATA_PKT}/DATA packet, no gaps)")
    else:
        print(f"  ⚠️  {dbc_errors} DBC discontinuities")

    # ── SPH timestamps ───────────────────────────────────────────────────────
    print(f"\n{'─'*70}")
    print("  SPH TIMESTAMPS (MOTU V3 presentation timestamps)")
    print(f"{'─'*70}")

    sph_errors = 0
    sph_deltas = []
    prev_block_ticks: Optional[int] = None

    for p in data_pkts[:20]:   # first 20 DATA packets → 160 blocks
        raw = p['raw']
        if len(raw) < 8 + MOTU_BLOCK_BYTES:
            continue
        payload = raw[8:]  # strip CIP header
        n_blocks = len(payload) // MOTU_BLOCK_BYTES
        pkt_sph_info = []
        for b in range(n_blocks):
            block = payload[b * MOTU_BLOCK_BYTES : (b + 1) * MOTU_BLOCK_BYTES]
            sph_raw = struct.unpack('>I', block[0:4])[0]
            s = decode_sph(sph_raw)
            pkt_sph_info.append(s)

            if prev_block_ticks is not None:
                # Ticks wrap at SPH_TICKS_PER_SEC; handle wraparound
                delta = (s['ticks'] - prev_block_ticks) % SPH_TICKS_PER_SEC
                sph_deltas.append(delta)
                if delta != SPH_TICKS_PER_SAMPLE:
                    sph_errors += 1
                    if verbose:
                        print(f"  ⚠️  pkt#{p['pkt']} blk{b}: SPH=0x{sph_raw:08x}"
                              f" ticks={s['ticks']} delta={delta} (want {SPH_TICKS_PER_SAMPLE})")
            prev_block_ticks = s['ticks']

        if verbose:
            sphs = " ".join(f"0x{s['raw']:08x}" for s in pkt_sph_info)
            print(f"  pkt#{p['pkt']:3d}: SPH blocks = {sphs}")
        else:
            s0 = pkt_sph_info[0]
            print(f"  pkt#{p['pkt']:3d}: SPH[0]=0x{s0['raw']:08x}"
                  f"  cyc={s0['cycles']} off={s0['offset']} ticks={s0['ticks']}")

    if sph_deltas:
        bad = sum(1 for d in sph_deltas if d != SPH_TICKS_PER_SAMPLE)
        print(f"\n  SPH delta check: {len(sph_deltas)} inter-block deltas"
              f"  |  ✅ correct={len(sph_deltas)-bad}"
              f"  |  {'❌' if bad else '✅'} wrong={bad}"
              f"  (expected +{SPH_TICKS_PER_SAMPLE} ticks/block @ 48kHz)")
        if sph_errors == 0:
            print("  ✅ SPH timestamps monotonically advancing at 512 ticks/sample — MOTU will accept frames")
        else:
            print("  ❌ SPH non-monotonic — MOTU will likely reject frames → silence")
    else:
        print("  (no inter-block deltas computed)")

    # ── PCM slot analysis ────────────────────────────────────────────────────
    print(f"\n{'─'*70}")
    print("  PCM SLOT ANALYSIS (first DATA packet, all 8 blocks)")
    print(f"{'─'*70}")
    print(f"  {'Block':>5}  {'Main-L(s0)':>20}  {'Main-R(s1)':>20}  "
          f"{'Analog7(s8)':>20}  non-zero slots")

    first_data = data_pkts[0] if data_pkts else None
    if first_data and len(first_data['raw']) >= 8 + MOTU_BLOCK_BYTES:
        payload = first_data['raw'][8:]
        n_blocks = min(len(payload) // MOTU_BLOCK_BYTES, MOTU_FRAMES_PER_DATA_PKT)
        for b in range(n_blocks):
            block = payload[b * MOTU_BLOCK_BYTES : (b + 1) * MOTU_BLOCK_BYTES]
            v0  = pcm_signed24(block, 10)   # slot 0 — Main L
            v1  = pcm_signed24(block, 13)   # slot 1 — Main R
            v8  = pcm_signed24(block, 34)   # slot 8 — Analog 7
            nz  = [i for i in range(MOTU_PCM_SLOTS)
                   if pcm_signed24(block, 10 + i*3) != 0]
            nz_names = ", ".join(
                f"s{i}({MOTU_SLOT_NAMES[i] if i < len(MOTU_SLOT_NAMES) else '?'})"
                for i in nz
            )
            ml  = f"0x{v0 & 0xFFFFFF:06x} ({pcm_dbfs(v0)})"
            mr  = f"0x{v1 & 0xFFFFFF:06x} ({pcm_dbfs(v1)})"
            a7  = f"0x{v8 & 0xFFFFFF:06x} ({pcm_dbfs(v8)})"
            ok_a7 = "✅" if v8 == 0 else "❌ NON-ZERO"
            print(f"  blk{b}:  {ml:>20}  {mr:>20}  {a7:>20} {ok_a7}  [{nz_names or 'all zero'}]")

    # ── Non-zero slot survey across all DATA packets ──────────────────────────
    print(f"\n{'─'*70}")
    print("  NON-ZERO SLOT SURVEY (all DATA packets)")
    print(f"{'─'*70}")
    slot_has_audio = [False] * MOTU_PCM_SLOTS
    slot_max_abs   = [0]     * MOTU_PCM_SLOTS
    for p in data_pkts:
        raw = p['raw']
        if len(raw) < 8 + MOTU_BLOCK_BYTES:
            continue
        payload = raw[8:]
        n_blocks = len(payload) // MOTU_BLOCK_BYTES
        for b in range(n_blocks):
            block = payload[b * MOTU_BLOCK_BYTES : (b + 1) * MOTU_BLOCK_BYTES]
            for sl in range(MOTU_PCM_SLOTS):
                v = pcm_signed24(block, 10 + sl * 3)
                if v != 0:
                    slot_has_audio[sl] = True
                    slot_max_abs[sl] = max(slot_max_abs[sl], abs(v))

    for sl in range(MOTU_PCM_SLOTS):
        name = MOTU_SLOT_NAMES[sl] if sl < len(MOTU_SLOT_NAMES) else f"slot{sl}"
        if slot_has_audio[sl]:
            flag = "⚠️ UNEXPECTED" if sl not in (0, 1) else "✅ AUDIO"
            print(f"  slot {sl:2d} {name:12s}: NON-ZERO  peak={pcm_dbfs(slot_max_abs[sl])}  {flag}")
        else:
            flag = "❌ SILENT (no audio?)" if sl in (0, 1) else "✅ silent"
            print(f"  slot {sl:2d} {name:12s}: silent  {flag}")

    # ── Summary ──────────────────────────────────────────────────────────────
    print(f"\n{'='*70}")
    print("  SUMMARY")
    print(f"{'='*70}")
    audio_in_main = slot_has_audio[0] or slot_has_audio[1]
    junk_slots = [sl for sl in range(2, MOTU_PCM_SLOTS) if slot_has_audio[sl]]
    print(f"  Main Out L/R (slots 0/1) has audio:  {'✅ YES' if audio_in_main else '❌ NO'}")
    print(f"  Garbage in other slots:               {'⚠️  slots ' + str(junk_slots) if junk_slots else '✅ none'}")
    print(f"  SPH timestamps valid:                 {'✅ YES' if sph_errors == 0 else '❌ NO (' + str(sph_errors) + ' bad deltas)'}")
    print(f"  DBC continuity:                       {'✅ YES' if dbc_errors == 0 else '❌ NO (' + str(dbc_errors) + ' gaps)'}")
    print()


if __name__ == '__main__':
    import sys

    # If a file argument is given, run MOTU V3 stream analysis
    if len(sys.argv) > 1:
        fname = sys.argv[1]
        verbose = '--verbose' in sys.argv or '-v' in sys.argv
        analyze_motu_v3_stream(fname, verbose=verbose)
        sys.exit(0)

    print("=" * 60)
    print("Isochronous Packet Analyzer")
    print("=" * 60)
    
    # Example packet from FireBug (already in WIRE order, which is BIG-ENDIAN)
    # FireBug shows: 02020050 9002ffff (8 bytes, empty packet)
    print("\n--- Empty Packet Example ---")
    empty_pkt = hex_to_bytes("02020050 9002ffff")
    cip, samples = parse_isoch_packet(empty_pkt)
    print(cip)
    print(f"  Empty packet (NO-DATA): {cip.is_empty}")
    
    # Example with audio samples
    # FireBug shows: 02020050 9002863b 4000002a 40000049 40000049 40000079 ...
    print("\n--- Audio Packet Example ---")
    audio_pkt = hex_to_bytes("02020050 9002863b 4000002a 40000049 40000049 40000079 40000036 40000047")
    cip, samples = parse_isoch_packet(audio_pkt)
    print(cip)
    print(f"  Data samples: {len(samples)}")
    for i, s in enumerate(samples):
        print(f"    [{i}] {s}")
    
    # Decode expected ContextMatch value
    print("\n--- ContextMatch Register Analysis ---")
    ctx_match = 0xF0000000  # From logs
    tag_mask = (ctx_match >> 28) & 0xF
    channel = (ctx_match >> 6) & 0x3F
    print(f"ContextMatch = 0x{ctx_match:08x}")
    print(f"  Tag mask: 0x{tag_mask:x} (accepts tags: {[i for i in range(4) if tag_mask & (1 << i)]})")
    print(f"  Channel: {channel}")
    
    # OHCI descriptor example (from log: ctl=0x200c1000 data=0x803b8000 br=0x803a8010 stat=0x00001000)
    print("\n--- OHCI Descriptor Analysis ---")
    desc_bytes = struct.pack('<IIII', 0x200c1000, 0x803b8000, 0x803a8010, 0x00001000)
    desc = OHCIDescriptor.from_bytes(desc_bytes)
    print(desc)
    print(f"  cmd nibble=0x{desc.cmd:x} (expect 2 for INPUT_MORE)")
    print(f"  reqCount={desc.reqCount} (expect 4096)")
    print(f"  resCount={desc.resCount} xferStatus=0x{desc.xferStatus:04x}")
    print(f"  Status interpretation: NO DATA RECEIVED (resCount == reqCount)")
    
    # Key insight about run=1 active=0
    print("\n--- IR Context State Analysis ---")
    ctl = 0x40008000  # From logs
    run = (ctl >> 15) & 1
    active = (ctl >> 10) & 1
    dead = (ctl >> 11) & 1
    isoch_header = (ctl >> 30) & 1
    print(f"ContextControl = 0x{ctl:08x}")
    print(f"  run={run} active={active} dead={dead} isochHeader={isoch_header}")
    print()
    if run == 1 and active == 0:
        print("  ⚠️ ISSUE: run=1 but active=0 means context is waiting!")
        print("  Possible causes:")
        print("    1. No matching packets on the wire (wrong channel/tag?)")
        print("    2. Context is waiting for first packet arrival")
        print("    3. CommandPtr not pointing to valid descriptor")
        print("    4. DMA coherency issue (descriptor not visible to hardware)")
