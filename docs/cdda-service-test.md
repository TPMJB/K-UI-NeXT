# Cooperative CDDA service and controlled-client test

Run **profile 07 service handoff** once and photograph its final screen. This
is one automatic test lasting about 90 seconds plus initialization. Profiles
00–06 already passed their requested numerical checks; no earlier profile or
15-minute run needs repeating. The [mixed-job console result](evidence/cdda-mixed-jobs-hardware-2026-10-07.md)
remains a separate tested checkpoint. Profile 07 is prepared and untested on
the console until this new run.

The new step places an original, controlled client in a separate memory
region and lets it call the CDDA engine regularly through an explicit service
interface. The client performs its own work between calls while the engine
maintains generated stereo playback. This establishes a controlled program
and service handoff contract. It does not run Toy Commander, install an
interrupt hook, or establish shared SR, VBR or FPU state with a retail game.
The released 1.8.5 readers remain unchanged.

## Install and run

Use `K-UI-CDDA-Service-Test.zip` and its `build.json`. The archive contains only
profile 07: its default `KUI/runtime.kui` and
`runtimes/07-service-handoff.kui` are identical. Its `stereo.raw` and 8 MiB
`stress.bin` are the same generated fixtures used in the passing earlier
tests. No owned game audio or new upload is needed.

1. Power off the Dreamcast and attach its existing SCI card to the computer.
2. Preserve `/KUI/runtime-before-cdda.kui`, the original working-runtime backup.
   Do not overwrite it with a test. If you already restored the ordinary
   runtime and that backup name is absent, copy the working
   `/KUI/runtime.kui` to the backup name before installing.
3. Copy the archive's `KUI` folder onto the card. This places the temporary
   `/KUI/runtime.kui`, `/KUI/tests/cdda/stereo.raw` and
   `/KUI/tests/cdda/stress.bin`. Identical existing fixtures can be reused.
4. Safely eject the card, install it in SCI and use the usual SCI boot path.
5. Check profile 07 and its build ID against the manifest. Let the automatic
   sequence finish and photograph the whole final screen, including its
   profile/build labels and every result row.

No controller input, new CD, card format or Wi-Fi firmware is needed. The
harness reads the card without writing a log. Keep your original game dump
and prior passing-test archives.

## What changes in this test

The existing interior one-second tone region supplies **440 Hz left and
660 Hz right**. It has no fixture silence. Planned command transitions may
pause or restart playback; an unannounced dropout during continuing playback
should be reported separately.

The controlled client calls the service itself. No interrupt wakes the
engine when the client forgets to call it. The automatic test therefore
checks both the client handoff and regular servicing during client work;
successful results apply to that cooperative contract.
SCI reads remain serialized through the engine. A checked data chunk must
continue to complete within each five-second interval; losing data progress
fails the test even if audio service calls continue. The guard refuses work
at synchronous call boundaries and does not abort an active SD/DMA transfer.

| Phase | Expected behavior |
| --- | --- |
| Initial handoff | Validate the owned descriptor, reject eight deliberately malformed descriptors, launch the separate client and refuse one nested service attempt |
| Client work through about 30 seconds | Continue the stereo tone while the client does its own integer work and calls service regularly; the engine admits bounded checked reads from `stress.bin` |
| Pause and command checks | Pause for one second while continuing service calls, check that played STATUS stays fixed, then resume and seek within the tone loop |
| Client work through about 90 seconds | Continue cooperative playback and checked data reads |
| Expected missed-service case | Deliberately omit service for about 190 milliseconds; refuse the late call, stop owned audio, restart with a new epoch and reject two retired-epoch requests |
| Finish | Stop playback and confirm stopped STATUS before returning from the client |

The planned pause and deliberate missed-service case can create audible
silence or transitions. The expected late-call refusal is counted separately
from failures. It does not promise to prevent stale audio already heard during
an arbitrary long client stall.

| Memory owner | Reserved range |
| --- | --- |
| Engine code/data | Begins at `0x8c010000`, remains below its stack |
| Engine private stack | `0x8c200000` through `0x8c210000` |
| Service private stack | `0x8c220000` through `0x8c230000` |
| Controlled client code/data | `0x8c300000` through `0x8c310000` |
| Controlled client private stack | `0x8c310000` through `0x8c320000` |

