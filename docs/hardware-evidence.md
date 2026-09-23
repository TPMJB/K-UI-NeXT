# Hardware evidence

## Ripper destinations and reference results — 2026-09-23

The next UI update adds a saved destination (default `/Games`), title-numbered
job folders, named GDI descriptors, Advanced actions and a structured stream-CRC
badge. Only a full catalogue match is green; partial/reference-unavailable
results and saved-file readback remain separate. See [ripper controls](ripper-controls.md).

Both baseline suites passed on clean `54711b931554` before the limited
`capture.c` destination/metadata/result changes; the protected source hashes
matched before and after that baseline. Acquisition, DMA, command and retry
behavior remain unchanged. New host tests pass on FAT32 and exFAT for naming,
collisions, full-identity resume selection, legacy fallback, path bounds,
publication failure/recovery and structured reference results.

**Pending on hardware:** destination selection/persistence, one ordinary named
capture with its catalogue result, and complete saved-file checking on a PC.
The same bootstrap CD is used. No new optical benchmark or measured speed claim
is part of this change. A dedicated salvage workflow remains planned.

## Completed MDK2 on the restored menu — 2026-09-23

The owner's initial impression that speed was restored is now accompanied by a
completed log for runtime **8bae3efe7c2f**. Its [sanitized evidence](evidence/m15-mdk2-2026-09-23.json)
records **all 31 tracks captured and TOSEC FULL TRACK MATCH** using stream CRC32.
Capture took **1223.545181 s (20 min 23.5 s)** at **973.96 KiB/s**. CRC32,
automatic end readback Off, DMA and UI 2 Hz were active; no capture retry messages
appear. Saved bytes were not reread, and this job has no supplied PC verification.

The accepted same-disc Trip 12 averaged 1011.67 KiB/s. This run is **3.73% slower**,
adding 45.61 s of capture time. UI scheduled time is reported as **8.3%**, versus
4.9% in Trip 12. Its extra 45.032 s of UI scheduled time nearly matches the
extra 45.936 s of whole-operation wall time, supporting remaining UI overhead.
These overlapping timers cannot be added to the write/hash wall times. The
earlier first-shell 12.4% figure used another game, so that comparison does not
isolate the display-clear change.

This completed ordinary capture supports the restored-menu runtime. It does not
establish the later destination-browser package's hardware acceptance and does
not call for changing or rebenchmarking the accepted reader.

## M1.5 first UI report — 2026-09-23

The owner reports that the shell seems functional during an ongoing game rip,
with a highest observed speed of about **933 KiB/s**, and requests restoration of
the original K-UI menu design. [Recorded observation](evidence/m15-shell-first-ui-report-2026-09-23.json).
No completed-rip average, build identifier, effective options or new verification
log accompanied this message. It does not replace the accepted reader results or
establish a performance regression. The remaining UI checks are still pending.

The follow-up UI update restores the original assets/layout and the earlier KOS
store-queue screen clear. This is a source-level display correction; its effect
on physical-console throughput has not been measured. The reader is unchanged.


### Completed log received after that observation

The uploaded log identifies
runtime **24d5cfe735d7** and **The House of the Dead 2**, not the restored-menu
update. [Sanitized results and input hash](evidence/m15-house-of-the-dead-2-2026-09-23.json).
The raw log remains in the project conversation and is not published here.

- All three tracks captured; **TOSEC FULL TRACK MATCH** for US v1.001 [12S].
- **1,188,966,576 bytes in 1273.291252 s: 911.9 KiB/s**, or 21 min 13 s of
  capture. Including setup/finish: 1278.391955 s (21 min 18 s), 908.3 KiB/s.
- CRC32, end readback Off, DMA, UI 2 Hz. **15,795 DMA chunks and
  four PIO chunks**; the duplicate summary lines describe the same counts.
- UI scheduled time **159.235 s / 1278.394 s**, reported **12.4%**, versus 5.1%
  in Trip 11, 4.9% in Trip 12 and 4.1% in the accepted FAT32 Sword run. This
  supports increased UI overhead. It does not isolate the clear's cost or prove
  the newer runtime's effect. UI time overlaps the stage wall timers.
