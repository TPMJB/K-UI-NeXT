<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Toy CDDA: R/C/S results and loader priority

## Current decision after S

The owner clarifies that CDDA music begins **after** Toy Commander's intro
FMV. The intro remained unchanged in S. The owner requests that working CDDA
be preserved and further audio changes be deferred. Retain the established
continuous-audio fallback `7b55156aafa2`; S is a silent diagnostic. This
evidence update changes no runtime code or binaries.

The previous natural-intro comparison conflated two phases. Audio-source
cost cannot explain a pre-PLAY intro. The production worker's STOPPED,
quiescent path returns before clock sampling, ARM/port observation, sound
allocation, RAW reading or AICA copying. Pilot bridge, lifecycle, cache and
GD hooks remain installed. The cumulative report has no first-PLAY timestamp
or phase boundary; it cannot independently establish the owner's timing.

S build `b2e2b0e164de`, photos `62216` through `62223`, decodes as:

| Measurement | S | R comparison |
|---|---:|---:|
| Source operations | 906 generated | 1,058 physical audio reads |
| Mean / maximum source operation | 0.119809 / 0.120320 ms | 4.639419 / 6.289920 ms |
| GD read steps / logical sectors | 2,656 / 5,107 | 2,656 / 5,107 |
| Mean / maximum GD read step | 8.620889 / 10.689280 ms | 8.612232 / 10.685440 ms |
| Maximum worker visit | 8.659200 ms | 22.481 ms |

Source last/max/total/calls are `0x5d / 0x5e / 0x14b42 / 0x38a`.
GD total/max/steps are `0x0110f459 / 0x209f / 0xa60`; worker maximum is
`0x1a6d`. The nominal timer scale remains 781,250 ticks/s. S generated
12.08 seconds of source PCM, not elapsed run time. Terminal state is
intentional PAUSED, command 22. Fault, stack, source, queue,
active-bank-write, recovery and reserve-denial counters are zero; updater
entries and returns match at 1,105. No measured FPS is available.

S reduced audio-source and worker costs while GD timings stayed essentially
unchanged. Its unchanged pre-CDDA intro does not measure that audio benefit.
The 346-348 ms/s estimate below applies only during active real CDDA reads,
not to the earlier intro.

Prioritize DATA loading, read/completion scheduling and movie presentation.
The strongest control is the **same Toy intro** with a verified ordinary
nonpilot loader, identical image/SCI settings and cold-boot procedure. R, C,
S and the clean-audio fallback all retain pilot hooks. There is no menu
toggle that removes those hooks. The historical ordinary backup name
`retail-boot-before-observe.kui` is documented, but its actual preservation
has not been verified; do not instruct an unverified runtime swap.

The pilot DATA path includes the ordinary resident reader, but fixes each
EXEC step at two sectors. The ordinary reader can enlarge steps using its
pacing policy. Thus this control compares the complete loader configuration,
not just audio activation. Separate request supply, completion notification
and presentation before attributing an FMV fault to bandwidth alone.

DOA2 has an owner-reported smooth-FMV baseline and Evolution a separate FMV
fault; use a second title only as a follow-up ordinary-loader comparison.
Those observations do not diagnose Toy. Freeze audio implementation work.

## Earlier R/C hardware result

The user reports no noticeable improvement from C or R. Both profiles reached
the expected cache policy and P2 native CHECK destinations. Neither report
recorded worker/stack, RAW, queue or active-bank-write faults. The terminal
PAUSED state and command 22 are intentional controls, not failures.

| Measurement | R: `73e8363c10f7` | C: `ca8d4adfcade` |
|---|---:|---:|
| Observed CCR | `0x101` | `0x105` |
| RAW requests | 1,058 | 2,003 |
| Mean synchronous RAW callback | 4.639 ms | 4.609 ms |
| Maximum RAW callback | 6.290 ms | 6.351 ms |
| GD read steps | 2,656 | 2,670 |
| Mean GD step | 8.612 ms | 8.629 ms |
| Maximum GD step | 10.685 ms | 10.683 ms |
| Maximum worker visit | 22.481 ms | 22.984 ms |
| Maximum mono copy, at most 256 bytes | 58.88 us | 60.16 us |

These are decoded supplied report photographs, using the admitted nominal
TMU0 rate of 781,250 ticks/s. No independent oscillator calibration or FPS
measurement is available. C processed more source PCM: 26.707 seconds versus
R's 14.107 seconds. Those are source lengths, not elapsed wall time. Compare
normalized call timings rather than cumulative totals.

