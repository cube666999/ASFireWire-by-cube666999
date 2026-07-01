/* cap_run3.d — FINALNY deref danych zapisow MOTU. Lancuch ustalony z run2:
 *   data = *(u32*)( *(u64*)( *(u64*)(arg7+96) ) )
 *   arg7=IOMemoryDescriptor*, _ranges@+96 -> inline IOVirtualRange{address@+0,length@+8}.
 * Walidacja: 0b00->61620000 0b04->ffc10001 0b10->00000002 0b14->0a000100.
 * Trigger 0b1c: re-start streamu (zmiana sample rate / przelaczenie wyjscia).
 * 0b38/0b08: wymagaja ZIMNEGO startu (replug MOTU / reboot z MOTU od zera). */
#pragma D option quiet
#pragma D option bufsize=16m
#pragma D option dynvarsize=8m
dtrace:::BEGIN { printf("=== cap_run3 deref START %Y ===\n", walltimestamp); }
fbt:com.apple.iokit.IOFireWireFamily:*asyncWrite*:entry
/ ((uint32_t)arg4 & 0xffff) >= 0x0b00 && ((uint32_t)arg4 & 0xffff) <= 0x0bff
  && (uintptr_t)arg7 >= 0xffffff8000000000
  && (uintptr_t)*(uint64_t*)((uintptr_t)arg7+96) >= 0xffffff8000000000
  && (uintptr_t)*(uint64_t*)(*(uint64_t*)((uintptr_t)arg7+96)) >= 0xffffff8000000000 /
{
    this->buf = *(uint64_t*)(*(uint64_t*)((uintptr_t)arg7+96));
    this->len = *(uint64_t*)((*(uint64_t*)((uintptr_t)arg7+96))+8);
    this->d0  = *(uint32_t*)(this->buf);
    printf("[D] %Y off=0x%04x size=%d len=%d DATA=0x%08x swap=0x%08x\n",
        walltimestamp, (uint32_t)arg4 & 0xffff, (int)arg9, (int)this->len, this->d0,
        ((this->d0&0xff)<<24)|((this->d0&0xff00)<<8)|((this->d0>>8)&0xff00)|((this->d0>>24)&0xff));
}
fbt:com.apple.driver.AppleFWOHCI:*createDCLProgram*:entry { printf("[ISOCH %Y] talking=%lld\n", walltimestamp, (int64_t)arg1); }
tick-120sec { exit(0); }
