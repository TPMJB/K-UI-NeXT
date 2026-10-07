# Isolated SCI / AICA CDDA test

This is a controlled homebrew audio experiment, not a game reader. It has
not yet been tested on a Dreamcast. The ordinary 1.8.5 SCI readers are unchanged.
Harness changes are included as `source-patch.mbox` in the test archive,
against the experimental CDDA foundation source linked in `base-source-url.txt`.
The public source push is pending approval; use `build.json` for the actual
local source commit/tree and binary identity.

## Install and run

Use the existing SCI microSD and existing K-UI boot setup; no new CD or Wi-Fi
firmware is required. Preserve any currently installed runtime before copying
the test. The archive contains a temporary replacement runtime and an original
generated stereo fixture; your Toy Commander audio is supplied separately.

1. Power off the Dreamcast and put its SCI card in the computer.
2. Rename the card's `/KUI/runtime.kui` to `/KUI/runtime-before-cdda.kui`.
   If that backup name already exists, preserve both files using another name.
3. Copy the archive's `KUI` folder onto the card. This creates
   `/KUI/runtime.kui` and `/KUI/tests/cdda/stereo.raw`.
4. Copy your uploaded `track14.raw` into `/KUI/tests/cdda/track14.raw`.
   Keep the original Toy Commander dump together in its existing game folder.
5. Safely eject the card, install it in SCI, and start K-UI through the usual
   SCI boot path. The test screen should show the build ID from `build.json`.
6. Let the five automatic stages finish (about 58 seconds plus initialization
   and prefill). Photograph the final screen. Record whether left/right tones,
   music, distortion, clicks or dropouts match the expectations below.

To restore 1.8.5, power off, remove this temporary `/KUI/runtime.kui`, and rename
your retained `/KUI/runtime-before-cdda.kui` back to `/KUI/runtime.kui`.
The card remains read-only while the harness runs. It writes no log file;
photograph the final screen, including a failure screen. It requires a power
cycle to exit because the old K-UI runtime has shut down.

## Expected audio and screen

| Stage | Expected output |
| --- | --- |
| 1/5, generated fixture, 12 seconds | 440 Hz left-only for 3 seconds, 660 Hz right-only for 3 seconds, both channels for 3 seconds, silence for 3 seconds |
| 2/5, seek, 2 seconds | Both tones from a non-sector-aligned sample seek |
| 3/5, repeated region, 2 seconds | The same two-second stereo region restarts |
| 4/5, Toy Commander | The uploaded track 14 for 40.95 seconds; roughly 1 second of trailing silence |
| 5/5, seek to final second | The last second, primarily silence, then stop at the actual file EOF |

The final screen should report five completed stages and zero failures, with
worst half-refill time, maximum service gap, minimum refill margin, checked
card blocks and observed stack use. The stereo ring gives 185.76 ms per half;
the scheduler reserves an 8 ms publication margin and refuses late refills.
Unexpected distortion, swapped/missing channels or dropouts fail the audible
test even if all software counters pass. Hardware measurements are still
required; host checks alone cannot verify sound output or G2 timing.

The first stage uses independently generated audio. Game audio is not included
in the source repository or test archive. Your sample is 7,222,992 bytes,
3,071 complete 2352-byte sectors, 40.9467 seconds at 44.1 kHz stereo PCM16. Its GDI
next-track span is 150 sectors longer, consistent with a two-second inter-track
gap. The reader bounds by actual file length rather than reading through that
gap. SHA-256 of the supplied sample:
`ae3d955fc817f433b4c5273581398215e3be5cf2cfd1dee2ec62aaaa677cf338`.

## Build and host checks

Fetch the project's pinned FatFs dependency using its normal dependency setup.
Put the SH-4 toolchain on `PATH`, then run:

```sh
make -f Makefile.cdda cdda cdda-fixture
python3 tools/test_cdda_host.py --sanitize
python3 tools/test_cdda_storage.py --fatfs-source .deps/fatfs/source
```

If FatFs is already available elsewhere, pass
`CDDA_FATFS_SOURCE=/absolute/path/to/fatfs/source` to `make`. Generated outputs
are below `build/cdda`; no KOS kernel or newlib runtime is linked. The existing
runtime envelope accepts a static SH ELF at 0x8c010000 and retains exactly one
bootstrap transport marker. It deliberately refuses a SCIF or IDE boot source.

The harness owns a separate main-RAM region through 0x8c210000, including a
guarded 64 KiB stack. AICA channels 0/1 and sound-RAM buffers at 0x100000/0x108000
belong entirely to the test; its ARM is held reset. TMU1 is an explicitly owned
12.5 MHz monotonic clock. The linked reservation includes gaps and stack, not
just the downloaded payload. These homebrew grants establish no ownership in
Toy Commander or another retail executable.

Remaining gates: measured on-console timing/audio, pause/resume and deliberate
underrun recovery, shared-card audio/game read arbitration, full retail command
semantics and independently demonstrated game sound/memory coexistence.
This test does not enable CDDA in Games or change automatic reader selection.
