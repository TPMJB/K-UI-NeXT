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
| MDK2 through part of audio track 4, diagnostics(9) | 142.33 KiB/s across mixed tracks; optical 71.92%, writes 17.75%, SHA-256 8.20%; zero retries | What is the isolated audio rate and timing split? |
| SD writes in those two traces | 804.21 and 801.95 KiB/s within write calls | No evidence here of SD writes slowing to 50 KiB/s |
| Memory snapshots | About 861 KiB main RAM used/reserved, unchanged sampled peak and allocator counters | Longer-run behavior remains separate; no current memory-pressure signal |
| Resume implementation and photos | All saved bytes are reread and hashed before appending | Measure read versus hash time; replace this preflight for a future fast-resume mode |

The two P1/P2 files describe one operation. The longer trace is another job.
Whole-phase averages do not establish track-4/5 throughput. The optical bucket
includes paired reads, mode setup, polling/waits and buffer checks. Do not treat
it as pure media transfer time or drop integrity checks solely from its share.

## Next console test: one resumed audio sample

Use the current timing runtime. Do not start another new dump for this test.

1. Boot the existing CD without holding B, confirm **SD runtime 0ef58878ccdd**,
   then insert MDK2. On the capture page press **X Resume latest**.
2. Check the displayed job. The longer uploaded log used
   `/KUI/dumps/df838eac34967ae16-0002`, with 59620848 committed bytes and capture
   stopped inside track 4. X selects the newest matching job, so a later job or
   later checkpoint may change that position. Preserve all existing jobs; do
   not rename/delete them to force selection. If the selected job is still in
   a data track, record its path and advance to the audio portion before using
   a new resume operation as the audio-only measurement.
3. Let **Checking saved prefix** finish. At the previously observed readback
   speed, the logged 56.86 MiB prefix takes about 2 minutes 42 seconds, plus
   startup/disc identification. This delay is still present in this build.
   Record a refusal or mismatch instead of treating it as a completed resume.
4. Confirm the screen changes to **Capturing** on an audio track (4-30), then
   capture for **30-60 seconds**. Note the track, displayed speed and retry
   count. A left-trigger mstats snapshot during that interval is useful.
5. Press **B** and wait for **STOPPED / READY**. Press Left/Right until the
   controls explicitly say **Y Save log**. Press Y and wait for the displayed
   **Report saved: /KUI/probes/pNNNN/diagnostics.txt** path and READY. Retrieve
   that exact report before starting another operation or rebooting. Y on the
   capture page means Verify, not Save log.

Upload the newly saved `diagnostics.txt`; raw track files are unnecessary for
this timing test. Its header should identify `0ef58878ccdd`. The important
sections are `TIMING resume` and `TIMING capture`, and the checkpoint, track
and Stop lines that establish what was measured. The full capture phase must
contain only new audio bytes to call it an audio-only result. The phase totals
exclude setup and the earlier prefix reread; do not combine those rates.

One successful run is sufficient to choose the next change. Do not repeat the
data-track baseline or complete a full MDK2 rip just for profiling. If the
existing audio checkpoint has changed, use its reported state when assessing
the result; the plan's byte count is evidence from the supplied log, not a
claim about the card's current contents.

## Use the result to choose the next change

| Observation in the new trace | Next action |
| --- | --- |
| Audio capture is near 50 KiB/s, optical bucket dominates, no application retries | Split and optimize optical command work first |
| Many raw mismatches, read failures or one-sector recovery calls | Investigate the exact FAD, track type and error cause before changing the transfer loop |
| SD write-call throughput drops substantially from the existing roughly 802 KiB/s baseline | Investigate card/filesystem allocation and write behavior separately |
| Resume time is mostly SD reads, with a smaller hash share | Avoid the unconditional prefix reread through a future checkpoint-resume design |
| SHA-256 is costly during resume/verification | Measure and optimize that phase independently; capture's 8-11% does not predict its share there |

The current build cannot distinguish first-read, second-read and polling costs.
After the audio sample, implement a single SD update with per-track timing and
optical subtimers: sector-mode setup, each read command, buffer checks/copy, and
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
