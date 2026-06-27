/* cap_run4.d — cap_run3 + DRUGI quadlet (dla 0x0b38, size=8 block-write).
 *   data = *(u32*)( *(u64*)( *(u64*)(arg7+96) ) )          // d0 = 1. quadlet
 *   d1   = *(u32*)( buf + 4 )  gdy len>=8                   // 2. quadlet (0x0b38)
 *   arg7=IOMemoryDescriptor*, _ranges@+96 -> inline IOVirtualRange{address@+0,length@+8}.
 * Walidacja: 0b00->61620000 0b04->ffc1/ffc2 0001 0b10->00000002 0b14->0a000100.
 * 0x0b38: pada TYLKO przy ZIMNYM starcie -> PRZEPNIJ KABEL FW MOTU w trakcie dzialania skryptu. */
#pragma D option quiet
#pragma D option bufsize=16m
#pragma D option dynvarsize=8m
dtrace:::BEGIN { printf("=== cap_run4 deref START %Y (replug MOTU FW dla 0b38) ===\n", walltimestamp); }
fbt:com.apple.iokit.IOFireWireFamily:*asyncWrite*:entry
/ ((uint32_t)arg4 & 0xffff) >= 0x0b00 && ((uint32_t)arg4 & 0xffff) <= 0x0bff
  && (uintptr_t)arg7 >= 0xffffff8000000000
  && (uintptr_t)*(uint64_t*)((uintptr_t)arg7+96) >= 0xffffff8000000000
  && (uintptr_t)*(uint64_t*)(*(uint64_t*)((uintptr_t)arg7+96)) >= 0xffffff8000000000 /
{
    this->buf = *(uint64_t*)(*(uint64_t*)((uintptr_t)arg7+96));
    this->len = *(uint64_t*)((*(uint64_t*)((uintptr_t)arg7+96))+8);
    this->d0  = *(uint32_t*)(this->buf);
    this->d1  = (this->len >= 8) ? *(uint32_t*)(this->buf + 4) : 0;
    printf("[D] %Y off=0x%04x size=%d len=%d D0=0x%08x D0swap=0x%08x D1=0x%08x D1swap=0x%08x\n",
        walltimestamp, (uint32_t)arg4 & 0xffff, (int)arg9, (int)this->len,
        this->d0, ((this->d0&0xff)<<24)|((this->d0&0xff00)<<8)|((this->d0>>8)&0xff00)|((this->d0>>24)&0xff),
        this->d1, ((this->d1&0xff)<<24)|((this->d1&0xff00)<<8)|((this->d1>>8)&0xff00)|((this->d1>>24)&0xff));
}
fbt:com.apple.driver.AppleFWOHCI:*createDCLProgram*:entry { printf("[ISOCH %Y] talking=%lld\n", walltimestamp, (int64_t)arg1); }
tick-180sec { exit(0); }
