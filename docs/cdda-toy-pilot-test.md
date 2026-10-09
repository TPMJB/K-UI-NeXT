# Toy Commander refill-window measurement

Install only `pilot/15-toy-gd-two-sector.kui`. This controlled test keeps the
continuous stereo ring and receive-paced SCI with status reuse disabled.
The existing two-sector cap, refill ownership checks and acknowledged STOP/retirement
fence remain in place. This build retains the 16 ms budget for admitting extra
audio refill work and the four-quantum maximum.
It retains the same reader, card deadline and five-millisecond copy reserve,
plus the conservative capture bound and pressure diagnostics. Two new report
values measure the observed refill window before the last refused copy.

The `62d9273595dd` report records five copy-reserve recoveries, compared with
nine in the prior `0a898d37a2de` run. The user reports much better audio,
mostly continuous until later interruptions. At the latest denied plane copy,
the next half had only 6,164 of 16,384 frames committed. Its conservative
remaining write window was 4.961 ms, just below the unchanged 5 ms reserve.
Raw callbacks averaged 4.566 ms, essentially unchanged. These counters do not
show when refill first became possible or how many visits served that half.

Independent stereo/PCM tests reproduce refill starvation with slower updater
calls, delayed publications, occasional long gaps and slow-read outliers.
The 16 ms policy fills ahead of the earlier modeled gaps in all twelve tested
timing phases. Slower and irregular schedules can still fail. Experiments
with longer admission or retained cursor history also regress previously
passing schedules, so neither is included. This candidate changes measurement
only. It does not claim to fix the remaining audio interruption or video holds.

The native ARM driver publishes the two cursor words separately. Two changes
per channel prove bounded freshness, but do not make their capture times
equal. A mixed reading can exceed 64 frames while the physical voices remain
paired. Immediate phase recovery still requires circular separation beyond
64 frames plus the sample advance possible during the probe's age. Smaller
ambiguous differences are retried within the existing bounded probe.

**Ownership acceptance stays at 64 frames**, with two changed publications
per channel, both in the same half and within 50 ms. An ambiguous reading
does not advance the accepted cursor or grant a new half. Any remaining
write permission derives from the previous proof and expires at its existing
conservative half-entry deadline and 5 ms reserve. Persistent ambiguity still
reaches a bounded STOP recovery. The existing paired START is retained.

An accepted cursor's capture-time lower bound now follows the last observation
of the value two distinct changes back, independently per channel. The next
publication happened after that observation, and the current capture followed
that publication. Each timestamp precedes both SH reads; the older channel
bound is used. Observing the intervening changed value can occur after the
next hardware capture, so its timestamp cannot safely anchor that capture.
The unchanged global 50 ms probe expiry remains in force. This retains a
conservative deadline while avoiding an unnecessarily old initial baseline.
Some slow or poorly observed schedules still require recovery; this does not
guarantee that every refill can meet its deadline.

**PILOT PAGE 5** retains the GD data-step measurement. **PILOT PAGE 6**
classifies successful internal restart attempts and measures sampled periods
when an owned data request prevents a needed refill. New **PILOT PAGE 7**
records copy pressure, raw-read duration, observed-window age and visit count.
The worker snapshot is version 7, 448 bytes. Its first 110 words retain their
order; the last two formerly reserved words now carry the window measurements. The
[continuity review](evidence/cdda-toy-continuity-2026-10-08.md) records the
baseline evidence and what this comparison can resolve. The earlier native
CHECK mapping and working Start/menu corrections are retained.

The exception is limited to the two verified SDK veneers' exact 16-byte
scratch areas: CHECK output and REQ_STAT36 parameters. Captured caller PR/SP,
access direction and linked stack bounds are required. The allowance exists
only during the serialized call. Other worker memory stays protected, and
normal wrong-token handling retains the genuine owned handle. A regression
executes the production dispatcher/map with the real GD core to reproduce
both the old failure and corrected retirement, including the chained status
request. No handle is forcibly cleared.

