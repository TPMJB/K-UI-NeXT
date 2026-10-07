# Isolated CDDA harness memory and instruction audit

Measured 2026-10-07 UTC, on the experimental foundation working tree. This is
host/cross-link evidence for Gate 2, not console playback evidence or retail
memory ownership. No ordinary reader reservation, source or release asset was
changed by this audit.

## Result

The controlled homebrew harness links in its own declared main-RAM allocation.
The PCM/ring/AICA engine does **not** fit either existing SCI low resident:
actual links retaining every public engine entry fail the unchanged stack
boundary assertions, before adding per-instance PCM state, working sample
buffers or an audio extent map. Retail integration must remain a separate,
explicitly admitted package; the homebrew allocation does not reserve a
retail game's RAM or sound resources.

The toolchain was `sh-elf-gcc 15.2.0-16 Debian 8.4` with its matching binutils,
provided at `/workspace/scratch/e58804339be4/ce-prefetch-preflight/bin`.
Freestanding builds used SH-4 little endian, integer division and reserved FPU
registers. No KOS/newlib/shell library is linked into the harness. FatFs uses a
private copy with read-only configuration; its source dependency was
`/workspace/scratch/b88160e88cfd/K-UI-NeXT/.deps/fatfs/source`.

## Ordinary payload identity

The ordinary native sources were independently built from `057f0e1` and from
a copy of the current experimental working tree, with fixed
`BUILD_ID=cdda-audit00`. The original `Makefile.dc` and linker scripts were used.
An empty temporary `KOS_BASE/Makefile.rules` satisfied the Makefile's parse-time
guard for these retail-only freestanding targets; no KOS build was needed.
The reader source/linker/ABI paths also have no diff between the release commit
`15ce191` (1.8.5) and `057f0e1`.

| Detached payload | Bytes | SHA-256 in both builds |
| --- | ---: | --- |
| `resident-sci.bin` | 11,140 | `0e9b3df402f6488701333f8574c543e489cb479225de22040637bc69d8230d99` |
| `resident-scia.bin` | 12,292 | `e0ecdea9a89a12428fcc1312a83f5d1e2df2a6fb16602f64a8d9bb0fb7cacf6c` |

The separately linked SCIF/IDE resident bins and common stage were also
byte-identical. The existing ordinary instruction/layout audits passed in
both isolated trees: 25,654 decoded instructions; one high-stage FPSCR setup;
no resident/relay FPU use or unresolved symbols. The copied trees have no Git
metadata, so the optional final `record_build.py` metadata step cannot run
there; ELF linking, binary extraction and these audits were completed directly.
This comparison deliberately tests fixed-ID payloads, not debug-containing ELF
identity or published ZIP identity.

Reproduce the SCI comparison in two isolated source copies:

```sh
make -f Makefile.dc KOS_BASE=/path/to/empty-kos-stub \
  BUILD_ID=cdda-audit00 build/retail/resident-sci.bin \
  build/retail/resident-scia.bin
sha256sum build/retail/resident-sci.bin build/retail/resident-scia.bin
```

## Actual low-link experiment

Each engine source (`src/core/cdda_pcm.c`, `src/core/cdda_ring.c`,
`src/loader/cdda_aica.c`) was compiled with the ordinary reader's
`RETAIL_FLAGS`, `RETAIL_SCI_FLAGS` and `-DKUI_RETAIL_TRANSPORT=1`, including
`-Os -flto -mrelax`. Its objects were appended to each actual SCI link from
`Makefile.dc`, with the same original linker script and all 13 public functions
forced using `-Wl,-u,_function_name`. They were:

```text
kui_cdda_pcm_init kui_cdda_pcm_seek kui_cdda_pcm_read_frames
kui_cdda_pcm_total_frames kui_cdda_ring_init kui_cdda_ring_observe
kui_cdda_ring_can_fill kui_cdda_ring_commit kui_cdda_aica_init
kui_cdda_aica_start kui_cdda_aica_stop kui_cdda_aica_position
kui_cdda_aica_write_samples
```

The `_` prefix is the compiler's symbol prefix. Forcing these names is
necessary: merely appending LTO objects allows section GC to remove the unused
engine and would produce a misleading fit result. The final trial includes
the ring guard rejecting an observed advance of at least one half-ring.

| Actual low link | Entry | Text | Read-only data | BSS | BSS end | Stack bottom | Over boundary |
| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |
| Stable standard SCI | 36 | 10,408 | 696 | 3,156 | `0x8c0077f4` | `0x8c007800` | Fits, 12 spare bytes |
| Standard SCI + engine | 36 | 12,924 | 696 | 3,188 | `0x8c0081d4` | `0x8c007800` | 2,516 bytes |
| Stable background SCI | 36 | 11,360 | 896 | 2,944 | `0x8c007ba0` | `0x8c007ba0` | Fits, 0 spare bytes |
| Background SCI + engine | 36 | 13,864 | 896 | 2,948 | `0x8c008564` | `0x8c007ba0` | 2,500 bytes |

Both candidate links returned failure with
`retail resident overlaps its guarded service stack`. Their failed-link maps
supply the section sizes/addresses above; no candidate executable was accepted.
The normal track/extent manifest alone occupies 2,172 bytes (standard SCI,
160 shared slots) or 1,020 bytes (background SCI, 64 shared slots). These are
retained in the experiment. There is no added CDDA map: these measurements
are a lower bound for a complete retail reader, not a complete integration
layout. No assertion was removed and no low-RAM boundary was expanded.

## Controlled homebrew link

Reproduce with the checked-in target:

```sh
make -f Makefile.cdda \
  CDDA_FATFS_SOURCE=/path/to/pinned/fatfs/source \
  CDDA_BUILD_ID=057f0e13c0c9 cdda
```

