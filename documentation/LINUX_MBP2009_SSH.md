# Linux Mint 22.3 na MacBook Pro 2009 — dostęp SSH

## ⛔ ŚLEPA ULICZKA: 828mk3 NIE gra czysto na Linuxie (potwierdzone 2026-06-12)

**Nie próbuj uzyskać słyszalnego, czystego audio z MOTU 828mk3 na Linuxie — to udokumentowany,
NIEROZWIĄZANY problem kompatybilności.** Spędziliśmy na tym sesję; każda droga (direct hw,
PipeWire surround, PipeWire Pro Audio) kończyła się **piskiem albo zacięciem strumienia**.

Źródło: [snd-firewire-improve issue #27](https://github.com/takaswie/snd-firewire-improve/issues/27)
— periodyczne xruny, **"Lost interrupts"**, `ohci_flush_iso_completions`, podejrzenie wewnętrznego
zegara 828mk3. Open, bez fixu. Plus nasz kontroler **LSI FW643 sam jest niestabilny** (quirks=0x10).
Fora ([MOTUnation](https://www.motunation.com/forum/viewtopic.php?t=27420)) potwierdzają pisk.

**Co Z Linuxa JEST wartościowe (i tylko to):**
- ✅ **Capture nagłówków CIP** przez ALSA tracepoint `amdtp_packet` — dał DBS=13, SYT=0xFFFF.
  To dane o **treści bajtów**, niezależne od problemu timingu/przerwań → nadal ważne.
- ✅ Odczyt rejestrów MOTU przez `firewire-request`.
- ❌ **Słyszalny dowód / czyste odtwarzanie — NIE.** Do tego: El Capitan (oficjalny, działa)
  albo nasz DriverKit na Tahoe, albo **snoop El Capitan→M3** (pełny payload z działającego źródła).

Nie wracaj do prób "zmuśmy Linux żeby zagrał" — to króliczą norą, sprawdzone.

---


## Dane maszyny

| | |
|---|---|
| **Hostname** | `macbook2009` |
| **System** | Linux Mint 22.3 Xfce (Ubuntu Noble base) |
| **Użytkownik** | `cube666` |
| **WiFi** | PLAY4279456 (autoconnect) |

## Połączenie SSH z M3 Maxa

```bash
# Przez alias (skonfigurowany w ~/.ssh/config na M3):
ssh cube666@macbook2009        # hasło: 72044277

# Bezpośrednio przez IP:
ssh cube666@192.168.0.38       # hasło: 72044277

# Przez mDNS (fallback):
ssh cube666@macbook2009.local  # hasło: 72044277
```

⚠️ **Klucz `~/.ssh/mbp2009` NIE istnieje na M3** — był z poprzedniej sesji, nie przeżył.
⚠️ **Klucz `~/.ssh/id_rsa` ma passphrase** → SSH bez hasła nie działa automatycznie.
→ Używaj `expect` w skryptach lub wpisuj hasło ręcznie.

**`~/.ssh/config` na M3 Macu (wpis dodany 2026-06-09):**
```
Host macbook2009
    HostName 192.168.0.38
    User cube666
    StrictHostKeyChecking no
```

**Wzorzec `expect` do użycia w skryptach (gdy trzeba sudo na MB2009):**
```bash
expect -c '
spawn ssh -t cube666@macbook2009 "echo 72044277 | sudo -S <KOMENDA>"
expect "password:"
send "72044277\r"
expect eof
'
```

## Sprawdzenie IP na Linux Mint

```bash
ip addr show | grep "inet "
```

## Partycje dysku

| Partycja | Rozmiar | Zawartość |
|----------|---------|-----------|
| `/dev/sda1` | 200 MB | EFI (rEFInd + GRUB) |
| `/dev/sda2` | 51 GB | macOS El Capitan 10.11.6 |
| `/dev/sda3` | 650 MB | macOS Recovery HD |
| `/dev/sda4` | 74 GB | **Linux Mint 22.3** |

## Boot

rEFInd (zainstalowany) automatycznie pokazuje wpis Linux Mint przy starcie.
GRUB EFI w `/boot/efi/EFI/linuxmint/grubx64.efi`.

## WiFi — sterownik Broadcom BCM4322

**Aktywny sterownik: `b43`** (nie `wl` — `wl` nie binduje BCM4322 rev 01 na kernelu 6.8).

Konfiguracja (ustawiona 2026-06-09):
- `/etc/modules-load.d/bcm4322.conf` → `b43` (autoload przy starcie)
- `/etc/modprobe.d/blacklist-wl.conf` → `blacklist wl` (blokuje stary proprietary driver)
- Firmware: `/lib/firmware/b43/ucode16_mimo.fw` ✅ (z pakietu `firmware-b43-installer`)
- **Serwis systemd `wifi-b43-autoconnect.service`** (enabled) — przy starcie ładuje `b43`
  i łączy z `PLAY4279456`. To główny mechanizm autoconnect (sam `b43` nie wystarczał).

Jeśli WiFi nie działa po restarcie:
```bash
sudo modprobe -r wl b43 ssb 2>/dev/null
sudo modprobe b43
sleep 3
sudo nmcli device wifi connect PLAY4279456 ifname wlan0
```

## FireWire — kontroler LSI FW643 wymaga quirks=0x10 (KRYTYCZNE)

**Problem (rozwiązany 2026-06-09):** kontroler `LSI FW643 [TrueFire]` na Linuxie 6.14
inicjalizuje się (`added OHCI card 0`), ale **magistrala FW nigdy nie wstaje** —
brak self-ID, brak bus reset, pusty `/sys/bus/firewire/devices/`, brak `/dev/fw*`.
Nie pojawia się nawet lokalny węzeł ani inne urządzenia (testowane z LaCie jako hubem).

**Przyczyna:** FW643 ma problem z **MSI (Message Signaled Interrupts)** — przerwania
bus-reset nie docierają → brak self-ID → brak węzłów.

**Fix:** `quirks=0x10` (disable MSI). Ustawione trwale:
```bash
# /etc/modprobe.d/firewire-fw643.conf
options firewire_ohci quirks=0x10
```

Po fixie magistrala wstaje natychmiast:
```
fw0: GUID 002500fffecfb722, S800   ← lokalny kontroler MacBooka
fw1: GUID 0001f20000087236, S400   ← MOTU 828 MK3 ✅
fw2: GUID 00d04b981e098292, S800   ← LaCie (hub/dysk)
```

ALSA widzi MOTU jako `card 1: D828mk3 (version:106800) at fw1.0, S400`.

**Jeśli po restarcie magistrala FW nie wstaje** (brak `/dev/fw1`):
```bash
sudo modprobe -r firewire_ohci
sudo modprobe firewire_ohci quirks=0x10
sleep 5
sudo modprobe snd_firewire_motu
aplay -l   # → powinien pokazać card 1: 828mk3
```

⚠️ **MOTU musi być podłączone i włączone** — bez quirka kontroler nie widzi NICZEGO,
więc brak `/dev/fwX` ≠ brak MOTU. Najpierw sprawdź quirks=0x10.

**Podłączenie podczas testów:** MOTU (FW400) → LaCie d2 (FW400 hub) → MacBook (FW800).
LaCie działa jako hub FireWire — potwierdzone że przepuszcza MOTU.

## ALSA tracepoint pakietów (ground-truth dla MOTU V3)

```bash
sudo trace-cmd record -e snd_firewire_lib:amdtp_packet
# → puść audio przez MOTU (card 1)
# → Ctrl-C → sudo trace-cmd report > ~/motu_amdtp_trace.txt
```
Loguje: DBC, syt, data_blocks, payload_quadlets, raw CIP header. Patrz `Focus.md` Metoda 4.

## SSH z powrotem na M3 Maxa

```bash
# Z Linux Mint na M3 Maxa:
ssh cube666@192.168.0.80   # IP M3 Maxa w sieci domowej
```

## Instalacja (historia)

Zainstalowano zdalnie 2026-06-11 przez SSH do live session Linux Mint z pendrive.
Metoda: `unsquashfs /cdrom/casper/filesystem.squashfs` → `/dev/sda4`, następnie GRUB EFI.
