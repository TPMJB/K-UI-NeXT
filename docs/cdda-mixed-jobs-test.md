# Mixed CDDA and data-job console test

Run **profile 06 mixed jobs** once and photograph its final screen. It is one
automatic test lasting about three minutes plus initialization. Profiles 00–05
already passed their requested numerical checks; no earlier profile or
15-minute run needs repeating. The [previous short-profile results](evidence/cdda-calibration-commands-hardware-2026-10-07.md)
remain separate tested checkpoints. Profile 06 subsequently
[passed its numerical console checks](evidence/cdda-mixed-jobs-hardware-2026-10-07.md)
on 2026-10-07 with build `6ec63f97aa0a`. No repeat is needed for that result;
these instructions remain the reproduction checklist.

The program combines generated audio playback, checked data reads, command
changes and controlled job cancellation in the detached SCI homebrew harness.
No Toy Commander executable or BIOS CDDA hooks run here. It owns its sound
resources and timer; the released 1.8.5 readers remain unchanged.

## Install and run

Use `K-UI-CDDA-Mixed-Test.zip` and its `build.json`. The archive's default
`KUI/runtime.kui` and `runtimes/06-mixed-jobs.kui` are identical profile-06
runtimes. It supplies only generated test data: the original `stereo.raw` and
the existing 8 MiB `stress.bin`. No owned game audio or new upload is needed.

1. Power off the Dreamcast and attach its existing SCI card to the computer.
2. Preserve `/KUI/runtime-before-cdda.kui`, the original working-runtime backup.
   Do not overwrite it with this or an earlier test. If you already restored
   the ordinary runtime and that backup name is absent, copy the working
   `/KUI/runtime.kui` to the backup name before installing.
3. Copy the archive's `KUI` folder onto the card. This places the temporary
   `/KUI/runtime.kui`, `/KUI/tests/cdda/stereo.raw` and
   `/KUI/tests/cdda/stress.bin`. Identical existing fixtures can be reused.
4. Safely eject the card, install it in SCI and use the usual SCI boot path.
5. Check profile 06 and the build ID against the manifest, let the automatic
   sequence finish, and photograph the whole final screen, including its
   profile/build labels and all 15 result rows.

No controller input, new CD, card format or Wi-Fi firmware is needed. The
harness reads the card without writing a log. Keep your original game dump
and earlier test archive; neither is replaced by this package.

## What the profile exercises

The audio uses the unfaded one-second stereo region of `stereo.raw`, from
6.1 through 7.1 seconds: **440 Hz left and 660 Hz right**. Automatic pause,
seek and stop/restart commands introduce planned silence and transitions.
These are distinct from an unannounced dropout during continuing playback.

| Phase | Required behavior |
| --- | --- |
| Complete data pass | Verify one contiguous 8 MiB logical job, reading at most 2,048 bytes per physical operation; finish that first pass before 120 documented-clock seconds |
| Mixed request sizes | Before 150 documented-clock seconds, complete at least 16 jobs in each of eight logical size classes: 1, 31, 511, 512, 513, 2,048, 4,096 and 32,768 bytes |
| Address coverage | Exercise deterministic unaligned, backward, sector-boundary and exact-EOF ranges; verify data against the offset-dependent fixture pattern |
| Cancellation | Cancel before dispatch, after a committed first chunk, and after a checked read before commit; reject stale dispatch and commit for each canceled job |
| Audio commands | Seek, pause/status/resume and stop/restart between synchronous reads; preserve played status and the currently selected job's identity |
| Continuing progress | Commit a checked data chunk at least once in every five-second interval while the workload is active |
| Duration | Continue the combined workload for at least 180 documented-clock seconds after successful initial playback start; initialization is excluded |

