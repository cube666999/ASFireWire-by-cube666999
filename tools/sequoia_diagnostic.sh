#!/bin/bash
# =============================================================================
# ASFW Diagnostic — macOS Sequoia + MOTU 828 MK3
# Uruchom PRZED podłączeniem MOTU.
# =============================================================================

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOGDIR="$HOME/Desktop/ASFW_Diagnostics_$TIMESTAMP"
mkdir -p "$LOGDIR"

exec > >(tee -a "$LOGDIR/run.log") 2>&1

section() { echo ""; echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"; echo "  $1"; echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"; }
ok()      { echo "  ✅  $1"; }
warn()    { echo "  ⚠️   $1"; }
info()    { echo "  ℹ️   $1"; }

# PIDs procesów tła — bash 3.2 safe
BG_PIDS=""

cleanup() {
    echo ""
    section "SPRZĄTANIE"
    for pid in $BG_PIDS; do
        kill "$pid" 2>/dev/null && echo "  zatrzymano PID $pid" || true
    done
    echo ""
    echo "  Logi: $LOGDIR"
    echo ""
    echo "  Wklej zawartość do sesji Claude:"
    echo "  \"Kontynuujemy ASFireWire — oto diagnostyka Sequoia + MOTU:\""
}
trap cleanup EXIT

# --------------------------------------------------------------------------
# 1. System info
# --------------------------------------------------------------------------
section "SYSTEM INFO"
sw_vers | tee "$LOGDIR/system_info.txt"
sysctl hw.model | tee -a "$LOGDIR/system_info.txt"

# --------------------------------------------------------------------------
# 2. SIP + sudo
# --------------------------------------------------------------------------
section "SIP STATUS"
SIP_STATUS=$(csrutil status 2>/dev/null || echo "unknown")
echo "$SIP_STATUS" | tee "$LOGDIR/sip_status.txt"

SIP_DISABLED=false
if echo "$SIP_STATUS" | grep -q "disabled"; then
    ok "SIP disabled — DTrace dostępny"
    SIP_DISABLED=true
    echo ""
    info "Wpisz hasło sudo raz (dla DTrace):"
    sudo -v
    # Odśwież token w tle żeby nie wygasł
    while true; do sudo -n true; sleep 50; done &
    BG_PIDS="$BG_PIDS $!"
else
    warn "SIP enabled — DTrace niedostępny"
fi

# --------------------------------------------------------------------------
# 3. MOTU kext
# --------------------------------------------------------------------------
section "KEXTY FireWire / MOTU"
kextstat 2>/dev/null | grep -iE "firewire|motu|1394" | tee "$LOGDIR/kextstat.txt" || true

MOTU_BUNDLE_ID=""
KEXT_PLIST="/Library/Extensions/MOTUFireWireAudio.kext/Contents/Info.plist"
if [ -f "$KEXT_PLIST" ]; then
    MOTU_BUNDLE_ID=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$KEXT_PLIST" 2>/dev/null || true)
    cp "$KEXT_PLIST" "$LOGDIR/MOTUFireWireAudio_Info.plist"
    ok "Bundle ID: $MOTU_BUNDLE_ID"
fi
if [ -z "$MOTU_BUNDLE_ID" ]; then
    MOTU_BUNDLE_ID=$(kextstat 2>/dev/null | grep -i "motu" | grep -i "firewire\|audio\|1394" | awk '{print $6}' | head -1)
    [ -n "$MOTU_BUNDLE_ID" ] && info "Bundle ID z kextstat: $MOTU_BUNDLE_ID"
fi

