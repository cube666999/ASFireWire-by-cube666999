# HANDOFF dla sesji Claude na macOS **Sequoia** (MacBook M3, drugi system)

> **Czytasz to jako świeży Claude uruchomiony na Sequoia.** Sesja deweloperska żyje na Tahoe
> (drugi system tej samej maszyny). Twoje JEDYNE zadanie w tej sesji opisane niżej. Wszystkie
> komendy są tu — nie musisz znać reszty projektu. Po zebraniu danych zapiszesz wynik z powrotem
> do tego repo (iCloud, widoczne z obu OS-ów), a sesja na Tahoe go skonsumuje.

---

## 0. Cel w jednym zdaniu

Zmierzyć **lead prezentacji SPH** oficjalnego sterownika MOTU = `SPH_cykl − cykl_przybycia_pakietu`
na **czysto grającym** strumieniu host→device IT, żeby skalibrować naszą stałą
`kMotuSphPresentationLeadTicks` (obecnie zgadnięta = `2*3072`). To jest TERAZ prowadzący podejrzany
o pisk (zegar/rate już wykluczone — patrz §6).

## 1. Topologia (3 maszyny, jedna szyna FireWire)

| Maszyna | System | Rola w tym teście |
|---------|--------|-------------------|
| **MacBook M3** — TEN system | **Sequoia** | **Oficjalny sterownik MOTU gra muzykę** (oracle czystego dźwięku). Stąd działasz. |
| **MacBook M3** — drugi system | Tahoe | Sesja dev (nasz dext). Tu NIE jesteś. |
| **MacBook Pro 2009** | Linux Mint | **Pasywny snoop** szyny (`fw_isoch_snoop`). Łączysz się przez SSH. |

MOTU + M3 + MBP2009 są spięte na jednej magistrali 1394 (MOTU ma 2 porty FW — daisy-chain).

## 2. Dane dostępowe MBP2009 (SSH)

```bash
# Hasło: <redacted LAN pw>   (klucz z passphrase WIESZA — wymuś hasło!)
ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no cube666@192.168.0.38
```

## 3. KOLEJNOŚĆ jest ważna (foreign-IRM gotcha)

Linux jako węzeł na szynie bywa obcym IRM. Żeby nie psuć startu streamu:
1. **NAJPIERW** uruchom muzykę na Sequoia (MOTU gra, diody stabilne).
2. **DOPIERO POTEM** odpal snoop na MBP2009.

Jeśli MBP2009 jest już wpięty i MOTU nie startuje na Sequoia — odłącz kabel FW od MBP2009,
uruchom dźwięk, wepnij z powrotem, potem snoop.

---

## 4. Krok A — przygotuj MBP2009 (pasywny snoop + załataj narzędzie o cykl)

Przez SSH na MBP2009:

```bash
# (a) FireWire quirks dla kontrolera FW643 + NIE pozwól Linuxowi przejąć MOTU
sudo modprobe firewire_ohci quirks=0x10
sudo modprobe -r snd_firewire_motu snd_firewire_lib   # inaczej Linux gasi diody MOTU

# (b) znajdź lokalny węzeł (zwykle /dev/fw0)
for d in /sys/bus/firewire/devices/fw*; do echo "$d $(cat $d/is_local 2>/dev/null)"; done
```

**Załataj `fw_isoch_snoop.c`** — dodaj wydruk cyklu przybycia (`ev->cycle`). Zapisz PONIŻSZY plik
jako `/tmp/fw_isoch_snoop_cyc.c` na MBP2009 (różni się od oryginału TYLKO linią `printf` nagłówka:
dochodzi `cyc=%u` z `ev->cycle`):

