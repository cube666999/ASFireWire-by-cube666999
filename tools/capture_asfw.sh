#!/usr/bin/env bash
# capture_asfw.sh — capture ASFW IT stream from MB2009 Linux + analyze payload
#
# Usage:
#   ./tools/capture_asfw.sh [channel] [n_packets]
#
#   channel    IT channel to sniff (default: auto-detect from ASFW logs, fallback 40)
#   n_packets  total packets to capture (default: 300 ≈ 37ms @ 8kpkt/s)
#
# What it does:
#   1. Finds current IRM-allocated channel from recent ASFW dext logs
#   2. SCPs fw_isoch_snoop.c to MB2009 and (re)builds it
#   3. Stops snd-firewire-motu on MB2009 so it doesn't conflict with M3
#   4. Captures N packets, SCPs result back to /tmp/
#   5. Runs analyze_isoch.py on the result
#
# Requires: sshpass  (brew install sshpass)
#           python3  (macOS built-in)
#
# MB2009 must be on, on the same WiFi (192.168.0.38), and connected via FW.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MB2009_HOST="192.168.0.38"
MB2009_USER="cube666"
MB2009_PASS="72044277"
SNOOP_SRC="$SCRIPT_DIR/fw_isoch_snoop.c"
ANALYZER="$SCRIPT_DIR/analyze_isoch.py"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
REMOTE_CAPTURE="/tmp/asfw_snoop_${TIMESTAMP}.txt"
LOCAL_CAPTURE="/tmp/asfw_snoop_${TIMESTAMP}.txt"

# ── arguments ────────────────────────────────────────────────────────────────
CHANNEL="${1:-}"
N_PACKETS="${2:-300}"

# ── helpers ──────────────────────────────────────────────────────────────────
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10 -o PubkeyAuthentication=no -o PreferredAuthentications=password"

ssh_cmd() {
    # expect handles password prompt (sshpass hangs when local id_rsa has passphrase)
    /usr/bin/expect -c "
set timeout 60
spawn ssh $SSH_OPTS ${MB2009_USER}@${MB2009_HOST} {$*}
expect {
    \"password:\" { send \"${MB2009_PASS}\r\"; exp_continue }
    timeout       { exit 1 }
    eof           {}
}
" 2>/dev/null
}

scp_to() {   # local → remote
    /usr/bin/expect -c "
set timeout 30
spawn scp $SSH_OPTS {$1} ${MB2009_USER}@${MB2009_HOST}:{$2}
expect {
    \"password:\" { send \"${MB2009_PASS}\r\"; exp_continue }
    timeout       { exit 1 }
    eof           {}
}
" 2>/dev/null
}

scp_from() {  # remote → local
    /usr/bin/expect -c "
set timeout 30
spawn scp $SSH_OPTS ${MB2009_USER}@${MB2009_HOST}:{$1} {$2}
expect {
    \"password:\" { send \"${MB2009_PASS}\r\"; exp_continue }
    timeout       { exit 1 }
    eof           {}
}
" 2>/dev/null
}

check_sshpass() {
    if ! command -v sshpass &>/dev/null; then
        echo "❌ sshpass not found. Install with: brew install sshpass"
        echo "   Then re-run this script."
        exit 1
    fi
}

# ── step 1: find channel from ASFW logs ──────────────────────────────────────
find_channel() {
    echo "==> [1/5] Searching ASFW logs for IRM-allocated IT channel..."
    # Look for patterns like "channel=40", "ch=40", "IT channel 40", "isoch channel 40"
    local ch
    ch=$(/usr/bin/log show --last 10m --debug --info 2>/dev/null \
        | grep "ASFWDriver.dext" \
        | grep -oiE "(IT|isoch|channel)[^0-9]*([0-9]+)" \
        | grep -oE "[0-9]+" \
        | tail -1 || true)

    if [[ -z "$ch" ]]; then
        # Fallback: try broader grep
        ch=$(/usr/bin/log show --last 10m --debug --info 2>/dev/null \
            | grep "ASFWDriver.dext" \
            | grep -oiE "ch(an)?[= ]+[0-9]+" \
            | grep -oE "[0-9]+" \
            | tail -1 || true)
    fi

    if [[ -n "$ch" ]]; then
        echo "    Found channel $ch in ASFW logs."
        echo "$ch"
    else
        echo "    Not found in logs. Using default ch=40 (last known IRM allocation)."
        echo "40"
    fi
}

# ── main ─────────────────────────────────────────────────────────────────────
check_sshpass

if [[ -z "$CHANNEL" ]]; then
    CHANNEL="$(find_channel)"
fi

echo ""
echo "==> Configuration:"
echo "    Channel:   $CHANNEL"
echo "    Packets:   $N_PACKETS"
echo "    MB2009:    ${MB2009_USER}@${MB2009_HOST}"
echo "    Output:    $LOCAL_CAPTURE"
echo ""

