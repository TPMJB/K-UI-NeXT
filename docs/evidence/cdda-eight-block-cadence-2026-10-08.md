<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Eight-block actual-worker asynchronous model, 2026-10-08

The final eight-block worker passes all 72 canonical schedules, retaining all 54 baseline successes. It passes 120 of 144 expanded schedules and retains all 63 expanded baseline successes. Optimized and ASan/UBSan executions agree exactly; all independent source, stereo PCM, physical destination, callback cap, and lifecycle assertions pass.

This is a synthetic host model of the actual C worker. The consumer advances at 44.1 kHz throughout charged raw reads, G2 transfers, register reads, and timer reads. ARM captures and later channel publications are independently timed. These profiles are a controlled comparison, not a reconstruction of console cadence or a hardware playback result.

| Updater rate (Hz) | Hook spacing (ms) | Baseline | Eight blocks, entry+exit probes |
| --- | --- | --- | --- |
| 29.99962 | 10.00064 | 12/12 | 12/12 |
| 29.99962 | 16.66688 | 7/12 | 12/12 |
| 26.04167 | 10.00064 | 11/12 | 12/12 |
| 26.04167 | 16.66688 | 10/12 | 12/12 |
| 24.00006 | 10.00064 | 7/12 | 12/12 |
| 24.00006 | 16.66688 | 7/12 | 12/12 |
| 22.32143 | 10.00064 | 3/12 | 12/12 |
| 22.32143 | 16.66688 | 6/12 | 12/12 |
| 20.83333 | 10.00064 | 0/12 | 12/12 |
| 20.83333 | 16.66688 | 0/12 | 12/12 |
| 19.99974 | 10.00064 | 0/12 | 0/12 |
| 19.99974 | 16.66688 | 0/12 | 0/12 |

Each cell spans four long-gap phases and three ARM publication phases. Each profile runs up to 300 main updates with two hooks. An 85.41696 ms gap replaces one hook spacing every 32 updates. Raw callbacks cost 4.59392 ms, with an 11.30240 ms tail every 32 callbacks; the synthetic callback mean is 4.80356 ms before timing-read overhead. Word/copy operations cost 12 ticks, timer reads 1 tick. Detailed integer parameters, compile arguments, full source SHA256 manifests, binary SHA256s, and each profile's first denial are retained in the JSON evidence and JSONL logs.

The baseline worker at 56df22966bbb9093374372f8dad7a2e09a05fcb7 passes 54/72 canonical schedules. Eight blocks with the original observation scheduling pass 60/72 but lose seven baseline successes to UNOBSERVED_HALF. A bounded exit observation passes 67/72 but still loses two baseline successes. One additional bounded entry observation plus one bounded exit observation, with the unchanged proof contract, passes 72/72. Correctly counting a split cached sector as one sector quantum is retained. Four physical sector quanta and the 16 ms admission budget remain; the budget can overrun by one complete bounded quantum. The largest modeled visit is 20191 ticks (25.84448 ms), with at most three physical callbacks in these profiles. The explicit four-callback cap remains asserted and is exercised by the other worker regressions.

The 20 Hz characterization retains a limit: all 24 schedules recover with UNOBSERVED_HALF while the ring is already populated. No reserve, acceptance, 50 ms witness expiry, or STOP fence is relaxed to turn those negatives into successes. No raw, queue, stale, or actual active-block-copy failure occurs. After each first recovery, the fixture dispatches STOP and checks twenty subsequent visits inside its full 32768-frame horizon admit no raw or plane work.

The targeted ASan/UBSan suite additionally checks short finite sources, unaligned repeat wraps, real per-channel 24/64-frame skew in either direction, delayed multi-block observations, numeric ring wrap, simultaneous ownership of two straddled blocks, missing/stale intermediate blocks, deferred stereo/cache retry, partial-stereo PAUSE, and revoke. A one-sector finite source may reach EOF before the two-publication START ownership witness, so that test verifies the complete START/EOF lifecycle rather than requiring an intermediate PLAYING snapshot.

Final C worker SHA256: `2643866843f836ea363b8ee871a4ab7d568c3b59e1f27a1b5852bee8bf58e065`. Frozen baseline C worker SHA256: `3613e3c338f6ad4cde3bb03db429e7a5c65baf3122649854da2226bf9d09618c`. Full identities are in [cdda-eight-block-cadence-2026-10-08.json](cdda-eight-block-cadence-2026-10-08.json); profile logs are [cdda-eight-block-cadence-2026-10-08-optimized.jsonl](cdda-eight-block-cadence-2026-10-08-optimized.jsonl) and [cdda-eight-block-cadence-2026-10-08-sanitized.jsonl](cdda-eight-block-cadence-2026-10-08-sanitized.jsonl).

Reproduction from this repository (the comparison runner requires the pinned
baseline Git object `56df22966bbb9093374372f8dad7a2e09a05fcb7`; the ordinary
host regression runner also works from the packaged source snapshot):

```sh
python3 tools/test_toy_pilot_cadence.py --characterize --output /tmp/toy-cadence-optimized
ASAN_OPTIONS=detect_leaks=0 python3 tools/test_toy_pilot_cadence.py --characterize --sanitizers --output /tmp/toy-cadence-sanitized
python3 tools/test_toy_pilot.py
```