Logical jobs can span many physical operations. The single SCI owner finishes
one synchronous read before another operation or command runs. Cancellation
invalidates a selected job/token at these boundaries; it does **not** abort an
active SD or DMA transfer. The after-read cancellation case deliberately
refuses to publish a checked chunk under a stale identity. This is a controlled
job/command test, not retail command delivery or game sound-driver sharing.

## Corrected timer contract, unchanged audio pitch

This build consistently uses the documented **12,468,720 Hz TMU reference** for
duration conversion, deadlines and admission budgets. The
[calibration record](evidence/cdda-calibration-commands-hardware-2026-10-07.md)
explains why the earlier convenient 12,500,000 Hz approximation produced the
relative-rate discrepancy. The reference comes from the pinned KallistiOS
timer implementation; it is not an independent frequency measurement of this
particular console.

The build does not retune AICA pitch or alter clock hardware. Configured pitch
remains 0, and there is no automatic calibration or resampling. Historical
screens retain their original nominal conversions; the new timer contract is
explicit in this package rather than silently changing those recorded values.

## Final screen and stop criteria

Photograph the final screen even if the test stops early. Its paired rows
report duration/full passes, checked bytes/errors, covered size classes/minimum
class count, completed jobs/chunks, expected cancellations/stale refusals,
command actions/STATUS checks, and worst data-job/no-progress gap. Common rows
also show refill/service timing, margin, card blocks, private stack and failures.

The pass requires **seven completed stages**, at least 180 TMU seconds,
exactly one complete 8 MiB pass, all eight classes with at least 16 completed
jobs each, zero data errors, **three expected cancellations and six expected
stale refusals**, **seven audio command actions and four STATUS checks**, and
zero unexpected failures. The no-progress gap must stay below 5,000,000
microseconds, including the planned one-second pause and cancellation steps.
Expected cancellation/refusal counters are deliberate checks, not unexpected
I/O errors.

Stop after photographing an unexpected failure, data mismatch, guard failure,
deadline stop or fixture/read error. Also record any obvious unannounced glitch,
sustained stale playback after a stop, or a hang with its last visible stage.
Power off to end the harness. Casual listening through your existing setup is
fine for this numerical test; precise channel separation remains a later short
check with clearly separated stereo output.

Return the photograph and a brief note on any noticeable audio issue. Do not
repeat old passing profiles or format the card to work around a new failure.

After the test, power off, remove the temporary `/KUI/runtime.kui`, and restore
the retained working runtime by renaming `/KUI/runtime-before-cdda.kui` back to
`/KUI/runtime.kui`. The old shell has shut down; a power cycle exits the test.

## Fixture and source identity

The generated inputs are unchanged:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `stereo.raw` | 2,116,800 | `94b1b9e619e9be5a5dc5068d2fd9073eaf86d0c93e0b5b39a76108e5586f65ea` |
| `stress.bin` | 8,388,608 | `29299d0448bab8ace4c8a478255ac009ebd8896b85fcf6b4271e7e1059a2fc49` |

Use `build.json`, `source-url.txt` and `SHA256SUMS` for the exact published source,
runtime and input identities. The package includes the committed source
snapshot, exact private FatFs inputs, ELF/map/stack reports, provenance and
licenses. Host checks establish the implemented model and bounds. The
[console result](evidence/cdda-mixed-jobs-hardware-2026-10-07.md) records the
subsequent tested run; neither evidence set admits retail CDDA.

With the SH-4 toolchain on `PATH` and pinned FatFs sources available, rebuild
from the published checkout using:

```sh
python3 tools/test_cdda_host.py --sanitize
make -f Makefile.cdda CDDA_FATFS_SOURCE=/path/to/fatfs/source cdda-mixed-test
```

The runtime is `build/cdda-mixed/cdda-harness.kui`. A source snapshot without
Git history also needs `CDDA_BUILD_ID=` set to the published revision's first
twelve hexadecimal characters from `build.json`. Review the mixed result
before adding retail hooks, a periodic game service bridge or any game-owned
RAM/AICA allocation.