# ── step 2: copy fw_isoch_snoop.c to MB2009 and build ────────────────────────
echo "==> [2/5] Copying fw_isoch_snoop.c to MB2009..."
scp_to "$SNOOP_SRC" "/tmp/fw_isoch_snoop.c"

echo "==> [2/5] Building fw_isoch_snoop on MB2009..."
ssh_cmd "gcc -O2 -o /tmp/fw_isoch_snoop /tmp/fw_isoch_snoop.c && echo '    Build OK'"

# ── step 3: ensure firewire_ohci + find/fix FW bus, stop snd-firewire-motu ────
echo "==> [3/5] Preparing FireWire bus on MB2009..."

# Unload conflicting MOTU audio driver first
ssh_cmd "sudo modprobe -r snd_firewire_motu snd_firewire_lib 2>/dev/null; echo '    snd-firewire-motu: unloaded (or was not loaded)'"

# Ensure firewire_ohci is loaded with quirks=0x10 (required for LSI FW643 on MB2009)
# The quirks are set persistently in /etc/modprobe.d/firewire-fw643.conf, but if
# the module was unloaded (e.g. after rebooting into live USB), reload it.
ssh_cmd "
if ! lsmod | grep -q firewire_ohci; then
    echo '    firewire_ohci not loaded — loading with quirks=0x10 ...'
    sudo modprobe firewire_ohci quirks=0x10
    sleep 3
    echo '    Loaded.'
else
    echo '    firewire_ohci already loaded.'
fi
"

# Find local FW node on MB2009
echo "==> [3/5] Finding local FireWire node on MB2009..."
FW_DEV=$(ssh_cmd "
    for d in /sys/bus/firewire/devices/fw*; do
        [[ -f \"\$d/is_local\" ]] && [[ \"\$(cat \$d/is_local)\" == '1' ]] && \
            echo \"/dev/\$(basename \$d)\" && break
    done
" 2>/dev/null || true)

if [[ -z "$FW_DEV" ]]; then
    echo "    ⚠️  No local FW node found in /sys/bus/firewire/devices/"
    echo "    Trying to reload firewire_ohci with quirks=0x10 ..."
    ssh_cmd "sudo modprobe -r firewire_ohci 2>/dev/null; sleep 1; sudo modprobe firewire_ohci quirks=0x10; sleep 3"
    FW_DEV=$(ssh_cmd "
        for d in /sys/bus/firewire/devices/fw*; do
            [[ -f \"\$d/is_local\" ]] && [[ \"\$(cat \$d/is_local)\" == '1' ]] && \
                echo \"/dev/\$(basename \$d)\" && break
        done
    " 2>/dev/null || true)
fi

if [[ -z "$FW_DEV" ]]; then
    echo "    ❌ Still no FW node. Check: MOTU must be ON and connected to MB2009 via FW."
    exit 1
fi
echo "    Local FW node: $FW_DEV"

# ── step 4: capture ──────────────────────────────────────────────────────────
echo ""
echo "==> [4/5] Capturing $N_PACKETS packets on ch=$CHANNEL ..."
echo "    (this takes ~$((N_PACKETS / 8000 + 1))s — make sure ASFW is streaming)"
echo ""

ssh_cmd "sudo /tmp/fw_isoch_snoop $FW_DEV $CHANNEL $N_PACKETS > $REMOTE_CAPTURE 2>/tmp/snoop_err.txt; \
         echo \"Captured \$(wc -l < $REMOTE_CAPTURE) lines\" ; \
         cat /tmp/snoop_err.txt"

# ── step 5: fetch + analyze ──────────────────────────────────────────────────
echo ""
echo "==> [5/5] Fetching capture file..."
scp_from "$REMOTE_CAPTURE" "$LOCAL_CAPTURE"
echo "    Saved to: $LOCAL_CAPTURE"

LINES=$(wc -l < "$LOCAL_CAPTURE")
echo "    Lines: $LINES"

if [[ "$LINES" -eq 0 ]]; then
    echo ""
    echo "❌ Empty capture. Possible causes:"
    echo "   - ASFW not streaming (open ASFW app, select MOTU output in macOS Sound)"
    echo "   - Wrong channel (try: ./capture_asfw.sh 1 or other channel)"
    echo "   - firewire_ohci quirks not set on MB2009:"
    echo "     ssh ${MB2009_USER}@${MB2009_HOST} 'sudo modprobe -r firewire_ohci && sudo modprobe firewire_ohci quirks=0x10'"
    exit 1
fi

echo ""
echo "==> Analyzing payload..."
python3 "$ANALYZER" "$LOCAL_CAPTURE"
