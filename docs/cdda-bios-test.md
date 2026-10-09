# Controlled GD BIOS command-delivery test

Run **profile 08 BIOS commands** once and photograph its final screen. This
is one automatic test lasting about 60 seconds plus initialization. Profiles
00–07 already passed their requested numerical checks; no earlier profile or
15-minute run needs repeating. The [profile 07 console result](evidence/cdda-service-hardware-2026-10-07.md)
remains a separate tested checkpoint. Profile 08 is prepared and untested on
the console until this new run.

The separate original client now delivers audio commands through the actual
temporarily owned GD BIOS vector at `0x8c0000bc`, using raw `r4` through `r7`.
The test saves the original vector, installs and reads back its adapter, then
restores the original pointer on every returning success or failure path.
All test audio commands use this BIOS boundary. It does not execute a retail
game or establish interrupt completion or shared game CPU/sound resources.
The released 1.8.5 readers remain unchanged.

## Install and run

Use `K-UI-CDDA-BIOS-Test.zip` and its `build.json`. The archive contains only
profile 08: its default `KUI/runtime.kui` and
`runtimes/08-bios-commands.kui` are identical. Its `stereo.raw` and 8 MiB
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
5. Check profile 08 and its build ID against the manifest. Let the automatic
   sequence finish and photograph the whole final screen, including its
   profile/build labels and every result row.

No controller input, new CD, card format or Wi-Fi firmware is needed. The
harness reads the card without writing a log. Keep your original game dump
and previous passing-test archives.

## Synthetic tracks and command scope

The client sees this deliberately small synthetic map, using the existing
generated files. Range upper endpoints are exclusive.

| Track | Synthetic FAD range | Generated input |
| --- | --- | --- |
| 1, audio | `[150, 225)` | `stereo.raw` frames `[268128, 312228)`: 75 CDDA sectors, exactly 44,100 stereo frames |
| 2, data | `[45150, 49246)` | All 8,388,608 bytes of `stress.bin`, with 2,048-byte data sectors |

Track 1 selects the unfaded one-second **440 Hz left / 660 Hz right** region
from 6.08 through 7.08 seconds of the original stereo fixture. It contains no
fixture silence. Planned pause, stop, one-shot EOF and restart commands can
produce audible silence or transitions.

| Command | Controlled accepted form |
| --- | --- |
| PLAY, 20 | Three 32-bit parameters `{first_track=1, last_track=1, repeat=0 or 15}`; 0 plays once and 15 loops indefinitely |
| PAUSE, 22 | No parameters |
| RELEASE, 23 | No parameters; resume the paused played cursor |
| STOP, 33 | No parameters |
| NOP, 29 | No parameters |
| PIO READ, 16 | Four 32-bit parameters `{FAD, count=1, destination, test=0}`; read exactly 2,048 bytes from data track 2 |

Finite repeat values 1–14 are refused. PLAY2 is deferred because its sector
endpoint fidelity has not been established. DATATYPE, DMA, retail execution
and interrupt completion are outside this profile's accepted contract.

REQUEST copies parameters and returns a handle. CHECK reports processing
before EXEC, and EXEC performs the bounded engine work. Terminal success is
published only after the full EXEC service lease finishes successfully. A
failed read reports zero accepted bytes; its destination data must be
discarded even if the buffer was partly touched. A terminal CHECK consumes
the result once; later checks of that handle are unknown. The
automatic client also exercises retired handles, queue reset, queued abort
and refused nested execution. This adapter admits INIT/RESET only while audio
is STOPPED or at EOF. It refuses RESET while playing or paused without
changing audio, and checks that accepted RESET retires a queued read while
stopped. Queued abort is not an active SD/DMA transfer abort.

The client verifies returned data against the generated offset-dependent
pattern and checks surrounding canaries. Its buffers use cached P1 addresses
only. Physical and P2 aliases are normalized by the portable boundary, but
cache coherence for callers accessing those aliases is unproved. Its minimum
data gate is 64 KiB of
verified completed reads; this is not a required contiguous full-file pass.
SCI remains serialized through one owner. Regular EXEC calls service the
audio engine cooperatively; this test does not install an interrupt to run
the engine when the client stops calling it.

The automatic sequence uses the client-entry clock, starting before its
protocol checks:

| Stage | Expected behavior |
| --- | --- |
| 1 | Validate ownership and the temporary BIOS vector |
| 2 | Refuse invalid requests; complete NOP and a copied-parameter read; consume a terminal result once; abort a queued read and consume its cancellation |
| 3 | PLAY track 1 once with repeat 0, then observe one-shot EOF after about one second while continuing checked reads |
| 4 | PLAY with repeat 15 and continue the loop/data workload through about 20 seconds; refuse RESET while playing and one nested EXEC |
| 5 | PAUSE for one second with regular EXEC calls; verify a frozen played cursor, refuse RESET while paused, then RELEASE |
| 6 | Continue through about 40 seconds, STOP, reset a queued read while stopped, refuse its stale handle and replay the loop |
| 7 | Continue through about 60 seconds, STOP and report the client checks |
| 8 | Return to the engine, restore and verify the original vector, and validate the final gates |

## Owned memory and timer

The engine, client and worker retain the three separate guarded stacks from
profile 07:

| Owner | Reserved range |
| --- | --- |
| Engine code/data | Begins at `0x8c010000`, remains below its stack |
| Engine private stack | `[0x8c200000, 0x8c210000)` |
| Worker private stack | `[0x8c220000, 0x8c230000)` |
| Controlled client code/data | `[0x8c300000, 0x8c310000)` |
| Controlled client private stack | `[0x8c310000, 0x8c320000)` |

The runtime envelope reserves 3,211,264 bytes from `0x8c010000` through
`0x8c320000`. The archive includes the final linked ELF and map; the packager
checks initialized client code in the payload and the exact three writable
NOLOAD stack ranges. These resources belong to the controlled test and do
not allocate memory in a retail game's map.

The timer retains the documented **12,468,720 Hz TMU reference**, and AICA
configured pitch stays 0. There is no automatic calibration, pitch retuning
or resampling. This is not an independent oscillator measurement of the
console. Ordinary integer call checks do not establish shared SR, VBR, GBR,
FPU or interrupt state with a game, and cannot recover a call that breaks its
stack pointer or fails to return.

## Final screen and stop criteria

Photograph the final screen even if the test stops early. The automatic pass
requires **eight completed stages** and **zero failures**, with these paired
rows:

| Final row | Required result |
| --- | --- |
| Client seconds / vector calls | At least 60 seconds; call count depends on card timing |
| BIOS requests / completions | Requests equal completions plus 2, for the deliberately aborted and reset reads |
| Audio actions / DRIVE checks | 7 / 14 |
| Checked bytes / service errors | At least 65,536 / 0; errors cover the whole service/test sequence |
| Client / service stack bytes | Both nonzero and at most 65,472 bytes, with intact guards |
| ABI checks / vector restored | At least 100 / 1; ABI checks equal vector calls minus 1, for the refused nested EXEC |
| EXPECTED rejects / cancels | 14 / 1 |

Completion also requires three stale CHECK results, one accepted queued
RESET, one refused nested EXEC, eight malformed-descriptor refusals, intact
32-byte canaries before and after each data buffer, and the controlled CPU
context check. These are enforced even though they do not each occupy a
separate final row. The checked-byte count must match the client's independent
verification. The sequence ends with owned audio stopped, the command queue
empty and the original vector restored. `build.json` records the gates.
Expected protocol refusals and queued aborts are deliberate checks, separate
from unexpected failures.

Record any data mismatch, unexpected command failure, vector/guard failure,
deadline stop, fixture error or hang with its last visible stage. Also note
an obvious unannounced audio glitch or sustained playback after final STOP.
After taking the photograph, power off to end the harness. Casual listening
through your existing setup is fine for this numerical test; precise channel
separation remains a later short check with clearly separated stereo output.

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
ELF/map/stack reports, provenance and licenses. Host checks establish the
controlled queue and its bounds; they do not replace the new console run or
prove retail compatibility.

With the SH-4 toolchain on `PATH` and pinned FatFs sources available, rebuild
from the published checkout using:

```sh
python3 tools/test_cdda_host.py --sanitize
make -f Makefile.cdda CDDA_FATFS_SOURCE=/path/to/fatfs/source cdda-bios-test
```

The runtime is `build/cdda-bios/cdda-harness.kui`. A source snapshot without
Git history also needs `CDDA_BUILD_ID=` set to the published revision's first
twelve hexadecimal characters from `build.json`. Review the BIOS-boundary
result before admitting retail code, interrupt servicing or game-owned
RAM/AICA resources.