The audited pre-commit checkpoint uses build ID `057f0e13c0c9` and package
SHA-256 `5129f0888c014dc695e602e1618b5a7af54a9c07b2801009e49f11d67e741432`.
A later committed build regenerates its ID/checksums; use that build's package
manifest for installation identity.

| Allocation/section | Start | Exclusive end | Bytes |
| --- | --- | --- | ---: |
| Entry | `0x8c010000` | `0x8c01008c` | 140 |
| Text | `0x8c010090` | `0x8c016b40` | 27,312 |
| Read-only data | `0x8c016b40` | `0x8c01b344` | 18,436 |
| Initialized data | `0x8c01b344` | `0x8c01b358` | 20 |
| Live BSS | `0x8c01b360` | `0x8c01bf60` | 3,072 |
| Private stack | `0x8c200000` | `0x8c210000` | 65,536 |
| Owned left sound ring | sound offset `0x100000` | `0x108000` | 32,768 |
| Owned right sound ring | sound offset `0x108000` | `0x110000` | 32,768 |

The ELF has three nonoverlapping LOAD segments: RX code/rodata, RW initialized
data/BSS and RW private stack. The runtime envelope contains 45,912 payload
bytes and declares exactly 2,097,152 memory bytes from `0x8c010000` through
`0x8c210000`, including the unused address gap before the stack. BSS ends
1,982,624 bytes below the stack bottom. There are no undefined ELF symbols.
The old shell's filesystem, pointers, music thread and audio driver do not
survive into this program. Read-only FatFs owns `FATFS`/`FIL` state here; it is
not using the retail track/extent map.

Selected live BSS allocations: PCM cursor/cache 576 bytes; ring bookkeeping
24; left/right work arrays 256 each; FatFs file 572; FatFs volume 564; card
44; SCI aligned DMA scratch 512. The 64 KiB stereo sound ring is in explicitly
owned sound RAM, not main BSS. Initialization owns/reset-mutes the sound system,
channels 0/1, G2 PIO and TMU1. These destructive sound/timer operations are
permitted only in the controlled detached homebrew environment.

## Stack and instruction audit

All 117 compiler `.su` records are static. Maximum single frame:
`boot_volume.c:scan`, 4,788 bytes; `f_open`, 1,280 bytes. An independent
same-flags compile adding only `-fcallgraph-info=su` produced the conservative
main chain of 5,928 bytes:

```text
cdda_main(24) -> play(60) -> cdda_storage_open(36)
 -> cdda_storage_init(68) -> kui_boot_volume_scan(44) -> scan(4788)
 -> header.isra.0(568) -> raw_read(40) -> kui_loader_sd_read_multi(60)
 -> kui_loader_sd_stream_start(60) -> stream_finish(44)
 -> multi_command.constprop.0(40) -> transfer_block(76) -> prepare(20)
```

Indirect calls were explicitly bounded to the harness's actual callbacks:
PCM -> `read_at`; volume scan -> `raw_read`/`raw_blocks`; card operations ->
the six SCI bus callbacks. The optional SCI profiling callback remains null:
its setter is unreferenced and discarded, no callback is registered. No
reachable callback recursion was found. The path is conservative because
`cdda_storage_open` can invoke initialization even though the normal test
initializes storage before playback. The refill chain is 592 bytes before
its caller frames. Add 256 bytes for compiler library/assembly helpers:
6,184 bytes is below the 65,472 usable stack bytes above the 64-byte canary.
As a looser independent bound, the sum of *all* compiled frames, even ones
removed by GC, is 10,428; with the same allowance it still fits. Linked libgcc
helpers were integer shifts/division, not floating-point routines.

The linked `objdump -d` audit classified PC-relative literal pools before
checking instructions: 12,421 nonliteral instructions. Exactly one intentional
startup `lds r0,fpscr` at `0x8c01000c` initializes FPSCR; no other instruction
uses FPU operations/registers. Literal data such as the upper halfword of
`0xff00001c` must not be mistaken for executable `fadd` instructions.

Startup fills the entire stack with `0xa5a5a5a5`, replaces its bottom 64 bytes
with `0x43444441`, and clears aligned BSS. The final screen's guard/watermark
check runs after storage shutdown and before final report rendering. A
watermark is observed write-depth, not a worst-case proof: untouched stack
reservations, untested error paths and values matching the pattern can be
missed. Static bounds remain the acceptance evidence, and hardware readings
must still be recorded.

## Handoff/layout check

The existing runtime envelope checksum/size/address checks and bootstrap
staging guards are used. There is exactly one complete initialized transport
marker, at payload offset `0xb344`; SCI patches that record after the original
checksum passes. Main/storage use the same marker. The bootstrap admits no
unsafe on-stack trampoline/staging placement and calls `arch_exec` after
unmounting/disconnecting storage; KOS teardown precedes detached entry.

No retail high stage or low service stack is embedded in this harness. Its
initial entry executes through a P2 alias while caches are disabled, sets
masked-interrupt SR, initializes owned stack/BSS, writes CCR from P2 and waits
eight instructions before jumping to cached C code on the private stack.
The original bootstrap stack is constrained above its 4 MiB runtime maximum
plus 64 KiB; it is disjoint from the final private stack. A staging buffer may
occupy the eventual allocation only while copying; it is discarded before
BSS/stack initialization. There are no live staging pointers in C. Main code,
BSS, stack, left/right sound buffers and inherited framebuffer are in separate
address ranges. The framebuffer is external VRAM, not main RAM or sound RAM.

This establishes a bounded, independently owned development environment.
It does not establish Toy Commander memory/channel coexistence, a periodic
retail service bridge, concurrent game/audio card arbitration or underrun-free
hardware playback. Those remain later gates.
