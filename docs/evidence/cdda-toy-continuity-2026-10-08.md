# Toy Commander continuity follow-up

The focused test keeps the continuous stereo ring, receive-paced SCI
and disabled status reuse, changing GD data steps to a fixed two-sector cap.
Ordinary K-UI 1.8.5 remains the runtime and recovery reader. The known working
CDDA pilot is preserved separately.

## Recorded ring result

The earlier standard `c426c10f1af6` numerical photographs
show snapshot version 4, 320 bytes, PAUSED state, fault 0 and matching
generation/applied generation 10. `raw_errors`, `queue_errors`,
`active_bank_writes` and stack faults are zero; there is no separate copy-error
field. There were 13 STARTs and 12 internal recovery gaps. The internal
recoveries precede the intentional terminal PAUSE and must not be explained
as 12 deliberate pauses.

| Recorded maximum | Standard ring build |
|---|---:|
| Worker visit | 13.514 ms |
| Service gap | 72.307 ms |
| Recovery gap | 1.8193 s |

The recovery duration includes acknowledged STOP, the full-ring retirement
fence and restart work. It is not an isolated card-read latency. The PAUSED
snapshot is the state at report capture; it does not establish that the
whole movie played while PAUSED.

Aligned recordings take 20.231 seconds for the standard build and 20.189
seconds for the optimized build from the opening black frame to text. That
difference is approximately one phone frame. The first roughly four seconds
of audio progression are close to real time in both. A distinct visual stage
appears in the optimized recording, so these measurements do not establish
identical whole-movie behavior. There are no optimized numerical photographs
from which to compare its recovery counters.

## Two-sector hardware follow-up

The new `a645eb2c2245` recording (`62074.mp4`) displays the girl shot after
the mouth sequence, which was absent from both preceding recordings. The
mouth begins at 6.380 seconds, the girl appears at 9.925 seconds, and the
next black transition is at 14.097 seconds. This is an improvement in the
displayed sequence; without the original movie frames, it does not establish
an exact dropped-frame count.

The black-to-first-story-text interval is 20.231 seconds, essentially the
same as the earlier standard run. Different recording starts make total
clip length unsuitable for comparing movie speed. Matched original-track
passages in the new recording, around phone times 14.69–19.19 seconds,
progress close to real time; later waveform matches are insufficient to
establish uninterrupted playback for the whole track.

The new pages 0–4 show fault 0, matching generation/applied generation 10,
PAUSED at capture, and no raw, queue, stack or active-bank-write errors.
The cumulative counters are compared below; the photographs do not establish
equal-duration runs, so their differences are not recovery rates.

| Recorded value | Standard ring | Fixed two-sector |
|---|---:|---:|
| START publications | 13 | 15 |
| Observed starts | 13 | 14 |
| Internal recovery gaps | 12 | 14 |
| Successful half transitions | 22 | 24 |
| Maximum worker visit | 13.514 ms | 13.541 ms |
| Maximum active service gap | 72.307 ms | 73.783 ms |
| Maximum copy | 62.72 µs | 57.60 µs |
| Maximum recovery gap | 1.8193 s | 1.8122 s |

Only internal recovery sets `gap_pending`; finite ends are zero. The 14 gaps
therefore remain internal restarts, rather than intentional pauses or EOF.
The service-gap maximum is below one 371.5 ms half and does not by itself
prove refill starvation. No photograph of page 5 was supplied for this run,
so the actual GD read-step duration remains unmeasured in the received
evidence. These pages do not justify increasing cursor tolerances or removing
the acknowledged STOP/ownership fence.

The user also tried normal loading of a converted 2048-byte image and both
background choices with original and converted images. Their code-8 refusal
screen is from pilot admission, before the worker lease or game hooks.
`000B6B9C` is the correct executable size (748,444 bytes); it was generic
failure detail, not a size mismatch. The pilot admits only the standard
reader and the original raw descriptor/layout. The background slots in this
pilot contained copies of the synchronous resident, not a shared-card
background CDDA implementation. These refused launches provide no comparison
of transport speed. A subsequent cleanup gives these cases explicit early
errors and shares one embedded resident blob; neither change alters movie
or audio servicing.

The cleanup removes 36,432 duplicate resident bytes and adds 356 bytes of
launch diagnostics, reducing the stage from 87,952 to 51,876 bytes and the
runtime from 96,208 to 60,132 bytes. All 18 ASan/UBSan pilot regression suites
pass. Linked validation requires one shared blob and retains the previous
stack/layout gates. The worker remains byte-for-byte identical; the low
resident changes only its displayed build identity after the source commit.
Ordinary stage C and assembly preprocessing is unchanged with the pilot
option disabled. This is a startup-size cleanup, not a sustained throughput
optimization.

## Received six-page timing report

The six photographs supplied on 2026-10-08
show build `614295ab20d5`. Their filenames, in page order, are
`image-1791479657011.jpg`, `image-1791479665350.jpg`,
`image-1791479680368.jpg`, `image-1791479697846.jpg`,
`image-1791479710508.jpg` and `image-1791479725935.jpg`.
This is a separate capture from the preceding `a645eb2c2245` report. No new
movie recording accompanies these six photographs, and the run durations
are not established. Counter differences must not be treated as a change
in recovery rate.

The first five pages retain snapshot version 4, 320 bytes, PAUSED state,
fault 0 and matching requested/applied generation 10. Driver identity is
20,740 bytes, CRC32 `70CCEEB2`, verified 1. Worker stack use is 804 bytes,
with stack fault 0. The last requested command is PAUSE (22).

| Recorded value | `614295ab20d5` capture |
|---|---:|
| START publications / observed starts | 4 / 4 |
| Successful half transitions | 12 |
| Internal recovery gaps | 3 |
| Maximum recovery/restart gap | 1.59484032 s |
| Maximum worker visit | 13.184 ms |
| Maximum active service gap | 61.1264 ms |
| Maximum plane copy | 60.16 µs |
| Completed stereo-half fills | 18 |
| Raw audio sector reads / bytes | 539 / 1,267,728 |
| Raw read / queue / active-bank-write errors | 0 / 0 / 0 |
| Bus deferrals | 0 |
| Retired source frames | 221,488 |
| Committed source frames | 315,086 |
| Updater entries / returns | 1,182 / 1,182 |

Only internal recovery sets the gap counter. These three gaps therefore
record successful internal restart transitions, but the version-4 snapshot
does not record their causes. They must not be attributed to data exclusion,
cursor phase, missing refill data or any other specific recovery branch
from this report alone. The maximum gap includes STOP/quiescence and refill
work. Retired and committed source frames correspond to approximately
5.0224 and 7.1448 seconds at 44.1 kHz, respectively; neither is a count of
frames actually heard. Zero legacy DMA fields do not establish an absence
of DMA contention.

Page 5 is transcribed exactly below:

```text
47444D31 00000002 00000A60 000013F1
0104FA94 00001FE1 00000002 00000000
00001F31 0000020F 00000014 00000000
00000000 00000000 00000000 00000000
```

| GD measurement | Decoded value |
|---|---:|
| Credited data dispatches | 2,656 |
| Credited logical 2048-byte sectors | 5,105 |
| Credited user bytes | 10,455,040 |
| Accumulated measured dispatch ticks | 17,103,508 |
| Nominal accumulated dispatch time | 21.89249024 s |
| Mean credited dispatch time | 8.242654 ms |
| Maximum credited dispatch time | 10.44608 ms |
| Maximum credited sectors per dispatch | 2 |
| Invalid timer samples | 0 |
| Diagnostic calls / requests / rejections | 7,985 / 527 / 20 |
| Last recorded GD error | 0 |

