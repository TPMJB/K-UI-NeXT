# Controlled GD BIOS batch-read tests

Run **profile 09 BIOS batch reads**, then **profile 10 expected deadline**,
once each and photograph both final screens. Profile 09 lasts about 90
seconds plus initialization; profile 10 is a short deliberate deadline check.
Profiles 00–08 already passed their requested numerical checks; no earlier
profile or 15-minute run needs repeating. The
[profile 08 console result](evidence/cdda-bios-hardware-2026-10-07.md) remains
a separate tested checkpoint. Both new profiles are prepared and untested
on the console until these runs.

The separate original client now submits multi-sector PIO reads while CDDA
plays through the temporarily owned GD BIOS vector at `0x8c0000bc`. Each
request covers at most 16 sectors, or 32,768 bytes. Each EXEC admits at most
one physical 2,048-byte data chunk, then returns control to the client. This
keeps command progress cooperative and SCI access serialized through one
owner. The released 1.8.5 readers remain unchanged.

## Install and run

Use `K-UI-CDDA-Batch-Tests.zip` and its `build.json`. The archive contains only
the two new runtimes. Its default `KUI/runtime.kui` is identical to
`runtimes/09-bios-batch-reads.kui`; the second runtime is
`runtimes/10-bios-batch-deadline.kui`. Its `stereo.raw` and 8 MiB
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
5. Check profile 09 and its build ID against the manifest. Let the automatic
   sequence finish and photograph the whole final screen, including its
   profile/build labels and every result row.
6. If profile 09 passes, power off and reconnect the SCI card to the computer.
   Copy `runtimes/10-bios-batch-deadline.kui` from the archive over
   `/KUI/runtime.kui`. Keep the original working-runtime backup and fixtures.
7. Safely eject, reinstall and boot through the usual SCI path. Check profile
   10 and its build ID, let the short automatic test finish and photograph
   its whole final screen. The intentional deadline refusal is an expected
   check, separate from unexpected failures.

If either profile reports an unexpected failure, photograph its screen,
power off and return that result before continuing.

No controller input, new CD, card format or Wi-Fi firmware is needed. The
harness reads the card without writing a log. Keep the original game dump
and previous passing-test archives.

## Synthetic tracks and batch progress

The synthetic map retains the existing generated inputs. Range upper
endpoints are exclusive.

| Track | Synthetic FAD range | Generated input |
| --- | --- | --- |
| 1, audio | `[150, 225)` | `stereo.raw` frames `[268128, 312228)`: exactly 44,100 stereo frames |
| 2, data | `[45150, 49246)` | All 8,388,608 bytes of `stress.bin`, with 2,048-byte sectors |

Audio uses the unfaded one-second **440 Hz left / 660 Hz right** region from
6.08 through 7.08 seconds of the fixture. Planned audio commands can produce
silence or transitions; unannounced glitches should be reported.

PIO READ, command 16, accepts four 32-bit parameters
`{FAD, count=1..16, destination, test=0}`. The complete requested source and
destination ranges must fit. REQUEST copies parameters and returns a handle;
it performs no SD or AICA work. CHECK reports the committed progress-byte
count. EXEC advances at most one 2,048-byte physical data chunk, and publishes
that progress only after the complete service lease succeeds. A request
remains processing until all its sectors are committed.

On FAILED, the reported byte count identifies only the already committed
whole-sector prefix. The caller must discard the unconfirmed remainder even
if its memory was partly touched. A terminal CHECK consumes the result once;
later checks of that handle are unknown. Cancellation occurs between EXEC
operations and does not interrupt an active physical SD transfer. A queued
ABORT reports the confirmed prefix with its cancellation result. RESET
invalidates queued handles and their reported progress; the native adapter
still refuses RESET while audio is playing or paused. The accepted reset
check takes place while audio is stopped.

The controlled client uses cached P1 buffers only, with 32-byte canaries
before and after its 32 KiB data buffer. Physical and P2 aliases are normalized
by the portable boundary, but caller cache coherence for those aliases is
unproved. The client verifies bytes against the generated offset-dependent
pattern and checks monotonic, whole-sector progress bounded by the request.

