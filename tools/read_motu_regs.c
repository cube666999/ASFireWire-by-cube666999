/*
 * read_motu_regs.c
 *
 * Odczytuje rejestry MOTU (przestrzen 0xFFFFF000_0000 + offset) przez IOFireWireLib,
 * zeby zobaczyc wartosci jakie OFICJALNY sterownik ustawil w urzadzeniu.
 * Szczegolnie interesuja nas: 0x0b04, 0x0b08, 0x0b1c, 0x0b38 (oficjalny pisze, my nie).
 *
 * Platforma: macOS El Capitan, natywny FireWire, oficjalny sterownik MOTU.
 *
 * KOMPILACJA:
 *   clang -framework IOKit -framework CoreFoundation -o read_motu_regs read_motu_regs.c
 *
 * URUCHOMIENIE (wazna kolejnosc — patrz instrukcja):
 *   1. Wybierz MOTU jako wyjscie dzwieku i pusc cos na ~5 s (sterownik konfiguruje rejestry).
 *   2. Przelacz wyjscie z powrotem na glosniki / zatrzymaj dzwiek (zwalnia urzadzenie).
 *   3. ./read_motu_regs
 *   Rejestry zachowuja wartosci dopoki urzadzenie jest zasilane.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/firewire/IOFireWireLib.h>
#include <libkern/OSByteOrder.h>
#include <stdio.h>

int main(void) {
    /* Znajdz urzadzenie MOTU (Vendor_ID 0x1f2 = 498). */
    CFMutableDictionaryRef match = IOServiceMatching("IOFireWireDevice");
    io_iterator_t iter = 0;
    if (IOServiceGetMatchingServices(kIOMasterPortDefault, match, &iter) != KERN_SUCCESS) {
        printf("Brak urzadzen FireWire.\n");
        return 1;
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
    if (!motu) {
        printf("Nie znaleziono MOTU (Vendor_ID 0x1f2). Podlaczone i widoczne?\n");
        return 1;
    }

    /* Interfejs IOFireWireLib. */
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    if (IOCreatePlugInInterfaceForService(motu, kIOFireWireLibTypeID,
            kIOCFPlugInInterfaceID, &plugin, &score) != KERN_SUCCESS || !plugin) {
        printf("IOCreatePlugInInterfaceForService nieudane.\n");
        return 1;
    }
    IOFireWireLibDeviceRef fw = NULL;
    (*plugin)->QueryInterface(plugin,
        CFUUIDGetUUIDBytes(kIOFireWireDeviceInterfaceID), (void **)&fw);
    if (!fw) {
        printf("QueryInterface (kIOFireWireDeviceInterfaceID) nieudane.\n");
        return 1;
    }

    IOReturn ro = (*fw)->Open(fw);
    if (ro != kIOReturnSuccess) {
        printf("Open nieudane (0x%x). Zatrzymaj dzwiek / przelacz wyjscie na glosniki,\n"
               "zeby oficjalny sterownik zwolnil urzadzenie, i uruchom ponownie.\n", ro);
        return 1;
    }

    UInt32 generation = 0;
    UInt16 nodeID = 0;
    (*fw)->GetGenerationAndNodeID(fw, &generation, &nodeID);
    printf("MOTU nodeID=0x%04x gen=%u\n", nodeID, generation);
    printf("offset    raw         swapped(BE->host)\n");
    printf("------------------------------------------\n");

    /* Czytamy szeroki zakres: 0x0b00..0x0c98 (stream/clock/route/opt). */
    for (UInt32 off = 0x0b00; off <= 0x0c98; off += 4) {
        FWAddress addr;
        addr.nodeID    = nodeID;
        addr.addressHi = 0xffff;
        addr.addressLo = 0xf0000000u + off;
        UInt32 val = 0;
        IOReturn rr = (*fw)->ReadQuadlet(fw, 0, &addr, &val, false, generation);
        if (rr == kIOReturnSuccess)
            printf("0x%04x    0x%08x  0x%08x\n", off, val, OSSwapInt32(val));
        /* offsety nieobslugiwane przez urzadzenie zwykle daja blad — pomijamy ciche */
    }

    (*fw)->Close(fw);
    printf("\nGotowe. Skopiuj powyzsza tabele i przeslij.\n");
    return 0;
}