Using the admitted nominal 1.28 µs tick conversion, credited user bytes
divided by cumulative measured dispatch time is **466.37 KiB/s**. This
ratio covers only GD EXEC dispatches that credit data sectors, excludes
time between dispatches and CDDA refill work, and omits surrounding hook
entry/exit and the trailing timer-profile check. It is not whole-movie
throughput or the physical SCI bandwidth ceiling. The cumulative timer
sum wraps modulo 32 bits, and there is no independent oscillator
calibration. All retained dispatches were limited to at most two sectors;
the report does not measure every masked interval.

The 20 rejections remain unclassified. The dispatch counter increments
for any negative result or a REQUEST returning zero, including an occupied
command handle and unsupported or malformed operations. A later successful
command can leave `last_error` at zero. This page cannot establish whether
all earlier rejections were harmless retries or whether an unsupported
operation contributed to playback behavior.

The next diagnostic keeps transfer and refill scheduling policies
unchanged while extending the pilot API/snapshot to version 5, 384 bytes.
The fixed-two-sector report retains pages 0–5 and adds page 6 for internal
recovery reasons and sampled data-command refill-denial intervals. Those
new counters distinguish recovery branches and the observed scheduling
exclusion span; they do not measure uninterrupted physical card ownership.
This is a diagnostic design, not hardware evidence of a playback fix.

The diagnostic candidate passes all 19 host suites with ASan and UBSan.
The actual worker fixture distinguishes all eight recovery causes, excludes
EOF, intentional controls and refused STOP attempts, and checks completed
versus open denial spans. Deterministic lifecycle interruptions verify that
short SR-preserving counter-publication masks prevent reopened spans and
double counting, while quiescent visits retain zero timer, card and G2 work.
The production terminal fixture destroys the live snapshot during reset and
display, verifying that both report variants retain their pre-reset words
within the existing 512-byte workspace.

The linked export/config/snapshot versions all agree at 5. The audited low
resident still ends at `8c0077e8`, with its existing 24-byte margin. Its
conservative GD-stack sum remains 1,228 of 1,232 available bytes; the worker
and shared low callback sum is 3,176 of 8,096 available bytes. The stage is
52,368 bytes and runtime payload 60,560 bytes, retaining the earlier removal
of duplicate resident blobs. These checks establish the diagnostic layout
and modeled contracts, not a new console playback result.

## Refill scheduling review

Both the worker and low raw callback block audio reads for the complete
lifetime of a native data command 16/17, including a completed result that
still awaits its matching CHECK. The synchronous card reader closes its
stream and releases SCI after each read step; the extended exclusion is a
priority policy, rather than uninterrupted physical bus ownership. A smaller
GD step may shorten each masked dispatch while lengthening this exclusion.

Serializing one audio sector between GD entries is a possible next experiment.
The existing entry lock and exact SR restoration must remain intact. The
physical-block cache is tagged by LBA, and raw reads do not alter the GD
handle, completion progress or CHECK ownership. However, each refill adds a
masked card visit and can evict a data boundary block; the existing four-refill
burst must not run unrestricted while a data command is owned. This could
improve audio continuity while reducing video throughput.

Before changing that policy, distinguish recovery reasons and measure data
exclusion on hardware. Validation must exercise the actual shared image/card
wrapper with alternating Mode1 and raw reads, including exact payload,
token/progress ownership, cache replacement and stop/release errors. The
existing GD-only step model does not establish this shared-reader behavior.

## Focused next measurement

The two-sector test reduces the number of data sectors admitted by one GD
EXEC step. It leaves the ring worker and paced SCI status-reuse option at
their standard settings. A sector cap can reduce time spent in a single
existing masked dispatch; it is not a time limit on a slow card transaction.

The worker snapshot ABI remains version 4, 320 bytes. Terminal **PILOT PAGE
5** appends 16 scalar words in the resident's existing `image.block`:

| Row | Values, left to right |
|---|---|
| 1 | marker `47444D31`, fixed step `2`, measured read steps, credited sectors |
| 2 | accumulated ticks modulo 32 bits, maximum ticks, maximum credited sectors per step, invalid timing steps |
| 3 | GD diagnostic calls, requests, rejected requests, last error |
| 4 | four reserved zero words |

Timing begins before the GD EXEC service dispatch and is retained only when
the dispatch credits data sectors. FRQCR/TMU0 profile checks run at both
ends for retained timing; invalid profiles, a zero start counter or zero
elapsed counts reject that step's timing.
This scope excludes the surrounding IRQ hook entry/exit and the trailing
profile-check overhead. Accumulated ticks wrap modulo 32 bits, and the
nominal admitted conversion is 1.28 microseconds per tick. It is not a
measurement of every masked interval, raw CDDA refill work or whole-movie
throughput. The unchanged snapshot counters retain recovery and service
evidence alongside this new data-read measurement.

Console comparison should use the natural intro, loading, music/effects and
the already working Start transition. The new read maxima can show whether
smaller admitted steps shorten credited data-read dispatches; only the
console can establish the effect on movie cadence and audible continuity.

## Historical finite baseline

The user confirmed Start/menu recovery in
`6bb04d248b351fd1393bf23737b81eb4f141668c` before the ring change.

The prior finite design inserted setup, acknowledgement and a 20 ms guard at
every 16,384-frame boundary. The ring removed those periodic restarts.
Original media and private instruction listings remain outside the source
and delivery packages.

## Audio design change

Finite banks contain 16,384 frames, about 371.5 ms at 44.1 kHz. Repeated setup,
START acknowledgement, finite retirement and the 20 ms guard can interrupt
playback even when the next bank is ready. Further reductions in post-retirement
handoff time cannot eliminate those boundaries.

The continuous test uses the existing 128 KiB allocation as two 64 KiB mono
planes, each containing a 32,768-frame hardware loop with two 16,384-frame
halves. The intended ownership contract is:

- Start only after both stereo halves are filled.
- Refill only a half proved outside the hardware consumption interval.
- Bound cursor freshness and stereo phase, including time spent reading and
  copying; revalidate before any live-loop write.
- Publish a half ready only after both planes are complete.
- Stop and retain ownership on a missed deadline or ambiguous cursor, then
  refill and restart only after quiescence is established.
- Preserve pause, abort, shutdown, track tail and repeat behavior.

Successful native PAUSE drains the acknowledged STOP/quiescence transition
with a separate two-second deadline. This admits up to one second of STOP
dispatch plus the approximately 763 ms full-ring retirement fence. Native
busy-result retries retain their prior one-second limit; neither path may
wait indefinitely on a stopped timer.

A complete SH-4 service freeze may repeat the owned loop until execution
returns. No autonomous ARM watchdog is claimed. This playback limitation does
not authorize overwriting or releasing memory still owned by the voices.

The architecture is independently implemented from K-UI's own buffer and Toy
SDK contracts. The separately pinned DreamShell review identifies continuous
playback as a missing behavior; no source or translated algorithm is imported.

## Movie transport investigation