The read sizes are **1, 2, 3, 4, 7, 8, 15 and 16 sectors**. Each size must
complete at least 16 requests. One sequential pass must cover the full 8 MiB
using completed logical spans; the two deliberately interrupted requests do
not count toward that pass. At least 32 CHECK calls must observe a positive
committed prefix while the request is still processing.

The automatic sequence uses the client-entry clock, starting before its
protocol checks:

| Stage | Expected behavior |
| --- | --- |
| 1 | Validate ownership and install/read back the temporary BIOS vector |
| 2 | Refuse malformed requests, PLAY the tone loop, commit one sector of a 16-sector request, ABORT it and verify its 2,048-byte prefix and untouched suffix; refuse one nested EXEC |
| 3 | Continue sequential mixed-size reads and audio through about 30 seconds |
| 4 | PAUSE for one second with regular EXEC calls; verify the played cursor stays frozen, then RELEASE |
| 5 | Continue through about 60 seconds, STOP, commit one sector of a 16-sector request, RESET it while stopped, verify its preserved data prefix and retired handle, then replay the tone loop |
| 6 | Continue through about 90 seconds and finish the full 8 MiB pass, 24 start/end boundary reads and at least eight deterministic random reads |
| 7 | Final STOP, verify all client counters and report them to the engine |
| 8 | Return to the engine, restore/read back the original vector and validate final gates |

Coverage may extend the run beyond 90 seconds, with a 120-second limit.

PLAY 20 retains track parameters `{1,1,repeat=0 or 15}`; PAUSE 22, RELEASE 23,
STOP 33 and NOP 29 retain their controlled forms. Finite repeats 1–14, PLAY2,
DATATYPE, DMA and interrupt completion remain outside the accepted contract.
The vector is saved, installed, read back and restored on every returning
path. This profile does not run a retail game or establish shared game CPU,
RAM or AICA ownership.

## Owned memory and timer

The engine, client and worker retain the three separate guarded 64 KiB
stacks used by the passing BIOS-command test:

| Owner | Reserved range |
| --- | --- |
| Engine code/data | Begins at `0x8c010000`, remains below its stack |
| Engine private stack | `[0x8c200000, 0x8c210000)` |
| Worker private stack | `[0x8c220000, 0x8c230000)` |
| Controlled client code/data | `[0x8c300000, 0x8c310000)` |
| Controlled client private stack | `[0x8c310000, 0x8c320000)` |

The runtime envelope reserves 3,211,264 bytes from `0x8c010000` through
`0x8c320000`. The archive includes the linked ELF, map and compiler stack
reports. The packager checks initialized client code in the payload and all
three exact writable NOLOAD stack ranges. The larger client buffer remains
within its private stack budget.

The timer retains the documented **12,468,720 Hz TMU reference**, and AICA
configured pitch stays 0. There is no automatic calibration, pitch retuning
or resampling. Ordinary integer call checks do not establish shared SR, VBR,
GBR, FPU or interrupt state with a game, and cannot recover a call that breaks
its stack pointer or fails to return.

## Profile 09 final screen

Photograph the final screen even if the test stops early. The automatic pass
requires **eight completed stages** and **zero failures**, with these seven
paired rows:

| Final row | Required result |
| --- | --- |
| Client seconds / vector calls | 90–120 seconds; call count depends on card timing |
| BIOS requests / completions | Requests equal completions plus 2, for the partially aborted and reset requests |
| Checked bytes / full8MiB passes | At least 8,392,704 / 1; checked bytes include the two confirmed 2,048-byte interrupted prefixes |
| Size classes / minimum completions | 8 / at least 16 |
| Committed chunks / progress checks | Checked bytes equal chunks × 2,048; at least 32 positive-prefix processing checks |
| Client / service stack bytes | Both nonzero and at most 65,472 bytes, with intact guards |
| ABI checks / vector restored | At least 100 / 1; ABI checks equal vector calls minus 1, for the refused nested EXEC |

