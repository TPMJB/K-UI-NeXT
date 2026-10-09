# CDDA mixed-job console result

Recorded 2026-10-07 from the owner's photograph
`image-1791383987652.jpg`. The screen identifies profile 06, build
`6ec63f97aa0a`, detached SCI, owned AICA and read-only exFAT. It shows
`CDDA TEST COMPLETE`, all seven stages and zero failures / stopped audio.
The photograph remains external to this source record.

The tested source is
[`6ec63f97aa0a5bc264cae16f824ac7a326c9d420`](https://github.com/TPMJB/K-UI-NeXT/tree/6ec63f97aa0a5bc264cae16f824ac7a326c9d420),
tree `d7b03024645fae5ccb7afbce23a13cd6122b4b0d`. The
[mixed-test checklist](../cdda-mixed-jobs-test.md) defines the required
counters; the [memory, clock and ownership audit](cdda-mixed-jobs-memory-2026-10-07.md)
is separate static and host evidence.

## Exact photographed counters

| Counter | Value |
| --- | ---: |
| Completed playback stages | 7 |
| Worst half refill, us | 75,056 |
| Maximum service gap, us | 5,167 |
| Minimum refill margin, us | 110,702 |
| Checked card blocks | 161,275 |
| Observed private stack use, bytes | 5,492 |
| TMU seconds / full 8 MiB passes | 180 / 1 |
| Committed checked bytes / errors | 47,709,024 / 0 |
| Size classes / minimum completions | 8 / 971 |
| Completed logical jobs / chunks | 7,772 / 31,291 |
| Expected cancellations / stale refusals | 3 / 6 |
| Audio command actions / STATUS checks | 7 / 4 |
| Data job / no-progress gap, us | 5,476 / 1,310,730 |
| Failures / stopped audio | 0 |

## Numerical result

**Profile 06 passed its implemented console checks.** The workload reached
180 documented-clock seconds after initial playback start, completed one
contiguous 8 MiB logical pass and covered all eight request sizes with a
minimum of 971 completions each, above the required 16. The three controlled
cancellations, six stale refusals, seven completed audio actions and four
STATUS checks match the sequence. No data errors or unexpected failures
were reported, and final drain/stop checks completed.

The completed sequence also indicates that the implemented initial-pass
deadline of 120 seconds and size-coverage deadline of 150 seconds passed.
Their individual completion timestamps are not displayed in this photograph.
The greatest checked-chunk progress gap, 1.310730 seconds, is below the
five-second limit and includes the planned pause and command transitions.

The displayed worst data-job duration, 5.476 ms, measures one synchronous
physical read plus its observation and verification, at most 2,048 bytes.
It is not the latency of a complete multi-chunk 32 KiB logical request.
The 47,709,024 checked bytes include the initial full pass and subsequent
smaller jobs; they are not a count of additional complete file passes.

The known sequence also supplies an independent aggregation check. The
sum of the eight logical request sizes is 40,480 bytes, and one completed
round uses 28 physical chunks. With 971 completed rounds, one full pass,
three completed 4,096-byte replacement jobs and the committed 2,048-byte
chunk of the partially canceled job, the source sequence accounts exactly
for the photographed totals:

```text
bytes  = 8388608 + 971 * 40480 + 3 * 4096 + 2048 = 47709024
jobs   = 1 + 971 * 8 + 3                         = 7772
chunks = 4096 + 971 * 28 + 3 * 2 + 1             = 31291
```

The read held in READY and then canceled before commit is excluded. This
coherence check is derived from the tested source and photographed counters,
not an additional on-console measurement of each individual class.

The displayed minimum refill margin is 110.702 ms and the worst half refill
is 75.056 ms. The observed 5,492-byte stack watermark remains below both the
6,536-byte conservative static chain estimate and 65,472 usable private-stack
bytes. These are results for the exercised paths and this card, not bounds
on all cards, fault paths or arbitrary CPU stalls.

## Evidence limits and preserved checkpoint

The photograph establishes numerical completion of this serialized homebrew
workload: unrelated data jobs survived audio seek, pause/resume and
stop/restart, while explicit data cancellation retired their identities at
operation boundaries. It does not demonstrate interruption of an active
SCI/DMA transfer, retail BIOS command delivery, periodic service inside a
game, or coexistence with Toy Commander's RAM and sound driver.

No new listening report accompanied this photograph. It therefore adds no
claim about precise channel order, absolute pitch or audible continuity.
The fixed 12,468,720 Hz TMU reference and unchanged AICA pitch are the tested
program's documented contract; this run is not an independent oscillator
measurement.

Keep the tested `K-UI-CDDA-Mixed-Test.zip` immutable:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Test ZIP | 16,123,725 | `b0cb2a093373993ab41481eaf14ee5531406325cad9f11a311ea29f54df6a39f` |
| Profile 06 runtime | 57,112 | `f9fcc2eba0a5ce4975af1ec08abe83585afcbeff94a8eae7c65f7044560b8913` |

Its manifest records preparation before console testing; this later evidence
records the successful run without rewriting that archive. Ordinary 1.8.5
readers remain unchanged. No repeat of profiles 00–06 is required by this
result. The next work is the distinct package/handoff and periodic-service
contract in the [roadmap](../cdda-roadmap.md), before retail resource admission.
