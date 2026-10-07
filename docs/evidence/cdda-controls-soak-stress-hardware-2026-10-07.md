# CDDA controls, soak and read-stress console results

Recorded 2026-10-07 from four owner-supplied console photographs. All identify
build `cd207bc06def`, the published test checkpoint
`cd207bc06defab92929cb5c801549f3b525988c2`. The set contains two controls runs,
one uninterrupted soak and one audio plus serialized SCI data-read stress run.
The earlier build `9020d5101c7e` already has its separate
[baseline result](cdda-hardware-2026-10-07.md).

## Exact photographed counters

| Counter | Controls A (61956) | Controls B (61957) | Soak (61958) | Stress (61959) |
| --- | ---: | ---: | ---: | ---: |
| Completed playback stages | 5 | 5 | 1 | 1 |
| Worst half refill, us | 79,036 | 79,011 | 81,424 | 75,499 |
| Maximum service gap, us | 6,376 | 6,380 | 8,471 | 32,651 |
| Minimum refill margin, us | 106,530 | 106,553 | 104,149 | 110,090 |
| Checked card blocks | 2,857 | 2,857 | 312,189 | 899,880 |
| Observed private stack use, bytes | 5,280 | 5,280 | 5,244 | 5,380 |
| Played loop passes | 0 | 0 | 902 | 902 |
| Owned clock seconds | 9 | 9 | 900 | 900 |
| Observed clock wraps | 0 | 0 | 2 | 2 |
| Failures / stopped audio | 0 | 0 | 0 | 0 |

Both controls screens show an **expected 190,000 us deadline delay** and
**one recovered expected deadline**. The screens identify the refusal before
refill and subsequent stop. This event is separate from unexpected failures.

The stress screen also reports:

| Counter | Value |
| --- | ---: |
| Verified second-file bytes | 300,892,160 |
| Verified complete 8 MiB passes | 35 |
| Second-file read/check errors | 0 |
| Worst complete data job, us | 32,959 |
| Maximum verified-job completion gap, us | 151,441 |

All four screens show `CDDA TEST COMPLETE`.

## Acceptance and limits

Every photograph meets the implemented numerical conditions of its profile.
Controls completed five stages and the separately counted deadline recovery
twice. Both long profiles completed 900 clock seconds, at least 900 played
loops and two observed timer wraps. Stress verified approximately 286.95 MiB
of second-file data, well above its 8 MiB minimum, with zero read/check errors.
Its largest completion gap, 151.441 ms, is below the five-second starvation
limit. The completed profile also implies that its implemented 64 KiB per
completed minute check did not fail; interval-specific byte totals are not
individually shown in the photograph.

The smallest displayed refill margin in this set is 104.149 ms. The largest
displayed refill is 81.424 ms; the nominal half-ring budget remains about
185.76 ms. These are useful observed margins under the supplied workload,
not a worst-case bound for arbitrary cards or game reads. The stress refill
maximum being lower than the soak maximum does not prove that extra load
improves performance.

The largest stack watermark is 5,380 bytes, within the 65,472 usable bytes
and below the [reviewed conservative bound](cdda-next-memory-2026-10-07.md).
No stack-guard failure appears. The watermark describes these observed paths;
it does not replace static bounds or error-path checks.

Listening quality has not yet been explicitly reported. Photographs do not
establish correct left/right routing, absence of clicks or dropouts, silence
during pause/stop, or music quality. The repeated controls screen establishes
a second successful run; its power-cycle and chronological placement are not
encoded in the displayed counters. These details remain owner confirmation.

Both long runs report **902 one-second source-loop passes against 900 owned
clock seconds**. The displayed integers differ by about 0.22 percent when
treated as nominal durations. This is reproducible in the two photographs,
but they do not contain matched playback-start/end timer ticks or exact
played frames. Initialization time, endpoint rounding and actual clock rates
must be separated before attributing a cause or reporting calibrated sample
rate. Do not infer precise pitch, elapsed playback time or synchronization
from the rounded loop/seconds counters alone.

These programs own their homebrew main RAM, AICA channels/sound RAM and TMU1.
No retail game runs during these tests. Passing them does not establish Toy
Commander sound-driver coexistence, game-owned memory admission, GD command
completion semantics or background audio service when a game makes no reads.
The released 1.8.5 readers remain the fallback.

## Next engineering work

1. Obtain the owner's listening confirmation for channel order, continuous
   stereo, pause/stop silence and unexpected clicks/dropouts.
2. Add matched-endpoint exact played-frame and elapsed-tick diagnostics to
   the next controlled build. Preserve these successful profiles as a
   checkpoint rather than silently changing their timing interpretation.
3. Implement and host-test explicit audio/data job state and command semantics:
   play, stop, pause/resume, seek, status, completion and cancellation. Exercise
   randomized offsets/sizes, short reads, switching, retries and starvation
   through one serialized SCI owner while checking independent source bytes.
4. Establish a periodic service and RAM/AICA ownership contract before retail
   admission. Observe the owned Toy Commander revision's sound allocation and
   effects behavior; retain an explicit unsupported result until coexistence
   is proved. A fast card or a passing homebrew loop does not reserve resources
   on behalf of a game.

The [roadmap](../cdda-roadmap.md) defines the remaining retail gates. The
[test checklist](../cdda-next-test.md) remains the procedure for reproducing
these profiles; the console photographs close their numerical test request.