The ranges' upper endpoints are exclusive. All three stacks belong to this
controlled test. They do not reserve space in a retail game's memory map.
The archive includes the final linked ELF and map, and the packager checks
that initialized executable client bytes are present in the runtime payload
at the separate client address. Engine service work uses the dedicated worker
stack while suspended engine launch frames and client caller frames remain in
their respective stacks. A pure exported clock call may stay on the client
stack. The bridge checks ordinary integer C call preservation of `r8` through
`r14` and `PR`; it does not establish SR, VBR, GBR, FPU or interrupt-state
sharing, or recover a call that breaks its stack pointer or cannot return.

The timer uses the documented **12,468,720 Hz TMU reference** from the
[calibration record](evidence/cdda-calibration-commands-hardware-2026-10-07.md).
This is not an independent frequency measurement of this console. AICA
configured pitch stays 0; this test does not retune clock hardware, calibrate
automatically or resample audio.

## Final screen and stop criteria

Photograph the final screen even if the test stops early. The pass requires
**seven completed stages** and **zero failures**, with these paired rows:

| Final row | Required result |
| --- | --- |
| Client seconds / service calls | At least 90 seconds; service-call count is reported and depends on card timing |
| Client / service stack bytes | Both nonzero and at most 65,472 bytes, with intact guards |
| ABI checks / context preserved | At least 100 / 1; ABI checks equal service calls plus 2 |
| Checked bytes / service errors | At least 65,536 / 0; errors cover the whole service/test sequence |
| Audio actions / STATUS checks | 6 / 5 |
| EXPECTED gaps / stale refusals | 1 / 2 |
| Descriptor rejects / reentries | 8 / 1 |

The context check compares the controlled handoff's SR (excluding its ordinary
caller-clobbered T/Q/M bits), VBR and GBR before and after the client returns. It
does not establish shared state with a game. The final counter gates are also
recorded in `build.json`. The sequence must end with owned audio stopped.

Record any unexpected deadline stop, data mismatch, guard failure, fixture
error or hang with its last visible stage. Also note an obvious unannounced
audio glitch or sustained playback after the final stop. After taking the
photograph, power off to end the harness. Casual listening through your
existing setup is fine for this numerical test; precise channel separation
remains a later short check with clearly separated stereo output.

Return the photograph and a brief note on noticeable audio issues. No old
passing profile needs repeating, and a new failure is not a reason to format
the card.

After the test, power off, remove the temporary `/KUI/runtime.kui`, and restore
the retained working runtime by renaming `/KUI/runtime-before-cdda.kui` back to
`/KUI/runtime.kui`. The old shell has shut down; a power cycle exits the test.

## Fixture and source identity

The generated inputs are unchanged:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `stereo.raw` | 2,116,800 | `94b1b9e619e9be5a5dc5068d2fd9073eaf86d0c93e0b5b39a76108e5586f65ea` |
| `stress.bin` | 8,388,608 | `29299d0448bab8ace4c8a478255ac009ebd8896b85fcf6b4271e7e1059a2fc49` |

Use `build.json`, `source-url.txt` and `SHA256SUMS` for the exact published
source, runtime and input identities. The package includes the committed
source snapshot, exact private FatFs inputs, compiler identity,
ELF/map/stack reports, provenance and licenses. Host checks establish this
controlled interface and its bounds; they do not replace the new console run
or prove a retail-game service bridge.

With the SH-4 toolchain on `PATH` and pinned FatFs sources available, rebuild
from the published checkout using:

```sh
python3 tools/test_cdda_host.py --sanitize
make -f Makefile.cdda CDDA_FATFS_SOURCE=/path/to/fatfs/source cdda-service-test
```

The runtime is `build/cdda-service/cdda-harness.kui`. A source snapshot without
Git history also needs `CDDA_BUILD_ID=` set to the published revision's first
twelve hexadecimal characters from `build.json`. Review the service result
before adding retail command delivery, interrupt servicing or game-owned
RAM/AICA allocation.
