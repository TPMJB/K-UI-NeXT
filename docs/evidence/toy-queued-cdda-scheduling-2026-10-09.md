# Queued CDDA scheduling evidence — 2026-10-09

All 30 Toy pilot regression suites passed with ASan/UBSan enabled. The full
fresh output is recorded in `toy-queued-cdda-host-tests.txt`.

## Change and comparison

The withheld single-sector design coupled producer admission to worker
publication. Two clustered worker visits per 60-Hz update could accept only
60 sectors per second, below the 75 sectors per second required by CDDA.
The new opt-in design admits one bounded audio-track interval and prepares
up to four complete verified RAW sectors independently of publication.
The worker's existing four-quanta loop and eligibility gates remain unchanged.

The reserve hint counts only complete committed 4,096-frame PCM blocks ahead
of the faster accepted playback cursor, with conservative capture age deducted.
Prefill and STOP report zero reserve. The transport further decays the hint
using elapsed ticks, with low/high thresholds of 4,096/8,192 frames. A current
missing sector can still claim audio priority. Card ownership changes only
at verified 512-byte boundaries.

## Transport and physical PCM

The independently timed engine fixture delivers 150 sectors in two seconds
at clustered 60-Hz visits. The same window with an isolated 80-ms worker
absence delivers 150 sectors, uses a four-sector catch-up burst, and restores
the initial 8,192-frame PCM reserve. Production progresses between visits;
consumer polling cannot manufacture completed data.

The actual worker fixture uses a separate finite four-slot producer with
elapsed-time advancement outside worker calls. Its modeled rate is 100 sectors
per second, including modeled game-data occupancy; it does not prove the real
card's throughput. Physical playback consumes ordered left/right samples at
44,100 frames per second through more than 26 ring wraps. It delivers all
1,500 source sectors (882,000 source frames, 20 seconds) with 19 isolated
80-ms gaps and a maximum four sectors consumed per worker visit. The 882,176
verified physical frames include 176 zero-padding frames at finite EOF.

The worker fixture also checks pending RAW/PCM privacy, exact SR restoration,
generation cancellation, repeat-wrap, fresh PLAY, full-block reserve accounting,
fast-cursor choice, capture-age decay, prefill/STOP zero reserve, and timer wrap.

## Persistent sparse cadence remains a failure

The transport's negative case delivers only 96 sectors in two seconds when
worker visits occur in tightly clustered pairs every 80 ms. The finite PCM
reserve falls below zero in that rate model. The physical worker's separate
negative case delivers 43 sectors in 752,660 ticks and shows declining reserve;
it stops before finite reserve exhaustion. These are expected insufficient-rate
results, not successful sustained audio schedules.

A four-sector queue covers about 53 ms of RAW audio. Occasional 80-ms gaps can
use the committed PCM ring and later catch-up. Persistent sparse 80-ms updates
still need a different safe worker cadence or another independently validated
design. No new sound hook is introduced here.

## Mixed wire and console gates

The independent wire fixture runs continuous game-data requests throughout
each 20-second scored window. Normal clustered 60-Hz visits deliver 1,498 RAW
sectors and 7,840 KiB of verified MODE1 game data. With 8,192 initial reserve
frames, both the first- and last-second reserve minima are 5,693 frames; final
reserve is 7,016 frames. The bounded two-sector endpoint phase remains visible
instead of manufacturing a completed transfer at the scoring boundary.

The full-ring case starts with 32,768 reserve frames and adds one exact 80-ms
worker gap. It delivers 1,498 RAW sectors plus 7,824 KiB of verified MODE1 data.
First- and last-second reserve minima are 28,501 and 30,269 frames; final reserve
is 31,592 frames. Each 16-KiB data request must complete within 100 ms, including
the final scored request verified after the window. Measured maxima are 32,977
and 33,386 ticks (42.21 and 42.73 ms). All samples remain within the asserted
reserve bound; initial reserve is not hiding an accumulating audio deficit.

These tests execute the production SCI protocol, DMA-tail IRQ, CRC, fragmented
extent mapping, engine, and GD adapter against a timed hardware model. Its
payload timing is one timer tick per byte (1.28 microseconds, about 781,250
bytes/second). Foreground service occurs at 1-ms opportunities when DMA is
idle or token/repair work needs progress. Active DMA advances independently.
Neither modeled bandwidth nor service frequency is a measurement of the user's
card or the game's real cadence. The fresh ASan/UBSan log is retained alongside
this document. This source revision is not a claim of Dreamcast video quality.

SH-4 cross-build places worker end at `0x8cfdf120`, leaving 3,808 bytes
under the unchanged `0x8cfe0000` limit. Low resident ends at `0x8c0076e8`, below
`0x8c007800`. Guarded stack margins are positive: low 64 bytes, SCI 272 bytes,
GD 2,156 bytes, and audio 3,176 bytes. Final linked audits and source identities
are emitted into the test ZIP's `build.json`; early dirty-source results are
not used as the package's exact-build proof. The default GD2/non-shared worker
is byte-identical to clean `7b55156aafa2` (SHA-256
`f3b510df3034634e01e3dd911711f94551062534d6cbf4b635900a366f903d3a`).

The linked audit checks actual plan/helper call targets, queue geometry and
private BSS, bound low/high reserve constants, integer bridges, exact SR,
relocation/cache constraints, immutable title/driver admission, native scratch,
and conservative stack totals. Mutation checks reject deliberately corrupted
plan references and undersized queue storage as well as existing bridge faults.

The release gate requires both sustained queued transport and physical PCM
markers, complete host-suite success, exact clean-source SH build, linked audits,
and the retained launcher's real package-layout gate. The known working audio
runtime and fixed launcher are pinned by SHA-256 and included separately from
the new candidate. Hardware has not yet been tested.