The pilot deliberately enables receive-paced SCI DMA. The earlier paired Toy
run documented in `cdda-retail-read-comparison-hardware-2026-10-07.md` captured
SCI receive overrun without pacing, before integrated CDDA playback existed.
The paced counterpart completed its observed blocks, but the user already
reported movie skipping. Restoring unpaced feed is not an established fix.

Pacing affects movie reads as well as raw audio reads. The previous optional
status-reuse experiment retained pacing and all error/count/timeout checks;
the two-sector test uses the standard status path so it changes one policy.

CDDA source demand is 75 sectors/s, or 176,400 bytes/s before overhead.
Combined movie demand, sustained paced throughput and worst-case masked-read
duration still require hardware measurements. Existing evidence does not
establish the SCI physical bandwidth ceiling as the cause of the remaining
stutter.

## Host validation of the prior ring candidate

All 16 pilot suites pass with AddressSanitizer and UndefinedBehaviorSanitizer.
The replacement actual-worker fixture models independent PCM consumption,
delayed ARM command dispatch/publication, and elapsed card/G2 operations. It
checks physical half ownership and exact stereo source order, including sparse
10 ms cursor publication, underrun recovery, late card return, deferred plane
copy, logical tail, repeat, pause/release/reset and driver revocation. A
near-boundary case rejects a four-copy burst whose cost exceeds the safe
window; removing the per-copy gate makes its independent guard fail.

The real GD/pause integration also covers a 500 ms STOP acknowledgement followed
by the full-ring fence, with the original SR restored and the native successful
return retained. RELEASE at EOF does not start another voice or read exhausted
source data. The 794-line finite playback fixture was removed rather than
retaining a second obsolete playback simulation.

SCI first-fault, unpaced/paced/reused-status delayed-DMA models, and terminal
fault-inclusion tests pass with the same sanitizers. Status reuse is default
off and unavailable to ordinary production readers. The disabled option's
compiled bus objects were compared byte-for-byte with the prior source. Host
checks establish these modeled contracts; they do not establish console audio
quality or timing under the game's real ARM/FMV load.

The focused two-sector candidate passes the final 18 host suites with ASan
and UBSan. Tests cover the actual fixed-step branch and both clock profiles,
long-lived GD command ownership and audio starvation during sustained data
requests. A shorter data step does not by itself prove adequate refill
service or solve movie stutter; the new console comparison remains necessary.

## Received restart-cause report

The next seven photographs show build `e599eb2474a6`, snapshot version 5,
384 bytes. Their filenames in page order are
`image-1791481409352.jpg`, `image-1791481418573.jpg`,
`image-1791481438869.jpg`, `image-1791481447318.jpg`,
`image-1791481461767.jpg`, `image-1791481476974.jpg` and
`image-1791481492152.jpg`. No movie recording or new subjective playback
description accompanies this report. Neither the run duration nor an exact
match to an earlier capture's duration is established.

The captured state is PAUSED, fault 0, requested/applied generation 10/10,
with PAUSE as the last requested command. Read, queue, stack and
active-bank-write error counters are zero. Updater entries and returns are
both 1,128. Driver identity remains 20,740 bytes, CRC32 `70CCEEB2`, verified 1.

Page 1 is affected by camera motion. Its `service_calls` and
`service_gap_max` values cannot be transcribed confidently and are left
unresolved; no guessed values enter the timing analysis. The other clear
pages establish the following observations.

| Recorded value | `e599eb2474a6` capture |
|---|---:|
| Successful internal restart STOPs | 3 |
| Stereo-phase restart STOPs | 3 |
| Each of the other seven restart reasons | 0 |
| Restart START gaps counted later | 2 |
| START publications / observed starts | 3 / 3 |
| Successful half transitions | 16 |
| Maximum recovery/restart gap | 1.61075072 s |
| Maximum worker visit | 12.79488 ms |
| Maximum plane copy | 62.72 µs |
| Sampled needed-refill data refusals / completed intervals | 0 / 0 |
| Sampled data-exclusion maximum / total ticks / open flag | 0 / 0 / 0 |
| Last restart reason / owned GD command | 2 / 0 |
| Last restart age since accepted coherent cursor | 61.11744 ms |
| Committed / retired source frames | 357,606 / 303,559 |

The exact new page 6 words are:

```text
00000000 00000003 00000000 00000000
00000000 00000000 00000000 00000000
00000000 00000000 00000000 00000000
00000002 00000000 0000BA84 00000000
```

Three restart counts and two restart gaps are compatible counters with
different scopes. A restart reason is counted after its internal STOP attempt
succeeds; a gap is counted later only when the subsequent START is queued.
A native control can interrupt recovery before that START. The photographs
do not show which control interrupted the third restart, but their final
PAUSED state is consistent with this possibility. This difference is not
evidence that a restart was lost from the reason counters.

Page 5 contains 2,670 credited read steps, 5,133 credited sectors,
17,196,657 accumulated ticks, maximum 7,946 ticks, maximum two sectors per
step, and zero invalid timing steps. The exact words are:

```text
47444D31 00000002 00000A6E 0000140D
01066671 00001F0A 00000002 00000000
00001F0E 000001FB 00000014 00000000
00000000 00000000 00000000 00000000
```

At the nominal 1.28 microseconds per tick, the measured dispatch time totals
22.01172096 seconds, averaging 8.24409025 ms per credited step and reaching
10.17088 ms at the maximum. Credited logical user payload divided by that
measured time is approximately 466.388 KiB/s. This remains a rate within
credited GD dispatch windows, excluding time between them, hook entry/exit
and CDDA work. It is not whole-movie throughput or the physical SCI ceiling.
The GD diagnostic has 7,950 calls, 507 requests, 20 rejections and last error
zero. The same rejection-classification limitations described for the prior
report apply.

This report identifies the recovery branch: all recorded internal STOPs
followed rejected stereo-phase observations. It does not record the rejected
left/right pair or its individual publication ages, so it cannot distinguish
a genuinely separated pair of voices from an observation of differently aged
published cursor values. The last-cursor age describes time since the worker
accepted a coherent pair; it is not a measurement of ARM publication delay.
Zero sampled needed-refill refusals means the new diagnostic did not observe
that particular data-priority exclusion in this run. It does not establish
unlimited bandwidth, absence of all service contention, or smooth video.

The independently documented original Toy ARM driver scans active ports and
publishes sampled cursor words for each port in its normal service loop;
the host reads that table rather than a continuously updated stereo hardware
counter. Ports 62 and 63 have separate publication locations. See
[`cdda-toy-driver-command-static-2026-10-07.md`](cdda-toy-driver-command-static-2026-10-07.md).
This contract warrants investigating publication skew before changing card
scheduling. These are descriptions of the title's supplied sound driver;
no original instruction listings, game bytes or SWAT source are reproduced.

The targeted next candidate retries an ambiguous phase observation within
the existing bounded probe. A rejected pair still grants no ownership:
accepted observations must retain the 64-frame phase limit, the same-half
check, cursor freshness and the conservative per-copy deadline. Separation
that exceeds what observation age can explain still requests recovery. The
two-sector data cap, SCI pacing, refill priority and acknowledged STOP fence
remain unchanged. This is a focused hypothesis to test; the photographs do
not establish that this candidate fixes audio or video playback.

