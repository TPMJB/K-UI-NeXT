<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Three-sector GD mixed-worker model, 2026-10-08

The full 22-suite host run passes with ASan/UBSan. The GD suite retains the
previous fast-card policy comparisons and adds eighteen mixed GD2/GD3 cases.
It compiles the actual GD core, extracts the production `step()` unchanged
except its hosted timer address, and runs the full authored eight-block worker
against independently advancing ARM publications and physical PCM consumption.

The cases cover 1-, 5- and 17-sector requests at approximately 60/30 Hz, plus
first-callback latency tails and a 129-sector request at 60 Hz. Exact data
bytes, final destination bounds, clipped partial batches, status progress,
wrong-token refusals, ownership retained until CHECK, source PCM and physical
sound destinations are asserted. Data ownership denies worker raw/copy work;
short commands then refill continuously over several ring wraps.

| Synthetic 60 Hz command | GD2 | GD3 |
|---|---:|---:|
| 17 sectors, nominal first callback | 141.49 ms | 95.32 ms |
| 129 sectors | 1.071 s | 0.715 s |

The deliberately long command still reaches conservative audio recovery with
both limits. PCM remains valid through STOP and subsequent visits admit no
raw or plane work inside the STOP horizon. Other unchanged ownership suites
cover the full acknowledged fence and restart lifecycle. This test does not
claim that a larger batch protects audio during arbitrarily long GD ownership.

Callback costs are assumptions informed by the supplied hardware report:
GD2 nominal 6,445 ticks and tail 8,162 ticks, linearly scaled by sectors/2;
raw nominal 3,470 ticks and tail 7,715 ticks. Tails occur every 32 callbacks,
with a separate case placing the first GD callback on a tail. Using the
reported means as nominal costs and additionally inserting tails makes these
synthetic averages differ from the measured run. Linear scaling is not a card
latency model or prediction of exact callback setup savings. The largest
modeled GD3 masked interval is 12,243 ticks, about 15.671 ms.

The fixture asserts masks and restoration, but does not emulate the game's
movie decoder, scheduler, interrupt controller or display deadlines. It cannot
establish visual quality. A three-sector cap is a byte-count limit, not an
elapsed-time deadline; console timing still needs measurement.

Reproduce with `python3 tests/test_toy_pilot_gd_chunk.py`. Full output is in
[cdda-toy-video-host-tests.txt](cdda-toy-video-host-tests.txt). The linked SH-4
build retains the exact worker hash, 24-byte low-resident space margin, and
4-byte conservative low-stack margin; actual linked evidence is in `build.json`.
