# First DOA2 launch experiment

The selected-image GD probe has already passed on hardware: build
`c4cfd4585ec5`, DEAD OR ALIVE 2, all 11 checks, 93 physical SD blocks after
launcher shutdown. Do not repeat that probe for this test.

This update adds the first **experimental retail launch**, not a claim of
working DOA2 gameplay. It only offers launch for the native GD-ROM
`DEAD OR ALIVE 2` / `1ST_READ.BIN` profile. A different region or revision
may still need work. The product number in a synthetic test fixture is not
used as a hardware identification or compatibility claim.

## Install and test

1. Power off. Extract this build's `sd-update` archive onto the existing SD
   card, merging its `KUI` directory. Keep the existing boot CD and game dump.
2. Boot K-UI and open **Games**, then inspect the same DOA2 GDI.
3. Press **Y — Launch (experimental)**. On its confirmation screen press
   **A — Launch**. The existing **A — Test image reads** action on image
   details is the previously accepted probe, not this launch.
4. Photograph the last visible screen, including its build ID and any error
   details. If the game starts, report the furthest point reached: title,
   menu, or an actual fight, and whether controller input works.
5. Power off/on to return to K-UI. No hot return is provided.

Preparation reads the IP and boot executable to record their checksums;
the independent loader checks those bytes again after K-UI shuts down.
This is not a new disc capture or full-image verification. The loader never
writes the SD card or modifies the stored game files.

## What the experiment does

The temporary stage loads the owner's complete 32 KiB IP and native linear
boot executable. It runs the owner's bootstraps at their original addresses.
A temporary 128-byte executable-entry trampoline captures the resulting
integer CPU state, then restores the original entry bytes before the first
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
a game timer. Requests complete through explicit polling in bounded chunks;
command 17 still copies through the CPU. Hardware DMA interrupts, streaming,
image CDDA, Windows CE, IDE/CF, and broad game compatibility are not implemented.
The first game call requiring an unsupported operation may therefore stop
progress. A diagnostic records the request and SD state when possible.

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

No DreamShell ISO-loader implementation was read, imported, or translated
for this experiment. The accepted optical capture/drive/command files remain
unchanged. Host tests and binary layout checks cannot establish retail game
compatibility; this console run is the next evidence needed.
