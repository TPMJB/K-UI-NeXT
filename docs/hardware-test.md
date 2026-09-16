# First hardware test

This is a capability diagnostic for a real Dreamcast with a working GD-ROM drive
and a standard external serial/SCIF SD adapter. It is not a full dumper. Start
with a spare test card and a known-good retail disc.

## Prepare

1. Download the `diagnostic` artifact from a successful **Diagnostic build** run.
   Record the commit/build identifier. The artifact contains a CDI, ELF, binary,
   checksums, this guide, and `verify_probe.py`.
2. Burn `kui-diagnostic.cdi` as a disc image with your usual CDI-capable tool.
   Copying the CDI file onto a data disc will not create a selfboot disc. The
   console must support MIL-CD booting.
3. Use a FAT32 or exFAT card with 512-byte logical sectors and either one MBR
   primary partition or a whole-device filesystem. Leave space for a test file
   of at least 2 MiB; with large clusters it uses twice the cluster size + 173
   bytes. GPT and multiple-partition layouts are rejected. The app will not
   format or repair a card.
4. With power off, connect the SD adapter, insert the card and attach a controller.
   Boot the diagnostic CD. Photograph any failure screen, including the build ID.

## Disc samples

1. Confirm the K-UI screen appears and the controller responds.
2. Replace the boot CD with a known-good retail GD-ROM, close the lid and let it
   settle. A clean Evolution 2 or Skies of Arcadia is a reasonable first fixture
   if available; use the actual disc condition, not the title, to choose.
3. Press **A**. The log should show LOW and HIGH TOCs, individual sample addresses,
   raw repeat/guard results, and raw/cooked payload comparisons for data tracks.
4. A `DISC PROBE PASS` applies only to those samples. Any other outcome remains
   incomplete. Record the failed address, command, sense values and mode.
5. Press **Y** to save the log. It creates a new directory such as
   `/KUI/probes/p0001/diagnostics.txt`. D-pad Up/Down scrolls; Start shows the latest
   log. If saving fails, photograph the screen.

No part of the running diagnostic should need the boot CD after reaching its
screen. If it stops working after the swap, report that separately from a
sample-read failure. A failed abort requires a console reset; do not interpret
the reset as a successful recovery test.

## SD write/read test

1. Press **X**. This explicitly starts a new test-file write in `/KUI/probes/`.
2. Wait for writing, flush, remount and reread verification to finish. Pressing
   **B** requests Stop. Wait until the operation has ended before powering down.
   A stopped test preserves partial data and has no completion manifest.
3. Look for `STORAGE PASS` and note the path and CRC32. Press **Y** to save the log
   into another new probe directory. Do not remove the card while an operation
   is working or Stop is requested.
4. Power down, put the card in your PC, and run the supplied verifier against the
   directory containing **both** `storage.bin` and `storage.json`:

   ```sh
   python3 verify_probe.py /path/to/card/KUI/probes/p0002
   ```

   On Windows, `py verify_probe.py E:\KUI\probes\p0002` is equivalent.
   A pass checks the length, every byte of the known pattern and the console's
   CRC32. It also prints the file's SHA-256 for the test record.
5. Preserve the report and verifier output. Repeat on the other filesystem using
   another spare card, or after backing up and reformatting the spare on the PC.
   Do not reformat your working card just to satisfy this test.

## Send back

Include the build ID, console revision/BIOS if known, cable/video mode, serial
adapter type, card model/capacity, filesystem/partition layout, disc title and
region, `diagnostics.txt`, verifier output, and whether the app survived the boot
CD swap. Photograph any freeze or error that could not be saved.

M1.0 acceptance needs five successful cold boots and controller checks. M1.1
needs both filesystem tests on hardware and working samples across both disc
regions, preferably on a second disc with a different TOC layout. Do not start
with a damaged disc. Full dumps, reference hashes, read recovery, power-loss
behavior and stop/resume are separate later acceptance tests.
