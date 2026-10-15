<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE full CD music stream test

## K-UI 2.0 RC2 supply budget

This console candidate integrates the net changes from
`fix/armada-cdda-supply` at `be793d667cf0` into our current `eb06827d85da`
source. It preserves the working owner callback, full native identity checks,
priority 2, 16-sector reads and adaptive low-buffer grants.

Claude's cursor-authoritative pacing removes the cumulative 20 ms rate stop.
Native position still cannot jump beyond fresh PCM, advance ambiguously by
more than half a ring, or use an observation older than 999 ms. Refill follows
the driver's actual consumption. Restart now requires one second of fresh
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
bit 2 means urgent audio grants, bits 8–15 contain model state, and the upper
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