RAW total/calls are R `0x3a8392 / 0x422`, C `0x6e0e96 / 0x7d3`.
GD total/calls are R `0x0110ae2d / 0xa60`, C `0x0112a5ba / 0xa6e`.
The R GD total's final digit is mildly ambiguous in the photograph; that
does not change the conclusion. Recovery and reserve-denial pages are zero.
These counters do not measure movie presentation, between-visit contention
or native sound-effect loss. GD diagnostic rejection counts are not physical
card-error counts. Service gaps include deliberate controls.

## What changes the priority

The worker already reads passive positions published by the original ARM
driver at `0xa08014a4`, `0xa08015e8` and `0xa08015ec`. It does not write the
hardware channel monitor selector. SWAT's selector-ownership warning is
relevant to AICA design, but it is not a demonstrated race in this path.
Repeated G2 admissions and template/position checks may still be expensive.

RAW callbacks and GD EXEC reads hold interrupts masked; the low RAW callback
also sets SR.BL. Four RAW sectors can be admitted in one worker visit. Its
16 ms budget is checked between quanta and can overrun by a complete read.
The observed 22–23 ms visit maxima matter to game scheduling even if the
ring itself records no recovery.

At nominal 75 RAW sectors/s, 4.6 ms per callback implies approximately
346–348 ms of synchronous callback elapsed time per second of CDDA. This is
a conditional workload estimate, not measured whole-game CPU utilization.
At approximately 750 mono copies/s, multiplying the observed maximum copy
duration gives about 44–45 ms/s for timed copy bodies. Copy timing excludes
some proof, admission and preparation work, and boundary splits add calls.
This comparison supports prioritizing storage occupancy; it does not prove
that AICA observation is cheap or that DMA cannot help.

## Completed S control

S was built from the R policy, substituting zero PCM in the existing 2352-byte
RAW staging buffer on a normal source cache miss. Preserve LBA/generation,
normal `silence=false` frame accounting, source requests, conversion, copy
proofs, DATA exclusion, playback and controls. No card callback runs for
audio. The separate end-of-track silence path would freeze track progress
and is therefore unsuitable for this control.

S distinguishes audio source reads **while CDDA is active**, retaining the
native movie/SFX environment. It is deliberately silent and is not a fix.
The pre-CDDA intro is a separate loader comparison.
Use the [S checklist](../cdda-toy-synthetic-source-test.md).

## Deferred audio routes and separate loading routes

These remain research options. The owner has asked to freeze working CDDA
and prioritize the loader; they are not the next selected audio changes.

| Route | Concrete benefit | Constraint before console delivery |
|---|---|---|
| Prepared PCM with an independent consumer | Ready audio reaches AICA without waiting for card progress or the next sound updater | Prove a native wake/handler lease, bounded service and physical cancellation; a producer queue alone previously failed |
| Resumable SCI backend | Return game execution between bounded transfers rather than busy-feed whole sectors | DMA ownership, exact SR/vector restoration, immutable staging and quiescent cancellation |
| Packed DATA with unchanged RAW CDDA | Move 2048 payload bytes rather than 2352 per DATA record: 12.9% less sustained DATA traffic | Narrow title/descriptor admission and equivalence; later raw-DATA requests cannot be reconstructed |
| Passive cursor/template batching and fused stereo PIO | Reduce repeated G2 admissions without reducing proof opportunities | Aggregate time bounds, sequential stereo semantics and existing freshness/deadline gates |
| Native ARM RequestEvent wakeups | Potentially replace tight probing with block-crossing signals | SDK callback table, event consumer, IRQ ownership, overflow/rearm and generation association are unproved |
| Offline planar PCM16 / PCM8 / Yamaha ADPCM | Organize uploads, or reduce audio traffic by 50%/75% for lossy formats | Owned sidecar index, format admission, quality, cursor units and codec state at seek/pause/wrap |

The pipeline should keep source requests, fully prepared PCM chunks, physical
transfer completion, playback retirement and presentation as distinct states.
For G1, retain those contracts and replace the transfer backend. Polling a DMA
to completion on SH remains blocking; calling a full worker recursively from
an IRQ is not the selected architecture.

### Capacity and alignment

