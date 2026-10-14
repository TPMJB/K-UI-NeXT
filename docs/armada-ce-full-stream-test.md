<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE full CD music stream test

## Dropout fixes on top of the working owner-discovery build

The owner reports that `9159aa5` plays CD music but it cuts out at times.
In this controller every recovery (GAPS) stops native playback and refills
the ring before Play: an audible gap. This revision removes a false recovery
and shortens the real ones. Owner discovery, binding, 16-sector jobs,
priority 2, the DATA reader and the SCI transport are unchanged.

| Change | Before | Now |
| --- | --- | --- |
| Cursor "moved too fast" recovery | Total cursor progress compared with total GetTickCount time plus 20 ms | Removed |
| Play starts / restarts after a recovery at | whole 2 s ring fresh | 1 s fresh |
| Staging after a partial refill | Remainder (up to 37 KB) shifted byte by byte with IRQs masked | Head offset; only the sub-64-byte carry moves, unmasked |
| One-second sample AUX | bit 0 pending, bit 1 running, bits 8-15 state | same, plus bits 16-31 = worker visits in that second |

The removed check accumulated elapsed playback since `011f71d`. The cursor
follows the sound chip's 44.1 kHz clock and GetTickCount follows CE's timer;
any rate mismatch between them grows with playback time until it exceeds the
fixed 20 ms slack and stops healthy music. With the previous model, the new
player test (`drifting_clock`) stops 2 times in two minutes at a 0.05%
mismatch, 5 at 0.1% and 28 at 0.5%; this revision stops 0 times. The
console's actual mismatch has not been measured. The cursor is
authoritative: AGE (observations at most 999 ms apart, under half the ring)
bounds ambiguity, and a jump past fresh data remains UNDERRUN.

### Test

Install only the new `KUI/apps/games/ce-cdda-armada.kui`; keep the current
`runtime.kui`. Play two or three minutes in the scene where music worked.
Note each cut-out and roughly when it happened. Return with
**A+B+X+Y+Start** and record a full cycle of the three pages.

### Reading the result (hexadecimal)

| Where | Healthy | If not |
| --- | --- | --- |
| Summary `GAPS` | `0` | Each gap is one recovery and an audible cut-out. |
| Summary `AUDIO WHY` (no fatal fault) | `0` | `13` cursor, `14` AGE (worker gap over 999 ms), `15` UNDERRUN (supply). |
| Events | `1` START only | `2` UNDERRUN: READY at the time and the visit interval in AUX. |
| Samples `SOURCE` | Grows by about `2B110` per one-second row while playing | Slower growth with READY falling is a supply shortfall. |
| Samples `AUX` bits 16-31 | `0014` (20) or more visits per second | Low values mean the worker is still starved. |

CE's scheduler tick on this kernel is 25 ms (`tools/armada_ce_rom_audit.py`),
so worker sleeps of 1-20 ms all wake on the next tick and a visit comes at
best every 25 ms. A 16-sector job per visit still allows about 1.5 MB/s.

`13` should no longer occur. If cut-outs remain with `15`, the samples show
whether the card (SOURCE) or the worker (visits) fell behind.

## What this revision changes

Recording `62653.mp4` shows initial binding still refused in `4185d3932103`:
BIND `A`, WHY `10`, address `03CC6198`, read status `1` (ARGUMENT), current
process `8C14E8B4` (disc driver index 3). Armada's sound owner is process 5.
SOURCE, WORKER, PLAY and all running history remain zero. A focused authored
fixture reproduces the exact refusal; the last reader wrongly required the
sound DLL to be attached to the current driver before the owner callback.

Startup now discovers only the destination process from bounded kernel
module/process metadata. It repeats module links, exact profile fields,
attachment/ref counts and the selected process/index/slot/handle. It reads
no virtual DLL, game, object or API word in this phase. The existing pinned
kernel callback then enters that process and runs the complete native sound
binder. Game/module CRCs, sections, references, objects, methods and mapping
proofs must all pass before sound or thread APIs are used. An ambiguous owner
or changed generation is refused. Native values are never read through the
foreign driver's access-key context.

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
Events are START `1`, UNDERRUN `2`, FAULT `3`; samples use `4`.
Startup events DISCOVERY `5`, OWNER BIND `6` and WORKER `7` are recorded even
before the worker exists. Their MS field is zero because no native clock
exists yet, and AUX carries the admission/start status (zero means success).
Thus initial refusals no longer leave the event page empty; periodic samples
correctly stay zero until the worker starts. Rows contain
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
