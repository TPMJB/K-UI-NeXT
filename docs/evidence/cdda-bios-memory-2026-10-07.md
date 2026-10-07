# Controlled BIOS CDDA memory and native vector audit

Independently audited on 2026-10-07 UTC for profile 08. This establishes a
linked controlled client, owned GD vector and bounded cooperative EXEC on a
separate worker stack. Profile 08 console execution and stack watermarks remain
pending. Profile 07's separate hardware pass is recorded in
[cdda-service-hardware-2026-10-07.md](cdda-service-hardware-2026-10-07.md).
This audit does not establish a retail interrupt bridge or shared game resources.

## Measured build and preservation

Measurements use the final provisional `CDDA_TEST_PROFILE=8` build with
`CDDA_BUILD_ID=000000000000`, including the restore-readback retry fix.
The final published build's exact source ID and artifact hashes belong to
`build.json`; provisional measurements here are not published-artifact hashes.
The runtime envelope and all linked sections were checked against the ELF.

The compiler is recovered pinned `sh-elf-gcc (GCC) 15.2.0`, binutils 2.45.1,
from the validated [toolchain CI run](https://github.com/TPMJB/K-UI-NeXT/actions/runs/37642902424).
The recovery and archive verification are documented in
[cdda-service-memory-2026-10-07.md](cdda-service-memory-2026-10-07.md).
Compilation is freestanding with fixed FP registers, integer division and
`-nostdlib`; linked helpers come from libgcc. No KOS kernel or target libc is linked.
Private FatFs has `FF_FS_READONLY=1`, `FF_FS_EXFAT=1`, `FF_FS_REENTRANT=0`;
its pinned `ff.c` SHA-256 remains
`3c30280ffe2d0fb19a5241d9ed9f20a2c3911c2861652ebf6ad57f74fb369250`.

The ordinary SCI reader, startup, bridge, AICA and storage source bodies are
unchanged. New main/command/display branches and the BIOS target are conditional
on profile 08. `Makefile.dc`, released 1.8.5 reservations and bootstrap sources
are unchanged; no released runtime was replaced. Previously measured ordinary
fixed-ID SCI payload identities are retained without a redundant rebuild:

| Payload (`BUILD_ID=cdda-audit00`) | Bytes | Earlier measured SHA-256 |
| --- | ---: | --- |
| Standard SCI | 11,140 | `0e9b3df402f6488701333f8574c543e489cb479225de22040637bc69d8230d99` |
| Background SCI | 12,292 | `e0ecdea9a89a12428fcc1312a83f5d1e2df2a6fb16602f64a8d9bb0fb7cacf6c` |

Those earlier compiler identities are preservation evidence, not a claim that a
fresh build with a different compiler target/ABI reproduces those hashes.

Profile 07 was rebuilt with the recovered compiler and its published ID
`f8f35fab3723`. Its runtime remains byte-identical to the passing package:
SHA-256 `919f6fb9b47f4dedc71adc6009b4c89f066fb0f5b9a0c4663ac5e445f9a9fe5a`.

## Linked allocation

Payload is **3,084,256 bytes** (`0x2f0fe0`); reservation is **3,211,264 bytes**
(`0x310000`), `0x8c010000..0x8c320000`. Entry is `0x8c010000`.
The six PT_LOAD segments are disjoint and cover every allocated section:

| Allocation, exclusive end | Initialized bytes | Memory bytes |
| --- | ---: | ---: |
| Engine RX `0x8c010000..0x8c01e898` | 59,544 | 59,544 |
| Engine state RW `0x8c01e8a0..0x8c0201c0` | 20 | 6,432 |
| Engine stack RW `0x8c200000..0x8c210000` | 0 | 65,536 |
| Worker stack RW `0x8c220000..0x8c230000` | 0 | 65,536 |
| Client RX `0x8c300000..0x8c300fe0` | 4,064 | 4,064 |
| Client stack RW `0x8c310000..0x8c320000` | 0 | 65,536 |

Sections: `.entry` 140, `.text` 40,600, `.rodata` 18,784, `.data` 20, `.bss` 6,400,
`.client` 4,064, `.client_bss` 0; the three NOLOAD stacks total 196,608 bytes.
Client entry is `0x8c30084c`; it is actually loaded high, outside engine code.
Its mutable command/read/canary buffers reside on its own stack. The engine's
separate 2048-byte read buffer is 32-byte aligned at `0x8c01e960`.
Audio/data use separate private FILs under one SCI lease and one filesystem.
There is exactly one SCI boot marker, at payload offset `0xe8a0`, and no
undefined ELF symbols. Flattened initialized gaps are included in the payload.

The unchanged bootstrap admits this payload under its 4MiB payload and 8MiB
memory limits; the existing copy/staging requirements still apply. Profile 07
proved this large-envelope route on the supplied console/card. Profile 08's
exact linked image and native vector behavior still require its own console run.

## Stack and instruction audit

Every emitted C stack report is static. A separate compilation using the same
flags produced GCC call graphs; indirect storage, PCM, map and client callbacks
were bound to their actual controlled targets. Stack-switch edges were evaluated
as separate roots; suspended engine/client frames were never added to a worker
stack or reset. Conservative reachable paths include storage scan paths even
when the mounted-session path would avoid them.

| Stack | C/assembly path including destination anchor | With 256-byte helper allowance | Usable after 64-byte guard |
| --- | ---: | ---: | ---: |
| Engine | 5,988 | 6,244 | 65,472 |
| Client | 2,868 | 3,124 | 65,472 |
| Worker | 6,060 | 6,316 | 65,472 |

Largest C frame is partition `scan`: 4,788 bytes. Main is 144, client entry 2,432,
worker 200, fault worker 72 and native dispatcher 52. The client frame includes its
2048-byte data buffer and two 32-byte canaries. All compiled C frames sum 15,036;
adding 112 bytes for assembly saves/anchor and 256 for helpers gives a deliberately
loose 15,404-byte single-stack bound, also below 65,472. Inspected libgcc copy,
shift and division helpers use at most 20 additional stack bytes; the 256-byte
allowance covers them. Static estimates do not substitute for profile 08's
console stack guards and measured watermarks.

The generic bridge saves 32 caller-stack bytes and gives each destination a
16-byte anchor. The native vector at `0x8c019bb4` is 42 bytes of instructions:
it saves/restores PR and r8..r14, retains r0 and passes r4..r7 unchanged to
`cdda_bios_native_dispatch` at `0x8c011390`. Client code loads the actual pointer
slot `0x8c0000bc` before the call. Forced worker reentry loads the same slot;
its 32-byte vector frame plus 52-byte dispatcher costs 84 bytes on that worker.
Busy refusal precedes SP checks, timer reads, pointer mapping and worker switch.
No interrupted context or FPU register preservation is claimed.

A literal-aware scan examined 20,101 decoded nonliteral instructions, excluding
2,277 PC-relative literal halfwords. The sole floating-point configuration
instruction is startup `lds r0,fpscr` at `0x8c01000c`; no executable FPU arithmetic
was found. This is a decoded-code scan, not a proof that every instruction runs.
The new wrapper writes no SR/VBR/GBR, resets no SP and alters no IRQ configuration.
Ordinary integer T/Q/M flags remain caller-clobbered. Vector install and exact
old-word restoration use writeback/invalidation and readback; failed restoration
retains ownership until the cleanup retry verifies the old word.

## BIOS identities and host validation

REQUEST/CHECK copy or report queue state without AICA or SCI operations. EXEC
checks its lease before switching to the worker. Copied BIOS handle/queue epoch,
audio epoch and data-job token remain separate identities; exact work is
validated before writable mapping and again before completion. Successful READ
progress stays READY until the outer service lease passes. Failure stops owned
audio, retires queued/running work as FAILED with zero accepted bytes, and
preserves held terminals for one-time CHECK acknowledgement. A failed client
return uses the same guarded cleanup. The 8ms publication reserve is retained.

`tools/test_cdda_bios_integration.py --sanitize` passed all 10 strict ASan/UBSan
scenarios: normal, PCM/data failures, corruption, 90ms admission starvation,
200ms read deadline, 200ms late leave, RELEASE re-prime failure, frozen client
clock and one-shot restore-readback failure. Independent 64-bit timing begins
30 seconds before numerical wrap; the normal 60.094-second run crosses once.
The model checks every played sample between observations, copied request
ranges, exact physical READ spans, guest bytes/canaries, actual pause cursor,
original repeat origin, pure vector operations and guarded cleanup reentry.

Normal result: 8 gates, 0 failures, 4 key-ons, 5,459 EXECs, 18,404 vector calls,
7,813,120 checked bytes and 7 audio actions. 90ms starvation stops at 6.164 seconds
with a verified 5-second progress gap. Both 200ms cases accept zero bytes; late
leave retires READY without checked/completion progress. The restore-readback
case retries the exact old word: 3 writes, 4 readbacks, restored 1 and installed 0.
Physical SD/G2 timing, cache behavior, console watermarks and profile 08 execution
remain separate hardware gates; this controlled map is not complete retail BIOS fidelity.