```c
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

#define MAX_PKT_BYTES   1024
#define N_PACKETS       256
#define HEADER_QUADS    1

int main(int argc, char** argv) {
    const char* dev = (argc > 1) ? argv[1] : "/dev/fw0";
    const int   channel = (argc > 2) ? atoi(argv[2]) : 1;
    int         want    = (argc > 3) ? atoi(argv[3]) : 40;

    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    struct fw_cdev_create_iso_context cc;
    memset(&cc, 0, sizeof(cc));
    cc.type = FW_CDEV_ISO_CONTEXT_RECEIVE;
    cc.header_size = HEADER_QUADS * 4;
    cc.channel = channel;
    cc.speed = SCODE_400;
    if (ioctl(fd, FW_CDEV_IOC_CREATE_ISO_CONTEXT, &cc) < 0) { perror("CREATE_ISO_CONTEXT"); return 1; }
    const uint32_t handle = cc.handle;

    const size_t buf_size = (size_t)N_PACKETS * MAX_PKT_BYTES;
    uint8_t* buffer = mmap(NULL, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED) { perror("mmap"); return 1; }

    struct fw_cdev_iso_packet* pkts = calloc(N_PACKETS, sizeof(*pkts));
    for (int i = 0; i < N_PACKETS; ++i)
        pkts[i].control = FW_CDEV_ISO_PAYLOAD_LENGTH(MAX_PKT_BYTES) | FW_CDEV_ISO_INTERRUPT |
                          FW_CDEV_ISO_HEADER_LENGTH(HEADER_QUADS * 4);
    struct fw_cdev_queue_iso q; memset(&q, 0, sizeof(q));
    q.packets = (uintptr_t)pkts; q.data = (uintptr_t)buffer;
    q.size = N_PACKETS * sizeof(*pkts); q.handle = handle;
    if (ioctl(fd, FW_CDEV_IOC_QUEUE_ISO, &q) < 0) { perror("QUEUE_ISO"); return 1; }

    struct fw_cdev_start_iso start; memset(&start, 0, sizeof(start));
    start.cycle = -1; start.sync = 0;
    start.tags = FW_CDEV_ISO_CONTEXT_MATCH_ALL_TAGS; start.handle = handle;
    if (ioctl(fd, FW_CDEV_IOC_START_ISO, &start) < 0) { perror("START_ISO"); return 1; }

    fprintf(stderr, "[snoop] dev=%s ch=%d, dumping %d pkts...\n", dev, channel, want);

    uint8_t evbuf[8192];
    size_t  payload_off = 0;
    int     dumped = 0;
    while (dumped < want) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        if (poll(&pfd, 1, 8000) <= 0) { fprintf(stderr, "[snoop] timeout ch %d\n", channel); break; }
        ssize_t n = read(fd, evbuf, sizeof(evbuf));
        if (n < (ssize_t)sizeof(struct fw_cdev_event_common)) continue;
        struct fw_cdev_event_common* ec = (void*)evbuf;
        if (ec->type != FW_CDEV_EVENT_ISO_INTERRUPT) continue;
        struct fw_cdev_event_iso_interrupt* ev = (void*)evbuf;
        const uint32_t nquads = ev->header_length / 4;
        for (uint32_t h = 0; h < nquads && dumped < want; h += HEADER_QUADS) {
            const uint32_t hdr = __builtin_bswap32(ev->header[h]);
            const uint32_t len = (hdr >> 16) & 0xFFFF;
            const uint32_t tag = (hdr >> 14) & 0x3;
            const uint32_t ch  = (hdr >>  8) & 0x3F;
            const uint32_t sy  =  hdr        & 0xF;
            const uint8_t* p = buffer + (payload_off % buf_size);
            // NEW: cyc = arrival cycle of this packet (ev->cycle). Mask & meaning handled by analyzer.
            printf("pkt#%d cyc=%u len=%u ch=%u tag=%u sy=%u:", dumped, ev->cycle, len, ch, tag, sy);
            const uint32_t qn = (len > MAX_PKT_BYTES ? MAX_PKT_BYTES : len) / 4;
            for (uint32_t k = 0; k < qn; ++k)
                printf(" %02x%02x%02x%02x", p[k*4], p[k*4+1], p[k*4+2], p[k*4+3]);
            printf("\n"); fflush(stdout);
            payload_off += MAX_PKT_BYTES; ++dumped;
        }
    }
    fprintf(stderr, "[snoop] done (%d)\n", dumped);
    close(fd);
    return 0;
}
```

```bash
gcc -O2 -o /tmp/fw_isoch_snoop_cyc /tmp/fw_isoch_snoop_cyc.c
```

## 5. Krok B — start czystego dźwięku na Sequoia (TY, lokalnie na M3)

1. MOTU Audio Setup: **Sample Rate 48000, Clock Source Internal, Default Stereo Output = Main Out 1-2**
   (dokładnie jak na zrzutach El Cap — to znany czysty stan).
2. Puść **stały sygnał** jeśli się da (test tone / sinus 1 kHz) — ułatwia weryfikację slotów; jak nie,
   zwykła muzyka wystarczy (lead jest niezależny od treści PCM).
3. Potwierdź: **gra czysto, diody stabilne na Main L/R.**

## 6. Krok C — znajdź kanał IT i zrób capture

Host→device IT to strumień playbacku z Sequoia. El Capitan używał **ch33**; oficjalny sterownik
prawdopodobnie podobnie. DATA-pakiet rozpoznasz po `len=424` i CIP `xx0d04xx 8222ffff`.

```bash
# Spróbuj kandydatów aż zobaczysz len=424. Każda próba kończy się po 8 s ciszy.
for CH in 33 32 1 0 2 3; do
  echo "=== ch $CH ==="
  sudo /tmp/fw_isoch_snoop_cyc /dev/fw0 $CH 8
done
```

Gdy znajdziesz właściwy kanał (sypią się `len=424`), zrób **duży zrzut** (≥2000 pakietów) do pliku:

```bash
sudo /tmp/fw_isoch_snoop_cyc /dev/fw0 <CH> 2000 > /tmp/sequoia_it_cyc.txt 2>/tmp/snoop_err.txt
```

