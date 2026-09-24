# Selected-image GD request probe

This is the next focused Games hardware test. The original resident probe has
already passed: build `7a8493ae825e`, ten checks and 84 SD blocks after launcher
shutdown. [That acceptance remains unchanged](evidence/games-resident-probe-hardware-2026-09-24.json).
The selected-image test described here is implemented; **its hardware result is
pending**. It does not start the selected retail game.

**Ready for this test:** [download sd-update, 7.73 MiB](https://github.com/TPMJB/K-UI-NeXT/actions/runs/35993291859/artifacts/10805331976).
The runtime and selected-image payload both show build **`c4cfd4585ec5`**.
[Build 35993291859](https://github.com/TPMJB/K-UI-NeXT/actions/runs/35993291859)
passed the full host/filesystem suite, SH-4 compilation, both independent-loader
layout checks and packaging. The downloaded ZIP, all file checksums and both
payload envelopes also validate. Source `07b3a8a7497c` has the same tree as the
packaged PR merge build. [Build evidence](evidence/games-selected-image-ci-2026-09-24.json).

## Install and run

1. Finish any active operation and switch the Dreamcast off.
2. From the new `sd-update` package, replace **`/KUI/runtime.kui`** and copy
   **`/KUI/apps/games/image-probe.kui`** to the same path on the SD card. Copying
   the included `KUI/apps/games/` folder also works. Keep the existing boot CD,
   game dumps, settings and backups; no new burn or rip is needed.
3. Boot normally into **SD runtime**. Open **Games** and select the existing
   **Dead or Alive 2** raw GDI dump. Press **A** to inspect it. If it is outside
   `/Games`, use **Start → Browse SD folders** to find it.
4. On the image-details screen, press **A — Test image reads**. Check that the
   selected path is correct, then press **A — Start test**. **B** cancels while
   the menu is still preparing the handoff.
5. After the menu closes, wait for the **K-UI: selected-image GD probe** screen
   to finish. Photograph **PROBE PASSED** or **PROBE FAILED**, with the build ID,
   title, check results and **Post-handoff SD blocks read** visible.
6. Power off/on to return to K-UI. The standalone test has no controller/menu
   service after handoff. Keep the SD card inserted throughout the test.

This is one selected-image test. No repeat of the original synthetic probe,
ARMADA metadata inspection, full rip, full verification or speed benchmark is
requested. An optical game disc is not used for these image reads.

If preparation returns to **Diagnostics**, press **Y** there to save the log.
Send that log and the small selected `.gdi` descriptor; do not upload whole
tracks. If the standalone screen stops progressing, photograph its last stage
and report approximately how long it remained there. The standalone test cannot
write a K-UI diagnostic log after the launcher has gone.

## What the test checks

Before handoff, the launcher opens the selected GDI, validates its track lengths
and boot metadata, and maps its FAT32/exFAT file allocations to physical SD
blocks. Fragmented files are represented by bounded extent lists. This maps the
files without reading every byte of the image. The launcher also reads a small
set of sector samples and records their CRC32 values as references.

After KOS shuts down, the separate resident program erases retired launcher RAM,
installs our own client and initializes the independent read-only SD backend.
The client calls the actual GD BIOS vector at `0x8c0000bc` with the documented
SH-4 register convention. The resident hook services those requests from the
selected image's mapped SD blocks. The launcher filesystem, its threads and its
sample buffers do not supply these reads.

The client's own CRC32 calculation compares returned bytes with the small
pre-handoff reference set. Samples cover the boot extent, the high-density
session, selected track starts and the final stored sector; raw audio and an
adjacent-track boundary are included when present in the selected image. It
checks both read-command variants, TOC/status and sector-mode requests, repeated
random reads, rejected requests, cancellation before I/O, reads after cancel,
physical SD activity and the resident hook stack guard. The final SD block
count depends on the image and sample set; **84 is not an expected value for
this new test**.

All image preparation and resident SD access are read-only. The backend has no
SD write command. An explicit Diagnostics log save is a separate write before
handoff, if needed after a preparation failure.

## What a pass would establish

A hardware pass would connect three pieces: the selected GDI's filesystem map,
post-shutdown physical SD reads, and the implemented subset of the retail GD
request calling convention. The sampled bytes would match those read through
the launcher's filesystem before handoff. These are consistency checks, not
TOSEC/Redump comparisons or full-image verification.

The label **DMA-command reference CRCs** means command 17 is accepted and returns
the correct bytes. It uses the same CPU-driven serial-SD backend as command 16.
It does **not** use hardware SD DMA or generate retail DMA completion interrupts.
This correctness probe keeps caches disabled and interrupts masked; its memory
map and execution state are not a finished retail boot environment. It makes no
throughput or game-performance claim.

The selected boot executable is identified but **not executed**. Retail boot
state, interrupt/callback behavior, streamed reads, image-backed CDDA, Windows CE
and game compatibility remain subsequent work. Dead or Alive 2 remains the
first intended retail launch target after these foundations pass. The accepted
optical capture engine remains unchanged.

## Implementation boundary

- `src/apps/games_image_probe.c`: selected-image validation, allocation mapping,
  small reference samples and package preparation.
- `src/core/resident_image.c`: pointer-free manifest and bounded mapped reads.
- `src/core/gd_service.c`: supported GD request lifecycle and buffer bounds.
- `src/loader/image_*.c`, `image_*.S` and `gd_hook.S`: separately linked resident,
  independently authored client, native vector call/hook and standalone screen.

The original **Games → Start → Resident loader probe** remains available for
reproduction of its accepted synthetic-fixture test. It is separate from this
selected-image test. Neither imports DreamShell's game-loader implementation.