Refill uses up to four single-sector quanta per visit, admitting extra
work only while less than nominally 16 ms has elapsed since service entry.
Each card operation restores interrupts before the next. A slow first read
that exhausts this budget prevents additional reads; the budget can overrun by one quantum
and is not a new card timeout. Data reads, changed generations, no progress,
bus deferrals and completed half fills stop the burst. The two existing updater
visits remain, without assuming they occur 60 times per second. Host tests
cover 30 Hz and irregular cadences with data-priority interruptions.
Longer synchronous bursts can delay video work or the game's next data request;
the next hardware run must assess that tradeoff. Sixteen milliseconds is an
admission threshold, not a hard duration limit for a visit.

The existing raw buffer now keeps its exact LBA and request generation,
reusing a sector's remainder at a half boundary or a deferred copy. The
single-sector callback and 2,352-byte private raw buffer are unchanged.
Ring playback removes the finite successor's setup/START boundary; it does
not assume that a raw read will meet a deadline merely because it began in
the inactive half.

The earlier [recording and interrupt review](evidence/cdda-toy-recording-irq-2026-10-08.md)
records the scratch-mapping diagnosis.
The [SWAT design comparison](cdda-toy-dreamshell-comparison.md) explains the
continuous-buffer and service differences. No SWAT implementation was copied
or translated. Earlier overhaul/playback reviews remain historical evidence;
the former claim that native IRQs always use a separate stack was incorrect.

This is a manually installed Toy Commander test. It uses the game's
own ARM sound driver and tracked sound-heap records. It keeps the working 1.8.5
runtime and ordinary reader behavior unchanged. Use the same complete original
Toy Commander image and card that passed preflight 13; do not repeat tests
00–14 for this unchanged image.

The pilot tests whether disc music continues alongside the game's effects and
movie sound while preserving the working Start transition. It does not
establish compatibility with another game. Each channel's ring holds 32,768
PCM16 frames in two halves of 16,384 frames, approximately 743 milliseconds
total at 44.1 kHz. Audible continuity and stereo playback remain console
observations. A SH-4 path that stops servicing the worker can leave the
hardware loop playing previously buffered audio.

Both stereo halves must be filled before START. A refill window requires two
changed cursor publications from each channel within 50 ms, both cursors in
the same half and no more than 64 frames apart. Each cooperative observation
attempt admits at most 16 observations and nominally 0.5 ms of polling;
one already-admitted observation can exceed that budget, and each G2 access
retains its separate bounded transaction. Refills retain a nominal 5 ms reserve
for the two bounded stereo copies and overhead, recheck after a
card read, and refuse an expired or active-half destination. Missed deadlines
or ambiguous progress request STOP; the worker retains the allocation and
does not refill/restart until STOP is consumed, a full-ring interval plus
the 20 ms guard has elapsed, and both ports are observed inactive.

The pilot now refreshes `GETSCD`, `REQ_STAT` and `DRIVE` from logical CDDA
state. PLAYING is drive `3`, audio `0x11`; PAUSED is drive `1`, audio `0x12`;
EOF is drive `1`, audio `0x13`; faults are drive `9`, audio `0x14`.
Positions are clamped to the admitted audio track. These describe command
state and programmed source progress, not independent speaker measurements.

## Install one file

With the Dreamcast powered off:

1. Preserve your working ordinary reader backup. The earlier instructions
   used `/KUI/apps/games/retail-boot-before-observe.kui`; use that same known
   working backup if you retained it. If the ordinary reader is currently
   installed and there is no backup, copy it to that path first. Do not
   replace a working backup with a diagnostic reader.
2. Keep the known working CDDA backup at
   `/KUI/apps/games/retail-boot-working-cdda.kui`. If it does not exist, copy
   the confirmed working `/KUI/apps/games/retail-boot.kui` there before
   replacing it. Keep this separate from the ordinary reader backup.
3. Keep the working 1.8.5 runtime at `/KUI/runtime.kui`. Leave the game folder,
   its GDI, all 15 track files and the existing card-path configuration
   unchanged. This ZIP contains no game files and installs no configuration.
