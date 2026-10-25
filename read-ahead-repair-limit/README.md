<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution 2 read-ahead repair-limit diagnostics

This diagnostic extends the read-ahead candidate with the stage and result
of each failed speculative operation, first/last failure snapshots, and
counters for skipped production opportunities. Scheduling, recovery and
transport limits retain the preceding candidate's policy. The extra register
sampling adds work, so this run is intended to identify the failure path.

SCI remains at priority 1. A 192 KiB physical-card cache fills after a demand
read completes, using the time before the game asks for the next chunk.
The cache preserves the existing raw/cooked sector decoder and request
completion rules. Misses and backward seeks use the existing demand reader.

The isolated profile reserves 384 KiB instead of 192 KiB through the same
verified game allocator. The 128 KiB VMU worker, map and reader region keep
their addresses within that allocation. New cache code and its 4 KiB private
stack occupy separately admitted space; cache data lives in the upper 192 KiB.
No additional game timer, runtime SD log or future game buffer is retained.

Speculation stays inside the current validated track extent and never crosses
an unproved physical-card boundary. A block becomes valid only after the
existing SCI CRC check and a complete 512-byte copy. Speculative failures do
not complete or fail an unrelated game request. VMU storage loans quiesce
speculative DMA; checked cached disc bytes survive the loan.

## Install and capture every run

