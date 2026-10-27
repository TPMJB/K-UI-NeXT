# Native read-ahead checkpoint, 2026-10-25

63369.mp4 contains all 17 frozen report pages from build 7568957f5e68.
The overview returns at about 73 seconds; matching cache-page transitions
give a complete cycle of 74.0–74.5 seconds. The key cache, exit, FIRST/LAST,
and timeline pages are already present around 14–31 seconds. No recapture is
needed. Future targeted recordings may stop when LOADER TIMELINE appears
after the failure pages; allow 25 seconds from the cache heading.

The two retained failures are POLL/OVERRUN with `LOAN=00010000`:
loan IDLE and `unrepaired=1`. The lifetime `<2` repair gate remains open.
Both use receive area 1, where `lost[1]` is already nonzero: 390 in FIRST,
399 in LAST. The stream is receiving an already-resumed block and encounters
another incomplete overrun. The per-block single-erasure guard rejects it;
the optional job is abandoned. Clearing either repair guard is not justified.
The first missing byte has not yet been reconstructed or CRC-validated.

The capture reports eight POLL-stage exits, 184 CRC-checked speculative blocks
(92 KiB), eight requests with some cached feed, and 5040 producer returns after
abandonment. No loan skips occurred. The intermediate exit result codes are
not displayed; the first and last snapshots alone establish the double-loss
case. These counters do not prove the cause or duration of every rendered hitch.

An ASan/UBSan run of the actual SCI/card/DMA and auxiliary fixture reproduces
this refusal with `unrepaired=1`, one injected loss followed by another in the
same block. A separate scratch experiment completes 12 fresh PAUSED blocks
through the existing CPU-driven PIO receive path, validates their CRCs, and
appends them without starting DMA or changing the guest DMA1 tuple.

The next forward candidate should restrict optional production to one fresh
PIO block per eligible controller heartbeat. Require the existing cache,
extent, loan, demand, port and DMA admission guards, plus an already-open
CMD18 stream PAUSED at the exact next block. Never start this work from a GD
delivery or SCI interrupt, and never use `fetch(polled=true)` for RESUME:
that branch still starts DMA. Keep the demand reader, priority 1, CRC and both
repair limits unchanged. Measure producer-only visit time as well as cache
production and request latency; PIO trades transport robustness for synchronous
CPU time, so the model result is not a hardware performance result.

This checkpoint changes evidence and documentation only. The latest testable
binary remains 7568957f5e68. No rollback is needed. The PIO candidate has not
been built or tested on the console.

Exact transcription, video fingerprint, caveats and experiment definition:
`docs/evidence/native-read-ahead-second-overrun-2026-10-25.json`.