- Preferences saved, reloaded and applied in the same session; L mstats,
  benchmark Stop and capture auto-report save are evidenced. Reboot persistence
  and restored-layout appearance still need their own observations.

Saved tracks were **not reread**; the reference match uses captured-stream CRCs.
No PC/saved-file verification was supplied for this job. The initial benchmark's
unreadable-disc result precedes the successful capture. The current
next step at that point was installing **8bae3efe7c2f** on the existing card and
checking its appearance during ordinary use. Current pending named-output checks
are recorded above. No reader changes are called for by this log.


These results cover the uploaded logs, storage fixtures and user reports received on
2026-09-16 and 2026-09-17. They establish the specific checks below on the user's console;
they do not complete the full dumping milestone.

## MDK2 optical command timing and automatic report save

The untruncated `diagnostics(20260917-220946).txt` identifies SD runtime
`92484724c538`. It is a **manual report** containing the preceding automatic
save's success message, `Report saved: /KUI/probes/p0005/diagnostics.txt`.
Automatic report saving after controlled Stop therefore worked according to
the console log; the separately saved automatic report was not uploaded.

This run resumes the same MDK2 job, `/KUI/dumps/df838eac34967ae16-0002`, from
checkpoint 11 with **75576816 committed bytes**, exactly where the prior log
ended. The completed tracks 1-3 and partial track 4 pass the console's CRC32
and SHA-256 checks. The 72.08-MiB prefix pass takes **212.448470 seconds**, or
**347.40 KiB/s**. Saved-file read calls consume 160.577146 seconds; SHA-256
and CRC32 together consume 51.099671 seconds. These are resume costs, separate
from the capture measurements below.

New capture spans **audio track 4, FAD [62456,70040)**: **17837568 bytes**
(7584 sectors) in **305.611520 seconds**, averaging **57.00 KiB/s**. The final
committed count is **93414384 bytes**. Thus the profiling build establishes
essentially the same slow audio throughput as the preceding 57.46-KiB/s run;
it does not establish a speed improvement.

| Capture category | Seconds | Share of capture wall time |
| --- | ---: | ---: |
| Optical read callback | 270.600197 | 88.54% |
| Track-file writes | 21.921474 | 7.17% |
| SHA-256 | 11.033157 | 3.61% |
| CRC32 | 1.989986 | 0.65% |
| Checkpoint sync/publication | 0.050895 | 0.02% |
| Other | 0.015811 | less than 0.01% |

The new optical subtimers separate costs that the older logs combined:

| Optical operation | Elapsed seconds | Calls | Time inside scheduler waits |
| --- | ---: | ---: | ---: |
| First PIO read | 130.570325 | 238 | 124.779241 s across 15687 waits |
| Second PIO read of the same range | 138.971324 | 238 | 133.207952 s across 16684 waits |
| Raw mode setup | 0.005312 | 238 | Not measured separately |
| Buffer preparation, checking and copy | 1.033794 | — | Not applicable |

The two read commands together spend **257.987193 seconds inside scheduler
waits**, 84.42% of capture wall time and 95.34% of the optical adapter's timed
interval. The runtime calls `thd_sleep(1)` at each wait; the measured elapsed
averages are **7.954 ms** for the first read and **7.984 ms** for the second.
There are 66.91 and 71.10 polls per command on average, respectively, for
32-sector requests. This makes the repeated read and the frequency of waits
between firmware service calls concrete optimization targets. These averages
are not a distribution of individual sleeps, and elapsed wait time includes
time in which the drive can progress or other threads run: it must not all be
treated as recoverable overhead.

Simply subtracting the measured second-read time while holding every other
cost fixed gives **104.53 KiB/s**. That is an arithmetic comparison, not a
prediction: a single sequential read and a different polling policy can also
change the first read's latency and other costs. Removing SHA-256 alone cannot
explain or fix this result; both capture hashes together account for 4.26%.
Mode setup is only 0.00174% of capture time. SD write calls average
**794.63 KiB/s**, so this trace does not show track-file writes imposing the
57-KiB/s overall rate.

There are **zero application retries, comparison mismatches or buffer-guard
failures**. The extra optical request ends in the user's controlled Stop:
read 1 completes 238 times; read 2 completes 237 times and is cancelled once.
Its abort succeeds, and 237 successful 32-sector chunks are committed. The
single optical `fatal` result describes that cancellation, not an exhausted
sector retry or unrecoverable media error. Internal drive retries are not
visible in these counters. The initial initialization command's sense 6/40
is followed by successful identification, prefix validation and capture.

