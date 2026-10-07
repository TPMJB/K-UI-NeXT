# CDDA controls, soak and data-stress memory audit

Measured 2026-10-07 UTC on the experimental branch after the first console
baseline passed its five software stages. The photographed 5,184-byte stack
watermark is baseline hardware evidence, not a bound for these new profiles.
This audit covers the new cross-linked programs; they still require console
listening and timing tests. No retail game is launched.

## Build checkpoint and unchanged ordinary readers

All four programs were built with provisional `CDDA_BUILD_ID=000000000000`
using the standalone SH GCC 15.2.0 toolchain and `Makefile.cdda`. The package
contains the profile maps and compiler `.su` records. The final published
commit replaces only this fixed-length build identifier; its package hashes
come from the final build manifest. Compare final section geometry with this
checkpoint before delivering that build.

Independent copies of the ordinary native source were rebuilt after these
changes at `BUILD_ID=cdda-audit00` using the unmodified `Makefile.dc` and native
linker scripts. Both payloads remain byte-identical to foundation `057f0e1`;
its ordinary reader source paths are unchanged from 1.8.5 release `15ce191`.

| Stable reader | Bytes | SHA-256 |
| --- | ---: | --- |
| Standard SCI | 11,140 | `0e9b3df402f6488701333f8574c543e489cb479225de22040637bc69d8230d99` |
| Background SCI | 12,292 | `e0ecdea9a89a12428fcc1312a83f5d1e2df2a6fb16602f64a8d9bb0fb7cacf6c` |

The low-reader fit failure remains as measured in the [initial memory audit](cdda-harness-memory-2026-10-07.md). These programs use their explicit homebrew
allocation; they do not enlarge the stable low-reader reservation.

## Linked layout

| Profile | Payload | Entry | Text | Read-only data | Data | Live BSS | BSS end |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 baseline | 46,028 | 140 | 27,384 | 18,480 | 20 | 3,648 | `0x8c01c220` |
| 1 controls | 48,840 | 140 | 29,736 | 18,940 | 20 | 3,712 | `0x8c01cd60` |
| 2 soak | 46,576 | 140 | 28,024 | 18,388 | 20 | 3,680 | `0x8c01c460` |
| 3 stress | 49,144 | 140 | 30,056 | 18,920 | 24 | 5,792 | `0x8c01d6a0` |

All profiles enter at `0x8c010000` and have three disjoint LOAD segments:
RX entry/code/read-only data, RW initialized data/BSS, and the RW private
stack at `0x8c200000..0x8c210000`. Each envelope declares **2,097,152 bytes**
including the address gap and 65,536-byte stack. Even the largest live BSS
ends well below the stack. There are no unresolved symbols.

Stress owns a 2,048-byte, 32-byte-aligned data buffer in BSS. Its second `FIL`
is 572 bytes and has a separate cursor/read buffer. Audio and data share one
read-only filesystem and one synchronous SCI lease. Audio open/close does not
alter the data file; shutdown closes both before unmount/release. Data calls
cannot race audio calls because only the detached main loop executes them.

The two sound rings remain at sound offsets `0x100000..0x108000` and
`0x108000..0x110000`, 32,768 bytes each, on owned channels 0/1. The owned
TMU1, reset AICA ARM, sound system and inherited framebuffer contract are
unchanged. None of these grants establishes ownership in a retail game.

## Stack and generated instructions

Independent same-flags compiles added only `-fcallgraph-info=su` to inspect
generated direct/indirect call paths. All emitted compiler stack records are
static. Indirect callbacks are bounded to `read_at`, the volume raw-read/block
callbacks and the six actual SCI bus callbacks; the unused SCI profiling
callback remains null. No callback recursion or missing/dynamic frame was
found. Compiler/library helpers receive an additional 256-byte allowance.

| Profile | Main frame | Conservative main chain | With 256-byte allowance | Sum of all compiled frames | Audited instructions |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 baseline | 32 | 5,936 | 6,192 | 10,548 | 12,449 |
| 1 controls | 120 | 6,012 | 6,268 | 10,696 | 13,370 |
| 2 soak | 84 | 5,928 | 6,184 | 10,544 | 12,707 |
| 3 stress | 220 | 6,064 | 6,320 | 10,680 | 13,533 |

The usable private stack is **65,472 bytes**, excluding its 64-byte guard.
The largest conservative chain plus allowance is 6,320 bytes. Even the
looser bound summing every compiled frame, including code discarded by GC,
is at most 10,696 + 256 = 10,952 bytes. Maximum single frame remains the
partition scanner at 4,788 bytes; FatFs `f_open` uses 1,280 bytes.

The deepest stress path is conservatively:

```text
cdda_main(220) -> cdda_storage_open(36) -> cdda_storage_init(68)
 -> kui_boot_volume_scan(44) -> scan(4788) -> header.isra.0(568)
 -> raw_read(40) -> kui_loader_sd_read_multi(60)
 -> kui_loader_sd_stream_start(60) -> stream_finish(44)
 -> multi_command.constprop.0(40) -> transfer_block(76) -> prepare(20)
```

It includes a possible initialization call even though normal playback has
already mounted storage. Linked integer division/shift helpers were inspected.
The literal-aware instruction audit permits exactly one startup
`lds r0,fpscr` at `0x8c01000c` in each program; no other instruction uses
FPU operations/registers. Each binary retains exactly one full initialized
transport marker, at offsets `0xb3b8`, `0xbeb4`, `0xb5dc` and `0xbfe0` for
profiles 0–3. Startup/BSS/stack/cache handoff remains as previously audited.

The runtime watermark remains an observed write depth rather than a
worst-case proof. Keep static bounds and guard checks; record each new
profile’s console watermark rather than assuming the first run’s 5,184 bytes.

## Arithmetic, playback and scheduling review

- Segment ends are validated by subtraction before adding the first frame.
  Repeat reads cross loop boundaries within each 128-frame chunk, preserving
  channel order. Cursor mapping uses `played % span` and a bounded addition;
  nonrepeating exact EOF is legal, and beyond-EOF seeks are rejected.
- Pause captures the latest observed AICA-played cursor, including the
  initial hardware position. It discards the prefetched PCM cursor, confirms
  mute/stop, then reopens/reprimes from the observed frame. This is an
  observed-cursor restart, not a claim of sample-exact stop latency.
- The 15-minute target is 39,690,000 played frames. Cumulative played-frame
  addition has an overflow guard. Timer elapsed time accumulates seconds and
  subticks without an overflowing 32-bit sum; each regular observation handles
  one counter boundary. Fifteen minutes must complete at least two wraps and
  900 loop passes. Polling must remain more frequent than a complete counter
  period; missed full wraps cannot be inferred from one 32-bit counter value.
- Controls deliberately miss the conservative service deadline by 190 ms.
  The rejected ring cursor is discarded, no refill is published, owned audio
  is stopped, and a fresh session tests recovery. This policy refusal is
  counted separately from unexpected faults. Arbitrary CPU stalls can replay
  already queued audio until control returns; there is no autonomous mute IRQ.
- Stress jobs are at most 2,048 bytes, only run with both halves ready, and
  require remaining active-half time greater than twice the learned complete
  job cost plus the 8 ms margin. Read completion is followed immediately by
  an observation before another job or audio publication. Every returned
  byte is checked using a 32-bit offset avalanche pattern, avoiding repeated
  128 KiB aliases. The learned cost includes observation and verification.
- Stress cannot pass after a token amount of data: it requires at least the
  full 8 MiB checked, at least 64 KiB per 60-second interval, and no five-second
  gap between verified jobs. A slower learned budget that permanently closes
  admission causes a starvation failure. Byte counters reject overflow.

The host integration suite exercises the actual profile orchestrator with
real PCM/ring/stream helpers and independent card/AICA models. Its passing
cases cover cursor continuity, controls/recovery, 900-second timer wraps,
verified data, and injected PCM/data failures, corruption, blocking delay and
slow successful workload starvation. These are simulation checks; they do
not establish real SCI/G2 timing, audible quality or retail coexistence.

## Provisional package identities

| Profile | SHA-256, provisional build `000000000000` |
| --- | --- |
| 0 baseline | `b9283d3b7401509172a4bb045e8968fa7987e9e316c7c82f99bb91e658accaa9` |
| 1 controls | `95e2c3a75b1f99987e6a7b27c1b9fdf46697e1d3e95080ba535c621d77741bc8` |
| 2 soak | `967fca1b8118257776cced0e5d30ae0eb450774d4076f3b8bf2bec7c4395a092` |
| 3 stress | `af5b27d117d9e00d4ed03c8cb3fc6b174580bc5f8b9ca6dff53db2c4821f755b` |

Reproduce using the pinned FatFs source and SH toolchain:

```sh
make -f Makefile.cdda CDDA_BUILD_ID=000000000000 \
  CDDA_FATFS_SOURCE=/path/to/pinned/fatfs/source cdda cdda-profiles
python3 tools/test_cdda_host.py --sanitize
python3 tools/test_cdda_storage.py --fatfs-source /path/to/pinned/fatfs/source
python3 tools/test_cdda_harness.py
```

Use [the next console checklist](../cdda-next-test.md) for admission order,
listening, duration and result recording. A safe failure stop is useful
evidence but does not pass a soak/stress profile. Retail command hooks,
periodic service, bounded audio/game arbitration and Toy Commander sound/RAM
coexistence remain separate later gates.