The candidate's host fixture now schedules hardware captures and shared-table
publications independently of SH reads, while physical stereo PCM advances
through card operations and G2 copies. Three serial-publication schedules
preserve every checked source sample across multiple half transitions and
ring wraps without internal recovery. A locked publication cadence that never
converges, and a persistent 65-frame disagreement, still stop before unsafe
reuse; a gross 2,300-frame disagreement still records phase recovery. Copy
checks use the physical playing half before and after each transfer. The same
healthy serial fixture run against the prior worker fails PCM continuity,
establishing that it detects the former immediate-restart behavior.

All 19 host regression suites pass with AddressSanitizer and
UndefinedBehaviorSanitizer. The linked worker adds 52 bytes; resident placement
and its conservative stack estimate are unchanged. The strict package audit
retains the existing scalar/leaf, instruction, native-scratch, pause, cache and
single-resident-blob checks. These validations establish behavior under the
modeled schedules and linked bounds, not a hardware playback result.

## Received copy-reserve report

The next seven photographs show build `db4e19a415dc`, snapshot version 5,
384 bytes. Their filenames in page order are
`image-1791483078344.jpg`, `image-1791483093289.jpg`,
`image-1791483102569.jpg`, `image-1791483117655.jpg`,
`image-1791483132444.jpg`, `image-1791483149985.jpg` and
`image-1791483163873.jpg`. All seven pages, including page 1, are readable.
No new recording or subjective playback description accompanies this report.
Equal run durations are not established, so recovery counts are not compared
as rates and counter changes are not evidence of smooth audio or video.

The exact page words are:

```text
PAGE 0
54595031 00000005 00000180 00000005
00000000 0000000A 0000000A 00000001
00000016 00000000 00000000 00000000
0005BA87 0000000E 0005C24E 8CFD0000

PAGE 1
8D000000 8CFD6E80 00000324 00000000
00005104 70CCEEB2 00000001 A09D3FE0
00020000 00000001 00001F28 00000000
0000D3EC 00002880 00000535 002FD6F0

PAGE 2
00000000 00003468 0000002F 00000029
0000000A 0000001E 00000009 0014410B
0000001B 000013A2 00000000 00000000
00000000 00000000 00000000 00000000

PAGE 3
0000000A 00000000 00000000 00000001
000982A6 000BEE8A 00000013 0000B340
00000000 00000000 0000000A 00000000
00000000 00000000 00000725 00000725

PAGE 4
00000007 00000002 00000002 00000002
00000000 00000001 00000101 00000002
00000000 00000000 00000000 00000000
00000000 000002E5 8CFD6C30 00000002

PAGE 5
47444D31 00000002 00000A6E 0000140D
010657C1 00001FDE 00000002 00000000
0000281C 000002E6 00000015 00000000
00000000 00000000 00000000 00000000

PAGE 6
00000000 00000000 00000000 00000000
00000000 00000000 00000009 00000000
00000000 00000000 00000000 00000000
00000007 00000000 0000001C 00000000
```

The captured state remains PAUSED, fault 0, generation/applied generation
10/10, and last command PAUSE. The driver remains verified with its expected
20,740-byte size and CRC32 `70CCEEB2`; stack use is 804 bytes with fault 0.
Raw, queue, active-bank-write and bus-deferral error counters remain zero.
Updater entries and returns are both 1,829.

| Recorded value | `db4e19a415dc` capture |
|---|---:|
| Stereo-phase restart STOPs | 0 |
| Copy-reserve restart STOPs | 9 |
| Each of the other six restart reasons | 0 |
| Restart START gaps counted later | 9 |
| START publications / observed starts | 10 / 10 |
| Successful half transitions | 30 |
| Maximum recovery/restart gap | 1.69903488 s |
| Maximum worker visit | 13.27104 ms |
| Maximum active service gap | 69.44256 ms |
| Maximum plane copy | 60.16 microseconds |
| Sampled needed-refill data refusals / completed intervals | 0 / 0 |
| Sampled data-exclusion maximum / total ticks / open flag | 0 / 0 / 0 |
| Last restart reason / owned GD command | 7 / 0 |
| Last restart age since accepted coherent cursor | 35.84 microseconds |
| Completed stereo-half fills | 41 |
| Raw audio sector reads / bytes | 1,333 / 3,135,216 |
| Committed / retired source frames | 781,962 / 623,270 |

The raw-byte count is exactly 1,333 times 2,352. Committed and retired source
frames correspond to approximately 17.7316 and 14.1331 seconds of PCM at
44.1 kHz, respectively; these are accounting quantities, not demonstrated
audible durations. Page 6 identifies nine successful internal STOPs caused
by the conservative copy-reserve gate, with no successful restart STOPs
attributed to stereo phase in this run. That is a change in the observed
branch, not proof that the prior phase experiment improved playback.

The most recent copy-reserve recovery occurred with GD command zero and
only 28 ticks since the worker accepted a coherent cursor pair. That
35.84-microsecond age is measured from acceptance. It is not the age of the
underlying hardware captures or the older capture bound used for the copy
deadline. The report cannot determine whether the gate expired because of
a needlessly old bound, insufficient remaining fill time, or both; those
decision inputs were not included in version 5. Zero sampled needed-refill
data exclusions again applies only to that diagnostic's scope.

Page 5 records 2,670 credited GD read steps, 5,133 logical sectors,
17,192,897 accumulated ticks and a maximum of 8,158 ticks. The nominal
conversion gives 22.00690816 seconds of measured dispatch time,
8.24228770 ms per credited step on average and 10.44224 ms at the maximum.
Maximum sectors per step remains two, and invalid timing steps remain zero.
Credited logical payload divided by measured dispatch time is approximately
466.490 KiB/s. The existing dispatch-window exclusions apply; this does not
measure whole-movie throughput or an SCI physical ceiling. GD diagnostics
record 10,268 calls, 742 requests, 21 rejections and last error zero. These
rejections remain unclassified.

The next candidate derives a newer conservative capture bound separately
for each channel. It retains the last observation time of a value two
publication changes before the current value, then uses the older of the
two channel bounds. The timestamps precede both SH bus reads. This follows
the original driver's serial capture/publication ordering without assuming
that the observed changed word was captured when SH first saw it. Acceptance
still requires two publication changes per channel, the unchanged 64-frame
phase limit, the same half and the bounded probe. An ambiguous pair grants
no write permission, and every plane copy retains its physical ownership
and deadline gates. No private ARM instructions or SWAT implementation are
copied into this derivation.

An independently modeled serial-publication schedule with 8.96 ms raw reads reproduced
the old copy-reserve refusal while the independently modeled hardware still
had about 37 ms before entering the destination half, which needed another
964 PCM frames. The revised capture bounds remove that initial refusal.
Two nearby slow-card schedules preserve exact modeled PCM through 600 updater
calls without recovery; reverting only the accepted timestamp to the old
probe-start anchor fails the new continuity fixture. The original harder
schedule eventually reaches another copy-reserve refusal, which remains a
tested STOP case with exact PCM preserved until refusal. These results identify excess conservatism in a
specific modeled case; they do not establish successful playback for every
latency schedule or for this console.

The proposed version 6 snapshot is 448 bytes and retains the version 5
prefix, appending a pressure page 7 to the fixed-two-sector report. The new
page records the last copy-reserve decision's cursor, elapsed age of the
capture bound, that bound's age when the pair was accepted, frames remaining
within the sampled half, destination half and its fill progress/state,
plus probe age and the gate's call site. It also records
actual raw-callback timing last/maximum/total and count, excluding cached
sector remnants. Decision inputs are captured before the STOP attempt,
so they are not another successful-recovery count. These observations are
intended to distinguish an aged proof from inadequate refill progress while
leaving the card cap, SCI pacing, data priority and STOP fence unchanged.

