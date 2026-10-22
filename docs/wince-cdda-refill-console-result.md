<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Worms console result: urgent-refill build 8a22ec45229a

Console evidence received 2026-10-15: `62691.mp4` and `62692.mp4`.
The owner reported no audible dropouts, but noticeable lag in both original
and cooked 2048 Worms. The clips do not identify which file corresponds to
each image, so this note does not assign them.

Both clips show build 8a22ec45229a, GAPS zero, one worker, no audio fault,
two successful Play calls, no refusal and zero HRESULT. All fields on the
frozen last-underrun page are zero, and no retained UND event appears.
Together with the listening report, this establishes successful playback
without an observed underrun for these runs, not universal zero-dropout
compatibility.

| Recording | Maximum worker interval | Maximum source-job age | Completed jobs |
| --- | --- | --- | --- |
| 62691.mp4 | 768 ms | 600 ms | 364 |
| 62692.mp4 | 1102 ms | 850 ms | 313 |
| Earlier original image, 62687.mp4 | 1106 ms | 450 ms | 136 |

The maxima are global elapsed-time observations, not CPU-time measurements
and not tied to a particular gameplay frame. They cannot establish how much
CPU an individual polled refill consumed. Both new recordings show returned
diagnostic screens only; the owner's report is the gameplay evidence.

The latest runtime changes comprise decoded cooked track controls and
urgent worker-only PIO reception. The latter uses CPU-driven transfer while
the worker retains priority 2, above kernel normal 3. It may introduce or
worsen gameplay contention even though interrupts are restored between
512-byte receptions. This is a plausible regression mechanism, not a
measured attribution. Raw and cooked images share the audio worker, so their
similar symptoms do not isolate image conversion as the cause.

Keep this build as the observed working-audio baseline. The smallest next
control is the same original image and match with Background loading still
On and CD music Off, comparing normal gameplay for about 30 seconds. This
compares the optional music path against the ordinary loader; it does not
isolate polling from every other adapter difference. No new binary or
repeated diagnostic-page recording is necessary for that comparison.

If the lag disappears, investigate CPU/read scheduling without losing the
source-continuity fix. If it remains comparable, assess the ordinary-loader
baseline before attributing it to this PIO change. Do not blindly lower
worker priority or shorten batches: insufficient source supply was already
observed before this build.