Skopiuj wynik na M3 (przez scp z Sequoia) do iCloud:
```bash
scp -o PreferredAuthentications=password -o PubkeyAuthentication=no \
  cube666@192.168.0.38:/tmp/sequoia_it_cyc.txt \
  "<ŚCIEŻKA_DO>/FireWire/ASFireWire-dice/documentation/raw-captures/2026-06-26_sequoia_official_it_cyc.txt"
```
(ŚCIEŻKA_DO = Twój iCloud `~/Library/Mobile Documents/com~apple~CloudDocs` — na Sequoia username
może się różnić; zlokalizuj `FireWire/ASFireWire-dice` w iCloud Drive.)

## 7. Krok D — policz lead (analiza, robisz TY na Sequoia)

Dla każdego **DATA**-pakietu (`len=424`):
- **CIP Q0** = quadlet[0], **Q1** = quadlet[1] (oczekuj `8222ffff`).
- **SPH pierwszego bloku** = quadlet[2] (np. `00d31bc0`).
  - `sph_cyc = (sph >> 12) & 0x1FFF`  (cykl, 0–7999)
  - `sph_off = sph & 0xFFF`           (offset w cyklu, 0–3071)
- **cykl przybycia** z pola `cyc=` w tej samej linii: `arr_cyc = cyc & 0x1FFF`.
- **lead [cykli]** = `(sph_cyc - arr_cyc + 8000) % 8000`  (spodziewaj się MAŁEJ, STABILNEJ liczby —
  IR ground-truth pokazał ~5 cykli; tu oczekuj jednocyfrowej dodatniej, ewentualnie ujemnej po
  korekcie modulo > 4000 → odejmij 8000).

Gotowy parser (uruchom na Sequoia, Python3):
```python
import re, statistics, sys
leads=[]
for ln in open(sys.argv[1]):
    m=re.search(r'cyc=(\d+) len=424:\s+([0-9a-f ]+)', ln)
    if not m: continue
    cyc=int(m.group(1)); q=m.group(2).split()
    if len(q)<3 or q[1]!='8222ffff': continue
    sph=int(q[2],16); sph_cyc=(sph>>12)&0x1FFF
    lead=((sph_cyc-(cyc&0x1FFF))+8000)%8000
    if lead>4000: lead-=8000
    leads.append(lead)
print(f"n={len(leads)} lead median={statistics.median(leads)} cyc  "
      f"mean={statistics.mean(leads):.2f}  min={min(leads)} max={max(leads)}  "
      f"(×3072 ticks = {statistics.median(leads)*3072})")
```
```bash
python3 parser.py /tmp/sequoia_it_cyc.txt
```

**Sanity:** SID (Q0 bajt0, `(q0>>24)&0x3f`) = node-id M3 na Sequoia (≠ 0); DBS `(q0>>16)&0xff` = 0x0d=13;
bajt2 `(q0>>8)&0xff` = 0x04. PCM (slot 0/1) niezerowy na bajtach 10/13 jeśli grał Main Out.

## 8. Krok E — zapisz wynik z powrotem (żeby Tahoe go skonsumował)

Dopisz na KOŃCU pliku `documentation/SEQUOIA_SNOOP_RESULT.md` w repo (utwórz go):
- zmierzony **median lead [cykli]** i w **tickach** (`lead*3072`),
- SID jaki nadawał oficjalny sterownik,
- ścieżkę do surowego capture,
- 3–5 przykładowych linii DATA-pakietów (z `cyc=` i SPH) jako dowód.

Sesja dev na Tahoe odczyta ten plik i ustawi `kMotuSphPresentationLeadTicks` w
`ASFWDriver/Audio/DriverKit/Config/.../ASFWAudioDriverZts.cpp` na zmierzoną wartość
(zamiast obecnego zgadniętego `2*3072`).

## 9. Pułapki (z DevLog dice)

- SSH **musi** wymusić hasło (`-o PreferredAuthentications=password -o PubkeyAuthentication=no`).
- `modprobe -r snd_firewire_motu snd_firewire_lib` — inaczej Linux przejmuje MOTU, diody gasną.
- `quirks=0x10` dla kontrolera FW643 (MBP2009) — inaczej FW niestabilne.
- Start: **najpierw dźwięk na Sequoia, potem snoop** (foreign-IRM).
- `cyc` z firewire-cdev to cykl odbioru; maskuj `& 0x1FFF`. Jeśli lead wychodzi losowy/duży →
  pole `cyc` może być w innej skali na tym kernelu; wtedy zrzuć też kilka kolejnych pakietów i
  zweryfikuj że `cyc` rośnie ~o tyle co odstęp pakietów, i napisz to w RESULT — Tahoe doradzi dalej.

---

### Kontekst (gdybyś chciał głębiej — opcjonalne)
Pełny stan problemu: `Focus.md` (sekcja „AKTUALNY STAN") + pamięć projektu. Skrót: nasz drut jest
**byte-perfect vs El Cap** (różni tylko SID 0 vs 3); zegar/rate **wykluczone** (MOTU = 48k Internal,
brama health to potwierdza); pisk → najpewniej **lead SPH** (faza karmienia urządzenia-mastera). Ten
capture mierzy poprawny lead z działającego stosu. Branch dev: `integrate-dice-c2bdf11`.