All 19 host regression suites pass with AddressSanitizer and
UndefinedBehaviorSanitizer. The linked candidate passes the existing strict
layout, instruction, embedding and stack audits: the worker occupies 16,600
payload bytes and ends at `0x8cfd7000`; the resident ends at `0x8c0077e8`.
The conservative resident-stack sum remains 1,228 of 1,232 available bytes.
These are build/model checks; hardware continuity for this candidate is not
yet established.

## Received refill-pressure hardware report

The next eight photographs show build `0a898d37a2de`, snapshot version 6,
448 bytes. Their filenames in page order are
`image-1791485644948.jpg`, `image-1791485652834.jpg`,
`image-1791485666593.jpg`, `image-1791485681121.jpg`,
`image-1791485696568.jpg`, `image-1791485711430.jpg`,
`image-1791485728476.jpg` and `image-1791485748692.jpg`.
Page 2 is too blurred to transcribe reliably. Values from that page, including
the read/queue-error and gap maxima, are unresolved rather than guessed.

The user reports better behavior overall: background CDDA resumes for about
one second and then quits, whereas it previously stopped when the bear
started shooting. This is an observed improvement in that run's behavior,
not an objectively measured change in continuity or video speed. No new
recording accompanies this numerical report, and equal-duration comparisons
with earlier runs are not established.

The exact seven readable pages are:

```text
PAGE 0
54595031 00000006 000001C0 00000005
00000000 0000000A 0000000A 00000001
00000016 00000000 00000000 00000000
0005BA74 0000000E 0005C24E 8CFD0000

PAGE 1
8D000000 8CFD7000 00000324 00000000
00005104 70CCEEB2 00000001 A09D3FE0
00020000 00000001 00000E34 00000000
0000E176 0000288E 00000531 002FB230

PAGE 3
00000009 00000000 00000000 00000001
000986AF 000BDB40 0000001C 0000B3D0
00000000 00000000 00000009 00000000
00000000 00000000 00000714 00000714

PAGE 4
00000007 00000002 00000002 00000002
00000000 00000001 00000101 00000002
00000000 00000000 00000000 00000000
00000000 0000020F 8C00F18C 00000002

PAGE 5
47444D31 00000002 00000A60 000013F1
0105166C 00001F0F 00000002 00000000
000027A3 000002E0 00000013 00000000
00000000 00000000 00000000 00000000

PAGE 6
00000000 00000000 00000000 00000000
00000000 00000000 00000009 00000000
00000000 00000000 00000000 00000000
00000007 00000000 0000CF37 00000000

PAGE 7
00007AE6 0000DF22 00000FED 0000051A
00000000 000020FE 0000ADFE 00000001
00000EE8 00002283 00479A4C 00000531
00000001 000001FB 00000000 00000000
```

The captured state is PAUSED, fault 0, requested/applied generation 10/10,
with PAUSE as the last command. The expected driver remains verified,
stack fault is zero, active-bank writes and bus deferrals are zero, and
updater entries/returns are both 1,812. There were nine observed starts and
hardware loops. Committed source frames total 777,024 and retired frames
624,303, approximately 17.6196 and 14.1565 seconds of source PCM at 44.1 kHz;
they are not measured audible durations.

Page 6 records nine successful copy-reserve restart STOPs, zero restart
STOPs for each other reason, and no sampled needed-refill data exclusion.
The last recorded recovery has owned GD command zero. Page 7 exposes the
last denied gate's inputs before its STOP attempt:

| Latest reserve input | Captured value |
|---|---:|
| Accepted maximum stereo cursor | 31,462 |
| Frames to the accepted cursor's next half boundary | 1,306 |
| Nominal duration of those remaining frames | 29.6145 ms |
| Justified capture-bound age at denial | 73.11616 ms |
| Capture-bound age when the pair was accepted | 5.21856 ms |
| Age since the accepted observation, page 6 | 67.90016 ms |
| Destination bank / state | 0 / FILLING |
| Committed frames in that bank | 8,446 of 16,384 |
| Frames still uncommitted in that bank | 7,938 |
| Gate site | 1, playback |
| Pending probe age | 0.64896 ms |

The destination is only about 52% filled. The 73.116 ms age is the elapsed
time from the justified hardware-capture lower bound; it includes both the
5.219 ms age already present at acceptance and approximately 67.900 ms
after acceptance. These are distinct from an instantaneous physical hardware
position. The gate's conservative deadline is already expired, while its
destination remains substantially underfilled. The counters do not reveal
where that interval was spent or prove which physical half the hardware was
consuming at the decision. Page 7 records a denial before the STOP attempt;
page 6 records a successful recovery afterward, with slightly later time
sampling, so exact tick sums need not match.

The raw callback timing count is 1,329, matching the raw-sector call count;
3,125,808 bytes is exactly 1,329 times 2,352. Callback timings total
4,692,556 ticks, or 6.00647168 seconds. The mean callback is 4.51954227 ms,
the maximum 11.3088 ms and the last 4.88448 ms. These are cumulative
callback observations and do not reconstruct the particular refill window
shown in the pressure capture.

The mean callback alone exceeds the worker's existing four-millisecond
budget for admitting an additional refill quantum within one visit. That
budget is checked before additional work; it is not a timeout that truncates
a card callback already underway. Card latency plus observation/copy work
can consume the budget after the first quantum, leaving later quanta for
another visit. Together with the underfilled destination, this makes refill
scheduling a likely contributor worth modeling next. These counters do not
establish the fraction of visits limited this way, the exact available
service cadence, or the root cause of every recovery.

GD timings record 2,656 credited steps, 5,105 logical sectors, 17,110,636
accumulated ticks and maximum 7,951 ticks: 21.90161408 seconds of measured
dispatch time, 8.24608964 ms per credited step on average and 10.17728 ms
maximum. Maximum credited sectors remains two and invalid timing steps zero.
Credited payload divided by measured dispatch time is approximately
466.176 KiB/s, retaining the prior scope exclusions. GD diagnostics record
10,147 calls, 736 requests, 19 unclassified rejections and last error zero.
The worker-visit maximum is 13.28896 ms and active service-gap maximum
73.87904 ms. None of these establishes a physical SCI ceiling or whole-movie
throughput.

### Focused refill-service candidate after `0a898d37a2de`

The production change increases only the existing extra-quantum admission
threshold from 3,125 to 12,500 TMU0 ticks (4 to 16 ms). The four-quantum cap,
single-sector callbacks, two hooks, data priority, exact SR restoration,
capture proof, copy reserve, per-plane gates and acknowledged STOP fence are
unchanged. The linked worker retains its size and differs only in the
compiler's comparison-threshold literal. The snapshot remains API 6 / 448
bytes with the same eight-page fixed-two-sector report.

The physical stereo/PCM fixture targets 30 Hz main-context updates with two
hooks 10 ms apart; every 32nd update substitutes a 73.87904 ms inter-hook
gap. Card callbacks cost 4.29824 ms, with an 11.3088 ms callback every 32
reads, giving a nominal callback mean of 4.51732 ms before the fixture's
separate timer-read cost. Actual card and sound-bus work delays overdue hooks
and updates; these costs are not compressed into an ideal fixed cadence.
Four gap phases and three serial-publication phases make twelve schedules.

