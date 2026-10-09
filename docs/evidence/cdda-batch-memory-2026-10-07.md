# Controlled BIOS batch-read memory and native vector audit

Independently audited on 2026-10-07 UTC for bundled profiles 09 and 10. This checkpoint adds
incremental multi-sector PIO reads to the controlled CDDA client. Each EXEC
performs at most one 2,048-byte physical data read; confirmed progress belongs
to the complete BIOS request. Profile 10 deliberately omits service after one
confirmed chunk and validates the resulting failure. Both new console runs and
stack watermarks remain pending. The preceding profile 08 hardware pass is recorded in
[cdda-bios-hardware-2026-10-07.md](cdda-bios-hardware-2026-10-07.md).
This is a cooperative integer-call experiment, not a retail interrupt bridge.

## Measured build and preservation

Measurements use the final provisional `CDDA_TEST_PROFILE=9` build with
`CDDA_BUILD_ID=000000000000`, including whole-request chunk-ID capacity
admission and checked fault cleanup. Profile 10 is measured separately below.
Exact published source IDs and artifact hashes belong to `build.json`;
these provisional measurements are not published-artifact hashes.

The recovered pinned compiler is `sh-elf-gcc (GCC) 15.2.0`, binutils 2.45.1,
from the validated [toolchain CI run](https://github.com/TPMJB/K-UI-NeXT/actions/runs/37642902424).
Recovery and archive verification are documented in
[cdda-service-memory-2026-10-07.md](cdda-service-memory-2026-10-07.md).
Compilation is freestanding with fixed FP registers, integer division and
`-nostdlib`; libgcc supplies integer helpers. No KOS kernel or target libc is linked.
Private FatFs retains read-only exFAT configuration. Its pinned `ff.c` SHA-256 is
`3c30280ffe2d0fb19a5241d9ed9f20a2c3911c2861652ebf6ad57f74fb369250`.

The accepted profile 08 core and client files remain separate and unchanged.
Shared SCI, AICA, storage, startup and bridge source bodies are unchanged;
main/command/display branches and the new targets are conditional on profiles 09/10.
`Makefile.dc`, released 1.8.5 reservations and bootstrap sources are unchanged.
After both profiles were finalized, rebuilding profile 08 with its published
`f7318ac45e3e` ID reproduced the accepted runtime byte-for-byte: SHA-256
`e1773971c79f6e5781e0de887f989b8689d6a02d6f4f52250b21d1c3afcb2404`.
Previously measured ordinary fixed-ID SCI identities are retained; this audit
performs source preservation checks without a redundant ordinary-reader rebuild:

| Payload (`BUILD_ID=cdda-audit00`) | Bytes | Earlier measured SHA-256 |
| --- | ---: | --- |
| Standard SCI | 11,140 | `0e9b3df402f6488701333f8574c543e489cb479225de22040637bc69d8230d99` |
| Background SCI | 12,292 | `e0ecdea9a89a12428fcc1312a83f5d1e2df2a6fb16602f64a8d9bb0fb7cacf6c` |

Those earlier compiler identities are preservation evidence, not a claim that a
fresh build with a different compiler target/ABI reproduces their hashes.

## Linked allocation

Payload is **3,085,120 bytes** (`0x2f1340`); reservation is **3,211,264 bytes**
(`0x310000`), `0x8c010000..0x8c320000`. Entry remains `0x8c010000`.
Six disjoint PT_LOAD segments cover every allocated section:

| Allocation, exclusive end | Initialized bytes | Memory bytes |
| --- | ---: | ---: |
| Engine RX `0x8c010000..0x8c01ee40` | 60,992 | 60,992 |
| Engine state RW `0x8c01ee40..0x8c020800` | 20 | 6,592 |
| Engine stack RW `0x8c200000..0x8c210000` | 0 | 65,536 |
| Worker stack RW `0x8c220000..0x8c230000` | 0 | 65,536 |
| Client RX `0x8c300000..0x8c301340` | 4,928 | 4,928 |
| Client stack RW `0x8c310000..0x8c320000` | 0 | 65,536 |

Sections: `.entry` 140, `.text` 42,008, `.rodata` 18,824, `.data` 20,
`.bss` 6,560, `.client` 4,928 and `.client_bss` 0. Three NOLOAD stacks total
196,608 bytes. Client entry is actually loaded high at `0x8c300cec`.
Its 32,768-byte data buffer and canaries reside on its private stack; the engine
still uses a separate 32-byte-aligned 2,048-byte buffer at `0x8c01ef60`.
Audio/data retain separate private FILs under one SCI lease and one filesystem.
Exactly one SCI marker occurs at payload offset `0xee40`; undefined symbols: 0.
Flattened initialized gaps are included in the payload.

The unchanged bootstrap admits this payload under its 4MiB payload and 8MiB
memory limits, subject to its existing allocation/staging/copy checks.
Passing profiles 07/08 established this large-envelope route on the supplied
console/card; the exact new profile 09 image remains a separate console gate.

## Stack and instruction audit

All emitted C stack reports are static. A separate compilation with the same
flags produced GCC call graphs, with indirect storage, PCM, map and client
callbacks bound to their actual targets. Stack-switch edges are separate roots:
suspended caller frames remain on their own stacks. Conservative paths include
storage scan branches even when an already-mounted session would avoid them.

| Stack | C/assembly path including destination anchor | With 256-byte helper allowance | Usable after 64-byte guard |
| --- | ---: | ---: | ---: |
| Engine | 5,992 | 6,248 | 65,472 |
| Client | 33,788 | 34,044 | 65,472 |
| Worker | 6,072 | 6,328 | 65,472 |

Largest C frame is now client entry: 33,324 bytes, including its bounded batch
buffer. Main is 148, worker 212, fault worker 84 and native dispatcher 56 bytes.
The partition scanner remains 4,788 bytes. All compiled C frames sum 46,300;
adding 112 for assembly saves/anchor and 256 for helpers gives a deliberately
loose 46,668-byte single-stack bound, also below 65,472. Linked integer helper
families remain the previously inspected copy/shift/division routines with at
most 20 additional stack bytes. Static estimates and host guard stubs are not
console watermarks; the new larger client frame needs profile 09's actual guards.

The unchanged generic bridge saves 32 caller-stack bytes and gives a destination
16-byte anchor. Native vector entry is `0x8c01a144`, dispatch is `0x8c0114e0`.
The shared 42-byte wrapper still saves/restores PR and r8..r14, retains r0 and
passes r4..r7 unchanged. Client and forced worker reentry load the actual
`0x8c0000bc` slot. Nested busy refusal adds 88 worker-stack bytes: wrapper 32
plus dispatcher 56, with no further worker switch. Busy refusal precedes SP
validation, timer reads, mapping and SCI. Install/restore retain exact old-word
readback and cleanup retry. No interrupted context or FPU preservation is claimed.

A literal-aware scan examined 21,070 decoded nonliteral instructions, excluding
2,444 PC-relative literal halfwords. Sole FPU configuration is startup
`lds r0,fpscr` at `0x8c01000c`; no executable FPU arithmetic was found.
This is a decoded-code scan, not a proof that every instruction runs. The native
wrapper does not write SR/VBR/GBR, reset SP or alter IRQ configuration;
ordinary integer T/Q/M flags remain caller-clobbered.

## Batch identities and host validation

REQUEST validates copied count 1..16, the complete source FAD span and the
complete destination span before admission. It reserves enough remaining chunk
IDs for the full request. Handle, queue epoch, chunk ID, audio epoch and data-job
token are separate nonwrapping identities. `owner.work` stays immutable;
`owner.pending` describes the exact current chunk, including its confirmed
prefix, offset and destination. Exact validation precedes mapping/I/O and runs
again after the complete service lease. Only successful completion publishes
progress; another chunk returns to QUEUED/PROCESSING with ATA 4.

ABORT operates between EXEC calls, preserving the confirmed prefix. Fatal
cleanup uses pending for RUNNING and take/failure for QUEUED; FAILED reports
only earlier committed bytes and ATA 0. Callers discard the unconfirmed remainder,
even when a late failure left bytes touched there. Terminal CHECK acknowledges
once. The tested partial ABORT occurs while playing; partial RESET occurs after
STOP. Neither interrupts an active physical SD transfer or changes audio state.

`tools/test_cdda_batch_integration.py --sanitize` passed all ten strict
ASan/UBSan scenarios: normal, PCM/data faults, corruption, 90ms admission
starvation, 200ms read and late-leave failures, RELEASE re-prime failure,
frozen client clock and transient restore-readback failure. An independent
64-bit clock begins 30 seconds before wrap. The model verifies every played
sample, copied full requests, each exact 2,048-byte physical span, confirmed
prefixes, canaries and untouched suffixes, actual pause cursor and repeat origin.
Five stale/forged adapter callbacks refuse with no mapping, time, I/O or switch
effects: prior chunk, prior request, canceled/reset work and forged destination.

Normal: 8 gates, 0 faults, 90.105 seconds, 3 key-ons, 8,116 EXECs, 17,728 vector
calls, 5,747 chunks and 11,769,856 confirmed bytes. Independent completed logical
requests establish one full 8MiB pass under looping audio and all eight size
classes with at least 16 completions each; partial cancel/reset prefixes are
excluded from full-pass coverage. Class/progress counters match the engine.
Physical and late-leave faults after an earlier chunk retain a 2,048-byte
request prefix without counting the failed chunk. Starvation stops at 5.160
seconds; frozen clock returns at 40.1 seconds. Vector retry restores the old word.
Physical timing, cache effects, audible quality and native watermarks remain
hardware gates; retail IRQ and active-transfer abort are unproved.


## Separate profile 10 expected-deadline companion

The final provisional `CDDA_TEST_PROFILE=10` build uses the same owned engine,
vector wrapper and export header with a separately linked fault-client object.
It does not replace profile 09's sustained coverage client. Source/ID/hash
metadata for each published image belongs to that image's `build.json`.
Payload is **3,082,656 bytes** (`0x2f09a0`); memory remains **3,211,264 bytes**
(`0x310000`). The package including its header is 3,082,720 bytes.

| Allocation, exclusive end | Initialized bytes | Memory bytes |
| --- | ---: | ---: |
| Engine RX `0x8c010000..0x8c01ee08` | 60,936 | 60,936 |
| Engine state RW `0x8c01ee20..0x8c0207e0` | 20 | 6,592 |
| Engine stack RW `0x8c200000..0x8c210000` | 0 | 65,536 |
| Worker stack RW `0x8c220000..0x8c230000` | 0 | 65,536 |
| Fault client RX `0x8c300000..0x8c3009a0` | 2,464 | 2,464 |
| Client stack RW `0x8c310000..0x8c320000` | 0 | 65,536 |

All six LOADs are disjoint and cover their allocated sections. The map places
only `cdda_batch_fault_client.o` in the high client section; the normal client
object is absent. Sections: entry 140, text 41,944, rodata 18,832, data 20,
BSS 6,560, client BSS 0. Client entry is `0x8c300954`; its initialized section
contains the actual vector-slot load and raw four-register call. The private
engine buffer is at `0x8c01ef40`. SCI marker occurs exactly once at `0xee20`;
undefined symbols: 0. The same bootstrap limits admit this image.

| Stack | Path including anchor | With 256-byte helper allowance | Usable |
| --- | ---: | ---: | ---: |
| Engine | 5,984 | 6,240 | 65,472 |
| Fault client | 33,464 | 33,720 | 65,472 |
| Worker | 6,072 | 6,328 | 65,472 |

The client entry shim is a zero-frame tail jump; its split implementation has
a 33,044-byte frame and is included in the graph. Main uses 140 bytes; worker,
fault worker and dispatcher remain 212/84/56. All compiled C frames sum 45,628;
the loose sum plus assembly/anchor/helper allowance is 45,996. Callback binding
is conservative, including larger callable targets where an indirect site could
be resolved more narrowly. Actual console watermarks remain pending.
Native vector entry is `0x8c01a108`, dispatcher `0x8c0114a4`; the shared wrapper
and busy-before-SP/time/map/I/O ordering remain intact. Literal-aware scanning
covers 19,915 decoded nonliteral instructions excluding 2,347 literal halfwords;
only the unchanged startup FPSCR setup is present, with no FPU arithmetic.

The client starts looping audio, submits a copied 16-sector READ and confirms
one 2,048-byte EXEC chunk. Two PROCESSING polls return that same prefix. Its
profile-specific ARM_GAP diagnostic requires the exact queued handle, 32 KiB
request, 2 KiB confirmed prefix, running audio and ACTIVE service lease. It then
waits 200 ms using only clock calls. The next EXEC must refuse specifically
with service-enter DEADLINE before a worker read or audio observation. Ordinary
worker-stack fault retirement stops owned audio and fails the queued remainder.
Terminal CHECK reports FAILED/IO, 2 KiB and ATA 0 once; duplicate CHECK is unknown.
The entire suffix and both canaries remain unchanged. A later bad-count REQUEST
and stale ABORT cause no physical I/O; DRIVE reports FAULT.

`tools/test_cdda_batch_integration.py --profile 10 --sanitize` passed six strict
ASan/UBSan cases. The planned case is **8 gates / 0 failures**, with exactly one
expected deadline, zero service errors, one physical read/chunk, 2,048 confirmed
bytes, 16 client probes and 17 actual vector calls including one nested refusal.
The model independently records ARM_GAP time/identity/counters and verifies no
PCM read, data read, hardware position query or sample write afterward. Both
early read failure and corruption fail at 2/1 with zero confirmed bytes and zero
expected gaps. A 200 ms physical-read delay before ARM_GAP also fails at 2/1;
its deadline is unannounced and cannot satisfy the planned-fault gate. Frozen
client time fails at 1/1 after the bounded 40.1-second polling limit. A transient
restore-readback failure after the planned gap fails at 7/1 and cleanup retries
the exact old vector word. Planned execution itself lasts 0.266 model seconds.
A fresh profile 09 normal ASan/UBSan run after the shared cleanup changes remains
8/0 with the sustained coverage values above. No older broad suite was repeated
for this profile-specific addition. Both bundled images still require console
execution; this experiment does not establish active SD abort or retail IRQ use.