R/C's worker ends at `0x8cfd7520`, leaving 35,552 bytes before SDK scratch at
`0x8cfe0000`. Twelve RAW slots cover 160 ms and add 25,872 bytes over the
current single slot, leaving 9680 bytes for code/metadata. Sixteen slots add
35,280 bytes and leave only 272 bytes: not a credible reservation. Four
slots cover 53.33 ms, below the rejected build's roughly 124 ms service gap.
Source coverage alone does not remove AICA safe-write deadlines.

A 512-frame assembler produces two 1024-byte aligned mono planes spanning
RAW boundaries. Each RAW sector has 588 frames and 1176-byte mono planes,
which are not 32-byte transfer multiples. Preserve exact leftovers, finite
tails and repeats; never round writes past an inactive leased span.

### Packed DATA is a separate loading experiment

The current Toy admission rejects cooked DATA even though the general reader
already supports mixed cooked DATA and RAW audio. A narrow future profile can
pin the canonical converter descriptor CRC `b7100f9e`, original 15-track
starts/ends, DATA controls/stride, RAW audio extents and exact boot identity,
while keeping all executable/driver hashes and ordinary admission unchanged.
Its canonical descriptor is derived from the converter, not verified against
the user's current card. Do not silently admit arbitrary converted images.

Sequential 32 DATA sectors use 147 physical 512-byte blocks for the original
RAW stride versus 128 for packed DATA. Cold two-sector requests use 9–10
versus 8. The verified DATA geometry removes 102,017,840 stored bytes. None
of this reduces CDDA traffic or proves decoder/presentation compatibility.
The existing reader safely refuses raw-DATA requests on cooked tracks.

### Format and hardware ownership

At 44.1 kHz stereo, PCM16 needs 176,400 bytes/s; PCM8 needs 88,200 and 4-bit
ADPCM needs 44,100. Planar PCM16 preserves samples and simplifies aligned
uploads but does not reduce bandwidth. Contiguous left/right chunk records
avoid switching between separate whole-track files. Compressed assets need
a separate verified sidecar contract, not relabeled RAW tracks.

ADPCM is not a format-byte substitution. The current worker pins PCM16 sizes,
offsets and templates. Decoder predictor/step history and ring wrap/seek must
be established. The Yamaha manual marks format 3 prohibited while current
KOS names it long-stream ADPCM; resolve that against the exact hardware and
unchanged Toy driver. Zero encoded ADPCM bytes are not PCM silence.

G2 DMA is independent of SH-4 DMAC but requires a native descriptor/event
lease. A hardware-idle check is not ownership. Store queues also need native
graphics ownership: classic SH7750 queue contents cannot be read back, so
saving QACR cannot preserve a partially prepared native queue.

## Console lessons from PSX and Saturn

PSn00bSDK's CD example separates a main-RAM producer from a SPU IRQ/DMA
consumer and releases buffered work after physical completion. Its compressed
VAG duration and STR's hardware MDEC/drive XA cannot be transferred directly
to this game. The reusable lesson is explicit staged ownership and independent
consumption. SEGA's Cinepak manual couples buffer size, task frequency and DMA
burst length and recommends tracing supply, consumption and presentation.

Primary references reviewed:

- [PSn00bSDK CD producer](https://github.com/Lameguy64/PSn00bSDK/blob/master/examples/sound/cdstream/main.c)
  and [SPU consumer](https://github.com/Lameguy64/PSn00bSDK/blob/master/examples/sound/cdstream/stream.c).
- [PSn00bSDK STR](https://github.com/Lameguy64/PSn00bSDK/blob/master/examples/mdec/strvideo/main.c).
- [SEGA Cinepak manual](https://www.infochunk.com/saturn/segahtml_en/movi/cine/hon/p04_101.htm).
- [KOS G2 contracts](https://kos-docs.dreamcast.wiki/group__system__g2bus.html),
  [passive positions](https://kos-docs.dreamcast.wiki/sound_8h.html) and
  [sample types](https://kos-docs.dreamcast.wiki/group__audio__aica__samples.html).
- [Renesas SH7750 hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware), section 4.7.
- [Yamaha AICA manual](https://manuals.plus/m/d260f79355dbff7664bdab58c9b8c9bc4b04fa272fdfc7c933ea28a47a167836.pdf), codec/control sections.

Exact native contracts remain in the existing cursor, command, sound and
lifecycle evidence notes. No SWAT implementation was inspected or copied.
