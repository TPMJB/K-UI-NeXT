# CDDA mixed-job memory, clock and ownership audit

Measured independently on 2026-10-07 UTC for profile 06 after the final
counter-overflow and workload-clock fixes. This covers the linked program,
generated instructions, stack graph and serialized job/control model. The
new profile still needs its console run; earlier stack watermarks are not a
bound or hardware result for this build.

## Build checkpoint and preserved ordinary readers

The audited program used `Makefile.cdda`, standalone SH GCC 15.2.0 and
provisional `CDDA_BUILD_ID=000000000000`. Pinned FatFs `ff.c` has SHA-256
`3c30280ffe2d0fb19a5241d9ed9f20a2c3911c2861652ebf6ad57f74fb369250`.
The filesystem headers/configuration are private read-only copies. The final
published identifier is the same length; compare its linked geometry with
this checkpoint and use its final manifest for delivered package identities.

Both ordinary native SCI payloads were independently rebuilt from a source
archive at fixed `BUILD_ID=cdda-audit00`, using the unmodified `Makefile.dc`
and native linker scripts. An empty temporary KOS make-rules stub allowed
native-only targets to parse; no KOS runtime or working-tree release binary
was changed. Both results still match the foundation/1.8.5 identities in the
[earlier audit](cdda-next-memory-2026-10-07.md).

| Ordinary reader | Bytes | SHA-256 |
| --- | ---: | --- |
| Standard SCI | 11,140 | `0e9b3df402f6488701333f8574c543e489cb479225de22040637bc69d8230d99` |
| Background SCI | 12,292 | `e0ecdea9a89a12428fcc1312a83f5d1e2df2a6fb16602f64a8d9bb0fb7cacf6c` |

Their reservation remains unchanged. The previously measured low-reader
CDDA fit failure is not overridden by this explicit homebrew allocation.

## Linked memory and handoff

| Payload | Entry section | Text | Read-only data | Data | Live BSS | BSS end |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 57,048 | 140 | 37,400 | 19,468 | 20 | 6,048 | `0x8c01f680` |

The ELF entry is `0x8c010000`. It has three disjoint LOAD segments:

| Segment | Start | End, exclusive | Permissions |
| --- | --- | --- | --- |
| Entry/code/read-only data | `0x8c010000` | `0x8c01dec4` | RX |
| Initialized data/BSS | `0x8c01dec4` | `0x8c01f680` | RW |
| Private NOLOAD stack | `0x8c200000` | `0x8c210000` | RW |

The envelope declares exactly **2,097,152 bytes**, including the address gap
and 65,536-byte stack. Live BSS ends well below the stack. There are no
unresolved symbols. The payload contains exactly one complete initialized
AUTO transport marker, at offset `0xdec4`; the bootstrap validates the
envelope before patching it to SCI. Main and storage share that definition,
and main refuses an invalid or non-SCI marker.

The mixed buffer is 2,048 bytes, 32-byte aligned at `0x8c01df60`. The job object
is 56 bytes; its pending identity is kept separately from returned read
bytes. The independent data `FIL` is 572 bytes. Audio and data retain separate
cursors/read buffers while sharing one private read-only mount and synchronous
SCI lease. Audio open/close and reprime do not close the data file; shutdown
closes both before unmount/release.

Startup and handoff are unchanged: `arch_exec` removes KOS, assembly enters
through P2, masks interrupts, initializes FPSCR, clears private BSS, fills
the stack watermark/64-byte guard, writes CCR from P2, waits eight instructions
and installs the private stack pointer before entering cached C code. No old
shell pointer, KOS service or retail stage was added.

Homebrew AICA ownership remains channels 0/1 and two 32,768-byte sound-RAM
rings at offsets `0x100000..0x108000` and `0x108000..0x110000`. AICA ARM remains
reset and G2 DMA is suspended before bounded PIO. The runtime owns TMU1 and
its existing framebuffer contract. This allocation grants no ownership of
a retail game's RAM, sound driver or channels.

