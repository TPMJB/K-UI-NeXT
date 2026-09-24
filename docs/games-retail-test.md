# DOA2 launch — first gameplay baseline

The selected-image GD probe has already passed on hardware: build
`c4cfd4585ec5`, DEAD OR ALIVE 2, all 11 checks, 93 physical SD blocks after
launcher shutdown. Do not repeat that probe for this test.

**Gameplay confirmed by owner report:** build `7fd48f11be02` reaches actual
DOA2 gameplay. Loading seemed slower than DreamShell, gameplay lag was tolerable,
and FMVs were estimated around 0.5 fps. Save/load through the physical VMU and
repeated loading transitions remain unconfirmed. See
[evidence and performance findings](evidence/games-doa2-gameplay-2026-09-24.md).
Keep this build as the working baseline.

## First SD performance comparison

The next build changes only CPU work in the existing synchronous read path:

- Dedicated receive-only SPI loop for normal-speed `0xff` transfers, with
  precomputed pin values and no transmit shifts or slow-delay branch per bit.
- Algebraic CRC16 byte update instead of eight polynomial iterations; card
  data CRC verification remains enabled, including rejection of corrupt data.
- Aligned 32-bit integer copies with byte fallback/tails, without borrowing
  game FPU or store-queue state.

There is no compression, asynchronous DMA, prefetch, change to the eight-sector
execution chunk, or removal of the existing diagnostic pauses. The shared
CRC/copy changes are enabled only for retail stage/resident builds, leaving
the accepted standalone probes on their original implementation.

Focused validation is `ASAN_OPTIONS=detect_leaks=0 make test-retail-fast-io`:
SD pin edges/work accounting, protocol/CRC fixtures (including corrupt data),
and copy alignment/tails/canaries. These passed locally with address/undefined
behavior sanitizers. Native size, stack and instruction checks still run as
part of the console build. Speed and tighter SPI timing require hardware
confirmation; this is a candidate, not a measured improvement.

Use the same card, dump, boot CD and game settings as `7fd48f11be02`. One normal
launch is enough for this comparison: note the time from confirming launch to
the title/menu, watch the same opening FMV, then time the same fight load and
check controls/audio during play. A short phone video can capture these in one
run. Report an SD/CRC diagnostic if one appears; retain the baseline SD update
for restoring the previous working version. No rerip or accepted-probe rerun
is needed.

The first retail build `d19f1e0ebaf3` reached the independent loading screen
after an approximately four-minute wait, flashed more text, then returned to
the stock Dreamcast menu. This correction remains an **experimental retail
launch** with the limited hardware result above. It only offers launch for the native GD-ROM
`DEAD OR ALIVE 2` / `1ST_READ.BIN` profile. A different region or revision
may still need work. The product number in a synthetic test fixture is not
used as a hardware identification or compatibility claim.

Builds `255e63f79d8d` and `f26d1a883109` returned to the menu from
`0x8c012450` with zero recorded GD calls. The owner then supplied the exact
startup files: DOA2 `T3601N`, `V1.100`, region `U`. Its startup fills
`0x8c00c000..0x8c00f3ff`, overwriting our old reader-stack guard. The assembly
hook rejected calls before the C counters could increment. This correction
moves the entire reader stack below that range and reports the guard on a
menu return. See `evidence/games-retail-startup-return-2026-09-24.md`.

Build `289e10a1ab20` passed that startup guard and reached the reader, stopping
on unsupported command `0x28` (GET_VERS). The current correction implements
its bounded 28-byte compatibility response. DOA2's next visible startup step
is a one-sector ISO volume-descriptor read through the existing read commands.
Evidence: `evidence/games-retail-version-query-2026-09-24.md`.

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
the original entry bytes and resumes the owner's `0xac010000` uncached alias
before the first original game instruction. The loader does not bundle proprietary bootstrap
code or apply compatibility patches to the executable.

The final reader must fit below `0x8c00bb00`, starting at `0x8c008300`.
Its guarded 1,280-byte stack occupies `0x8c00bb00..0x8c00c000`; the native
build rejects code/BSS overlap or a conservative stack bound over 1,232 bytes.
The owner executable's startup stack fill begins at `0x8c00c000`. Firmware low RAM, IP metadata/TOC, the upper bootstrap
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
counts for all four routes, the hook guard fault flag and current guard word.
A healthy guard is `4B554947` and its fault flag is zero. Exact evidence and
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
