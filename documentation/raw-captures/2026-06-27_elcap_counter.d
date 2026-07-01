#pragma D option quiet
fbt:com.apple.iokit.IOFireWireFamily:*asyncWrite*:entry { @aw = count(); @off[(uint32_t)arg4 & 0xffff] = count(); }
fbt:com.apple.driver.AppleFWOHCI:*createDCLProgram*:entry { @dcl = count(); printf("[ISOCH %Y] createDCLProgram talking=%lld\n", walltimestamp, (int64_t)arg1); }
tick-1sec { printf("tick %Y\n", walltimestamp); }
tick-60sec { exit(0); }
END {
  printa("TOTAL asyncWrite = %@d\n", @aw);
  printa("TOTAL createDCL  = %@d\n", @dcl);
  printf("--- offsety zapisow ---\n");
  printa("off=0x%04x cnt=%@d\n", @off);
}
