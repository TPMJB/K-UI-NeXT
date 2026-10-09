# Toy shared SCI foreground top-up console result

Candidate: `dc455cbfa50b` (`dc455cbfa50bd9b9fddeab630891863304b5f4b6`).
User report, 2026-10-08 America/Chicago / 2026-10-09 UTC:
“About as good as it was two builds ago.” In this sequence that is the
`d4d18c11ffeb` GD3 candidate. The initial shared build `2faa62067610` had been
reported substantially worse. The top-up therefore recovered that regression
without a reported FMV improvement over GD3. No audible outcome was explicitly
provided with this capture; zero audio error counters do not prove continuity.

## Page 5 transcription

```text
00001808 0000016E 000051EA 000000B2
000008C2 00000000 000008C2 000000E6
00000000 00000011 00000030 00000000
000006D2 00000066 00000000 00000000
```

| Field | Decimal result |
| --- | ---: |
| Game-data pump/service calls | 6,152 |
| SCI IRQ calls | 366 |
| Verified blocks consumed in calls | 20,970 |
| Verified blocks consumed in IRQs | 178 |
| Audio card claims / releases | 2,242 / 2,242 |
| Deferred audio claims | 0 |
| Data stream resumes | 230 |
| Terminal transport errors | 0 |
| Retries | 17 |
| Token yields | 48 |
| PIO fallback blocks | 0 |
| Maximum service duration | 1,746 ticks = 2.23488 ms |
| Maximum IRQ duration | 102 ticks = 0.13056 ms |
| Active token / card owner at capture | 0 / 0 |

These are 512-byte physical blocks, not 2,048-byte game sectors. The admitted
TMU profile is 1.28 microseconds per tick. Service admission is between steps,
so its observed maximum exceeding the 1.92 ms admission threshold is permitted.

Against the preceding shared capture, calls per verified block fell from
31.1983 to 0.2909; retries per 1,000 verified blocks fell from 89.7263 to 0.8039.
The captures cover different amounts of work and are not a matched throughput
test. The latest IRQ contribution is only 0.842% of verified blocks. Progress
has chiefly moved into the bounded foreground path, rather than establishing
reliable background IRQ delivery throughout playback.

## Audio and service timing

Page 2 raw-read errors, handoff gaps and queue errors are zero. All page 6
fields are zero. Page 0 indicates paused audio, with no fault or stack fault.
Page 1 service-gap maximum is clearly `0000F4FC` (80.27648 ms), essentially
the same as GD3's `0000F4F4` (80.26624 ms). Its max-step glyphs are blurred;
no exact max-step value is certified from this photograph.

Page 7 has only the timing row populated:

```text
00000E94 0000124A 0076386B 000008C2
```

This is last raw read 4.77696 ms, maximum 5.99296 ms, and 9.91704448 seconds
of raw callback time over 2,242 calls: mean 4.42330 ms. GD3's independently
decoded mean was approximately 4.4401 ms. This revision did not make the
synchronous one-sector audio callback asynchronous.

## Remaining hypotheses

The restored foreground safeguard reduced excessive entry/retry/fallback
events, but the user reports no meaningful video gain over GD3. It is therefore
insufficient as the FMV solution.

The shared data adapter still performs DMA completion, bit reversal/CRC,
cursor validation/copy, then starts the next receive. The existing authored
native adapter overlaps the next physical receive with CPU processing in two
private areas. That is a smaller possible optimization, requiring explicit
successor ownership, CRC recovery and audio-boundary tests; it is not a
one-line transplant or a demonstrated cure.

The larger remaining serialization source is raw CDDA: the worker still
invokes a complete synchronous 2,352-byte read, and refill visits retain a
16 ms between-quantum admission budget. A separate asynchronous audio cursor
using the shared receiver could allow the game CPU to run during receipt.
This needs new pending/ready/generation handling and verified-buffer lifetime
tests while retaining the established eight-block sound ring. The timing
evidence motivates that experiment but does not prove it is the sole cause.

Source review used `toy_pilot_sci.c`, `retail_async.c`, `sci_stream.c`,
`retail_cursor.c` and `toy_pilot_worker.c` from the exact candidate checkpoint.
No candidate production code or delivered ZIP was changed for this report.
