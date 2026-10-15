<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE full CD music stream test

## K-UI 2.0 RC8 transient supply reserve

This console candidate integrates the net changes from
`fix/armada-cdda-supply` at `be793d667cf0` into our current `eb06827d85da`
source. It preserves the working owner callback, full native identity checks,
priority 2, 16-sector reads and adaptive low-buffer grants.

Claude's cursor-authoritative pacing removes the cumulative 20 ms rate stop.
Native position still cannot jump beyond fresh PCM, advance ambiguously by
more than half a ring, or use an observation older than 999 ms. Refill follows
the driver's actual consumption. RC1 reduced restart to one second of fresh
PCM instead of two. Staging keeps an offset after partial refills; only the
sub-64-byte carry moves before the next read, outside the interrupt mask.
Worker visits are recorded in the upper half of sample AUX.

Recording `62655.mp4` retained a real UNDERRUN: delta 9,088 bytes was below
the 11,388-byte rate allowance. Its first two playing sample intervals supplied
94,464 B/s against nominal 176,400 B/s. Thus the rate-stop removal addresses a
latent clock-drift fault, while this test also needs faster source delivery.

Recording `62656.mp4` confirms RC1 plays substantially better but still
underruns. Across three uninterrupted one-second intervals (MS `23348` to
`23f00`), source grows by 413,952 bytes: 137,984 B/s. Source minus READY
change implies 176,811 B/s consumed. Each row records 20 worker visits. The
last underrun advances 9,088 bytes with only 4,416 fresh bytes. Thus the
remaining stops have a measured supply deficit; the removed rate guard is
not involved. Maximum recorded job interval fell from 1,050 to 646 ms.

SCI's polling fallback completes a card block immediately without another
interrupt. RC1 drains at most 32 ready blocks per source visit, while a
37,632-byte, 16-sector job spans up to 75 card blocks when unaligned. Its
urgent grant of 64 blocks also forces a DATA handoff before that job finishes.
RC2 matches the urgent grant to 75 blocks and the worker to 76 pumps (including
the initial fetch-only pump). A ready polling job can complete within one visit.
Interrupts are restored between pumps; incomplete DMA, DATA handoff, a new
command, error or completion still ends the drain. This removes two avoidable
sources of scheduler delay; it does not guarantee card or CE timing.
DATA's 16-block quantum and immediate foreground preemption are unchanged.

Recording `62658.mp4` confirms further improvement in RC2. During the final
five uninterrupted one-second sample intervals (MS `1f95f` to `20ce7`), source
grows 903,168 bytes (180,633.6 B/s) against implied consumption of 176,947.2 B/s.
READY remains only 52,992–88,192 bytes while the maximum job interval is 650 ms.
The last underrun sees 8,448 bytes consumed with only 2,432 fresh bytes. Supply
can now match playback in this sample, but has little reserve for delayed work.

RC3 fixes a completed-read publication delay. The final replacement source
visit can now complete a whole PIO job, but RC2 returned with that PCM still
in staging. The next visit observed the native cursor before refilling, so it
could stop despite up to 37,632 bytes already read. A refill immediately after
that replacement source visit publishes eligible PCM before Sleep. It retains
all native locks, cursor/frontier checks and command-epoch publication checks;
a still-pending read makes refill return without acquiring the native lock.
No additional source jobs, transport grants or scheduling changes are added.
A focused player regression fails on RC2 and passes with this handoff: two
completed jobs reach the native ring before a 350 ms delayed visit, rather
than leaving the second job unavailable to observation. This proves the code
path, not that every console underrun has this cause.

Recording `62659.mp4` retains real underruns in RC3: the summary reports 13
gaps and a maximum approximate source-job interval of 1,152 ms, while worker
visits remain around 20/s (maximum 54 ms). Across two uninterrupted one-second intervals (MS `22137` to `22907`),
SOURCE grows from `e56af0` to `ea02f0`: 150,528 B/s, below nominal 176,400 B/s;
READY falls by 49,536 bytes. The final displayed underrun has
1,088 fresh bytes and a 50 ms observation interval. That recording does not
independently count token waits, so it cannot prove how much of its source
latency comes from the following source-level stall.

SCI's bounded token search can return PENDING after 256 clocked bytes without
a ready block or DMA in flight. No reception interrupt can wake that token
search. RC3's ready-only predicate ended the worker visit in this state, adding
a native sleep/visit delay between token slices. RC4's state query permits the
worker to continue a pending, AUDIO-owned transfer with no DMA in flight.
Token slices share the existing 76-pump bound and each retains its own interrupt
mask. Starting DMA, DATA handoff, cancellation or completion stops the drain.
The query performs no I/O, poll or wait. No descriptor, reader, allocation,
priority, batch-size, grant or buffer-start changes are made.
Focused tests exercise token-only pending states with no IRQ, enforce the pump
bound, stop immediately at DMA/preemption/cancellation and verify exact audio.
The next console result must establish whether this removes the remaining
interruptions; host checks cannot establish CE or SD timing.