4. Copy only `pilot/15-toy-gd-two-sector.kui` from this ZIP over
   `/KUI/apps/games/retail-boot.kui`. Extracting the ZIP by itself installs
   nothing on the card.
5. Safely eject, cold boot with **SCI**, and launch the same Toy Commander
   original raw image through the normal Games menu with **A**, the standard
   SCI reader.

This pilot supports the original 2,352-byte tracks and the standard reader
only. The converted **2048-byte copy** and **X/Y background readers** are not
implemented CDDA test paths. The retained 1.8.5 launcher still offers these
ordinary-reader choices; the pilot now refuses them immediately with their
specific reason, before selecting its resident or beginning detached storage
reads. When both
choices are unsupported, the screen reports both. Refusal detail is a bit
mask: `1` means X/Y reader, `2` means stored sector format, and `3` means both.
The earlier boot error `8`, detail `000B6B9C`, could describe those same
admission failures; that detail was the correct 748,444-byte executable size,
not a failed read or evidence of an SCI bandwidth limit.

The launcher checks the known original boot/image identity before installing
the title-specific hooks. A refusal is a result to report; it is not an
instruction to alter the GDI or game files.

## Run one intro

Let the intro run without manually skipping it. Note whether its video and
sound stay smooth, whether a freeze eventually skips the video, and whether
the earlier repeating short sound returns. These observations matter even
if the game reaches its menu.

At the first menu, note whether disc music is audible, then enter the report.
Keep this run focused on the natural intro so later gameplay does not add
unrelated recoveries to its counters. Extended gameplay and track-end checks
can follow once these measurements identify the intro's remaining problem.

Record the outcome in ordinary terms: intro behavior, menu music, loading
time, and the character of any gaps or repeated sound. Counter
values alone do not establish that the output was audible or stereo.

A worker fault should display the eight report pages automatically. If music
continues, hold **A+B+X+Y+Start** together to enter them through the early
return callback. Photograph the build ID and each page that is visible.
An original-game hang may still prevent the reset decision from running;
that is a distinct unresolved result. There is no numerical PASS target
before this pilot has run on hardware.

One natural-intro run is sufficient for this diagnostic. Preserve the working
Start/menu result; a separate skip test is not required for these added
counters. Use the original raw image and **A** throughout.

## Read the eight pages

The caption is **PILOT PAGE**. The displayed page numbers
are **0, 1, 2, 3, 4, 5, 6 and 7**. Each page has four `WORDS` rows with four hexadecimal
values per row. Pages repeat until power-off. A page remains for 900 video
frames: approximately 15 seconds at 60 Hz or 18 seconds at 50 Hz. A stopped
video scan can shorten this bounded display wait; this is not a wall-clock
timer calibration.

The exact word order is:

