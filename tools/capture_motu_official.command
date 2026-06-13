#!/bin/bash
#
# capture_motu_official.command  (v2 — precyzyjne sondy host->device)
#
# Klikalny capture oficjalnego sterownika MOTU (El Capitan / MBP 2009 / natywny FW).
# Skrypt DTrace WBUDOWANY. Wynik na PULPICIE: motu_official_<data>.txt
#
# Sondy (na podstawie listy symboli z fw_symbols.txt):
#   [OUT]   IOFireWireController::asyncWrite  — KAZDY zapis host->device (offset rejestru)
#   [QUAD]  IOFWWriteQuadCommand::reinit      — zapisy quadlet (CLOCK_STATUS, packet fmt, routing)
#   [BLK]   IOFWWriteCommand::reinit          — zapisy blokowe
#   [ISOCH] AppleFWOHCI::createDCLProgram     — setup transmisji isoch (DBS/pakiet)
#
# WAZNE — timing: skrypt da Ci czas, zebys PO jego starcie wybral MOTU jako wyjscie
# dzwieku. Wtedy zlapiemy pelna inicjalizacje urzadzenia od zera.
#
# UZYCIE:
#   1. chmod +x ~/Desktop/capture_motu_official.command   (raz)
#   2. Dwuklik. Podaj haslo sudo.
#   3. Gdy zobaczysz ">>> TERAZ wybierz MOTU...": w Preferencje > Dzwiek ustaw
#      wyjscie na MOTU (jesli juz jest — przelacz na glosniki i z powrotem na MOTU).
#      Potem pusc dzwiek na ~10 s.
#   4. Wcisnij ENTER, zeby zakonczyc i zapisac log.
#
# WYMAGANIA: SIP wylaczony (csrutil disable z Recovery), oficjalny sterownik MOTU.

set -u
DESKTOP="$HOME/Desktop"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="$DESKTOP/motu_official_${STAMP}.txt"

echo "==============================================="
echo "  MOTU — capture oficjalnego sterownika (v2)"
echo "==============================================="
echo "Wynik trafi do: $OUT"
echo
echo "Najpierw podaj haslo (sudo) ponizej:"
sudo -v || { echo "Brak uprawnien sudo — przerwano."; exit 1; }
echo "OK, haslo zapamietane."
echo

DSCRIPT="$(mktemp /tmp/motu_capture.XXXXXX.d)"
trap 'rm -f "$DSCRIPT"' EXIT

cat > "$DSCRIPT" <<'DTRACE_EOF'
#pragma D option quiet
#pragma D option bufsize=8m
#pragma D option dynvarsize=4m

dtrace:::BEGIN
{
    printf("=== MOTU official capture v2 started %Y ===\n\n", walltimestamp);
}

/*
 * [OUT] Wychodzacy zapis host->device. Czyste argumenty:
 *   arg1=generation arg2=nodeID arg3=addrHi(16b) arg4=addrLo(32b)
 *   48-bit adres = (addrHi<<32)|addrLo ; offset rejestru = dolne bity addrLo.
 */
fbt:com.apple.iokit.IOFireWireFamily:*IOFireWireController10asyncWrite*:entry
{
    this->dle = *(uint32_t *)arg7;   /* dane w buforze (wire/big-endian) */
    this->dbe = ((this->dle & 0xff) << 24) | ((this->dle & 0xff00) << 8) |
                ((this->dle >> 8) & 0xff00) | ((this->dle >> 24) & 0xff);
    printf("[OUT] %Y off=0x%04x node=0x%x dataBE=0x%08x dataLE=0x%08x\n",
           walltimestamp, (uint32_t)arg4 & 0xffff, (uint32_t)arg2,
           this->dbe, this->dle);
}

/*
 * [QUAD] Zapis quadlet (rejestry konfiguracyjne). Drukujemy surowe argumenty —
 * FWAddressStruct (8B) jest pakowany w jeden rejestr: dolne16=nodeID,
 * srodek16=addrHi, gorne32=addrLo. Offset = (arg>>32). Dekoduje offline.
 */
fbt:com.apple.iokit.IOFireWireFamily:*IOFWWriteQuadCommand6reinit*:entry
{
    printf("[QUAD] %Y a1=0x%llx a2=0x%llx a3=0x%llx a4=0x%llx\n",
           walltimestamp, (uint64_t)arg1, (uint64_t)arg2,
           (uint64_t)arg3, (uint64_t)arg4);
}

/* [BLK] Zapis blokowy (np. config ROM, wieksze struktury). */
fbt:com.apple.iokit.IOFireWireFamily:*16IOFWWriteCommand6reinit*:entry
{
    printf("[BLK] %Y a1=0x%llx a2=0x%llx a3=0x%llx a4=0x%llx\n",
           walltimestamp, (uint64_t)arg1, (uint64_t)arg2,
           (uint64_t)arg3, (uint64_t)arg4);
}

/*
 * [ISOCH] Setup transmisji isoch (warstwa OHCI). talking=arg1, DCL=arg2.
 * createDCLProgram(bool talking, DCLCommandStruct* opcodes, DCLTaskInfo*, j,j,j)
 */
fbt:com.apple.driver.AppleFWOHCI:*createDCLProgram*:entry
{
    printf("[ISOCH] %Y createDCLProgram talking=%lld DCL=0x%llx a4=0x%llx a5=0x%llx a6=0x%llx\n",
           walltimestamp, (int64_t)arg1, (uint64_t)arg2,
           (uint64_t)arg4, (uint64_t)arg5, (uint64_t)arg6);
}

dtrace:::END
{
    printf("\n=== capture ended %Y ===\n", walltimestamp);
}
DTRACE_EOF

sudo dtrace -w -s "$DSCRIPT" > "$OUT" 2>&1 &
DPID=$!

sleep 3
echo
echo ">>> DTrace dziala."
echo ">>> TERAZ wybierz MOTU jako wyjscie dzwieku (Preferencje > Dzwiek)."
echo ">>>   Jesli MOTU juz jest wybrane: przelacz na glosniki i z powrotem na MOTU."
echo ">>> Potem pusc dzwiek na ~10 s."
echo
echo ">>> Na koncu wcisnij ENTER, zeby zakonczyc i zapisac log."
read -r _

sudo kill -INT "$DPID" 2>/dev/null
wait "$DPID" 2>/dev/null

echo
echo "==============================================="
echo "  Zapisano: $OUT"
echo "  Linii: $(wc -l < "$OUT" 2>/dev/null | tr -d ' ')"
echo "==============================================="
echo "Przeslij ten plik do analizy. (Okno mozna zamknac.)"
