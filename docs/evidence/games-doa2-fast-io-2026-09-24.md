# DOA2 first SD optimization: owner result, 2026-09-24

## Accepted performance baseline

The owner tested packaged build `3ebbf8f9846b` and reported a substantial
improvement over the preceding working build:

- About 15 seconds from launch in K-UI to the game starting.
- About 10 seconds on the initial screen before Start worked, compared with
  roughly three minutes in the preceding run.
- Actual gameplay has minimal lag.
- Loading at stage starts and FMVs remains noticeable. Character speech is
  often disrupted during those loads. Heavy CPU load is the owner's suspected
  explanation, not an instrumented diagnosis.

These are approximate owner timings and observations, not measured SD
throughput or frame-rate traces. Retain this working package for comparison.

- Source: `6ff3f512c414ff348e56ea6f7c7b666c85af2fff`.
- Packaged merge: `3ebbf8f9846bc78681951c8671419edb38c3046a`.
- CI run: `36031151786`; SD artifact: `10822645749`.
- ZIP SHA-256: `617c747cd870df0545debcbe9e7334c650a1a354afbff1caa63d53178cb22e28`.
- Runtime CRC32 `b1a4cf08`; retail loader CRC32 `974a4b15`.
- Native resident end `0x8c00bae8`; stack bound 928/1232 bytes.
- Same DOA2 T3601N/V1.100/U dump, serial SD adapter and boot CD.

## Next candidate: shorter blocking reads

The first optimization reduced per-bit SPI work, CRC work and memory-copy
work. The next candidate addresses latency in the existing synchronous path:

1. Reduce EXEC work from eight to two game sectors, returning to the caller
   between chunks. The hook still restores the caller's exact SR; it does not
   force-enable interrupts or run SD transfers in the background. Preflight
   checks remain eight sectors at a time, avoiding extra validation work.
2. Keep the existing CRC-checked 512-byte cache across calls on the same
   immutable card/image. Clear it on initialization and before replacing it;
   a failed block read leaves it invalid. No extra buffer is allocated.
3. Validate a submitted read's entire destination without purging it, then
   purge each chunk immediately before its P2 copy. All address, alias,
   alignment and resident-overlap checks remain in place.

The resident binds the manifest already checked by its decoder instead of
retaining a second image-init validation routine. The assembly entry clears
the image's BSS, including cache and counters. This recovers code space below
the existing guard; neither the reserved memory range nor stack moves.

For 32 sequential raw-track sectors starting at file offset zero, the
one-block cache can serve the Mode1 or raw requests with 147 physical block
reads regardless of splitting into one-, two- or eight-sector calls. Discarding
it at every call required 153 reads for two-sector Mode1 chunks, versus 148
for eight-sector chunks. These are fixture read counts, not speed measurements.

Smaller chunks may improve speech/interrupt latency but can reduce throughput
if the game polls infrequently. A hypothetical two-sector EXEC once per 60 Hz
frame has a 240 KiB/s payload ceiling even before accounting for the blocking
work. Inspection of the owner's executable found a tight polling path through
`0x8c11653c` and `0x8c115e0e`, but did not establish FMV polling cadence.
Consequently this remains a hardware comparison, not a promised FMV fix.

The two focused GD-service and image-reader tests passed locally under
ASan/UBSan (with LeakSanitizer disabled for the execution environment). They
cover two-sector progress/cancel/failure, whole-destination validation without
access, exact payloads/tails, and equal physical-read counts across chunk sizes
on contiguous and fragmented fixtures. Native memory, stack and instruction
checks remain mandatory in the console build; no broad host suite was repeated.

Compare the same opening movie and the same stage introduction/speech/fight.
Record whether speech improves and whether loads get longer; a short video is
sufficient. Existing CRC checks and finite SD work limits remain enabled.
There is no compression, asynchronous DMA, speculative prefetch, media rewrite,
new boot CD, or repeat of the accepted read probes.