| Page | Row | Values from left to right |
|---|---|---|
| 0 | 1 | `magic`, `version`, `bytes`, `state` |
| 0 | 2 | `fault`, `generation`, `applied_generation`, `driver_generation` |
| 0 | 3 | `command`, `parameters[0]`, `parameters[1]`, `parameters[2]` |
| 0 | 4 | `position_fad`, `track`, `end_fad`, `main_begin` |
| 1 | 1 | `main_end`, `worker_end`, `stack_used`, `stack_fault` |
| 1 | 2 | `driver_bytes`, `driver_crc`, `driver_verified`, `sound_address` |
| 1 | 3 | `sound_bytes`, `sound_generation`, `service_calls`, `service_skips` |
| 1 | 4 | `service_gap_max`, `step_ticks_max`, `raw_calls`, `raw_bytes` |
| 2 | 1 | `raw_errors`, `copy_calls`, `copy_ticks_max`, `bank_fills` |
| 2 | 2 | `bank_starts`, `bank_ends`, `handoff_gaps`, `gap_ticks_max` |
| 2 | 3 | `start_waits`, `stop_waits`, `queue_errors`, `stale_actions` |
| 2 | 4 | `active_left`, `active_right`, `cursor_left`, `cursor_right` |
| 3 | 1 | `started_observed`, `finite_ends`, `shutdowns`, `sdk_init_result` |
| 3 | 2 | `retired_frames`, `filled_frames`, `queue_producer`, `queue_consumer` |
| 3 | 3 | `dma_busy`, `dma_suspended`, `hardware_loops`, `active_bank_writes` |
| 3 | 4 | `bus_last_result`, `bus_deferrals`, `updater_entries`, `updater_returns` |
| 4 | 1 | `pause_entries`, `pause_retries`, `pause_pumps`, `pause_retired` |
| 4 | 2 | `pause_detail`, `pause_max_attempts`, `cache_policy`, `service_visits_per_update` |
| 4 | 3 | `native_owner`, `native_work_token`, `gd_owned_command`, `gd_owned_token` |
| 4 | 4 | `native_gd_state`, `check_token`, `check_destination`, `check_result` |
| 5 | 1 | marker `47444D31`, fixed step `2`, `measured_read_steps`, `measured_sectors` |
| 5 | 2 | `read_ticks_total_modulo32`, `read_ticks_max`, `actual_sectors_max`, `timer_invalid_steps` |
| 5 | 3 | GD diagnostic `calls`, `requests`, `rejected`, `last_error` |
| 5 | 4 | four reserved zero words |
| 6 | 1 | recovery counts: invalid ports, phase beyond publication-age allowance, unobserved half, implausible progress |
| 6 | 2 | recovery counts: initial start proof, missing ready half, copy reserve, stream overflow |
| 6 | 3 | `data_blocked_calls`, `data_blocked_intervals`, `data_blocked_ticks_max`, `data_blocked_ticks_total` |
| 6 | 4 | `recovery_last_reason`, `recovery_last_gd_command`, `recovery_last_cursor_age`, `data_blocked_open` |
| 7 | 1 | `reserve_last_cursor`, `reserve_last_proof_age`, `reserve_last_sample_age`, `reserve_last_remaining` |
| 7 | 2 | `reserve_last_bank`, `reserve_last_bank_filled`, `reserve_last_fill_stream`, `reserve_last_site` |
| 7 | 3 | `raw_read_ticks_last`, `raw_read_ticks_max`, `raw_read_ticks_total`, `raw_read_timing_calls` |
| 7 | 4 | `reserve_last_bank_state`, `reserve_last_probe_age`, `reserve_last_window_ticks`, `reserve_last_window_calls` |

The telemetry header is magic `54595031`, version `00000007`, bytes
`000001C0`. Apart from version and size, the first 96 snapshot words retain
their existing layout. All-zero worker pages mean that valid worker telemetry was not
available; it does not mean that audio passed. The expected driver identity
is 20,740 bytes (`00005104`), CRC32 `70CCEEB2`; `driver_verified` records whether
the successfully loaded file matched before SDK installation. It is cleared
when the pilot shuts down, so a zero on the return report alone is not proof
of failed verification. The retained CRC and fill/start counts distinguish
that case. It is not a
readback hash of the installed ARM image. `main_begin` is `8CFD0000`, `main_end` is
`8D000000`, and `worker_end` must remain at or below `8CFE0000`.

Page 5 is appended from the resident's existing scratch block, separate from
the worker's extensions on pages 6 and 7. Its first two words should be `47444D31`
and `00000002`, and `actual_sectors_max` should be at most two. Measurements
cover GD EXEC service dispatches that credit data sectors, under the existing
dispatcher mask. They exclude the surrounding IRQ hook entry/exit and the
trailing clock-profile check. Both ends validate the admitted FRQCR/TMU0
profile for retained timings; an invalid profile, zero start counter or zero elapsed time increments
`timer_invalid_steps`. Timing totals wrap modulo 32 bits. The nominal
conversion is 1.28 microseconds per tick, with no independent oscillator
calibration. This page does not measure the whole movie, CDDA refill work,
or the duration of every interrupt-disabled interval.

`state` is hexadecimal **0**: off, **1**: stopped, **2**: prefill,
**3**: waiting for start, **4**: playing, **5**: paused, **6**: end of track,
or **7**: fault. `fault` is **0**: none, **1**: configuration,
**2**: driver identity, **3**: heap allocation, **4**: port ownership,
**5**: command queue, **6**: card read, **7**: track/range,
**8**: clock, **9**: stack, **A**: stale generation, **B**: stereo phase, **C**: bounded bus failure, or **D**: native control adapter.