Main-RAM use/reservations and sampled peak stay at **900888 bytes**, with
**15876328 bytes available**. The allocator's 232 free bytes describe only
its existing arena; a further 15876096 bytes remain unclaimed main RAM.
This log supplies no evidence of memory pressure or growth during the run.

[Machine-readable results](evidence/mdk2-optical-waits-2026-09-17.json) retain
the upload's SHA-256, exact phase/track/optical counters, prefix hashes and
derived rates. All timing groups and byte/FAD continuity checks reconcile.
MDK2 remains incomplete, and the uploaded log alone does not independently
verify the saved track bytes or an uninterrupted/reference dump match. The
profiling test has answered its question; the next hardware round should
measure the optical performance changes, using the existing bootstrap disc,
rather than repeat this unchanged baseline. See the [optical test guide](optical-test.md).

## MDK2 isolated audio capture and successful checkpoint resume

The untruncated `diagnostics(10).txt` comes from SD runtime `0ef58878ccdd` in
a fresh runtime session. It resumes the same MDK2 job as `diagnostics(9).txt`,
`/KUI/dumps/df838eac34967ae16-0002`, from checkpoint 9 with **59620848 committed
bytes**. Tracks 1-3 and the saved track-4 prefix pass the console's CRC32 and
SHA-256 checks before any new writes. Capture then starts at **track 4, FAD
55672**, writes **15955968 new audio bytes** (6784 sectors), and stops with
**75576816 committed bytes**. The counts reconcile exactly across the two logs.

The entire new capture phase is audio. It lasts **271.160576 seconds** (4 minutes
31 seconds), averaging **57.46 KiB/s**. This directly establishes slow audio
capture in the reported 50-KiB/s range; the log does not record individual
instantaneous screen readings or establish the earlier track-5 duration.

| Capture category | Seconds | Share of capture wall time |
| --- | ---: | ---: |
| Optical read callback | 240.661598 | 88.75% |
| Track-file writes | 19.840484 | 7.32% |
| SHA-256 | 8.596927 | 3.17% |
| CRC32 | 1.983868 | 0.73% |
| Checkpoint sync/publication | 0.065075 | 0.02% |
| Other | 0.012624 | less than 0.01% |

SD write calls average **785.36 KiB/s**, compared with 801.95-804.21 KiB/s in
the earlier runs. The much slower overall capture rate is dominated by the
optical callback. Its combined timer still cannot identify how much belongs
to mode setup, the first/second read, polling or buffer checks. SHA-256 is
3.17% of this capture; removing it alone would not resolve the slowdown.
There is no capture EDC bucket because the new bytes are all audio; the setup
phase's EDC checks are separate.

The preceding **resume prefix check** takes **167.466368 seconds** (2 minutes
47 seconds) for 56.86 MiB, averaging **347.67 KiB/s**:

| Resume category | Seconds | Share of resume wall time |
| --- | ---: | ---: |
| Saved-file reads | 126.206689 | 75.36% |
| SHA-256 | 34.167135 | 20.40% |
| CRC32 | 6.431161 | 3.84% |
| Other | 0.661383 | 0.39% |

The SD reads alone average **461.33 KiB/s** within those calls. Hashing adds
about 40.60 seconds to the prefix pass. These are measurements of this build,
not an A/B comparison with the other ripper. The 12.615276-second setup phase
is separate from both resume and capture. Every phase's categories sum exactly
to its wall time.

The final `CMD 16 CANCELLED` is the controlled Stop; 213 optical calls versus
212 successful writes account for the cancelled request. The log reports
**zero application retries**, and memory use, sampled peak and allocator
counters remain unchanged at the earlier values. This does not expose internal
drive retries. The initial `CMD 24 FAILED` is followed by successful TOCs,
identification, resume and capture; it is not a capture retry failure.

This establishes checkpoint/prefix validation and continued hardware writing
after a new runtime startup, followed by another controlled Stop. The log
does not independently identify how the console was restarted. MDK2 remains
incomplete; final saved-file verification and equality with an uninterrupted
or independent reference dump are still untested for this resumed job.