Scratch counterfactual comparisons reproduce refill STOP under the 4 ms
baseline; 8 ms passes five of twelve schedules and 12 ms passes eleven.
The remaining 12 ms case has 1,952 unfinished frames with about 13.7 ms of
physical playback before half entry, insufficient for its remaining card
reads and copies. Sixteen milliseconds passes all twelve schedules through
300 updates each, checking every physical stereo sample and the inactive
destination before and after every plane copy. A tested visit can reach
about 27.95 ms with the slow-read tail: the threshold admits work, rather
than imposing a hard 16 ms duration limit.

Sustaining the 11.3088 ms latency on every callback still produces a safe
copy-reserve STOP, with incomplete refill and pressure telemetry retained.
The candidate therefore does not claim service for every latency or
publication schedule. Longer synchronous bursts may delay the game's next
data request or video work, despite restoring SR between raw quanta. The
hardware comparison must assess audio continuity and this pacing tradeoff.

Exploratory 20 Hz schedules with a 10 ms inter-hook separation and a 10 ms
serial scan still fail to converge on a timely two-change proof, even with
the next bank READY. All tested admission budgets eventually request the
unchanged unobserved-half STOP. Increasing refill service does not cure that
separate publication/cadence limit; the locked-publication negative retains
safe failure coverage.

The final 19 host regression suites pass with AddressSanitizer and
UndefinedBehaviorSanitizer. Strict linked layout, instruction, embedding and
stack audits pass with the unchanged 16,600-byte worker payload ending at
`0x8cfd7000` and resident ending at `0x8c0077e8`. Hardware audio continuity and
video pacing for this candidate remain to be measured.

### Hardware run of refill-service build `62d9273595dd`

The next eight photographs, `62115.jpg` through `62122.jpg` in page order,
all show build `62d9273595dd`, API 6 / 448 bytes. Every page is readable,
including page 2, whose counterpart in the preceding report was unresolved.
The accompanying recording is `62114.mp4`, with duration approximately
38.7055 seconds. Only observations and diagnostic values are retained here;
the private game media is not included in the project or package.

The user reports substantially better behavior, with audio mostly continuous
until a later point, and suspects that video freezing is a separate issue.
The recording has visible picture holds around 7.7–11.1 seconds on the mouth
shot and 11.2–15.3 seconds on the boy shot, followed by an animated bear
sequence around 18–35.6 seconds. These are approximate recording timestamps,
not exact game frame boundaries. Nonzero audio RMS during picture holds
does not isolate the soundtrack from sound effects, room sound or other
audio. No independent claim of hearing continuous music is made. Neither
these observations nor the report proves that the video issue is independent
of refill work, game data service or update cadence.

The exact eight pages are:

```text
PAGE 0
54595031 00000006 000001C0 00000005
00000000 0000000A 0000000A 00000001
00000016 00000000 00000000 00000000
0005BD54 0000000E 0005C24E 8CFD0000

PAGE 1
8D000000 8CFD7000 00000324 00000000
00005104 70CCEEB2 00000001 A09D3FE0
00020000 00000001 00001D28 00000000
0000FBFC 00004082 00000778 00449E80

PAGE 2
00000000 00004AD6 00000020 00000041
00000005 0000003C 00000004 00103BC8
00000013 0000120B 00000000 00000000
00000000 00000000 00000000 00000000

PAGE 3
00000005 00000000 00000000 00000001
00101F8F 00111C7E 00000019 0000B3A0
00000000 00000000 00000005 00000000
00000000 00000000 0000062F 0000062F

PAGE 4
00000007 00000002 00000002 00000002
00000000 00000001 00000101 00000002
00000000 00000000 00000000 00000000
00000000 00000234 8CFD6DAC 00000002

PAGE 5
47444D31 00000002 00000A60 000013F1
0104E9CB 00001FDA 00000002 00000000
00002648 00000295 00000015 00000000
00000000 00000000 00000000 00000000

PAGE 6
00000000 00000000 00000000 00000000
00000000 00000000 00000005 00000000
00000000 00000000 00000000 00000000
00000007 00000000 00002661 00000000

PAGE 7
00007B38 00004590 00001F31 000004C8
00000000 00001814 00039814 00000005
00000BC3 00002286 006813AD 00000778
00000001 00001519 00000000 00000000
```

The captured state is PAUSED, fault 0, requested/applied generation 10/10,
with PAUSE as the last command. The expected driver is verified, stack fault
is zero and observed stack use is 804 bytes. Raw errors, queue errors,
stale actions, active-bank writes and bus deferrals are all explicitly zero.
Updater entries and returns both equal 1,583. There are five observed starts
and hardware loops, with no finite end or shutdown recorded.

Page 6 records five successful COPY_RESERVE recovery STOPs and zero for
each other recovery reason, compared with nine COPY_RESERVE STOPs in the
preceding run. Retired source PCM increases from 624,303 to 1,056,655 frames
(approximately 14.1565 to 23.9604 seconds at 44.1 kHz), while filled frames
increase from 777,024 to 1,121,406 (approximately 17.6196 to 25.4287 seconds).
These are cumulative source accounting, not measured audible durations.
The runs are not established as equal-duration measurements. Together with
the user's observation they support improvement, but do not quantify every
audible interruption or certify continuous playback.

The last denied copy gate is at site 5, a plane copy:

| Latest reserve input | Captured value |
|---|---:|
| Accepted maximum stereo cursor | 31,544 |
| Frames to the accepted cursor's next half boundary | 1,224 |
| Nominal duration of those remaining frames | 27.75510 ms |
| Justified capture-bound age at denial | 22.79424 ms |
| Capture-bound age when the pair was accepted | 10.22080 ms |
| Elapsed interval after that acceptance | 12.57344 ms |
| Age since accepted observation at successful recovery, page 6 | 12.57600 ms |
| Conservative time remaining at denial | 4.96086 ms |
| Destination bank / state | 0 / FILLING |
| Committed frames in that bank | 6,164 of 16,384, approximately 37.62% |
| Frames still uncommitted in that bank | 10,220 |
| Gate site | 5, plane copy |
| Pending probe age | 6.91328 ms |

The conservative remaining time is just below the unchanged 5 ms copy
reserve. Unlike the preceding 73.116 ms bound age at playback entry, this
gate is reached during refill work, with a substantial incomplete bank and
10.221 ms of bound age already present when its pair was accepted. This
shows an ongoing refill shortfall in that captured window. It does not
identify when the bank first became writable, how many refill visits were
available, which visit reached the four-quantum cap, or the physical cursor
at denial. The pressure capture precedes the STOP attempt and the recovery
breadcrumb samples time slightly later; their exact tick sums need not
match. No capture, progress, copy or ownership threshold is relaxed on the
basis of this snapshot.

The current report has 1,912 raw callbacks and timing calls, with 4,497,024
bytes exactly equal to 1,912 times 2,352. Raw callback timings total
6,820,781 ticks, or 8.73059968 seconds, giving a mean of 4.56621322 ms,
maximum 11.31264 ms and last 3.85408 ms. These are close to the preceding
4.51954227 ms mean and 11.3088 ms maximum. Improvement is therefore not
explained by faster measured raw callbacks. The active service-gap maximum
is now 82.57024 ms and worker-visit maximum 21.13792 ms, compared with
73.87904 and 13.28896 ms previously. These maxima do not identify the
particular interval that produced the latest denial.

