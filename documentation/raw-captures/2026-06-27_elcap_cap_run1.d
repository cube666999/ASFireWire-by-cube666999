/*
 * cap_run1.d — Runda 1: ustal przeciazenie asyncWrite + zrzuc naglowek IOMemoryDescriptor.
 * Cel: znalezc wskaznik do bufora danych (realne quadlety 0x0b1c/0x0b38).
 * Auto-exit po 40 s (tick), wiec uzytkownik ma czas przelaczyc audio na MOTU i zagrac.
 */
#pragma D option quiet
#pragma D option bufsize=16m
#pragma D option dynvarsize=8m

dtrace:::BEGIN
{
    printf("=== cap_run1 START %Y (graj na MOTU teraz, ~10s) ===\n", walltimestamp);
    printf("ZNANE: 0b00=61620000 0b04=ffc10001 0b10=00000002 0b14=0a000100 (raw read-back: 0b14=0001000a)\n\n");
}

/* (1) przeciazenie void* — gdyby bylo uzyte, dane sa pod arg7 wprost (deref w rundzie 2) */
fbt:com.apple.iokit.IOFireWireFamily:*asyncWriteEjttjiiPviP16IOFWAsyncCommand:entry
/ ((uint32_t)arg4 & 0xffff) >= 0x0b00 && ((uint32_t)arg4 & 0xffff) <= 0x0bff /
{
    printf("[VOID] off=0x%04x a5=%d a6=%d buf=0x%llx a8=0x%llx\n",
        (uint32_t)arg4 & 0xffff, (int)arg5, (int)arg6, (uint64_t)arg7, (uint64_t)arg8);
}

/* (2) przeciazenie IOMemoryDescriptor — to najpewniej ono. Zrzut naglowka obiektu (24 x u64). */
fbt:com.apple.iokit.IOFireWireFamily:*asyncWriteEjttjiiP18IOMemoryDescriptor*:entry
/ ((uint32_t)arg4 & 0xffff) >= 0x0b00 && ((uint32_t)arg4 & 0xffff) <= 0x0bff /
{
    printf("[IOMD] off=0x%04x size=%d woff=%d md=0x%llx node=0x%x\n",
        (uint32_t)arg4 & 0xffff, (int)arg9, (int)arg8, (uint64_t)arg7, (uint32_t)arg2);
    printf("  f00=%016llx f01=%016llx f02=%016llx f03=%016llx\n",
        *(uint64_t*)((uintptr_t)arg7+0),   *(uint64_t*)((uintptr_t)arg7+8),
        *(uint64_t*)((uintptr_t)arg7+16),  *(uint64_t*)((uintptr_t)arg7+24));
    printf("  f04=%016llx f05=%016llx f06=%016llx f07=%016llx\n",
        *(uint64_t*)((uintptr_t)arg7+32),  *(uint64_t*)((uintptr_t)arg7+40),
        *(uint64_t*)((uintptr_t)arg7+48),  *(uint64_t*)((uintptr_t)arg7+56));
    printf("  f08=%016llx f09=%016llx f10=%016llx f11=%016llx\n",
        *(uint64_t*)((uintptr_t)arg7+64),  *(uint64_t*)((uintptr_t)arg7+72),
        *(uint64_t*)((uintptr_t)arg7+80),  *(uint64_t*)((uintptr_t)arg7+88));
    printf("  f12=%016llx f13=%016llx f14=%016llx f15=%016llx\n",
        *(uint64_t*)((uintptr_t)arg7+96),  *(uint64_t*)((uintptr_t)arg7+104),
        *(uint64_t*)((uintptr_t)arg7+112), *(uint64_t*)((uintptr_t)arg7+120));
    printf("  f16=%016llx f17=%016llx f18=%016llx f19=%016llx\n",
        *(uint64_t*)((uintptr_t)arg7+128), *(uint64_t*)((uintptr_t)arg7+136),
        *(uint64_t*)((uintptr_t)arg7+144), *(uint64_t*)((uintptr_t)arg7+152));
    printf("  f20=%016llx f21=%016llx f22=%016llx f23=%016llx\n",
        *(uint64_t*)((uintptr_t)arg7+160), *(uint64_t*)((uintptr_t)arg7+168),
        *(uint64_t*)((uintptr_t)arg7+176), *(uint64_t*)((uintptr_t)arg7+184));
}

/* kontekst: start streamu */
fbt:com.apple.driver.AppleFWOHCI:*createDCLProgram*:entry
{
    printf("[ISOCH] %Y createDCLProgram talking=%lld\n", walltimestamp, (int64_t)arg1);
}

tick-90sec { printf("\n=== auto-exit (90s) ===\n"); exit(0); }