# --------------------------------------------------------------------------
# 4. Baseline IORegistry
# --------------------------------------------------------------------------
section "IOREG BASELINE (przed MOTU)"
ioreg -l -rc IOFireWireUnit    >  "$LOGDIR/ioreg_baseline_fw.txt"    2>&1
ioreg -l -rc IOFireWireAVCUnit >> "$LOGDIR/ioreg_baseline_fw.txt"    2>&1
ioreg -l -rc IOAudioDevice     >  "$LOGDIR/ioreg_baseline_audio.txt" 2>&1
# Zapamiętaj ile linii ma ioreg teraz — do detekcji nowego urządzenia
BASELINE_LINES=$(ioreg -rc IOFireWireUnit 2>/dev/null | wc -l | tr -d ' ')
ok "Baseline: $BASELINE_LINES linii IOFireWireUnit"

# --------------------------------------------------------------------------
# 5. Start log stream (bez sudo — bezpieczne)
# --------------------------------------------------------------------------
section "LOG STREAM (start)"
log stream \
    --predicate 'senderImagePath contains "FireWire" OR senderImagePath contains "MOTU" OR senderImagePath contains "CoreAudio" OR senderImagePath contains "AudioHAL"' \
    --level debug \
    > "$LOGDIR/fw_logstream.txt" 2>&1 &
BG_PIDS="$BG_PIDS $!"
ok "log stream uruchomiony → fw_logstream.txt"