There are 65 bank fills, five bank starts, 60 bank ends and four handoff
gaps. The handoff-gap maximum is 1,361.76640 ms. There are 19,158 copy calls
with a maximum timing of 40.96 microseconds, 19 start waits, 4,619 stop waits
and 7,464 service calls with zero skips. Corresponding fields from the
preceding blurred page 2 remain unresolved; they are not backfilled from
this run.

GD timings again record 2,656 credited steps and 5,105 logical sectors.
Their accumulated 17,099,211 ticks are 21.88699008 seconds, giving
8.24058361 ms per credited dispatch and a maximum of 10.43712 ms.
Credited payload divided by those dispatch windows is approximately
466.487 KiB/s, close to the previous 466.176 KiB/s. Maximum credited sectors
remains two and invalid timing steps zero. GD diagnostics record 9,800
calls, 661 requests, 21 unclassified rejections and last error zero. These
scoped measurements do not establish whole-movie throughput or the source
of the visible holds.

The needed-refill data-exclusion counters remain zero, and the last recovery
has owned GD command zero. As before, these only measure the existing
sampled priority refusals and do not exclude all card contention. No
physical SCI throughput ceiling or independent video fault is inferred.

The next candidate is measurement-only: API 7 retains the 448-byte worker
snapshot and fixed-two-sector eight-page report, repurposing the final two
reserved words for observed half-window age and worker-visit count at the
last denied copy gate. The 16 ms extra-quantum admission budget, four-quantum
cap and every existing gate remain unchanged. These measurements aim to
distinguish an already short writable window from insufficient refill
opportunities without changing live playback policy. Hardware confirmation
of that candidate remains outstanding.

### Refill-window measurement and timing counterfactuals

Scratch comparisons use the new raw-read mean and 82.57024 ms gaps. Slower
main-context schedules can reproduce incomplete-bank COPY_RESERVE even when
the earlier twelve 30 Hz schedules pass. Retaining cursor history until the
original 50 ms expiry, with fresh changes in both channels before renewal,
improves some schedules but regresses others and an existing slow-card
positive test. A 24 ms admission threshold with the same four-quantum cap
also improves some slower schedules while regressing previously passing
phases. Neither experiment is included in the candidate. These comparisons
do not identify the console's actual cadence or justify relaxing freshness,
progress, ownership, or copy-reserve requirements.

A new retained negative regression uses 30 Hz target updates, two hooks
10 ms apart, regular raw reads of 3,396 ticks and an 8,837-tick outlier every
32 callbacks. Their modeled mean, including timer sampling, is approximately
4.566 ms. The independently timed ARM scan is 7,813 ticks, with an 8-tick
inter-channel gap, 1,000-tick right publication delay and 5,208-tick initial
offset. After update 100, every fourth update with phase 2 substitutes an
82.57024 ms second-hook gap. Card and G2 work delay overdue hooks; no physical
playback time is compressed.

At update 191, this schedule requests COPY_RESERVE at a plane-copy gate with
7,052 of 16,384 frames committed. The physical consumer still has 816 frames,
about 18.50 ms, before half entry, while approximately sixteen sectors remain
uncommitted. Every stereo PCM sample and every physical copy destination is
checked through STOP. The full four-read cap is exercised and the maximum
modeled visit is 27.64672 ms. No raw reads or PCM copies follow the queued STOP.

The new telemetry records an observed refill window of 101,840 ticks,
130.3552 ms, with four counted service visits in that failure. In this model,
the accepted half transition was substantially later than physical half entry;
the bank received too few visits afterward to finish. These are modeled
observations, not a reconstruction of the hardware's most recent recovery.
The console measurements will determine whether that distinction applies.

The production changes add two scalar anchors and populate the two formerly
reserved pressure words. Anchors use existing timer/call observations at
accepted START or valid half transition; same-half proofs leave them alone.
The denial captures both differences in its existing SR-preserving scope.
No measurement participates in admission or grants a write. API/export/config
version advances to 7 with the same 448-byte snapshot and eight report pages;
the first 110 words retain their layout.

All 19 host regression suites pass with AddressSanitizer and
UndefinedBehaviorSanitizer, including the clustered-gap failure, observation
anchors across positive half transitions, recovery and pause/release epochs,
natural timer/service-counter wrap, and both terminal-report layouts. An
independent review confirms the fields cannot grant write permission.
Strict linked layout, instruction, embedding and stack audits pass. The
worker payload is 16,656 bytes, with the aligned reservation ending at
`0x8cfd7040`; the low resident remains 12,136 bytes ending at `0x8c0077e8`.
Conservative stack totals are 3,200 of 8,096 worker bytes and 1,228 of 1,232
resident bytes, within the unchanged audit limits.

### Hardware run of observed-window build `372b435abd26`

The next eight photographs show build `372b435abd26`, API 7 / 448 bytes.
Their filenames in page order are `image-1791490540999.jpg`,
`image-1791490553037.jpg`, `image-1791490563558.jpg`,
`image-1791490578906.jpg`, `image-1791490594972.jpg`,
`image-1791490608860.jpg`, `image-1791490623699.jpg` and
`image-1791490639547.jpg`. The user reports: “A little better. Still some
audio that stops but it does seem to resume the audio. Not sure if this is
the best we can get with this”.

This build changes measurement only, retaining the preceding 16 ms admission
budget, four-quantum cap and all live-ring gates. The reported auditory
improvement may therefore reflect run variation; it is not evidence that
the new counters change playback policy. No new recording accompanies this
report, and equal-duration comparisons are not established.

All eight pages were inspected. Bloomed characters on pages 5 and 7 were
checked using enlarged channel-separated crops and the committed 5-by-7
renderer glyphs. The page 5 accumulated timing word is `010508BE`; its fifth
character has the zero glyph's diagonal interior, followed by the centered
upper/lower holes of 8. Independent review agrees on the page 7 observed
window `0003225A`; its other initially ambiguous fields decode as bank fill
`000032E2`, raw total `0098A651` and probe age `0000016E`. No unresolved
word is retained in the following transcription.

```text
PAGE 0
54595031 00000007 000001C0 00000005
00000000 0000000A 0000000A 00000001
00000016 00000000 00000000 00000000
0005C010 0000000E 0005C24E 8CFD0000

PAGE 1
8D000000 8CFD7040 00000324 00000000
00005104 70CCEEB2 00000001 A09D3FE0
00020000 00000001 00001F07 00000000
000104AC 000040F4 00000A69 005FA4B0

PAGE 2
00000000 00006B4C 00000032 00000058
00000006 00000055 00000005 00105822
0000001D 0000125D 00000000 00000000
00000000 00000000 00000000 00000000

PAGE 3
00000006 00000000 00000000 00000001
001667F5 0017DA88 0000001D 0000B3E0
00000000 00000000 00000006 00000000
00000000 00000000 00000781 00000781

PAGE 4
00000008 00000003 00000003 00000002
00000000 00000001 00000101 00000002
00000000 00000000 00000000 00000000
00000000 00000306 8C00F1F4 00000002

PAGE 5
47444D31 00000002 00000A60 000013F1
010508BE 00001FE1 00000002 00000000
0000284D 00000387 00000015 00000000
00000000 00000000 00000000 00000000

PAGE 6
00000000 00000000 00000000 00000000
00000000 00000000 00000005 00000000
00000000 00000000 00000000 00000000
00000007 00000000 00001255 00000000

PAGE 7
00007CAA 00003210 00001FC5 00000356
00000000 000032E2 000332E2 00000004
00000BA8 0000227F 0098A651 00000A69
00000001 0000016E 0003225A 00000008
```

