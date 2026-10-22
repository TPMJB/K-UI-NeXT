# Single-read capture: short console test

This update changes capture behavior: one PIO read per block, continuously
serviced between periodic runnable scheduler yields. It removes the duplicate
capture read and the sleep after every busy firmware status. It keeps transfer
length checks, guards, data-sector EDC, bounded retries, Stop, and final saved-file
CRC32/SHA-256 verification. Identification samples and diagnostic probes remain
paired; existing disc identities, checkpoint v1 jobs and bootstrap CDs work.

## Install and test MDK2 audio

1. Download **sd-update** from this revision's successful Diagnostic build.
   Keep the previous runtime on the PC. With the console off, replace only
   `/KUI/runtime.kui` with the artifact's `KUI/runtime.kui`.
2. Boot the **existing bootstrap CD** without B. Confirm **SD runtime** and
   the commit's first 12 characters from the artifact's `build.json`.
3. Insert MDK2, close the lid and press **X Resume latest**. The latest supplied
   log ends with **93414384 committed bytes** in
   `/KUI/dumps/df838eac34967ae16-0002`; its next capture FAD is **70040**, track 4.
   If the selected job differs, record it and keep the existing folders.
4. Allow **Checking saved prefix** to finish. This pass is unchanged: that
   89.09 MiB prefix may take about **4 minutes 23 seconds**, plus identification,
   based on the last measured prefix rate. Measure capture only after the
   display says **Capturing**. A failure should produce a report; keep it.
5. Capture audio for **60-90 seconds**, recording the track, displayed rate and
   retry count. Only about 9.70 MiB remain in track 4, so reaching track 5 or
   later is expected if faster. Per-track summaries separate these intervals.
   Check that scrolling and the left-trigger memory snapshot still respond.
6. Press **B once**, release it, and wait for **READY** and
   **Report saved: /KUI/probes/pNNNN/diagnostics.txt**. Record that path.
   Upload that report; track files are not needed for this first speed check.

The automatic report should say `Report trigger: auto resume` and
`Capture result: stopped`. Under **OPTICAL capture**, expect `policy=single`,
`read2 calls=0 bytes=0`, and no transfer-size failures (`opt short=0`). Setup
still reports `policy=paired` and two reads. Ordinary cancellation is logged
separately from retries and is expected at Stop. If transfer-size mismatches,
new read failures or unresponsive controls appear, stop and send the report.

An earlier capture Stop does not cancel its report. A new B press during
**SAVING LOG** cancels that save. For a failed/cancelled save, switch to the
diagnostics page and use **Y Save log**; Y on capture means Verify. Wait for the
exact saved path before shutting down. Existing reports and captures are kept.

## Compare and close correctness checks

The baseline on runtime `92484724c538` is **57.00 KiB/s** over new MDK2 audio,
with zero application retries. Compare the new capture and per-track averages,
not the prefix-check rate or only the highest display reading. Different FAD
ranges and track transitions prevent a perfectly controlled A/B comparison;
repeat a fixed range only if the first result is ambiguous. No new baseline
full rip is needed. A substantial improvement with `read2=0` and much less
scheduler waiting supports the targeted fix; no fixed 640 KiB/s is promised.

After this short result, use one short **data-track** capture to check EDC and
speed, then complete a mixed-track dump with final console readback and the PC
verifier. Keep the completed Sword of the Berserk dump for comparison; its
track 3 PC verification remains pending. Matching saved-file CRC/SHA proves
storage consistency, not an independent reference match or CDDA offset accuracy.

## Reading the timers

`TIMING` splits setup, resume, capture, verification and finish. `TRACK` reports
only this operation's capture interval for a track, including checkpoint/close.
`OPTICAL` is detail already inside the core disc time. Its mode, buffer, read1,
read2 and other buckets do not overlap. Each read's submit, poll, wait, abort
and other buckets are nested inside that read: **do not add the levels**.

`wait_us`/`waits` now measure actual periodic scheduler yields, not every busy
status. Poll calls still include firmware service; deadlines and cancellation
are checked on every command-loop iteration. A 2 ms service quantum is not a
promise about how quickly a firmware syscall or another scheduled thread returns.
Counters are fixed-size, and reporting happens after the operation. Automatic
report SD writes are outside the capture timers. Short/overreported successful
transfers are rejected before copying bytes to the capture core.