From the extracted folder:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2 --profile-runtime
```

Use SCI, Background On, Normal speed, Auto/default encoding, CD music Off and
RAM patches Off. Safely eject, then cold boot. Expect **SDK SCI ROUTE READY /
LEVEL 1**, followed by **BOUNDED READ AHEAD READY / 192 KIB CACHE**. If launch
stops at **PHASE 0B**, photograph the detail code.

Test the same new-game opening and movement area. Hold both triggers fully
plus B for about one second, release, and keep moving until the intentional
diagnostic stop after roughly 20 seconds. Wait for **CRC VALIDATED PHYSICAL
READ AHEAD**, then record until **LOADER TIMELINE - READ ONLY CAPTURE** appears.
This captures the cache page, **READ AHEAD EXIT REASONS**, and the first/last
failure pages. Allow **25 seconds** from the cache heading; the longest
targeted section is four pages, about 16 seconds at 60 Hz or 19.2 seconds at 50 Hz,
plus drawing time. A single failure uses a combined **BOTH** page;
no failure skips the snapshot pages.

All timeline and request data is still collected on every run with the same
command. If the full timeline recording is needed, record until the first
page repeats. The largest ordinary cycle now has 17 pages: 73 seconds at 60 Hz
or 87.6 seconds at 50 Hz, plus drawing time. Pages repeat the same frozen
capture; they do not collect more samples. No separate capture switch is needed.

Compare the remaining in-engine hitches and confirm that FMVs stay fluid.
The first uncached read and an unpredicted seek can still take the normal
reader's time. Subsequent sequential requests are the main target.

## Read-ahead page

All values are hexadecimal. **CAP** is 384 physical 512-byte blocks, not
Dreamcast sectors. **HITS** counts cached blocks fed to the original cursor;
**HIT REQS** counts requests receiving at least one cached block, including
partial hits. **MADE** counts CRC-checked speculative blocks. **HEARTBEAT**
and **IRQ** show the service opportunities; **MAX AUX VISIT TICKS** is elapsed
visit time at 781250 ticks/second and includes demand fallback work. Counters
reset at the capture gesture while valid cache data remains available.

The ordinary request pages still report whole-read times. A single cold
read may dominate the maximum even if later requests become faster. These
counters do not directly measure rendered frames or the duration of a hitch.

## Failure and skip pages

All values are hexadecimal. **FAIL** counts recorded speculative failure
exits. **POLL**, **TAKE**, **APPEND**, **FETCH**, **RESUME**, and **QUIESCE** count
those exits by operation stage. The complete stage/result histogram remains
in the retained diagnostics; the screen prints its stage totals and the
complete first/last records. **STOP** on the exit page is the QUIESCE stage;
on the cache page it counts quiesce operations.

The three **SKIP** rows follow the producer's return guards: quiesced,
abandoned, no seed, active VMU loan, active demand reader; pending/executing
request, failed reader, unopened/unacquired port, port fault, busy transport;
DMA admission guard, incompatible stream position, deferred resumption, and an already
owned speculative block. These are counts of exits, not elapsed time. Full
cache and extent-end exits retain their counters on the cache page. **DMA**
means the existing idle predicate failed; it does not identify the DMA owner.

Each first/last page has eight label/value pairs. The first label includes
**FIRST**, **LAST**, or **BOTH** when both records match. **ST**, **RES**, **EVT**,
**FL**, and **TOK** abbreviate stage, result, auxiliary event, flags, and token.

| Display row | Values, in order |
| --- | --- |
| ST RES EVT FL TOK | Operation stage, returned result, auxiliary event, software flags, reader token |
| BLK BASE COUNT END OP | Demand cursor card block, cache base, cached block count, exclusive extent end, card block attempted by this operation |
| LOAN RX R0 R1 KEPT | Packed loan/repair word, packed receiver flags, two ready card-block numbers, kept card-block number |
| ARRIVED LOST PRE STATE POS | Receiver arrival counter, packed loss counters, stream state and next card-block position before the operation |
| PRE SAR DAR TCR CHCR DMAOR | DMA source, destination, transfer count, channel control, global DMA control before the operation |
| POST SAR DAR TCR CHCR DMAOR | The same registers after the failed operation returns |
| PRE SCI PINS TMU POST STATE POS | Packed SCI, packed pin/power registers, raw TMU counter before; stream state and position after |
| POST SCI PINS TMU | The same packed registers and raw TMU counter after |

Operation stages: 1 poll, 2 take, 3 cache append, 4 fetch, 5 resume fetch,
6 quiesce stop. Results: 0 OK, 1 pending, 2 busy/ownership conflict, 3 command,
4 token, 5 CRC, 6 overrun, 7 reset. A recorded result 0 still indicates a failed
postcondition, such as a missing block or a transport that remains busy after
stop. Events: 1 demand delivery, 2 controller heartbeat, 3 quiesce, 4 resume,
5 reset, 6 capture start. Stream states: 0 closed, 1 paused, 2 DMA, 3 lost,
4 resume.

Software **FLAGS** use these hexadecimal bits: 1 active demand, 2 pending request,
4 executing request, 8 failed reader, 10 speculative ownership, 20 deferred resume,
40 quiesced, 80 abandoned, 100 seed present, 200 port acquired, 400 port fault,
800 reader opened, 1000 IRQ context. **RX** packs ready0, ready1, kept index
and fill index in successive bytes, least significant first. **LOST** packs
the two loss counters in its low/high 16 bits; their array indexes remain
meaningful even if the fill index is invalid.

**SCI** packs SMR, BRR, SCR and SSR in successive bytes, least significant
first. **PINS** packs SPTR in bits 0–7, STBCR in 8–15 and PDTR in 16–31.
**TMU** is the raw existing down-counter, not a new elapsed-time measurement.
The before/after snapshots are sequential read-only observations; DMA can
advance between register reads. POST precedes abandonment cleanup. These
records locate an observed failure path without proving that it caused a
rendered-frame pause.

This diagnostic candidate retains exact Evolution 2 executable admission.
It does not establish broad native-game VMU compatibility. Existing CE/CDDA
profiles keep their independently compiled paths. Console boot and performance
of this new candidate require this hardware test.

## Prior console result (afcf7e1524a9)

Capture 63352.mp4 booted the preceding cache candidate, afcf7e1524a9, and
confirmed cache delivery. It did not establish a perceptible improvement.
The new diagnostic build has not yet been verified on the console.
During that prior capture, read-ahead
produced only 62 physical blocks (31 KiB); 86 cache feeds represented about
3.8% of demand feed operations. Seven requests received some cached data,
and eight speculative attempts were abandoned without a reported CRC, token
or guard fault. The cache never reached capacity. Whole requests still took
up to approximately 139 ms. The preceding capture used a different workload,
so its approximately 143 ms maximum is not a controlled comparison.

The next diagnostic target is the exact abandonment stage and result code,
with stream/cache ownership snapshots. Increasing cache capacity alone does
not address the observed lack of production. See
`docs/evidence/native-read-ahead-observed-2026-10-24.json` for the transcription,
counter definitions, arithmetic checks and evidence limits.

## Repair-limit diagnostic update

The LOAN word now carries the native VMU loan state in its low16bits and
the existing SCI `unrepaired` counter in bits16–23. For example, `00020000`
means no VMU loan and two previous failed repair attempts. The screen label
and record layout are retained; read the upper bits using this definition.
No recovery limit is reset or raised by this candidate.

Capture63358.mp4 of build6675bd6fdcbe recorded eight POLL-stage failures.
Both retained snapshots returned OVERRUN(6), with active reader DMA and
SCI overrun before the operation, restored guest DMA afterwards, and a LOST
stream. The first pre-operation count was0x7A with no recorded lost-byte
repair; the last was1. The lifetime `unrepaired >= 2` gate and the CRC-tail
repair exclusion are hypotheses pending the next counter capture. The
pre-operation count is not the final fenced count used by `finish()`.

Use the same25-second targeted capture. This build adds the missing counter
without changing SCI priority, cache geometry, transport or retry policy.