[Machine-readable results](evidence/mdk2-audio-resume-2026-09-17.json) retain
the input fingerprint, exact counters, prefix hashes and continuity checks.
The requested baseline test is complete. The [next steps](performance-test-plan.md)
are optical subtimers/automatic reports and a versioned fast-resume design;
another identical baseline or full rip is unnecessary. This evidence update
does not change the runtime.

## MDK2 capture reaching high-density audio

The follow-up `diagnostics(9).txt` is an untruncated log from SD runtime
`0ef58878ccdd`, MDK2 job `/KUI/dumps/df838eac34967ae16-0002`. It completes tracks
1-3 and stops inside **audio track 4**. The saved prefix is 59620848 bytes
(56.86 MiB): 50782032 data bytes and 8838816 audio bytes. Track 4 has 3232
committed sectors (7601664 bytes); the next FAD is 55672 if this checkpoint is
still the newest job state. The preceding audio track 2 contributes 526 sectors.

The measured capture lasts **409.071777 seconds** (6 minutes 49 seconds), with
an overall rate of **142.33 KiB/s**. This mixes the earlier data tracks with audio;
the file has no per-track durations or instantaneous speed records, so it does
not directly measure the user's on-screen 50 KiB/s observation or their separate
roughly-20-minute duration report.

| Capture category | Seconds | Share of capture wall time |
| --- | ---: | ---: |
| Optical read callback | 294.224269 | 71.92% |
| Track-file writes | 72.602139 | 17.75% |
| SHA-256 | 33.542500 | 8.20% |
| CRC32 | 6.281438 | 1.54% |
| Sector EDC | 2.005522 | 0.49% |
| Checkpoint sync/publication | 0.354948 | 0.09% |
| Other | 0.060961 | 0.01% |

The counters sum exactly to the phase wall time. SD write calls average
**801.95 KiB/s**, close to 804.21 KiB/s in the earlier track-1 sample. Main-RAM
use, sampled peak and allocator counters remain at the same values as that
sample. There are **zero recorded application retries**. `CMD 16 CANCELLED`
occurs at the controlled Stop; one extra optical call has time but no committed
bytes (795 disc calls versus 794 successful writes). It is not evidence of a
failed sector or an exhausted retry loop. Internal drive retry activity is not
separately visible in this instrumentation.

The larger optical share, stable write-call throughput and unchanged memory
support investigating the optical path through the audio portion first. The
callback still combines both reads, mode setup, polling and comparisons, so
the mechanical transfer rate or one specific driver operation cannot yet be
identified as the cause. SHA-256's 8.20% is not the dominant cost in this run.

As an explicitly conditional estimate, charging data bytes at the earlier
track-1 sample's total per-byte time leaves about 143.86 seconds for the audio
bytes, equivalent to about 60 KiB/s. Different data-track speeds, command sizes
and run conditions can invalidate this assumption. It is consistent with slow
audio capture, not a replacement for a direct audio-only measurement.

[Machine-readable follow-up results](evidence/mdk2-audio-entry-2026-09-17.json)
retain exact counters and this estimate's limitations. The isolated audio
measurement above now supersedes the conditional estimate for choosing the
next change. It resumes this exact prefix and measures resume and capture
separately. See the [performance test plan](performance-test-plan.md).

## MDK2 capture timing and resume overhead

`P1 diagnostics.txt` and `P2 diagnostics.txt` identify SD runtime
`0ef58878ccdd`, MDK2 and the same job `/KUI/dumps/df838eac34967ae16-0001`.
P2 starts with the exact bytes of P1 and adds another log-save action. They are
two snapshots of **one** operation, not separate capture trials. Both logs are
untruncated. The operation stopped during data track 1 after 9120 sectors
(21450240 bytes) and **112.024259 seconds** of measured capture, averaging
**186.99 KiB/s**, with zero recorded application retries.

| Capture category | Seconds | Share of capture wall time |
| --- | ---: | ---: |
| Optical read callback | 70.794132 | 63.20% |
| Track-file writes | 26.047232 | 23.25% |
| SHA-256 | 11.945064 | 10.66% |
| CRC32 | 2.269059 | 2.03% |
| Sector EDC | 0.843589 | 0.75% |
| Checkpoint sync/publication | 0.104441 | 0.09% |
| Other | 0.020742 | 0.02% |