Page 6 records internal restart reasons only after their STOP attempt succeeds.
EOF and ordinary PAUSE/STOP/reset requests do not increment these counts.
`recovery_last_reason` is zero before a recorded recovery; values 1–8 follow
the order of the eight counts. Its command and cursor-age breadcrumbs are
captured at that recovery; cursor age is zero before a coherent start proof.
Reason 2 now requires separation beyond the bounded publication-age allowance.
A persistent smaller mismatch may instead exhaust the unchanged copy-reserve,
unobserved-half or initial-start deadline and report reason 7, 3 or 5. A change
in reason alone is not proof of smooth playback.

Page 7 records the last denied copy gate's inputs before its STOP attempt.
They may survive a refused, deferred or superseded STOP and are not another
successful-recovery count. `reserve_last_cursor` is the accepted maximum
stereo cursor. `reserve_last_remaining` is the number of PCM frames until
its next half boundary. `reserve_last_proof_age` is the age of its justified
capture bound at denial; `reserve_last_sample_age` was that bound's age at
acceptance. These differ from page 6's age since the accepted observation.
The copy deadline fails when the remaining frames' nominal time minus proof
age is at most 3,907 ticks (5 ms), or has already expired.

The bank index is 0 or 1; `reserve_last_bank_filled` gives committed PCM
frames out of 16,384. Bank state is 0 EMPTY, 1 FILLING, 2 READY, 3 START_WAIT
or 4 PLAYING. `reserve_last_fill_stream` counts frames committed across
the current ring run, including both primed halves. Gate sites are 1 playback,
2 fill begin, 3 before card access, 4 after card access, and 5 a plane copy.
`reserve_last_probe_age` describes a pending probe, or is zero when none is
pending. A nonzero deficit with an expired bound helps identify whether
capture age, card duration or refill scheduling is limiting continuity.

`reserve_last_window_ticks` measures time from the first accepted cursor in
the current playing half to the denied gate. `reserve_last_window_calls`
counts service visits from that observation through the denial, including
both visits. It follows `service_calls`; rejected entries counted only in
`service_skips` are excluded. The anchor changes only at the initial accepted start proof or
an accepted half transition; renewed proofs within the same half do not
reset it. These are observed-window measurements, not the physical half's
entry time or a count of successful raw reads. A short observed window near
the half boundary suggests late observation; a long window with few visits
suggests sparse service. Many visits with little committed PCM warrant a
closer look at work admitted in each visit. The values do not alone identify
which code occupied a gap. Times and call differences wrap modulo 32 bits.

The raw timing fields measure only physical raw callbacks, including failed
live reads. They exclude cached sector remnants and callbacks returning to
a revoked or superseded epoch. Last, maximum and total are timer ticks; the
total wraps modulo 32 bits. Timing-call count can therefore differ from
`raw_calls` after a revoked/stale return. This is not total CDDA processing
time or a promise about reader throughput.

`data_blocked_calls` counts existing data-priority refusals only when the ring
is running and its next half needs filling. Commands 16/17 retain their
existing priority. `data_blocked_intervals` counts completed sampled spans
from the first such refusal until a later refill opportunity sees no owned
data request, or a control/recovery ends the active state. Maximum and total
ticks describe those spans, including time between visits; they are not
physical SCI busy time or time spent inside the masked dispatcher. Totals
wrap modulo 32 bits. `data_blocked_open` identifies a span still open when
captured, whose unfinished duration is not included in the completed totals.
These observations distinguish refill exclusion from cursor/phase recovery
without relaxing a live-ring write gate.
Diagnostic publication and copy-pressure decisions use short, SR-preserving
masks so a lifecycle revoke cannot reopen a sampled span or mix the denied
gate's inputs. Those scopes cover RAM checks and the existing timer read,
with no card or sound-bus transaction; transfer masking policy remains
unchanged. Quiescent service still performs no timer, card or sound-bus work.

