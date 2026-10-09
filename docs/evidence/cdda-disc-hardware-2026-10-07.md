# Checked disc-map console results: profiles 11 and 12

Independently read on 2026-10-07 UTC from the two clear final-screen photos.
Both show build **`a0c36063a9ed`**, **eight completed stages**, **zero failures**
and the green `CDDA TEST COMPLETE` banner. All seven paired result rows match
the [test checklist](../cdda-disc-test.md). This records the two console runs;
the earlier [memory audit](cdda-disc-memory-2026-10-07.md) remains the static,
pre-console checkpoint. No earlier test was rerun for this observation.

## Photo and runtime identity

The original photographs were inspected directly without editing:

| Profile | Supplied photo | Photo SHA-256 |
| --- | --- | --- |
| 11 | `01-image-1791396507990.jpg` | `6708077e49f54a460e004e0cd463e1a502e561a5c1cb3408c47275bc0d3824ee` |
| 12 | `02-image-1791396653050.jpg` | `92a2021cde4560060a483abfabeef26e6c4c10f576197604ecb9092b8380a48c` |

The labels read `PROFILE11 DISC / complete generated track map` and
`PROFILE12 TOY / selected track14: incomplete disc`. Both identify detached
execution, owned AICA and read-only exFAT over SCI. Every reported value below
is legible; no numerical field requires estimation.

The delivered `K-UI-CDDA-Disc-Tests.zip` manifest binds the screen build ID to
[source commit `a0c36063a9ed4b46c63a3fe4eaf062870b5704bf`](https://github.com/TPMJB/K-UI-NeXT/tree/a0c36063a9ed4b46c63a3fe4eaf062870b5704bf),
tree `874d168c4b68032d9f82add3d9b28422c0f93cac`.
The 8,661,274-byte archive has SHA-256
`0700fb68a8605d7eee03c216c8b186008274abb675638021c2a60ef181488989`.
Its pre-run `hardware_tested: false` fields describe packaging time; this
separate checkpoint records the subsequently supplied console results.

| Runtime | Initialized payload bytes | Runtime SHA-256 from delivered manifest |
| --- | ---: | --- |
| `runtimes/11-disc-map.kui` | 3,086,400 | `bbd3c0b57c09ab8d0d1f6cbb9cc162bf1d45e09d0070a8764c0a51b52f168cf3` |
| `runtimes/12-toy-track14.kui` | 3,083,936 | `514ea78a259fa06781a36be2d5b7ab50771bb4bc12d403984f30828d22a02039` |

The photos identify their displayed build/profile; they do not independently
hash card files. Both successful boots establish admission of these new
large-envelope test profiles through the existing SCI boot path.

## Directly visible results

| Screen field | Profile 11 | Profile 12 |
| --- | ---: | ---: |
| Completed playback stages | 8 | 8 |
| Worst half refill, microseconds | 79,738 | 81,566 |
| Maximum service gap, microseconds | 18,725 | 7,735 |
| Minimum refill margin, microseconds | 101,950 | 99,977 |
| Checked card blocks | 7,233 | 14,259 |
| Observed private engine stack use, bytes | 19,704 | 34,628 |
| Mapped tracks / complete image | 6 / 1 | 1 / 0 |
| Client seconds / vector calls | 20 / 2,479 | 42 / 4,916 |
| Checked data bytes / chunks | 196,608 / 96 | 0 / 0 |
| TOC / map checks | 2 / 16 | 0 / 10 |
| Audio actions / EOF checks | 7 / 2 | 4 / 1 |
| Client / service stack bytes | 35,272 / 2,220 | 4,004 / 2,220 |
| ABI checks / vector restored | 2,478 / 1 | 4,915 / 1 |
| Failures / stopped audio | 0 | 0 |

The seven paired-row gates pass: map completeness is correct for each profile;
elapsed seconds are below the respective 60/90-second limits; logical data,
TOC/map and action/EOF counts match exactly; both subordinate stack watermarks
are positive and below 65,472 bytes; the vector is restored. The ABI arithmetic
is exact: **2,478 = 2,479 − 1**, **4,915 = 4,916 − 1**. The single intentionally
refused nested EXEC is outside the client integer-probe count.

Profile 11's logical coverage is **96 × 2,048 = 196,608 bytes**, comprising
all 64 cooked sectors of generated track 3 and all 32 raw Mode 1 payloads of
track 6. The checked-card-block row measures physical storage work and is not
a logical READ-byte total. Profile 12's zero data/TOC values are expected:
it admits only the selected original audio backing, without a complete disc
map, and refuses data and complete-TOC requests.

## Measured stacks against the audited bounds

Each of the three separate stacks reserves 65,536 bytes, with a 64-byte guard
and **65,472 usable bytes**. Static bounds below are the earlier linked-path
bounds including their conservative 256-byte helper allowance.

| Profile / stack | Console watermark, bytes | Static bound, bytes | Remaining usable space at watermark, bytes |
| --- | ---: | ---: | ---: |
| 11 engine | 19,704 | 24,200 | 45,768 |
| 11 client | 35,272 | 35,808 | 30,200 |
| 11 service worker | 2,220 | 6,716 | 63,252 |
| 12 engine | 34,628 | 34,884 | 30,844 |
| 12 client | 4,004 | 4,584 | 61,468 |
| 12 service worker | 2,220 | 6,716 | 63,252 |

All observed watermarks fit their static bounds. Profile 12's engine use is
exactly the previously derived 34,628-byte selected-map parsing path before
the helper allowance. Its larger engine watermark therefore matches the
known metadata builder path. These are measurements of the supplied runs,
not universal maxima or proof that every fault branch has executed natively.

## Gates inferred from successful completion

The following details are not individually printed in the photos. They are
inferred from the published build's checked REPORT and final-stage predicates,
which must pass before eight stages and the completion banner are displayed:

| Checked source gate | Profile 11 | Profile 12 |
| --- | --- | --- |
| Accepted requests / successful completions | 16 / 16 | 5 / 5 |
| DRIVE / expected protocol refusals / stale checks | 11 / 12 / 3 | 6 / 10 / 2 |
| Newly advanced positive-prefix processing checks | 90 | 0 |
| Original-track switches | 3 | 0 |
| Nested EXEC / ownership-descriptor refusals | 1 / 8 | 1 / 8 |
| Final ownership state | Audio STOPPED; queue EMPTY; lease retired | Same |
| Returning context and memory guards | Checked and intact | Checked and intact |

The generated sequence therefore passes unequal 88,200/132,300-frame EOFs,
looping, serviced pause/resume at the actual played cursor, active source
switching, prefix exclusion, both derived TOCs and verified cooked/raw data
extraction. The selected Toy sequence passes pause/resume and the real
**1,805,748-frame** EOF of its **3,071-sector** backing. Its checked FAD range
is **[374351, 377422)**; it does not extend playback through the 150-sector gap
before the next descriptor track at FAD 377572. Exact EOF positions, samples
and hidden ledger values are source-checked conclusions, not numbers directly
read from these final screens.

The positive refill margins record successful service headroom in these runs.
Timing uses the harness's documented timer conversion; the screens provide
no new absolute-clock calibration. No listening report accompanies these
measurements, so stereo balance, clicks, pitch and audible quality remain
unassessed here. The checkpoint establishes the controlled checked-disc
path on the console, not Toy Commander execution, full Toy disc/TOC support,
retail IRQ behavior, shared game AICA ownership or P2-buffer cache coherence.
