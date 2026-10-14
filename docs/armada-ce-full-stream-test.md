<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE full CD music stream test

## What this revision changes

Recording `62654.mp4` confirms audible gameplay music in `1ae245a365d8`.
The return summary records WORKER 1, PLAY `B` (11), REFUSED 0 and HRESULT 0,
with nine recoveries. The retained error is a real UNDERRUN; recorded playback
events also show buffer depletion. Startup binding now succeeds.

While playing, samples at 109,400 and 111,400 ms show SOURCE increasing from
`00669780` to `0068E3B0`: 150,576 bytes in two seconds, or 75,288 B/s.
Stereo CD audio consumes 176,400 B/s. READY falls from 254,208 to 50,944 bytes.
The longest source job is 1,050 ms, although a 16-sector job carries only
213 ms of audio. The longest worker visit is 56 ms. These measurements identify
insufficient source supply in this run, rather than a startup or native Play
refusal. They do not by themselves prove where every job's delay occurred.

Audio arbitration now enters urgency at one second of fresh PCM or less and
leaves at 1.5 seconds; pause, stop and failure clear it. An urgent audio grant
allows 64 card blocks instead of eight. DATA retains 16 blocks, permitting an
80% audio share in contended asynchronous turns instead of one third. Each
pump still handles at most one received block. Synchronous foreground DATA
retains immediate preemption after the in-flight block, and the shared DATA
reader is unchanged. This is a source-scheduling change, not a larger native
ring or an alteration to cursor/underrun recovery.

The working driver-to-game callback, full native identity admission, worker
priority 2, 16-sector reads and source reservations are retained. Startup
still uses metadata-only discovery followed by full binding inside Armada's
owner process before any sound/thread API. Hardware sustained supply remains
to be confirmed; host tests cannot establish CE scheduling or audibility.

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
Events are START `1`, UNDERRUN `2`, FAULT `3`; samples use `4`.
Startup events DISCOVERY `5`, OWNER BIND `6` and WORKER `7` are recorded even
before the worker exists. Their MS field is zero because no native clock
exists yet, and AUX carries the admission/start status (zero means success).
Thus initial refusals no longer leave the event page empty; periodic samples
correctly stay zero until the worker starts. Rows contain
code, native milliseconds, cumulative source bytes, fresh ring bytes and AUX.
Sample AUX bit 0 means pending read, bit 1 means native playback running,
bit 2 means urgent audio grants; bits 8 onward contain model state. Counts include maximum worker visit and
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