Recording `62660.mp4` confirms RC4 substantially improves playback: 3 recorded
underruns versus 13 in the prior run, and maximum source-job duration falls
from 1,152 to 550 ms. Worker visits remain around 20/s. This is not zero
underruns, and different run lengths/scenes prevent treating those counts as
a controlled benchmark. The final displayed underrun retains 7,168 fresh
bytes before an 8,448-byte cursor advance over 50 ms.

The 76-pump bound leaves almost no room beyond the 75 card blocks and initial
reception setup of a worst-case job. Token slices use that same budget without
advancing a card block. RC5 adds 16 pumps of explicit token allowance (at most
4,096 extra token-clock bytes), for a bound of 92. Reads still contain 16
sectors, urgent grants still contain 75 blocks, and an incomplete DMA or DATA
handoff still ends work immediately. No extra jobs or native scheduling changes
are introduced. A player fixture completes 16 token slices and a full job
inside one visit; a prolonged token wait still stops at the exact bound.

A new diagnostic counter records a read visit that hits this bound while
AUDIO could continue without DMA. Bits 4–7 of sample AUX give the number of
these budget yields since the previous sample, saturated at 15. This counts
forced yields, not milliseconds or underruns. It makes remaining budget
pressure visible without another page. Existing timestamps and SOURCE/READY
remain necessary to judge supply; this allowance is not a console-success claim.

Recording `62661.mp4` still contains audible cuts according to the console
report. Its retained six sample rows all have zero budget yields, with native
visits around 20/s and a maximum approximate job interval of 750 ms. Those
samples follow the last retained underrun, so they do not measure budget
pressure at that earlier instant. This evidence does not justify another
pump-limit increase; RC6 keeps the 92-pump bound and its counter.

The source review identified a separate ring-wrap publication gap. A native
refill ticket stops at the end of the circular buffer. A completed replacement
job can therefore publish only a short tail and leave its wrapped remainder
staged until the next visit. That visit observes consumption before draining
staging, and the native protected frontier can then block the waiting remainder.
RC6 drains at most two eligible refill segments before yielding. Each segment
retains its own native lock, current frontier, mapping witnesses, command epoch
and Unlock publication check. If no bytes publish, the drain stops immediately.
One staged job is smaller than the ring, so at most one wrap is involved.
The real-player regression reproduces RC5 leaving 36,608 bytes staged, then
underrunning and calling Stop after two 50 ms native visits. With this bounded
drain, all eligible checked bytes publish before that yield and those visits
remain playing. No source-prefix API, source-job size, DATA-reader or scheduling
change is made.

Recording `62662.mp4` retains two real UNDERRUN events in RC6. The last
has 8,000 fresh bytes and a 50 ms observation interval. SOURCE is unchanged
from the sample 250 ms before that event; maximum recorded source-job duration
is 700 ms, while the retained sample rows show zero pump-budget yields. This
short recording does not include the summary page or the total underrun count,
and cannot identify every interruption heard by the console tester.

RC7 removes the all-or-nothing source publication barrier. A pending physical
read already exposes an ordered, CRC-checked RAW prefix. The worker can now
commit its aligned prefix into staging and refill the native ring while the
same job continues. The ticket retains its identity and advances its RAW byte
offset; DONE commits the remaining suffix. Partial data never certifies EOF,
and final finite-track padding still waits for DONE. All command epochs,
native locks/frontiers, mapping witnesses and exact byte counts remain checked.
A late idempotent RELEASE retains the physical job until its publication can
commit; a genuine STOP or new PLAY cancels it through the existing control path.

Focused checks cover prefix-to-native publication before DONE, exact byte
continuity, resumed reads whose initial skip has not yet arrived, carry, ring
wrap, cancellation and RELEASE during pending/final publication. The model
suite passes 37,685 sanitizer checks and the player suite passes 45 cases.
This changes neither the shared DATA reader nor physical read size, priority,
transport grants, pump bound, start threshold or audited player/model layout.
It addresses a source-proven publication delay, not a measured hardware-success
claim; the next console run must establish its effect on the remaining cuts.

Recording `62663.mp4` confirms RC7 has one recorded underrun and no refusal or
fault. Maximum approximate source-job duration is 500 ms and maximum visit
interval is 56 ms. From START at MS `e7f0` to UNDERRUN at `11413` (11,299 ms),
SOURCE grows from `3a9500` to `562500`: 1,806,336 bytes, about 159,867 B/s.
Fresh bytes fall from 188,160 to 5,504. This is an early supply deficit,
not merely a single 500 ms job using up an otherwise-full two-second ring.
Recovery starts 902 ms later, after sourcing 187,120 bytes (about 207,450 B/s),
and no further underrun occurs for 36,448 ms before the next START. Final
sample READY values range from 278,608 to 348,672 bytes, with zero budget
yields and normal transport priority. Workload/selection changes prevent
treating the later samples as a controlled continuation of the earlier deficit.