The buckets add exactly to the capture wall time. Disc, EDC, write and both
hash buckets each account for the same 21450240 logical bytes in 285 chunks of
32 raw sectors. The optical callback includes both guarded PIO reads, mode
setup, polling, comparisons and scheduling. This measurement does not separate
those costs or establish that optical media transfer alone consumed 63.20%.

SHA-256 is a measurable cost but not the largest contributor in this trace.
Subtracting all measured SHA-256 time while holding other costs fixed gives
209.31 KiB/s, an idealized 11.94% speed increase, not a prediction of an actual
modified build. It does not account for the gap to the previously reported
DreamShell capture rates. SD write calls alone averaged 804.21 KiB/s for this
sample; that is neither a complete-rip rate nor a hardware maximum. No saved-file
verification or resume timing breakdown is present in these two logs.

Startup, operation start, controlled Stop and log-save snapshots all report
881976 bytes of main RAM used/reserved, 15895240 available, a sampled peak of
881976, and heap in-use/system counts of 88948/91708 bytes. These snapshots show
no growth or main-RAM pressure in this short operation; they do not establish
long-run memory behavior.

The user separately reports a longer run, roughly 20 minutes before noticing
capture near 50 KiB/s around track 5. The TOC identifies tracks 1/3/31 as data,
and 2 plus 4-30 as audio. The severe reported slowdown therefore occurred in
the audio portion, which this track-1 trace does not measure. The later mixed
data/audio and isolated audio logs are analyzed above. Photos show a
later checkpoint with about 70 MiB saved and a resume prefix check near
355-365 KiB/s, but not completion of that resume or its capture timing.

Current Resume deliberately rereads all committed bytes, validates CRC32/SHA-256
and rebuilds incremental hash contexts before appending. At 360 KiB/s, 70 MiB
takes about 199 seconds for that prefix alone. This is a separate source of
waiting from slow disc capture. Checkpoints currently retain finalized hashes,
not resumable SHA-256 state; a faster checkpoint resume would require a format
and validation-policy change. No checks have been removed by this evidence
update. Full saved-file and independent-reference verification remain separate.

[Machine-readable timing results](evidence/mdk2-timing-2026-09-17.json) retain
input fingerprints, exact counters, memory values, assumptions and limitations.
The isolated audio and resume baseline is now complete. Next performance
evidence should separate optical command work within the callback; hashing
and SD I/O must remain separately accounted for. See the
[test plan](performance-test-plan.md) for the next runtime work.

## First completed capture: Sword of the Berserk

The user reports that **SWORD OF THE BERSERK GUTS RAGE** completed capture and
saved-file verification on SD runtime `2657a97031e3`. The uploaded manifest
identifies that build/title and declares `complete` and `saved_data_verified`.
This build publishes the final manifest/GDI only after the complete SD reread
passes. Both checkpoints and the GDI layout support the reported completion.
The uploaded raw bytes independently establish the checks for tracks 1 and 2
below; track 3 was intentionally omitted.

| Track | Sectors | Bytes | CRC32 | Independent PC result |
| --- | ---: | ---: | --- | --- |
| `track01.bin` | 776 | 1825152 | `bcec7767` | Length, CRC32 and SHA-256 match the manifest and both checkpoints |
| `track02.raw` | 526 | 1237152 | `0ff934e3` | Length, CRC32 and SHA-256 match the manifest and both checkpoints |
| `track03.bin` | 504150 | 1185760800 | `2cfb5dcb` | Not supplied; values are recorded by the console, not independently recomputed |

The three declared tracks total **1188823104 bytes**. Their bounds, sector sizes,
type-change gap exclusion and `disc.gdi` entries agree with the declared profile.
This verifies metadata consistency, not agreement with an independent disc catalog.

Both 4096-byte checkpoints pass independent Python CRC32 and structure checks,
including identity/build, reserved bytes, track bounds/order and alternating
sequence parity. `checkpoint-b.bin` is sequence 126 with 503808 track-3 sectors;
`checkpoint-a.bin` is sequence 127 with all 504150 track-3 sectors and hashes
matching the completed manifest. Both record **zero retries**. The earlier
track-3 prefix hash could not be checked without track 3. Valid checkpoints on
their own do not demonstrate a hardware Stop/reboot/Resume cycle.