Completion also requires six audio actions, 11 DRIVE checks, eight expected
protocol refusals, three stale CHECK results, one partial ABORT, one partial
RESET, one refused nested EXEC, eight malformed-descriptor refusals, 24
boundary reads, at least eight random reads, intact canaries and the controlled
CPU context check. The engine and client independently reconcile checked
bytes and size/progress counters. Service/test errors must be zero. These
checks are enforced even when they do not have their own screen row.

Final audio must be stopped, the command queue empty and the original BIOS
vector restored. `build.json` records all gates. Expected refusals and the
partial cancellation/reset are deliberate checks, separate from unexpected
failures.

## Profile 10 expected deadline

This separate runtime starts the tone loop and a 16-sector read, confirms its
first 2,048-byte prefix, then deliberately makes no EXEC call for 200
milliseconds. The next EXEC must refuse the missed deadline before another
physical read, stop owned audio and terminate the queued read with FAILED/IO.
CHECK must retain exactly the previously confirmed 2,048 bytes. The remaining
destination suffix must stay untouched in this controlled refusal case.

The client consumes that terminal result once, checks a stale handle and
invalid follow-up work, then returns for vector restoration. The deliberate
gap does not have the 90-second workload gate. A brief tone followed by its
intentional stop is expected; this profile does not promise uninterrupted
audio during a deliberate missed deadline.

The final screen requires **eight completed stages** and **zero failures**,
with these seven paired rows:

| Final row | Required result |
| --- | --- |
| Client seconds / vector calls | No 90-second duration gate; 17 vector calls |
| EXPECTED gaps / service errors | 1 / 0 |
| Checked bytes / committed chunks | 2,048 / 1 |
| Confirmed prefix / progress checks | 2,048 / 2 |
| Client / service stack bytes | Both nonzero and at most 65,472 bytes, with intact guards |
| ABI checks / vector restored | 16 / 1; ABI checks equal vector calls minus 1 |
| Expected negatives / stale checks | 4 / 1 |

Its 16 client calls comprise three EXEC, seven CHECK, two DRIVE, three
REQUEST and one ABORT. The engine also counts its one refused nested EXEC,
making 17 vector calls. Exactly two requests are accepted; only PLAY completes
successfully, while the multi-sector read ends with the expected FAILED/IO
result. The four expected negative results are the overdue EXEC, FAILED/IO
CHECK, stale ABORT and invalid READ request. They are separate from service
errors and unexpected failures. Final owned audio is keyed off, the queue is
empty and the original vector is restored.

## Stop criteria and restore

Record a data mismatch, unexpected command failure, vector/guard failure,
unexpected deadline stop, fixture error or hang with its last visible stage. Note an
obvious unannounced audio glitch or sustained playback after final STOP.
After taking the photograph, power off to end the harness. Casual listening
through the existing setup is fine; precise channel separation remains a
later short check with clearly separated stereo output.

Return both photographs and a brief note on noticeable audio issues. No old
passing profile needs repeating, and a new failure is not a reason to format
the card.

After the two tests, power off, remove the temporary `/KUI/runtime.kui`, and restore
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
ELF/map/stack reports, provenance and licenses. Host checks establish the
controlled queue and its bounds; they do not replace the new console run or
prove retail compatibility.

With the SH-4 toolchain on `PATH` and pinned FatFs sources available, rebuild
from the published checkout using:

```sh
python3 tools/test_cdda_host.py --sanitize
make -f Makefile.cdda CDDA_FATFS_SOURCE=/path/to/fatfs/source cdda-batch-test
make -f Makefile.cdda CDDA_FATFS_SOURCE=/path/to/fatfs/source cdda-batch-fault-test
```

The runtimes are `build/cdda-batch/cdda-harness.kui` and
`build/cdda-batch-fault/cdda-harness.kui`. A source snapshot without
Git history also needs `CDDA_BUILD_ID=` set to the published revision's first
twelve hexadecimal characters from `build.json`. Review the batch-read result
before admitting retail code, interrupt servicing or game-owned resources.