## Stack and linked instruction audit

Independent same-flags compiles added only `-fcallgraph-info=su`. Every emitted
compiler stack record is static. Direct paths and the actual PCM, boot-volume
and SCI callbacks were resolved; the unused SCI profiling callback remains
null. No recursion, dynamic frame or unknown call was found. Linked integer
division/shift helpers were inspected and receive an extra 256-byte allowance.

| Main frame | Conservative main chain | With allowance | Sum of all compiled frames | Audited instructions |
| ---: | ---: | ---: | ---: | ---: |
| 332 | 6,280 | 6,536 | 11,632 | 16,661 |

Usable private stack is **65,472 bytes**, excluding the guard. Even the looser
sum of every compiled frame, including unused functions discarded by GC,
is 11,632 + 256 = 11,888 bytes. The largest single frame remains the partition
scanner at 4,788 bytes; FatFs `f_open` uses 1,280 bytes.

The conservative deepest path is:

```text
cdda_main(332) -> command_accept(104) -> cdda_storage_open(36)
 -> cdda_storage_init(68) -> kui_boot_volume_scan(44) -> scan(4788)
 -> header.isra.0(568) -> raw_read(40) -> kui_loader_sd_read_multi(60)
 -> kui_loader_sd_stream_start(60) -> stream_finish(44)
 -> multi_command.constprop.0(40) -> transfer_block(76) -> prepare(20)
```

It includes possible initialization even though ordinary playback has already
mounted storage. The runtime watermark observes actual stack writes and is
checked before final-screen work; retain the static bound and guard and record
profile 06's own console watermark.

The literal-aware instruction audit allows only the single startup
`lds r0,fpscr` at `0x8c01000c`. No other instruction uses FPU operations or
registers. PC-relative literal pools were excluded before classification,
including address constants whose bits resemble FPU opcodes.

## Timer contract and arithmetic

The fixed TMU reference is **12,468,720 Hz**, derived from the already pinned
official KallistiOS `timer.c` at commit
`fcfa7d869471591ca1c777543261a7bfea7cb726`: documented CPU 199,499,520 Hz,
peripheral clock CPU/4, and owned TMU selection peripheral-clock/4. The
[calibration hardware record](cdda-calibration-commands-hardware-2026-10-07.md)
preserves the previous nominal conversion and explains the conditional
agreement. The reference is not an independent oscillator measurement of
this console. Audio pitch and clock hardware are not retuned.

New conversions split quotient/remainder before multiplication, use only
32-bit arithmetic and reject output overflow without modifying the output.
Fixed remainder products stay below `UINT32_MAX`. An independent property
check compared five million randomized conversion results against 64-bit
floor/ceil reference math, including overflowing results; all passed.

| Conversion | Reduced ratio | Rounding |
| --- | --- | --- |
| Tick duration to microseconds | `12500/155859` | Floor |
| Milliseconds to ticks | `311718/25` | Ceil |
| Tick duration to audio frames | `735/207812` | Floor |
| Audio frames to ticks | `207812/735` | Floor or explicit ceil |

The ring's half threshold is 2,316,184 ticks, the floor of 8,192 frames;
the 8 ms reserve is 99,750 ticks, rounded up. Remaining margin is rounded
down; allowed frame progress is rounded down plus the existing 64-frame
observation allowance. The AICA 2 ms FIFO timeout is 24,938 ticks, rounded
up, with its independent finite poll limit retained. Duration displays,
stress budgets, deliberate delays and admission all use this same contract.

Unsigned subtraction measures durations across one numerical counter
boundary. Regular observations are mandatory: no 32-bit clock can infer a
missed complete counter period. The stream clock accumulates seconds/subticks
without an overflowing tick sum. The new workload clock and no-progress
origin start after the first successful audio key-on, excluding initial
file opens and prefill from the 180-second workload gate.

## Job identities, cancellation and audio service

