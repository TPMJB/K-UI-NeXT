# MDK2 optical timing: next console test

This SD update measures the delay inside the optical read routine and saves
capture reports automatically. The earlier `0ef58878ccdd` runtime measured
57.46 KiB/s during MDK2 audio capture, with 88.75% inside that routine. It did
not separate the two reads, mode setup or waits. This update adds that detail;
it does not change the paired-read policy, hashes or checkpoint format.

## One short run on the existing dump

1. Download **sd-update** from this revision's successful Diagnostic build.
   Keep your previous `runtime.kui` on the PC. With the console off, replace
   only `/KUI/runtime.kui` with the artifact's `KUI/runtime.kui`.
2. Boot your **existing bootstrap CD** without B. Confirm **SD runtime** and
   the first 12 characters of the commit recorded in the artifact's `build.json`.
   The startup log now says capture/resume/verify save reports automatically.
3. Insert MDK2, close the lid and press **X Resume latest** on the capture page.
   The last supplied log used `/KUI/dumps/df838eac34967ae16-0002`, with
   75576816 committed bytes, partway through audio track 4. Keep existing jobs;
   if the selected job differs, record what is shown rather than deleting or
   renaming jobs to force selection.
4. Allow **Checking saved prefix** to finish. For that 72.08 MiB checkpoint,
   the previous measured rate suggests about **3 minutes 32 seconds**, plus
   disc identification. Faster resume is still a future change. If checking
   fails, keep the automatic failure report instead of starting over.
5. Once **Capturing** appears on an audio track (4-30), let it run for about
   **60 seconds**. Note the displayed rate, track and retry count. There is no
   need to finish the disc or repeat the earlier data-track baseline.
6. Press **B once**, then release it. After checkpointing and closing capture
   I/O, the status changes to **SAVING LOG**. Wait for both **READY** and
   **Report saved: /KUI/probes/pNNNN/diagnostics.txt** before powering off or
   removing the card. Record that exact path; each report gets a fresh folder.

Upload that `diagnostics.txt`. Its header should say `Report trigger: auto
resume` and `Capture result: stopped` for a normal short test. It should include
`TIMING resume`, `TIMING capture`, `TRACK T04 ...` (or the actual audio track)
and `OPTICAL capture`. Raw tracks are unnecessary for this timing test.

The earlier B press stops capture and does not suppress its report. A **new** B
press during SAVING LOG cancels the log save. If saving fails or is cancelled,
the capture result stays separate and the display offers manual retry: switch
to diagnostics and use **Y Save log**. Y on the capture page means Verify.
An incomplete save may leave `diagnostics.tmp`; only the published
`diagnostics.txt` is reported as saved. Do not deliberately fill the card or
remove it mid-write for this test; report failure cases are exercised on host
filesystem images.

## How the measurements fit together

| Summary | Meaning |
| --- | --- |
| `TIMING` | Existing nonoverlapping setup, resume, capture, verification and finish categories |
| `TRACK` | This operation's capture interval for that track, start/next FAD, successfully written bytes and category times; includes checkpointing and close |
| `OPTICAL setup` / `OPTICAL capture` | Detail inside raw calls during identification or capture, kept separate |
| `mode_us`, `buffers_us`, `read1`, `read2`, optical `other_us` | Nonoverlapping children of optical `wall_us` |
| Each read's `submit_us`, `poll_us`, `wait_us`, `abort_us`, `other_us` | Children of that read's elapsed time, including the firmware server calls and actual scheduler waits |
| `max_us`, `max_fad`, `max_sectors` | The slowest observed command and its requested range |
| Outcomes, mismatches, guards and refused requests | Distinguish successful commands, ordinary Stop, failures, timeouts, unstable/underfilled data and refusal after unsafe recovery |

Command submit/poll/wait totals are already included in the first/second read
totals, which are included in the optical total and the core's disc category.
**Do not add these levels together.** Timer calls and work outside the driver's
measured interval can leave a small difference from the outer disc category.
Polling/wait counts include abort draining when an operation stops or times out.
Command bytes count each successful physical request; optical bytes count a
successful paired request once. A cancelled request contributes time but no
successful bytes. Track bytes need not equal checkpointed bytes after a storage
failure; the separate committed count remains authoritative.

Counters use fixed memory. Per-track and detailed optical summaries are printed
only when the operation ends; there are no new per-chunk logs or SD writes.
Report creation runs after the measured operation and is outside its timers.
Main-RAM usage will rise slightly because of the additional counters.

## Acceptance and following work

The test succeeds as a measurement when it identifies the expected SD build,
validates the existing prefix, appends audio, stops cleanly and automatically
saves the complete timing summaries. A readable failure report is also useful;
it is not a completed or verified dump. Capture may remain slow in this build.

Use the measured mode/first-read/second-read/poll/wait split to choose one
optical change. Compare identical data/audio ranges and output bytes before
claiming a speed improvement. Full MDK2 verification and a faster checkpoint
resume remain separate follow-up work. Keep the current partial MDK2 job and
completed Sword of the Berserk dump. No new CD burn is required.
