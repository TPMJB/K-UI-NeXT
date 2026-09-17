# Capture speed and resume: next tests

This plan follows the measured MDK2 results in
[hardware evidence](hardware-evidence.md). It is a testing and implementation
plan, not a new runtime release. The current working timing build is
**0ef58878ccdd**, available in the `sd-update` artifact from the
[successful build](https://github.com/TPMJB/K-UI-NeXT/actions/runs/35166664869).
Keep the existing bootstrap CD, SD card, partial MDK2 jobs and completed Sword
of the Berserk dump. No additional CD burn is needed for these tests.

## What we already know

| Evidence | Result | Remaining question |
| --- | --- | --- |
| MDK2 track-1 capture, P1/P2 snapshots | 186.99 KiB/s; optical callback 63.20%, writes 23.25%, SHA-256 10.66%; zero retries | Which optical operations consume the time? |
| MDK2 through part of audio track 4, diagnostics(9) | 142.33 KiB/s across mixed tracks; optical 71.92%, writes 17.75%, SHA-256 8.20%; zero retries | Audio-only follow-up now measured below |
| Isolated track-4 audio after Resume, diagnostics(10) | 57.46 KiB/s; optical 88.75%, writes 7.32%, SHA-256 3.17%; zero retries | Which operation inside the optical callback is slow? |
| SD writes across those three traces | 804.21, 801.95 and 785.36 KiB/s within write calls | No evidence here of SD writes slowing to 50 KiB/s |
| Memory snapshots | About 861 KiB main RAM used/reserved, unchanged sampled peak and allocator counters | Longer-run behavior remains separate; no current memory-pressure signal |
| Resume in diagnostics(10) | 56.86 MiB checked in 167.47 seconds; SD reads 75.36%, SHA-256 20.40%, CRC32 3.84%; new writes then succeed | Avoid the repeated prefix pass with versioned incremental hash state; retain strict checks and final verification |

The two P1/P2 files describe one operation. The longer trace is another job.
The mixed trace's whole-phase average does not establish track-4/5 throughput;
the follow-up contains only new track-4 audio and supplies a direct measurement.
The optical bucket includes paired reads, mode setup, polling/waits and buffer
checks. Do not treat
it as pure media transfer time or drop integrity checks solely from its share.

## Completed console baseline: resumed audio

`diagnostics(10).txt` completes the requested measurement on the same runtime
and job `/KUI/dumps/df838eac34967ae16-0002`. It validates checkpoint 9's
59620848-byte prefix, starts at audio track 4 FAD 55672, and captures another
15955968 bytes in 271.160576 seconds before a controlled Stop. The final
committed count is 75576816 bytes; all counts reconcile with the previous log.
Startup, resume, new writes and Stop are logged; a full resumed MDK2 dump and
its final verification remain untested. The log does not specify how the
console was restarted.

The resume and capture durations are separate, and neither includes the
12.615276-second setup phase. All phase counters reconcile. Exact counters,
input fingerprint and the console's verified-prefix digests are in
[the results](evidence/mdk2-audio-resume-2026-09-17.json).

**No further identical baseline or full rip is needed.** Preserve this partial
job and the completed Sword dump. The next console test should follow the next
SD runtime change, with short data/audio comparisons and exact report paths.
Until automatic reports are implemented, B/READY followed by the diagnostics
page's **Y Save log** remains necessary before rebooting or another operation.

## Next runtime change: optical timing and automatic reports

| Measured observation | Next action |
| --- | --- |
| Audio capture is 57.46 KiB/s, optical callback consumes 88.75%, no application retries | Split optical command work first, then optimize the measured cause |
| SD write-call throughput remains 785-804 KiB/s | Retain the storage baseline; the evidence does not identify writes as the audio bottleneck |
| Resume spends 126.21 of 167.47 seconds reading the old prefix | Implement and validate a separate fast-resume design before another long acceptance run |
| SHA-256 consumes 34.17 seconds during resume but 8.60 seconds during audio capture | Assess hashing separately by phase; disabling it cannot resolve this capture slowdown |

The current build cannot distinguish first-read, second-read and polling costs.
The next SD update should add per-track timing and optical subtimers:
sector-mode setup, each read command, buffer checks/copy, and
poll counts/waits within each command. Command subtotals belong inside the
optical total; do not add overlapping parent and child times together. Include
maximum command duration and retry reasons. Keep counters bounded and avoid
per-chunk log output or additional SD writes while capturing.

That update should also save a clearly named report automatically after each
Stop, failure or completion, once capture I/O is finished. Preserve older
reports and checkpoints, show the saved path, and report log-save failures
separately from capture/verification success. Retain manual Save log. Exercise
successful save, full-card/write failure and cancellation handling before
handing over the update; logging must not create a misleading verified result.

For optical changes, compare the same fixed data and audio ranges on the same
disc/card. Change one measured behavior at a time: redundant mode changes,
polling overhead or request batching, selected from the subtimers. Keep finite
deadlines, Stop responsiveness, guards, paired-read comparison and sector checks
in the baseline. Require identical output bytes/hashes and no new read errors
before claiming a speed improvement. A larger chunk must still meet command
and cancellation bounds. Repeat a short comparison only if variability makes
the effect unclear. Do not infer a fixed 640 KiB/s target from another program's
peak display or claim speed gains before hardware measurements.

## Plan faster resume separately

Ordinary fast resume should restore a validated checkpoint and incremental hash
state rather than reread every committed byte. This is a proposed change, not
a control available in the current build. Keep an explicit full-prefix-check
resume path and final full saved-file verification. Fast resume would trust
the recorded prefix until a full readback checks it; it must not label those
old bytes newly verified merely because a checkpoint checksum passes.

Before the next long-rip acceptance run:

- Define a versioned portable checkpoint representation for the partial track's
  incremental SHA-256 state, CRC32 state, byte count and buffered bytes. Preserve
  completed tracks' recorded digests without rehashing them at every checkpoint.
  Validate state invariants, identity, lengths, record checksum and sequence.
- Keep both alternating checkpoint records and conservative publication order.
  Existing v1 jobs need the current verified prefix reread to reconstruct hash
  state once; do not promise instant first resume of an old-format job. Preserve
  them if conversion or writing the new state fails.
- On FAT32/exFAT host images, compare resumed and uninterrupted output across
  hash-buffer and track boundaries. Cover torn/invalid newest records, wrong
  discs, short files, uncommitted tails, cancel and write/sync failures. Test the
  distinction between fast resume and full-prefix-check resume: changed old
  bytes must fail a later full verification and never produce a verified
  manifest, while the strict path rejects the corrupt prefix before appending.
- Then run one short controlled Stop, reboot and resume on the console. Confirm
  the selected job/offset, final byte/hash equality and time-to-first-new-byte.
  Test larger prefixes in host fixtures before asking for another lengthy
  console capture. Arbitrary power-loss recovery is not established by a
  controlled Stop/reboot test.

## Close the correctness checks with minimal repeat work

Run the bundled PC verifier on the **existing completed Sword of the Berserk
directory**, including track 3, and retain its output:

```sh
python3 verify_dump.py "/path/to/completed-sword-of-the-berserk-dump"
```

This requires no new Dreamcast capture. Track 3's recorded CRC32 is `2cfb5dcb`;
the verifier must independently calculate the full lengths, CRC32, SHA-256 and
GDI layout rather than merely inspect the manifest. An independent compatible
reference comparison is still a separate acceptance check.

After short performance and resume tests pass, perform one complete mixed-track
hardware acceptance run using the improved runtime, with a controlled stop and
resume, final SD readback and PC verification. Keep the existing completed dump
as a baseline. No new CDI is planned for logging, profiling, optical-loop or
checkpoint changes that stay within the existing SD runtime package format.
