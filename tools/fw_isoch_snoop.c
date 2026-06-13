// fw_isoch_snoop.c — passive FireWire isochronous channel sniffer (Linux firewire-cdev)
//
// Purpose: capture the ACTUAL wire bytes of an isoch stream on a given channel, to compare
// against what our ASFW DriverKit driver THINKS it transmits ([WIRE16] log). Built for the
// MOTU 828mk3 bring-up: run on MB2009 (Linux Mint, native FW800) chained to the MOTU's 2nd
// FireWire port, sniffing channel 1 (our M3→MOTU IT stream).
//
// Build (on MB2009 / Linux):
//   gcc -O2 -o fw_isoch_snoop fw_isoch_snoop.c
//
// Run (as root; pick the LOCAL card node, usually /dev/fw0):
//   sudo ./fw_isoch_snoop /dev/fw0 1 40
//     arg1 = firewire char device of the LOCAL node (try /dev/fw0, /dev/fw1 — see below)
//     arg2 = isoch channel to sniff (our IT channel = 1)
//     arg3 = number of packets to dump then exit (default 40)
//
// Finding the local node: `ls /dev/fw*`. The local card is the one whose
//   /sys/bus/firewire/devices/fwN/is_local == 1. Quick check:
//   for d in /sys/bus/firewire/devices/fw*; do echo "$d $(cat $d/is_local 2>/dev/null)"; done
//
// IMPORTANT — keep MB2009 PASSIVE so it doesn't fight the M3 for the MOTU:
//   sudo modprobe -r snd_firewire_motu snd_firewire_lib   # don't let Linux drive the MOTU
//   (snd-firewire-motu would try to stream to/from MOTU and conflict with the M3.)
//
// Output format mirrors our DriverKit snoop so packets line up 1:1:
//   pkt#N len=L ch=C tag=T sy=Y: <quadlet0> <quadlet1> ... (big-endian, wire order)
// Compare a len=424 DATA packet here against the [WIRE16] line from the M3 dext.

#define _GNU_SOURCE
#include <linux/firewire-cdev.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>

#define MAX_PKT_BYTES   1024          // 828mk3 DATA packet is 424B; round up generously
#define N_PACKETS       256           // ring of receive buffers
#define HEADER_QUADS    1             // capture 1 header quadlet/packet (iso header: len/tag/ch/sy)

int main(int argc, char** argv) {
    const char* dev = (argc > 1) ? argv[1] : "/dev/fw0";
    const int   channel = (argc > 2) ? atoi(argv[2]) : 1;
    int         want    = (argc > 3) ? atoi(argv[3]) : 40;

    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open"); fprintf(stderr, "try /dev/fw1, and check is_local\n"); return 1; }

    // 1) Create an isochronous RECEIVE context on the requested channel.
    struct fw_cdev_create_iso_context cc;
    memset(&cc, 0, sizeof(cc));
    cc.type        = FW_CDEV_ISO_CONTEXT_RECEIVE;
    cc.header_size = HEADER_QUADS * 4;   // bytes of per-packet header reported in events
    cc.channel     = channel;
    cc.speed       = SCODE_400;
    cc.closure     = 0;
    if (ioctl(fd, FW_CDEV_IOC_CREATE_ISO_CONTEXT, &cc) < 0) {
        perror("CREATE_ISO_CONTEXT"); return 1;
    }
    const uint32_t handle = cc.handle;

    // 2) mmap the DMA payload buffer (kernel writes received payloads here).
    const size_t buf_size = (size_t)N_PACKETS * MAX_PKT_BYTES;
    uint8_t* buffer = mmap(NULL, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED) { perror("mmap"); return 1; }

    // 3) Queue N receive packets. For RECEIVE, control = payload bytes to accept, plus the
    //    interrupt flag on each so we get an event per packet (simple, low-rate dump).
    struct fw_cdev_iso_packet* pkts = calloc(N_PACKETS, sizeof(*pkts));
    for (int i = 0; i < N_PACKETS; ++i) {
        pkts[i].control = FW_CDEV_ISO_PAYLOAD_LENGTH(MAX_PKT_BYTES) |
                          FW_CDEV_ISO_INTERRUPT |
                          FW_CDEV_ISO_HEADER_LENGTH(HEADER_QUADS * 4);
    }
    struct fw_cdev_queue_iso q;
    memset(&q, 0, sizeof(q));
    q.packets = (uintptr_t)pkts;
    q.data    = (uintptr_t)buffer;
    q.size    = N_PACKETS * sizeof(*pkts);
    q.handle  = handle;
    if (ioctl(fd, FW_CDEV_IOC_QUEUE_ISO, &q) < 0) { perror("QUEUE_ISO"); return 1; }

    // 4) Start receiving from the next cycle, accept all tags.
    struct fw_cdev_start_iso start;
    memset(&start, 0, sizeof(start));
    start.cycle  = -1;
    start.sync   = 0;
    start.tags   = FW_CDEV_ISO_CONTEXT_MATCH_ALL_TAGS;
    start.handle = handle;
    if (ioctl(fd, FW_CDEV_IOC_START_ISO, &start) < 0) { perror("START_ISO"); return 1; }

    fprintf(stderr, "[snoop] listening dev=%s channel=%d, dumping %d packets...\n",
            dev, channel, want);

    // 5) Event loop. Each ISO_INTERRUPT event carries one or more packet headers; the iso
    //    header quadlet's top 16 bits = data_length. Payload sits in the mmap buffer, packed
    //    in queue order. We walk a running payload offset to locate each packet's bytes.
    uint8_t evbuf[8192];
    size_t  payload_off = 0;
    int     dumped = 0;
    while (dumped < want) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        if (poll(&pfd, 1, 5000) <= 0) { fprintf(stderr, "[snoop] timeout — no packets on ch %d\n", channel); break; }

        ssize_t n = read(fd, evbuf, sizeof(evbuf));
        if (n < (ssize_t)sizeof(struct fw_cdev_event_common)) continue;

        struct fw_cdev_event_common* ec = (void*)evbuf;
        if (ec->type != FW_CDEV_EVENT_ISO_INTERRUPT) continue;

        struct fw_cdev_event_iso_interrupt* ev = (void*)evbuf;
        const uint32_t nquads = ev->header_length / 4;
        for (uint32_t h = 0; h < nquads && dumped < want; h += HEADER_QUADS) {
            // iso receive header quadlet (native endian as delivered by kernel):
            const uint32_t hdr = ev->header[h];
            const uint32_t len = (hdr >> 16) & 0xFFFF;   // data_length in bytes
            const uint32_t tag = (hdr >> 14) & 0x3;
            const uint32_t ch  = (hdr >>  8) & 0x3F;
            const uint32_t sy  =  hdr        & 0xF;

            const uint8_t* p = buffer + (payload_off % buf_size);
            printf("pkt#%d len=%u ch=%u tag=%u sy=%u:", dumped, len, ch, tag, sy);
            const uint32_t qn = (len > MAX_PKT_BYTES ? MAX_PKT_BYTES : len) / 4;
            for (uint32_t k = 0; k < qn; ++k) {
                // payload is on-wire big-endian; print quadlets in wire order
                printf(" %02x%02x%02x%02x", p[k*4], p[k*4+1], p[k*4+2], p[k*4+3]);
            }
            printf("\n");
            fflush(stdout);

            payload_off += MAX_PKT_BYTES;   // each queued slot reserved MAX_PKT_BYTES
            ++dumped;
        }
    }

    fprintf(stderr, "[snoop] done (%d packets)\n", dumped);
    close(fd);
    return 0;
}
