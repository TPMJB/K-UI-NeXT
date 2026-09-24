# DOA2 launch experiment — GD startup routing correction

The selected-image GD probe has already passed on hardware: build
`c4cfd4585ec5`, DEAD OR ALIVE 2, all 11 checks, 93 physical SD blocks after
launcher shutdown. Do not repeat that probe for this test.

The first retail build `d19f1e0ebaf3` reached the independent loading screen
after an approximately four-minute wait, flashed more text, then returned to
the stock Dreamcast menu. This correction remains an **experimental retail
launch**, not a claim of working DOA2 gameplay. It only offers launch for the native GD-ROM
`DEAD OR ALIVE 2` / `1ST_READ.BIN` profile. A different region or revision
may still need work. The product number in a synthetic test fixture is not
used as a hardware identification or compatibility claim.

The next build `255e63f79d8d` caught that menu return from executable address
`0x8c012450`, with zero recorded GD commands or resident SD reads. The current
correction handles the second GD vector and direct firmware entry points,
and acknowledges setup calls without loading a physical GD driver over the
image reader. Those setup calls previously were not included in diagnostics.

## Install and test

1. Power off. Extract this build's `sd-update` archive onto the existing SD
   card, merging its `KUI` directory. Keep the existing boot CD and game dump.
2. Boot K-UI and open **Games**, then inspect the same DOA2 GDI.
3. Press **Y — Launch (experimental)**. On its confirmation screen press
   **A — Launch**. The existing **A — Test image reads** action on image
   details is the previously accepted probe, not this launch.
4. The loading screen now has a progress bar. Handoff screens pause for
   approximately three seconds. Photograph the last screen, including its
   build ID and error details. Unsupported reader operations and a standard
   BIOS-menu return request now stop on a diagnostic screen. If the game starts, report the furthest point reached: title,
   menu, or an actual fight, and whether controller input works.
5. Power off/on to return to K-UI. No hot return is provided.

Preparation reads the IP and boot executable to record their checksums;
the independent loader checks those bytes again after K-UI shuts down.
Preparation keeps the track file open across sequential reads; it previously
reopened and retraversed its allocation chain for each sector. The detached
stage now enables CPU caches before SD transfers and CRC work. Neither change
is a measured hardware speed claim; the new console run establishes timing.
This is not a new disc capture or full-image verification. The loader never
writes the SD card or modifies the stored game files.

## What the experiment does

The temporary stage loads the owner's complete 32 KiB IP and native linear
boot executable. After checking both CRCs it clears the native Katana IP
flag at offset `0xfc`, bit `0x20`, matching the observed DreamShell setup,
and installs the resident before entering the owner's bootstrap 2 at
`0xac00e000`. It explicitly sets SP/VBR `0x8c00f400`, GBR `0x8c000000`,
SR/SSR `0x700000f0`, FPSCR `0x00040001` and CCR `0x00000909`. The precise
meaning of the IP flag is not established by the inspected source.
A temporary 128-byte executable-entry trampoline captures the resulting
integer CPU state, confirms the resident code remains intact, then restores
the original entry bytes before the first
original game instruction. The loader does not bundle proprietary bootstrap
code or apply compatibility patches to the executable.

The final reader occupies `0x8c008300..0x8c00d000`, with a guarded 4 KiB stack
through `0x8c00e000`. Firmware low RAM, IP metadata/TOC, the upper bootstrap
and conventional VBR/stack, and executable RAM beginning at `0x8c010000`
are kept outside that reservation. The temporary high stage is no longer
needed once the game starts. A different stack/VBR arrangement is rejected.
This placement is an experiment for the selected game, not a general SDK
memory-reservation guarantee.

GD reads use the independent, read-only serial-SD backend with finite work
budgets and scoped ownership of the serial pins. The backend does not borrow
a game timer. The temporary stage first retires the UART/FIFO state left by
K-UI's shutdown; subsequent game-time reads preserve serial controls and
refuse to take over active serial I/O. Requests complete through explicit polling in bounded chunks;
command 17 still copies through the CPU. Hardware DMA interrupts, streaming,
image CDDA, Windows CE, IDE/CF, and broad game compatibility are not implemented.
The first game call requiring an unsupported operation therefore stops on
a diagnostic with the request and SD state. The standard BIOS-menu vector
is intercepted for return command 1 to retain the caller address and last GD
request. A direct jump to ROM or a hardware reset can still bypass that trap.

GD routing covers the BC supervisor vector, the C0 raw GD vector and both
direct firmware RAM entries (`0x8c001000`, `0x8c0010f0`). BC miscellaneous
setup/registration calls return zero while retaining the independent reader;
C0 ignores incoming R6 and dispatches by R7. Original GD forwarding is removed
to avoid recursion through those patched entries. A menu-return screen reports
counts for all four routes and miscellaneous setup calls. Exact evidence and
ABI comparison: `evidence/games-retail-gd-routing-2026-09-24.md`.

## Independent interface references

- Marcus Comstedt, [IP.BIN layout](https://mc.pp.se/dc/ip.bin.html) and
  [system calls](https://mc.pp.se/dc/syscalls.html): original bootstrap
  addresses, native GD executable layout, and firmware vectors.
- [KallistiOS, pinned commit fcfa7d869471591ca1c777543261a7bfea7cb726](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726):
  `kernel/arch/dreamcast/kernel/{startup.S,exec.c,execasm.s}`,
  `hardware/{syscalls.c,video.c}`, and `include/dc/pvr/pvr_regs.h` establish
  shutdown/cache and hardware interface details.
- GD status/mode interface details are additionally cross-checked against
  the independently developed redream source pinned in `retail_gd.c`.

The first build was implemented without reading the DreamShell ISO loader.
After the failed hardware attempt, the user explicitly requested comparison
with their existing DreamShell tree. Its bootstrap entry, CPU setup, cache
setup, syscall ordering and Katana IP flag were inspected for this correction.
See `evidence/games-retail-boot2-correction-2026-09-24.md` for exact source
files and hashes. The independent image reader and GD service remain in use. The accepted optical capture/drive/command files remain
unchanged. Host tests and binary layout checks cannot establish retail game
compatibility; this console run is the next evidence needed.