`diagnostics(7).txt` is from **addd439aaea5**, not the capture build. Its bytes
are identical to the previously examined `diagnostics(6).txt`; it contains the
earlier nine-sample disc probe and storage probe, with no capture/mstats/timing
output. `storage(3).bin` and `storage(3).json` are also identical to the previous
storage uploads. Rechecking them passes all 2097325 expected bytes and CRC32
`a70f77ca`, but establishes no additional independent console session.

The user reports capture around 210-220 KiB/s. Earlier photos show capture near
209 KiB/s and saved-file verification near 311 KiB/s, with estimated main RAM
around 860 KiB and a sampled peak around 869 KiB. These are spot observations,
not whole-operation averages or detailed allocator measurements. The original
capture build has no timing instrumentation. Timing build `0ef58878ccdd` has
since supplied the MDK2 measurement above; this evidence change adds no runtime code.

[Machine-readable results](evidence/sword-of-the-berserk-2026-09-17.json) retain
the original upload names, lengths, SHA-256 fingerprints, per-track checks and
decoded checkpoints. No game track bytes are stored in the repository.

Still needed: local PC verification of track 3, a compatible independent
reference comparison and final verification of a complete resumed hardware
dump. MDK2 now supplies data/audio timing and a successful checkpoint resume
through new audio writes, followed by controlled Stop, as described above.
If the original capture log remains available, save/upload the report whose
header says `K-UI SD runtime 2657a97031e3`; do not repeat the full capture just
to replace a lost log. Keep the completed dump for later hash comparisons.

## Capture runtime host coverage

The SD capture runtime implements full-track GDI capture, CRC32/SHA-256 saved
file reread, checkpointed controlled resume and an mstats action. Host FAT32 and
exFAT image tests exercise complete six-track output, independent Python hash
verification, early/middle/late/verification Stop, resumed-versus-uninterrupted
equality, a damaged newest checkpoint, an uncommitted suffix, bounded retries,
media-change and storage-failure handling, corrupt-prefix and wrong-disc refusal,
and preservation of existing completed jobs. These are synthetic host tests.

The first physical completion and MDK2 resume through new writes are documented
above. Full PC track verification, final verification of a complete resumed
hardware dump and an independent disc-reference match remain pending. Use
[the capture guide](capture-test.md) with the existing bootstrap CD; the earlier
physical evidence below applies to the diagnostic builds identified there.

## SD runtime session and confirmed startup selection

