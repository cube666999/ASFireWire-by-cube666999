#!/bin/bash
#
# read_motu_regs.command — klikalny: kompiluje i uruchamia czytnik rejestrów MOTU.
# Kod C jest WBUDOWANY. Wynik na PULPICIE: motu_regs_<data>.txt
#
# KOLEJNOSC UZYCIA (wazne):
#   1. Wybierz MOTU jako wyjscie dzwieku i pusc cos na ~5 s
#      (oficjalny sterownik konfiguruje rejestry urzadzenia).
#   2. Przelacz wyjscie z powrotem na glosniki MacBooka (zwalnia urzadzenie FireWire).
#   3. Kliknij dwukrotnie ten plik.
#   Rejestry zachowuja wartosci dopoki MOTU jest zasilane.
#
# Wymaga: clang (Command Line Tools — juz masz).

set -u
DESKTOP="$HOME/Desktop"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="$DESKTOP/motu_regs_${STAMP}.txt"
SRC="$(mktemp /tmp/read_motu.XXXXXX.c)"
BIN="$(mktemp /tmp/read_motu.XXXXXX)"
trap 'rm -f "$SRC" "$BIN"' EXIT

cat > "$SRC" <<'CEOF'
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/firewire/IOFireWireLib.h>
#include <libkern/OSByteOrder.h>
#include <stdio.h>

int main(void) {
    CFMutableDictionaryRef match = IOServiceMatching("IOFireWireDevice");
    io_iterator_t iter = 0;
    if (IOServiceGetMatchingServices(kIOMasterPortDefault, match, &iter) != KERN_SUCCESS) {
        printf("Brak urzadzen FireWire.\n"); return 1;
    }
    io_object_t dev = 0, motu = 0;
    while ((dev = IOIteratorNext(iter))) {
        CFTypeRef vid = IORegistryEntryCreateCFProperty(dev, CFSTR("Vendor_ID"),
                                                        kCFAllocatorDefault, 0);
        int v = 0;
        if (vid) {
            if (CFGetTypeID(vid) == CFNumberGetTypeID())
                CFNumberGetValue((CFNumberRef)vid, kCFNumberIntType, &v);
            CFRelease(vid);
        }
        if (v == 0x1f2) { motu = dev; break; }
        IOObjectRelease(dev);
    }
    IOObjectRelease(iter);
    if (!motu) { printf("Nie znaleziono MOTU (Vendor_ID 0x1f2).\n"); return 1; }

    IOCFPlugInInterface **plugin = NULL; SInt32 score = 0;
    if (IOCreatePlugInInterfaceForService(motu, kIOFireWireLibTypeID,
            kIOCFPlugInInterfaceID, &plugin, &score) != KERN_SUCCESS || !plugin) {
        printf("IOCreatePlugInInterfaceForService nieudane.\n"); return 1;
    }
    IOFireWireLibDeviceRef fw = NULL;
    (*plugin)->QueryInterface(plugin,
        CFUUIDGetUUIDBytes(kIOFireWireDeviceInterfaceID), (void **)&fw);
    if (!fw) { printf("QueryInterface nieudane.\n"); return 1; }

    IOReturn ro = (*fw)->Open(fw);
    if (ro != kIOReturnSuccess) {
        printf("Open nieudane (0x%x). Zatrzymaj dzwiek / przelacz wyjscie na glosniki\n"
               "i uruchom ponownie (urzadzenie musi byc wolne).\n", ro);
        return 1;
    }
    UInt32 generation = 0; UInt16 nodeID = 0;
    (*fw)->GetGenerationAndNodeID(fw, &generation, &nodeID);
    printf("MOTU nodeID=0x%04x gen=%u\n", nodeID, generation);
    printf("offset    raw         swapped\n");
    printf("--------------------------------\n");
    for (UInt32 off = 0x0b00; off <= 0x0c98; off += 4) {
        FWAddress addr; addr.nodeID = nodeID; addr.addressHi = 0xffff;
        addr.addressLo = 0xf0000000u + off;
        UInt32 val = 0;
        IOReturn rr = (*fw)->ReadQuadlet(fw, 0, &addr, &val, false, generation);
        if (rr == kIOReturnSuccess)
            printf("0x%04x    0x%08x  0x%08x\n", off, val, OSSwapInt32(val));
    }
    (*fw)->Close(fw);
    printf("\nGotowe.\n");
    return 0;
}
CEOF

echo "Kompiluje..."
if ! clang -framework IOKit -framework CoreFoundation -o "$BIN" "$SRC" 2> "$OUT"; then
    echo "BLAD KOMPILACJI — szczegoly w: $OUT"
    cat "$OUT"
    exit 1
fi

echo "Uruchamiam, zapis do: $OUT"
"$BIN" | tee "$OUT"

echo
echo "==============================================="
echo "  Zapisano: $OUT"
echo "==============================================="
echo "Przeslij ten plik. (Okno mozna zamknac.)"