File ranges are checked with `bytes <= file_bytes - offset` before addition.
Chunk lengths are at most 2,048 bytes, and `first + done` remains within the
validated file. Job generations do not wrap. A separate chunk generation
rejects a duplicate callback from an older chunk of the same logical job.
READY and commit check the complete job/data-epoch/chunk/offset/length identity.
Aggregate bytes, chunks and completion counters are checked for overflow
before core progress is committed.

The data-file lifetime epoch is independent of audio-command generations.
Audio SEEK, PAUSE/STATUS/RESUME and STOP/restart preserve the selected data
request at serialized operation boundaries. Explicit data cancellation
retires its generation/epoch. Stale dispatch is rejected before a phase query
or storage operation; stale commit is rejected before publishing progress
or changing metrics. Tests also cover malformed spans and an old chunk while
a newer chunk of the same job is READY.

Cancellation is exercised before dispatch, after a committed first chunk,
and after a verified read in READY before commit. The first case performs
no read; the READY case discards checked bytes rather than counting them.
READING cancellation returns BUSY because this model does not abort active
SCI DMA. The token-only fatal cleanup API is explicitly owner-only, after
physical I/O has returned/quiesced; checked per-operation failures use the
full-span READY API. No asynchronous callbacks execute in this harness.

Dispatch requires both ring halves ready and remaining active-half time
greater than twice the learned read/observe/verify cost plus the 8 ms reserve.
Audio's needed refill runs before another data operation. A blocked read is
followed by a position/deadline observation before its bytes become READY,
progress is committed or another operation runs. Only independently verified,
committed chunks refresh the no-progress timestamp; a five-second gap fails
the workload, including permanent admission closure after a slow operation.

Once the audio model is initialized, fatal mixed paths commit its matching
current epoch to FAULT, stop owned audio and close its source, then close the
data file. Audio command completion
retains its stale-epoch preflight before hardware effects and credits the
already captured initial played position. Pause/resume/seek use observed
playback rather than PCM prefetch, and retain the original loop start/end.
These are cooperative homebrew transitions, not sample-exact key-off,
autonomous mute during arbitrary CPU stalls or retail command dispatch.

The pass gates require one contiguous 8 MiB request completed before the
120-second stage deadline, all eight logical size classes with at least
16 completions each, three expected cancellations, six expected stale
refusals, at least 180 workload seconds and zero unexpected errors. Final
drain leaves no live job, pending chunk or pending audio action. The seven
common result rows, seven mixed paired rows and final failure row total
15, within the existing scrolling result area; profile/build labels and
completion status remain in their fixed screen regions.

## Validation and provisional identity

All eight strict ASan/UBSan unit programs and all 28 integration scenarios
passed. The job suite includes 2,776,613 checks. An independent job review
confirmed range and token invariants under the serialized owner model.
Integration independently ledgers physical spans and verified logical
progress, checks all five audio command types while a data job remains live,
and starts TMU near `UINT32_MAX` so successful three-minute runs cross a
numerical wrap. It also exercises read failures/corruption, service-delay
failure, slow-read coverage refusal and no-progress refusal, later seek-reprime
failure and stale adapter calls without model or hardware mutation.

These simulations do not establish actual SCI/G2 latency, audible channel
order, absolute pitch or retail resource coexistence. Profile 06 remains
pending console evidence.

The provisional `cdda-harness.kui` SHA-256 is:

```text
40b415abaf4f071c1734e707476ad76eac7339c2ef1430adc8f7a7df92e4d5ff
```

Reproduce with the pinned source/toolchain:

```sh
python3 tools/test_cdda_host.py --sanitize
make -f Makefile.cdda CDDA_BUILD_ID=000000000000 cdda-mixed-test
```

Use the [mixed console checklist](../cdda-mixed-jobs-test.md) for the new
run. Preserve the earlier tested checkpoints and ordinary runtime backup;
record the result before adding retail hooks or assigning game-owned RAM/AICA.