Tested source: `addd439aaea504d765498f214a1e5409d5862fee`.
The [successful combined build](https://github.com/TPMJB/K-UI-NeXT/actions/runs/35147067592)
contains the CD bootstrap and SD runtime for this build. Keep using the existing
disc; this evidence update does not require a new binary or another burn.

The uploaded `diagnostics(6).txt` starts with `K-UI SD runtime addd439aaea5`
and records `Running SD runtime build addd439aaea5`, with successful disc and
storage operations. After initially reporting a CD bootstrap heading, the user
clarified that holding B selects CD bootstrap and booting without touching any
buttons reaches SD runtime. This resolves the apparent discrepancy and confirms
the normal SD handoff and manual fallback selection.

Source inspection shows both the title and saved report header use the same
compile-time role. The CD shows its own role during startup even on a successful
load; it remains there when loading is skipped or fails. The user also reports
that cold boots work. No count was supplied, so this is recorded as a successful
user-reported check rather than a documented five-boot sequence.

The user then completed the three-step missing-file/checksum/restoration check
on the same disc. With the runtime renamed, the console reported that
`runtime.kui` failed or was missing (wording recalled approximately). The
`bad-checksum.kui` test produced `Runtime Checksum mismatch`. Renaming the good
file back restored normal operation. These are user-reported hardware passes
for missing-file detection, payload-checksum rejection and good-runtime
restoration. No new log or screenshot was supplied, and diagnostic probes or
controller responsiveness during automatic fallback were not separately reported.

| Check | Observed result |
| --- | --- |
| SD execution handoff | Confirmed by the runtime log and the user's no-buttons startup report |
| Hold B at startup | User confirms CD bootstrap selection; fallback probe results were not separately supplied |
| Missing runtime file | User-reported pass: failed/missing-file explanation shown |
| Bad payload checksum | User-reported pass: `Runtime Checksum mismatch` shown |
| Restore good runtime | User-reported pass: normal operation returns after renaming the original file back |
| Cold boots | Dozens, with no controller problems (user report, 2026-09-20) |
| Video mode | 640x480 NTSC interlaced, buffered |
| Disc TOCs | Both density regions parsed; three tracks |
| Raw disc reads | All nine samples passed repeat and buffer-guard checks |
| Data-sector comparison | All six data samples matched independent cooked reads |
| Audio samples | Three passed repeat/guard checks; offset and subchannels were not validated |
| Storage layout | exFAT, MBR volume start 2048, 249997312 volume sectors, 131072-byte clusters |
| Console storage probe | `/KUI/probes/p0009`, 2097325 bytes; write, remount and reread passed |
| PC storage verification | Exact length, every expected byte and CRC32 `a70f77ca` passed |

The initial `CMD 24 FAILED` with sense `6/40` was followed by successful
initialization and the complete disc probe. The TOCs, sample addresses and
sample CRC32 values match the prior session; this does not establish a second
disc layout or independent full-disc accuracy.

The PC check ran `tools/verify_probe.py` on temporary copies of the uploaded
`storage(2).bin` and `storage(2).json` using the standard fixture filenames.
The original uploads were unchanged. The storage pattern is deterministic,
so its hash matching previous successful fixtures is expected.

| Uploaded file | Bytes | SHA-256 |
| --- | ---: | --- |
| `diagnostics(6).txt` | 2514 | `d2a93a39de88d08c3395d40c6031e8cbb29427c4d7301eec5cb029303d5310b3` |
| `storage(2).bin` | 2097325 | `e19146d42e4161db7890e39427fc5ccb95ef1da2a97b910c224d03d8cda6daff` |
| `storage(2).json` | 115 | `618b9b5553f0932666bcf0363e235ba7d5c1884974106cc353afd886f4b44849` |

## Earlier evidence

- `99a069777c7e`: both TOCs, nine raw samples, six data comparisons and the
  exFAT console/PC storage checks passed. The first five uploaded diagnostic
  files are progressively longer snapshots of one session, not five boots.
- `cf8210bc5442`: the user explicitly confirmed the rendering fix removed the
  visual errors. Disc samples and exFAT console/PC checks passed again.
- The user reported a FAT32 console pass on a 32 GB card. Its log and fixture
  remain unavailable because they lack a compatible reader. FAT32 SD runtime
  loading has not been established on hardware.

## Remaining checks on the same bootstrap disc

These are unconfirmed by the supplied logs and user reports. If they were already run,
record the observed outcome rather than asking for another burn or repeating
successful work.

**Update 2026-09-20: most of these are now done.**

- **Done:** all five rejection fixtures fail safely on hardware, each with its own reason and a
  usable fallback ([evidence](evidence/m12-runtime-rejection-2026-09-20.json)); dozens of cold boots
  with no controller problems; a second retail disc with a different TOC (MDK2, 31 tracks); full
  PC/reference verification of complete dumps, including a dump resumed twice
  ([handoff](HANDOFF-disc-reader.md)).
- **Hardware recorded:** NTSC console, motherboard reported as "V1A" (most likely VA1), RF video
  cable, SanDisk 128 GB microSD (Endurance line), exFAT. The SD adapter model is not recorded.
- **Done since:** the lid opened mid-capture stops cleanly and resumes to a TOSEC-verified finish
  ([evidence](evidence/lid-open-omikron-2026-09-20.json)); a disc with physical damage retries a
  fixed 10 times, pins the exact bad sector and stops with the partial job kept
  ([evidence](evidence/scratched-omikron-retry-2026-09-20.json)).
- **Done since:** FAT32 end to end: the runtime loads from a FAT32 card, the storage test passes,
  and a whole disc rips byte-exact and PC-verified
  ([evidence](evidence/fat32-sword-dma-2026-09-20.json)).
- **Still open:** a card that fills up mid-capture (host tests only).