RC8 starts with seven complete batches (263,424 bytes, about 1.49 seconds).
That adds 75,264 bytes, or about 427 ms of audio, beyond RC7's actual
188,160-byte start. The threshold is batch-aligned because admission waits
for the pending read to finish. A simple three-quarter-ring threshold would
round up to eight whole batches. This is a bounded reserve adjustment for
the measured transient deficit, with additional startup/recovery buffering;
it cannot fix a permanent supply shortage. Progressive publication, transport
grants, thread priority, DATA reader and native checks are unchanged. A focused
model regression exercises a quantized early deficit followed by recovering
supply; it does not reproduce the console's unknown exact per-visit timing.
The focused sanitizer replay passes 2,949 checks, retaining at least 67,800
fresh bytes. The same replay built with the former half-ring threshold
underruns at 10,900 ms. The 45 player cases also pass with the new reserve.

Focused player checks cover clock drift, real underruns, finite/infinite
playback, staging carry and bounded PIO draining/cancellation. Transport checks
cover immediate PIO readiness, DMA yield, quota handoff and cancellation.
Console memory/stack/bootstrap/owner-reader checks still apply. These establish
code behavior and build validity; continuous hardware music and FMV echo are
not yet certified. This is a 2.0 release candidate, not a final release claim.

## Console test

Replace both files from the ZIP's `KUI` folder with the console off and cold
boot. Select **Original Armada → Y Options → CD music On → START**.
Use the same in-game scene where the earlier loop was audible. Return after
music fails, or after about 60 seconds if it continues, with
**A+B+X+Y+Start**. Record one **15-second cycle** of the three diagnostic
pages, which repeat every five seconds until power-off. Then power-cycle.
No main-menu wait, gameplay video, new extraction or private upload is needed.
Main-menu CD music has not been verified for Armada.

The package includes saved launch toggles, tests in advanced options, the
GitHub link beneath the logo and removal of the unused L Memory shortcut.

## Reading the evidence

The summary retains binding, source, worker, native Play, buffer and cursor
values. The other pages retain six playback events and six periodic samples.
Events are START `1`, UNDERRUN `2`, FAULT `3`; samples use `4`.
Startup events DISCOVERY `5`, OWNER BIND `6` and WORKER `7` are recorded even
before the worker exists. Their MS field is zero because no native clock
exists yet, and AUX carries the admission/start status (zero means success).
Thus initial refusals no longer leave the event page empty; periodic samples
correctly stay zero until the worker starts. Rows contain
code, native milliseconds, cumulative source bytes, fresh ring bytes and AUX.
Sample AUX bit 0 means pending read, bit 1 means native playback running,
bit 2 means urgent audio grants, bits 4–7 count budget yields (saturating at15),
bits 8–15 contain model state, and the upper
16 bits count worker visits since the previous sample. Divide by the MS
interval to get visits per second; intervals need not be exactly one second. Counts include maximum worker visit and
source-job intervals, and completed jobs. Source-job duration is approximate.

While playback runs uninterrupted, required source growth is
`176400 × elapsed_ms / 1000` bytes. `2B110` bytes per row applies only to
exactly one-second rows. Prefill and recovery bytes do not measure audible
throughput. READY should not steadily fall to zero. Compare measured supply
and visit intervals before making further scheduling or recovery changes.

The earlier `0c0318266201` console run reached Play and reported an underrun
with an 82 ms interval; `569eb18f045a` booted but did not reach native Play.
These distinguish initial admission, initial filling and sustained supply.
Host tests establish those logic paths, not CE scheduling or console sound.

## FMV limitation

FMV echo remains unresolved and is reported even when WORKER and PLAY are
zero. CE GD dispatch masks interrupts while synchronous PIO completes its
piece; this can delay native sound service, but its duration and relationship
to the echo have not been measured. This revision does not change that reader
or claim an FMV fix. Native sound uses SYSINTR 23; synthetic GD drive/DMA
notifications use 20/21.

## Restore and provenance

**CD music Off** selects the ordinary CE loader. To restore the working
excerpt exactly, merge `restore-full-stream/KUI` with the console off and
cold boot. Both backup files are exact build **ecf23db0a902**.

Both new payloads must match clean compiled ELFs and the same source commit,
including display/envelope build IDs. The package contains source archives,
recovered Git history, dependencies/licenses, focused build evidence and
member SHA256/CRC manifests. Only this exact Original US Armada profile is
admitted. No private game executable, CE kernel, ARM firmware or audio sample
is distributed; the audio source map is blank until filled by the launcher.