# --------------------------------------------------------------------------
# 6. DTrace (jeśli SIP off) — kluczowe: < /dev/null żeby nie blokować TTY
# --------------------------------------------------------------------------
if [ "$SIP_DISABLED" = true ] && [ -n "$MOTU_BUNDLE_ID" ]; then
    section "DTRACE (start)"

    sudo -n dtrace -q -n "
        fbt:${MOTU_BUNDLE_ID}::entry  { printf(\"%Y  ENTER  %s\\n\", walltimestamp, probefunc); }
        fbt:${MOTU_BUNDLE_ID}::return { printf(\"%Y  LEAVE  %s rv=0x%x\\n\", walltimestamp, probefunc, arg1); }
    " < /dev/null > "$LOGDIR/dtrace_motu_calls.txt" 2>&1 &
    BG_PIDS="$BG_PIDS $!"
    ok "DTrace MOTU call trace → dtrace_motu_calls.txt"

    sudo -n dtrace -q -n '
        fbt:com.apple.iokit.IOFireWireFamily::entry
        /strstr(probefunc,"Isoch") != NULL || strstr(probefunc,"IRM") != NULL || strstr(probefunc,"Channel") != NULL/
        { printf("%Y  %s\n", walltimestamp, probefunc); }
    ' < /dev/null > "$LOGDIR/dtrace_fw_isoch.txt" 2>&1 &
    BG_PIDS="$BG_PIDS $!"
    ok "DTrace isoch/IRM → dtrace_fw_isoch.txt"

    sudo -n dtrace -q -n '
        fbt:com.apple.iokit.IOFireWireAVC::entry { printf("%Y  %s\n", walltimestamp, probefunc); }
    ' < /dev/null > "$LOGDIR/dtrace_avc.txt" 2>&1 &
    BG_PIDS="$BG_PIDS $!"
    ok "DTrace AVC → dtrace_avc.txt"
fi

# --------------------------------------------------------------------------
# 7. Czekaj na MOTU — detekcja przez wzrost liczby linii w ioreg
# --------------------------------------------------------------------------
section "CZEKAM NA MOTU"
echo ""
echo "  Podłącz MOTU 828 MK3 przez adapter TB→FireWire..."
echo ""

MOTU_FOUND=false
for i in $(seq 1 90); do
    CURRENT_LINES=$(ioreg -rc IOFireWireUnit 2>/dev/null | wc -l | tr -d ' ')
    if [ "$CURRENT_LINES" -gt "$BASELINE_LINES" ]; then
        MOTU_FOUND=true
        break
    fi
    # Też szukaj po vendor ID w różnych formatach
    if ioreg -l -rc IOFireWireUnit 2>/dev/null | grep -qiE "0001f2|00001f2|vendor.*1f2|1f2.*vendor"; then
        MOTU_FOUND=true
        break
    fi
    printf "\r  Oczekiwanie... %3ds" "$i"
    sleep 1
done
echo ""

if [ "$MOTU_FOUND" = true ]; then
    ok "Urządzenie FireWire wykryte!"
else
    warn "Nie wykryto nowego urządzenia FireWire po 90s"
fi
sleep 2

# --------------------------------------------------------------------------
# 8. IORegistry po podłączeniu
# --------------------------------------------------------------------------
section "IOREG PO PODŁĄCZENIU"
ioreg -l -rc IOFireWireUnit    >  "$LOGDIR/ioreg_connected_fw.txt"    2>&1
ioreg -l -rc IOFireWireAVCUnit >> "$LOGDIR/ioreg_connected_fw.txt"    2>&1
ok "IOFireWireUnit → ioreg_connected_fw.txt"

ioreg -l -rc IOAudioDevice     >  "$LOGDIR/ioreg_connected_audio.txt" 2>&1
ok "IOAudioDevice → ioreg_connected_audio.txt"

system_profiler SPFireWireDataType > "$LOGDIR/system_profiler_fw.txt" 2>&1
ok "system_profiler → system_profiler_fw.txt"

if grep -qi "motu\|828\|0001f2" "$LOGDIR/ioreg_connected_audio.txt" 2>/dev/null; then
    ok "MOTU widoczne w IOAudioDevice"
else
    warn "MOTU NIE widoczne w IOAudioDevice"
fi

# --------------------------------------------------------------------------
# 9. Streaming audio
# --------------------------------------------------------------------------
section "STREAMING AUDIO"
echo ""
echo "  Upewnij się że MOTU jest wybrane jako wyjście:"
echo "  System Settings → Sound → Output → MOTU 828 MK3"
echo ""
echo "  Naciśnij Enter gdy gotowe..."
read -r _

for sound in Glass Ping Tink; do
    f="/System/Library/Sounds/${sound}.aiff"
    [ -f "$f" ] && afplay "$f" 2>/dev/null && ok "Odtworzono: $sound" || true
    sleep 0.3
done
sleep 2

# --------------------------------------------------------------------------
# 10. IORegistry podczas streamingu
# --------------------------------------------------------------------------
section "IOREG PODCZAS STREAMINGU"
ioreg -l -rc IOFireWireUnit    >  "$LOGDIR/ioreg_streaming.txt" 2>&1
ioreg -l -rc IOFireWireAVCUnit >> "$LOGDIR/ioreg_streaming.txt" 2>&1
ioreg -l -rc IOAudioDevice     >> "$LOGDIR/ioreg_streaming.txt" 2>&1
ok "Stan podczas streamingu → ioreg_streaming.txt"

# --------------------------------------------------------------------------
# 11. DTrace IR packet format (5s snapshot)
# --------------------------------------------------------------------------
if [ "$SIP_DISABLED" = true ] && [ -n "$MOTU_BUNDLE_ID" ]; then
    section "DTRACE IR PACKET FORMAT (5s)"
    sudo -n dtrace -q -n "
        fbt:${MOTU_BUNDLE_ID}::entry
        /strstr(probefunc,\"eceiv\") != NULL || strstr(probefunc,\"Rx\") != NULL || strstr(probefunc,\"acket\") != NULL/
        { printf(\"%Y  %s  arg0=0x%llx arg1=0x%llx\\n\", walltimestamp, probefunc, arg0, arg1); }
    " -n 'tick-5s { exit(0); }' \
    < /dev/null > "$LOGDIR/dtrace_ir_packets.txt" 2>&1
    ok "IR packet probe → dtrace_ir_packets.txt"
fi

# --------------------------------------------------------------------------
# 12. Manifest
# --------------------------------------------------------------------------
section "PLIKI"
ls -lh "$LOGDIR/"

# cleanup() przez trap EXIT