The terminal state is PAUSED, fault zero, requested/applied generation
10/10, with PAUSE as the last command. The expected driver remains verified.
Stack fault, raw errors, queue errors, stale actions, active-bank writes and
bus deferrals are explicitly zero. Observed stack use is 804 bytes. Updater
entries/returns both equal 1,921. Six observed starts and six hardware loops
are recorded, with no finite end or shutdown.

Page 6 again records five successful COPY_RESERVE recovery STOPs, with zero
for every other recovery reason. The data-exclusion counters and last owned
GD command are zero, with their existing sampled scope. These do not exclude
all physical card contention. Retired PCM is 1,468,405 frames, approximately
33.2972 seconds of source audio at 44.1 kHz; filled PCM is 1,563,272 frames,
approximately 35.4483 seconds. Neither is a measured audible duration or a
sound-quality score.

The latest denied gate exposes a substantially incomplete next half after
a sparse observed refill window:

| Latest reserve input | Captured value |
|---|---:|
| Accepted maximum stereo cursor | 31,914 |
| Frames to the accepted cursor's next half boundary | 854 |
| Nominal duration of those remaining frames | 19.36508 ms |
| Justified capture-bound age at denial | 16.40448 ms |
| Capture-bound age when the pair was accepted | 10.41024 ms |
| Elapsed interval after that acceptance | 5.99424 ms |
| Age since accepted observation at successful recovery, page 6 | 6.00704 ms |
| Conservative time remaining at denial | 2.96060 ms |
| Destination bank / state | 0 / FILLING |
| Committed frames in that bank | 13,026 of 16,384, approximately 79.50% |
| Frames still uncommitted in that bank | 3,358 |
| Gate site | 4, after card access |
| Pending probe age | 0.46848 ms |
| Observed half-window time | 205,402 ticks, 262.91456 ms |
| Service visits across that observed window, including both endpoints | 8 |

The conservative remaining time is already below the unchanged 5 ms
reserve, while 3,358 destination frames remain uncommitted. The new fields
show that this is not merely a few-millisecond window first noticed near
the boundary: approximately 263 ms elapsed between the first accepted cursor
in that playing half and this denial, with only eight service visits.
Those visits include skipped work within an otherwise admitted service,
not only successful raw callbacks. This supports examining refill work
available between game updates and the work admitted in each visit. It does
not reveal the physical half-entry time, the exact raw-read sequence in the
window, or what occupied each gap. The accepted cursor and capture bound
are conservative observations, not an instantaneous physical position.

There are 2,665 raw callbacks and timing calls, with 6,268,080 bytes exactly
equal to 2,665 times 2,352. Their total duration is 10,004,049 ticks,
12.80518272 seconds, giving a mean of 4.80494661 ms, maximum 11.30368 ms
and last 3.81952 ms. The current mean is about 5.2% above the preceding
4.56621322 ms mean; the maximum remains close to the previous 11.31264 ms.
These cumulative callback measurements do not reconstruct the particular
window captured on page 7.

The active service-gap maximum is 85.41696 ms and worker-visit maximum
21.28384 ms. There are 7,943 service calls with zero skips. There are 88
bank fills, six bank starts, 85 bank ends and five handoff gaps; handoff-gap
maximum is 1,371.05664 ms. Copy calls total 27,468 with maximum 64
microseconds. Start waits total 29 and stop waits 4,701. These counts do not
identify the timing of each audible interruption.

GD data measurements retain 2,656 credited dispatches and 5,105 logical
sectors. Total 17,107,134 ticks corresponds to 21.89713152 seconds, with
mean 8.24440193 ms per credited dispatch and maximum 10.44608 ms. Credited
payload divided by those measured dispatch windows is approximately
466.271 KiB/s, close to the preceding 466.487 KiB/s. Maximum credited sectors
remains two, invalid timing steps zero, and GD diagnostics record 10,317
calls, 903 requests, 21 unclassified rejections and last error zero. These
measurements do not establish a physical SCI ceiling, whole-movie throughput
or an independent video-fault cause.

The snapshot identifies remaining refill pressure with sparse observed
service and a measurable completed-fill deficit. It does not establish the
best attainable hardware continuity. Further candidate work is not treated
as shipped or console-validated by this report.

### Follow-up scheduling experiments: retained runtime

The new callback distribution was modeled as 3,589 ticks for a regular raw
read and 8,830 ticks every 32nd read, with an independently charged timer tick.
Its mean is approximately 4.80484 ms and maximum 11.30368 ms. Long scheduled
hook gaps were 66,732 ticks, or 85.41696 ms. These are synthetic schedules
informed by cumulative console timings, not a reconstruction of this run.

The independent asynchronous PCM consumer swept four gap phases and three
ARM publication phases at target update rates of 30, 26 and 24 Hz, with
10 ms or 16.67 ms spacing between the two hooks: 72 profiles of 300 updates.
Card and G2 operations consumed actual model wall time and delayed later
hooks. Exact left/right samples, physical copy destinations, source order
and the unchanged ownership/deadline gates remained checked.

An authored scratch prototype used eight aligned, generation/LBA-tagged raw
sector slots in SH memory. It fetched ahead only with the active bank PLAYING
and the opposite bank READY. It retained the one-sector callback, whole-visit
16 ms admission budget and four-physical-callback cap; cached copying could
use additional bounded actions. It did not change sound RAM allocation or
cursor proof acceptance. Source-level review and ASan/UBSan checks covered
finite EOF, tiny repeated source wraps, data16/17 and resident exclusion,
SDK/driver/sound-generation admission, control/revoke during callbacks,
stale cache publication and a cached sector's deferred right-plane retry.

Those safety checks do not establish improved continuity. Eighteen queue
variants (two/four/eight slots, four/six/eight actions, and either READY-only
or current-visit-proof eligibility) were compared against the 72 profiles.
The best variants passed 62 profiles versus the current worker's 54, but
lost six previously passing profiles while gaining fourteen. The original
eight-slot/eight-action prototype lost ten while gaining fifteen. The
established positive regression fixtures passed; the expanded timing sweep
exposed the regressions. No queue variant was selected for hardware.

A smaller experiment omitted the post-raw cursor probe when an existing
accepted proof retained more than 100 ms of conservative write time. Every
service-entry observation and every pre/post-card and plane write gate stayed
in place. It passed 59 profiles, losing nine previously passing profiles and
gaining fourteen. Its new failures were unobserved-half recoveries, often
with the next bank already READY. This alternative was also rejected.

The original `372b435abd26` runtime remains the hardware recommendation.
No playback-policy change, new build or console retest is implied by these
experiments. Current evidence leaves both refill scheduling and cooperative
cursor publication as unresolved limits; it does not establish the card's
best attainable continuity. Longer synchronous bursts and a larger sound
ring were not selected as remedies. In particular, simply doubling the
power-of-two ring would program the previously reviewed prohibited `0xffff`
loop-end value, so that is not a valid constant-only enlargement.