The generation fields distinguish a requested command from the generation
the worker actually applied. The service, read, copy, fill/start/end and gap
counts describe the pilot's work; they are not sound-quality scores. Fields
ending in `ticks_max`, plus `service_gap_max` and `step_ticks_max`, use the
game's existing TMU0 clock units. This pilot does not reprogram that timer.
The worker stack includes a checked guard and watermark; the packaged
compiler stack sum covers authored code and an assembly allowance, rather
than the game SDK's internals.

`hardware_loops` records intentional continuous playback and is no longer a
zero-only field. `active_bank_writes` must remain zero: the currently playing
half is never a refill destination. `dma_busy` and `dma_suspended` are
retained legacy fields and remain zero.
The worker never suspends or programs DMA; `bus_deferrals` counts refusals
including enabled/active G2 DMA and occupied queue slots. Photograph the actual values even when an error
has already occurred. Queue consumption, bank starts and observed active
channels do not establish audible stereo output by themselves.
The original telemetry field names remain in the ABI, with these ring-mode
meanings:

| Field | Meaning in this build |
|---|---|
| `bank_fills` | Completed stereo-half fills |
| `bank_starts` | Paired hardware-loop START submissions, including restarts |
| `bank_ends` | Observed transitions into the next ready ring half |
| `hardware_loops` | Intentional ring START submissions |
| `finite_ends` | Observed end of a finite requested source range |
| `retired_frames` | Source progress credited from guarded cursor observations, capped at the requested end |
| `filled_frames` | Source frames committed to the ring; zero tail padding is excluded |
| `handoff_gaps`, `gap_ticks_max` | Recovery/restart gaps tracked by the existing handoff counters |

None of these counts proves that all frames were audible at the speakers.
A healthy long stream can have many half transitions with few STARTs;
the earlier finite-build expectation of one START per bank no longer applies.

A launch/installation refusal remains on its failure screen. An ordinary
reader failure can remain on its request-rejection screen instead of reaching
these eight controller-return pages. Photograph whichever screen is visible;
there is no need to wait indefinitely for another page.

## What this build reserves

The candidate uses the game's first main-heap allocation: a 192 KiB lease at
`[0x8cfd0000,0x8d000000)`. Only its lower 64 KiB,
`[0x8cfd0000,0x8cfe0000)`, holds the worker; the upper part is padding around
the game's known top-of-RAM scratch references. The sound buffer is a
128 KiB fixed allocation registered in the game's tracked sound-heap tail.
Its canary is written and drained before publishing allocation metadata. The observed game
voice picker and updater are restricted to ports 0–61; the pilot uses ports
62 and 63. These are exact-title contracts, not a general reservation scheme
for retail games.

The pilot reads the game's existing timer without reprogramming it. It does
not upload another ARM driver, reset the ARM, retune timers, or replace the
game's effect mixer. It retains SCI framing and CRC checks. A read or sound
failure observed at a completed worker boundary must be visible in the
reports. Continuous playback depends on the game continuing to call the
worker; no independent sound-driver watchdog is installed.

Clock admission checks the known console profile without changing registers:
the low 12 bits of FRQCR are `0E0A`, TMU0's reload is `FFFFFFFF`,
`TCR0 & 0x0027` is `0x0002`, and TMU0 is running. The timing conversion assumes this
console's 50 MHz peripheral clock divided by 64, nominally 1.28 microseconds
per tick. Register agreement is not an independent oscillator calibration.
An incompatible profile produces a clock fault rather than retuning a timer.

Each new pilot G2 transaction has a shared 10,000-poll cap and 1,563-TMU0-tick
cap (nominally 2 ms in the admitted clock profile). It restores exact caller
SR on every return. Enabled or running G2 DMA causes deferral, with no DMA
register writes. Repeated unresolved deferrals fault after nominally one
second of active service. A packet whose command header was published is
never republished after a final-drain failure. Canary failure publishes no
heap record. Inactive-bank copies never authorize reuse of a playing bank.

