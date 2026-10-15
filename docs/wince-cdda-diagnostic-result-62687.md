<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Worms original-image underrun: console build 4f4201208a88

Evidence received on 2026-10-15. The user manually transcribed the frozen
underrun page after an audible cut while playing the original Worms image.
This transcription is the evidence analyzed here; the unavailable videos
62686.mp4 (cooked image) and 62687.mp4 (original image) have not been decoded
in this analysis. Do not treat this result as a diagnosis of the cooked-image
failure.

Runtime source reference: c3d94105e746e2a787d230bc0ce874d0f9087228,
branch sci-armada-cdda. Console build ID: 4f4201208a88.

## Exact user transcription

```text
Last Underrun - Frozen context
MS ready delta staged read-ms
00011413 000013C0 00002C00 00001200 000001C2
Entry-Gap Sleep OBS-MS SRCAGE RINGAGE
00000032 00000001 00000000 00000032 00000096
Status Owner Read-Bytes Total Retries
00000001 00000002 00007100 00009300 00000001
Data-D Audio-D Pre-D Yield-D Copy-D
00000000 00000004 00000000 00000000 00000003
MS Source Ready Staged Flags
00011319 003F5100 000092C0 00000000 00120207
MS Source Ready Staged Flags
0001137D 003F7100 00005A80 00000000 00120207
MS Source Ready Staged Flags
000113E1 003F8F00 00002A80 00000C00 00120207
```

## Measured shortage

The native cursor observation at CE time 70,675 ms advanced by 11,264 bytes
against 5,056 fresh ring bytes: 6,208 bytes were missing from the fresh
playback reserve. The 4,608 staged bytes had not been published into that
reserve. Even publishing all of them immediately would still leave a
1,600-byte shortfall.

At 176,400 bytes/sec these deficits represent approximately 35.2 ms and
9.1 ms of PCM respectively. Neither number measures the audible cut or
recovery duration.

| CE time, ms | Cumulative source bytes | Fresh ring bytes | Staged bytes |
| --- | --- | --- | --- |
| 70,425 | 4,149,504 | 37,568 | 0 |
| 70,525 | 4,157,696 | 23,168 | 0 |
| 70,625 | 4,165,376 | 10,880 | 3,072 |

Source publication advanced 8,192 bytes and then 7,680 bytes in consecutive
100 ms intervals. That is 79,360 bytes/sec over this 200 ms window, about
45% of the required 176,400 bytes/sec. This is delivered progress during
this event, not a measurement of maximum physical SCI throughput.

The active 16-sector job was still pending after 450 ms, with 28,928 of
37,632 bytes checked. The complete job carries about 213.3 ms of audio.
Its elapsed age includes scheduling, arbitration, reception and retry time;
the report does not separate those costs.

## Transport and publication evidence

At the frozen snapshot the source status was PENDING and SCI owner AUDIO.
All three history flags also show an active, urgent, playing audio stream
with owner AUDIO and source status PENDING.

Since the immediately preceding history sample, the counters show zero
DATA blocks, four AUDIO blocks, no preemptions, no worker budget yields and
three Worms guarded-copy retries. The history sample is 50 ms before the
event; transport counters are sampled after the native consumer lease is
released, so they are not an exactly synchronized timing measurement.

The most recent checked source publication was 50 ms old, while ring
publication was 150 ms old. Source progress and playback-ring publication
were therefore both inadequate during this event. The copy retries are
consistent with deferred publication, but the retained staged bytes alone
cannot cover the measured shortage.

The worker entry gap was 50 ms after a requested 1 ms sleep. CE clock
resolution and unmeasured work prevent assigning that interval solely to
scheduler starvation. OBS-MS zero likewise does not establish zero native
API cost. RETRIES one is the current consecutive transport retry count,
reset after successful progress; it neither identifies a CRC error nor
counts every retry in the job.

## Implementation implications

This evidence supports a real supply underrun rather than relying on the
former clock-rate heuristic. It does not support assuming DATA quota
starvation in the final observed window: DATA made no checked-block
progress there and audio was already urgent. Instantaneous ownership still
cannot prove continuous ownership during the whole 450 ms job.

The next source review must isolate audio-owned reception/completion and
retry latency, including worker wake behavior. Separately, check how Worms
can publish refills before depletion reaches the native in-flight copy
window. Do not remove the overlap guard: it protects a real native copy
hazard, and removing it cannot supply the additional bytes already missing
from staging.

No root cause within scheduling, DMA completion or transport retries is
established by this page alone. Preserve this evidence before any runtime
change and compare a proposed change with these specific failure paths.
No additional console test is needed merely to recapture this event.
