# Native game-entry video preservation

## Console observations

The user confirms that recovery build `0749eeef721b` launches games again. Sonic Adventure still displays a vertical bar at the left edge and about half another bar beside it; a different copy shows the same symptom. Selection to the first bootstrap screen takes about 12 seconds. The user clarified this was a regular rip, not the 2048 variant. For a raw GDI, the additional executable checksum is therefore not an explanation.

## Delay audit

Preparation loads/checks the detached package, reads IP/ISO metadata, checks the full 32 KiB IP, builds physical file maps, encodes the manifest, and shuts down audio before handoff. Cooked 2048 GDI and every non-GDI format additionally read the entire executable, one 2048-byte logical sector per call, to bind the detached map to exact file bytes. This check began with cooked-track support and expanded to other formats. Genuine raw 2352 GDI skips the executable pre-read. There is no deliberate 12-second launch wait. A three-track raw GDI performs ten separate absolute game-folder path lookups; FatFs restarts each from the root. Large physically earlier directories can make those costly, but merely appending converted siblings after the original folder does not. Contiguous exFAT maps perform no FAT-sector reads; fragmented exFAT/FAT32 maps do. Automatic storage reconnect remembers its selected device and does not probe absent devices each launch. Exact phase timing would be needed to identify the reported raw-GDI delay. This comparison changes none of that work.

## Isolated change

The native successful relay previously restored 14 pre-bootstrap video registers, cleared 614,400 bytes of VRAM32 at `0xa5000000`, and drew several diagnostic lines after the owner's Bootstrap 2 reached executable entry. The captured scanout state omits other PVR rendering state, so restoring it can also mix unrelated video configurations. The relay did not restore the owner's registers/VRAM afterward. That is a real preservation hazard, but the specific Sonic bars are not proved to come from it; the owner's Bootstrap 2 has not been examined.

Successful native relay now performs no video restore, text, or hex-display calls. Stack/CPU validation, original executable entry restoration, executable CRC, resident comparison, assembly register/FPU/cache restoration, and the existing read-only frame pause remain. A native relay failure explicitly restores the diagnostic display before stopping. Windows CE diagnostics and behavior remain unchanged. Game-time reads, prelaunch checksums, mapping and startup disc stop are unchanged.

## Validation

The actual production C relay passes 9 native and 11 Windows CE host execution cases under UBSan at its original fixed RAM addresses. Native success preserves the caller frame and video-register/VRAM spies, restores the exact odd-tail executable entry, and performs no display writes. Native faults retain visible diagnostic restoration and details. CE status writes, VBR repair and kernel-slot checks pass. The same fixture fails against pre-fix native code on the real redraw assertion; its CE control still passes. Host ASan cannot use these fixed addresses because they overlap its shadow mapping. Strict SH-4 GCC 15.2 compilation/assembly passes both variants, and 11 workflow checks pass. GitHub runs the native layout/stack/instruction/package checks before delivery. Hardware compatibility remains unverified for this comparison. Test Sonic Adventure, then previously working Power Stone 1; report whether the bars persist and whether Sonic proceeds into the game.