`bus_last_result` is **0**: success, **1**: deferred busy, **2**: deadline or
poll-cap timeout, **3**: invalid argument, **4**: invalid installed state,
**5**: command already published when the final drain failed. `bus_deferrals`
counts busy observations. The updater entry/return counters surround the
original game updater; they are breadcrumbs, not a watchdog. A difference
can localize an unfinished original call only if a snapshot can still be
obtained. The report captures these fields before issuing a reset.

Page 4 distinguishes retry visits from actual native owner retirement.
`pause_detail` is **0**: none, **1**: blocked-exception context,
**2**: clock discontinuity/service gap, **3**: time or attempt budget,
**4**: unsupported native ownership/table/callback, **5**: post-pump context
changed, **6**: reserved (the incorrect dynamic delegate veto is removed),
**7**: invalid native caller stack. A validated pending file owner defers the manual pause pump, allowing
the existing VBlank handler to retire it within the retry budget. It is never
forcefully retired or passed to an authored completion callback. Returning
retries stop after nominally one second or 65,536 attempts; that attempt
limit does not guarantee a VBlank opportunity. A successful native PAUSE
has a separate bounded worker drain: nominally two seconds or 1,048,576
visits, whichever bound is reached first. The larger stopped-clock escape
allows native command application to progress; it does not add native SDK pump calls.
An idle PAUSE applies immediately without touching the sound bus.
`cache_policy` should be `00000101`; `service_visits_per_update`
should be `00000002`. Counts remain evidence of work, not an audio PASS.

Page 4 rows 3–4 record the first native-control fault before terminal display.
`native_owner` is the validated context's current owner address, while
`native_work_token` is the first control record's token. An invalid context
uses `FFFFFFFF` for those two fields. `gd_owned_command` and `gd_owned_token`
belong to the resident service; they are observed without acknowledging it.
`native_gd_state` packs pending in bit 0, executing in bit 1, native busy
guard in bit 2, valid native context in bit 3, caller IMASK in bits 4–7,
GD status in bits 8–15, error low byte in bits 16–23, resident GD entry lock
in bit 24, and caller SR.BL in bit 28. The last three words retain the exact last GD CHECK arguments and
its signed result as a 32-bit hexadecimal value. These are diagnostic
observations, not a license to discard a handle. They can remain zero when
no native-control fault has occurred; CHECK breadcrumbs may still be present.

The repeatable host command is `make -f Makefile.toy_pilot test`. It runs
the worker, native control and production scratch-dispatch regressions with
AddressSanitizer and UndefinedBehaviorSanitizer, including
the production worker/GD flow with asynchronous sound-queue application,
normal startup callback registration, command coalescing, native movie-pause
application, native queue reuse, scalar mapping and pause ownership. Host simulations do not establish console timing.

The game and its unchanged SDK still contain waits without deadlines.
The final 19 host suites pass with AddressSanitizer and
UndefinedBehaviorSanitizer, including the production two-sector branch and
clock-profile checks. Command ownership and sustained data-request
starvation are also modelled: shorter individual steps alone do not prove
that a long owned GD request will give audio refill enough service.
The scoped mapping correction removes the demonstrated command-retirement
blocker. The remaining original game paths and actual FMV speed still need
console verification.

## Restore

Power off. Copy `/KUI/apps/games/retail-boot-working-cdda.kui` over
`/KUI/apps/games/retail-boot.kui` to restore the confirmed CDDA baseline,
keeping the backup. To restore the ordinary reader instead, use the
separate preserved ordinary reader backup. Keep the working runtime
installed, safely eject, and reboot.

`build.json` identifies the source commit, exact tree and publication state.
An unpublished build includes its complete local source snapshot; source
links in its packaged documents lead to that archive instead of an unverified
public commit.
`SHA256SUMS` covers the package contents. The ZIP includes the complete
corresponding source snapshot, license notices, linked ELF/map files and
compiler stack evidence. Build checks establish linked fit and identity;
the listening and video observations establish what happened on this console.
This controlled test contains one runtime and its linked evidence under
`evidence/toy-pilot/`.
