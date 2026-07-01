#pragma D option quiet
#pragma D option bufsize=16m
#pragma D option dynvarsize=8m

dtrace:::BEGIN { printf("=== cap_run2 START %Y ===\n", walltimestamp); }

/* SZEROKI matcher (jak counter, ktory dzialal). off = arg4 & 0xffff. */
fbt:com.apple.iokit.IOFireWireFamily:*asyncWrite*:entry
/ ((uint32_t)arg4 & 0xffff) >= 0x0b00 && ((uint32_t)arg4 & 0xffff) <= 0x0bff /
{
    printf("[AW] %Y off=0x%04x node=0x%x fn=%s\n", walltimestamp,
        (uint32_t)arg4 & 0xffff, (uint32_t)arg2, probefunc);
    printf("    a5=0x%llx a6=0x%llx a7=0x%llx a8=0x%llx a9=0x%llx\n",
        (uint64_t)arg5,(uint64_t)arg6,(uint64_t)arg7,(uint64_t)arg8,(uint64_t)arg9);
}

/* Zrzut obiektu spod arg7 (IOMemoryDescriptor* albo void* buf) - 16 x u64, jesli kptr */
fbt:com.apple.iokit.IOFireWireFamily:*asyncWrite*:entry
/ ((uint32_t)arg4 & 0xffff) >= 0x0b00 && ((uint32_t)arg4 & 0xffff) <= 0x0bff
  && (uintptr_t)arg7 >= 0xffffff8000000000 /
{
    printf("    [a7dump off=0x%04x] f00=%016llx f01=%016llx f02=%016llx f03=%016llx\n",
        (uint32_t)arg4 & 0xffff,
        *(uint64_t*)((uintptr_t)arg7+0),  *(uint64_t*)((uintptr_t)arg7+8),
        *(uint64_t*)((uintptr_t)arg7+16), *(uint64_t*)((uintptr_t)arg7+24));
    printf("      f04=%016llx f05=%016llx f06=%016llx f07=%016llx\n",
        *(uint64_t*)((uintptr_t)arg7+32), *(uint64_t*)((uintptr_t)arg7+40),
        *(uint64_t*)((uintptr_t)arg7+48), *(uint64_t*)((uintptr_t)arg7+56));
    printf("      f08=%016llx f09=%016llx f10=%016llx f11=%016llx\n",
        *(uint64_t*)((uintptr_t)arg7+64), *(uint64_t*)((uintptr_t)arg7+72),
        *(uint64_t*)((uintptr_t)arg7+80), *(uint64_t*)((uintptr_t)arg7+88));
    printf("      f12=%016llx f13=%016llx f14=%016llx f15=%016llx\n",
        *(uint64_t*)((uintptr_t)arg7+96), *(uint64_t*)((uintptr_t)arg7+104),
        *(uint64_t*)((uintptr_t)arg7+112),*(uint64_t*)((uintptr_t)arg7+120));
}

fbt:com.apple.driver.AppleFWOHCI:*createDCLProgram*:entry
{ printf("[ISOCH %Y] createDCLProgram talking=%lld\n", walltimestamp, (int64_t)arg1); }

tick-120sec { printf("=== auto-exit ===\n"); exit(0); }
