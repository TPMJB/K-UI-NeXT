<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE full CD music stream test

## What this revision changes

Recording `62652.mp4` supplies all three return pages from `b1cd5b774eb5`.
SOURCE, WORKER, PLAY and every timing/history counter are zero. BIND is `8`
(CHANGED); the retained witness is WHY `10`, address `03CD1104`, observed
read status `8`, CCR `105`. Initial sound binding failed before the worker
started. This recording cannot measure streaming bandwidth or underruns.

The initial restored CE caller now observes sound-module values through its
native virtual address context. It captures the current process, index,
slot and handle twice; every native word read retains before/after mapping,
TTB, CCR and privilege checks. Candidate DLL ranges are available only while
checking their sections and CRC. Full module identity, reference, object and
method checks remain. Static callback storage survives initial API admission.
Worker revalidation already used native observations; this closes the earlier
initial-admission path that still used cache-array values.

Worker priority changes from `4` to `2`, above kernel normal priority `3`.
SNDCORE explicitly requests priority `1`, so native sound service keeps
precedence. Existing 16-sector jobs carry 37,632 bytes, about 213 ms of stereo
PCM, inside the same reserved source bank. The shared GD/DATA reader and
native ring size are unchanged.

## Console test

Replace both files from the ZIP's `KUI` folder with the console off and cold
boot. Select **Original Armada → Y Options → CD music On → START**.
Use the same in-game scene where the earlier loop was audible. Return after
music fails, or after about 30 seconds if it continues, with
**A+B+X+Y+Start**. Record one **15-second cycle** of the three diagnostic
pages, which repeat every five seconds until power-off. Then power-cycle.
No main-menu wait, gameplay video, new extraction or private upload is needed.
Main-menu CD music has not been verified for Armada.

The package includes saved launch toggles, tests in advanced options, the
GitHub link beneath the logo and removal of the unused L Memory shortcut.

## Reading the evidence

The summary retains binding, source, worker, native Play, buffer and cursor
values. The other pages retain six playback events and six periodic samples.
Events are START `1`, UNDERRUN `2`, FAULT `3`; samples use `4`. Rows contain
code, native milliseconds, cumulative source bytes, fresh ring bytes and AUX.
Sample AUX bit 0 means pending read, bit 1 means native playback running;
bits 8 onward contain model state. Counts include maximum worker visit and
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
