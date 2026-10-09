# Controlled CDDA service memory and C-call audit

Independently audited on 2026-10-07 UTC for profile 07. This checkpoint
establishes a linked controlled client, disjoint engine/client/worker stacks,
bounded cooperative service and an ordinary integer SH C-call boundary. The
profile's console run is still pending. This is not evidence of a retail
interrupt bridge, an untrusted-client sandbox or shared game-owned resources.

## Build checkpoint and ordinary readers

The measured build uses `Makefile.cdda`, `CDDA_TEST_PROFILE=7` and provisional
`CDDA_BUILD_ID=000000000000`. The exact cached pinned compiler was recovered
through the isolated
[toolchain CI run](https://github.com/TPMJB/K-UI-NeXT/actions/runs/37642902424):
`sh-elf-gcc (GCC) 15.2.0`, binutils 2.45.1. Its recovery source is
`d31e0a70631fe96dca65d48a511bd4389f55d047`. The artifact ZIP SHA-256 is
`941c137250ee207cccfac02c39f61de46e1355a5465e40b0bf921775059096fe`;
every internal `SHA256SUMS` entry was also verified. The freestanding build
links libgcc and private read-only FatFs, without a KOS kernel or target libc.

Pinned FatFs `ff.c` SHA-256 remains
`3c30280ffe2d0fb19a5241d9ed9f20a2c3911c2861652ebf6ad57f74fb369250`.
Its headers/configuration are private copies; `FF_FS_READONLY=1` and
`FF_FS_EXFAT=1` were checked. The ordinary `Makefile.dc`, native SCI reader
sources/linkers, reservation and bootstrap sources are unchanged from the
[profile 06 checkpoint](cdda-mixed-jobs-memory-2026-10-07.md). Their previously
measured fixed-ID identities are retained; no redundant ordinary-reader
rebuild or released-binary replacement was performed for this source-only
preservation check.

| Ordinary SCI payload, fixed `BUILD_ID=cdda-audit00` | Bytes | Prior measured SHA-256 |
| --- | ---: | --- |
| Standard reader | 11,140 | `0e9b3df402f6488701333f8574c543e489cb479225de22040637bc69d8230d99` |
| Background reader | 12,292 | `e0ecdea9a89a12428fcc1312a83f5d1e2df2a6fb16602f64a8d9bb0fb7cacf6c` |

These are the earlier compiler's measured identities, not a claim that a
fresh build with a different compiler target/ABI must reproduce them. The
released 1.8.5 allocation and the previously failed low-reader CDDA fit are
not changed by this explicit homebrew envelope.

## Linked allocation and executable client

The provisional runtime payload is **3,081,728 bytes** (`0x2f0600`). Its
envelope reserves **3,211,264 bytes** (`0x310000`) from `0x8c010000` through
`0x8c320000`, including gaps and all three stacks. Entry remains
`0x8c010000`. All six LOAD segments are physically and virtually identical,
nonoverlapping and inside that envelope.

| LOAD segment | Start | End, exclusive | Initialized bytes | Permissions |
| --- | --- | --- | ---: | --- |
| Engine entry/text/read-only data | `0x8c010000` | `0x8c01dd90` | 56,720 | RX |
| Engine data/BSS | `0x8c01dda0` | `0x8c01f620` | 20 | RW |
| Engine stack | `0x8c200000` | `0x8c210000` | 0 | RW |
| Service worker stack | `0x8c220000` | `0x8c230000` | 0 | RW |
| Controlled client | `0x8c300000` | `0x8c300600` | 1,536 | RX |
| Client stack | `0x8c310000` | `0x8c320000` | 0 | RW |

| Section | Bytes |
| --- | ---: |
| Engine entry | 140 |
| Engine text | 37,752 |
| Engine read-only data | 18,808 |
| Engine initialized data | 20 |
| Live engine BSS | 6,240 |
| Linked client text/literals/padding | 1,536 |
| Client BSS | 0 |
| Each private NOLOAD stack | 65,536 |

The client is initialized PROGBITS covered by its executable LOAD, with
exact ELF-section bytes present at the matching offset in the flat runtime.
Its SHA-256 is
`aac8c8221cf489f2155a105f52f25ed3aa0a157ce39dc9f3ef0b6325ba91e515`.
The actual `_cdda_client_entry` is `0x8c300154`; `_thunk`, `_invoke` and
`_work` are also in the high client section. Engine wildcards exclude the
client object across every text/read-only/data/BSS/COMMON selector before
the dedicated client section collects it. This matters because an
`EXCLUDE_FILE` placed inside a multi-selector wildcard excluded only its
first selector in the initial trial; the corrected linked placement was
checked, rather than inferred from the linker script.

The descriptor's aligned engine-code allocation ends at `0x8c01dda0`, where
its engine-state allocation begins; live state ends at `0x8c01f620`. The
controlled client owns the separate `0x8c300000..0x8c310000` code/data
reservation even though this executable uses only its first 1,536 bytes.
No engine BSS or stack reaches that region.

There are no unresolved ELF symbols. Exactly one complete initialized AUTO
transport marker occurs at payload offset `0xdda0`; the unchanged bootstrap
must patch it to SCI before main admits the harness. The 2,048-byte service
buffer is 32-byte aligned at `0x8c01de20`. Audio and data retain their own
572-byte `FIL` objects, one private read-only mount and one serialized SCI
lease. No filesystem write operation or old shell service is introduced.

## Bootstrap admission and initialization

The unchanged runtime contract admits at most 4 MiB of initialized payload
and 8 MiB of memory. This flat image includes the zero-filled gap before the
high client, so the actual load/staging requirement is approximately 3 MiB,
not merely the roughly 58 KiB of initialized engine/client sections. Both
declared limits are satisfied.

The existing reader allocates the entire payload, reads it in 32 KiB chunks
and verifies CRC. Before `arch_exec`, bootstrap requires a word-aligned
staging source at or above the destination, source and end below
`0x8d000000`, and a 64 KiB separation from its live stack. These checks bound
the forward copy, including permitted source/destination overlap. Bootstrap
stack placement must also remain above destination plus the full 4 MiB
payload allowance and 64 KiB reserve. No bootstrap change was needed.
Successful allocation, staging admission and execution of this larger image
still require profile 07's console result.

Existing assembly startup enters through P2, installs the controlled SR and
FPSCR setup, clears engine BSS, fills the engine watermark/64-byte guard,
enables caches through the existing P2 CCR sequence and selects the engine
stack. Before client entry, engine code initializes both additional stack
watermarks/guards and clears the separate client BSS range. `arch_exec`
removes KOS; no old shell pointer survives this handoff.

## Separate stacks and ordinary SH C calls

The engine calls the client with stack top `0x8c320000`. Accepted exported
service work calls the worker with top `0x8c230000`. It never resets the
suspended engine stack or client caller stack. The linked bridge is at
`0x8c01901c`; the register probe is at `0x8c019054`.

The bridge saves `r8..r14` and PR, consuming 32 bytes on the suspended caller
stack. It selects a disjoint target stack and reserves a 16-byte anchor
containing the old SP. After an ABI-conforming return, it restores that SP,
the saved registers and PR while preserving returned `r0`. The probe saves
its own original registers, seeds/checks `r8..r14`, and compares PR with its
actual linked return address `0x8c01907a` (literal at `0x8c0190d4`). Actual
linked call sites select the stated client/worker stack tops.

Pure stale/native-busy checks occur before clock, AICA phase, storage or a
worker-stack switch. A nested worker request is refused while the outer
call remains busy, so it cannot overwrite a live worker frame or unlock that
call. This also makes the apparent worker-to-export call-graph cycle a
single pure refusal path, rather than recursion. Matching owner retirement
occurs after successful client return; failed exits stop owned audio and
retire an ACTIVE lease.

These are ordinary integer C calls. The bridge does not preserve FPU/FPSCR
state, implement an interrupt trampoline or recover a callee that corrupts
SP or cannot return. Its own assembly writes no SR/VBR/GBR configuration.
The end-to-end snapshot compares VBR, GBR and SR configuration while
excluding ordinary caller-clobbered T/Q/M bits (`0x301`); integer comparisons
and libgcc division change those flags. Existing SCI operations still mask
and restore SR as before. This is not full retail CPU-context compatibility.

## Static stack and instruction measurements

Independent same-flags compiles added only `-fcallgraph-info=su`. All compiler
frames are static. Known PCM, partition scanner, SCI and client export-table
callbacks were resolved. The assembly target calls were split at their
stack-switch boundary and assessed as separate roots; suspended frames were
not mistakenly charged to or reset on the destination stack.

| Stack | Conservative chain including applicable assembly/anchor | With 256-byte helper allowance | Usable bytes |
| --- | ---: | ---: | ---: |
| Engine | 6,092 | 6,348 | 65,472 |
| Client | 236 | 492 | 65,472 |
| Worker | 6,104 | 6,360 | 65,472 |

The main frame is 144 bytes and the worker frame 140 bytes. The client path
includes entry (88), work (44), register probe (32), export wrapper (24),
caller bridge save (32) and destination anchor (16). Engine/worker deepest
paths conservatively include command acceptance, storage open/initialization,
partition scanning and SCI callbacks even when the mount is already live.
The largest individual frame is the partition scanner, 4,788 bytes;
its header helper is 568 bytes and FatFs `f_open` is 1,280 bytes.

The sum of every compiled C frame, including GC-discarded functions, is
12,128 bytes. Charging that entire sum to any one stack, plus its largest
assembly/anchor overhead of 80 bytes and the 256-byte helper allowance,
gives a looser per-stack cap of **12,464 bytes**. Linked integer copy, shift
and division helpers were inspected separately, including RTL-inserted
libgcc helpers absent from C callgraph edges; none invalidates that allowance.
No uncontrolled recursion or unknown dynamic frame was found.

The literal-aware audit examined **17,732 instructions**, excluding actual
PC-relative literal pools before classification. It found only the existing
startup `lds r0,fpscr` at `0x8c01000c`; no other FPU operation/register use
occurs in engine, client, bridge or linked libgcc code. All three bottom
64-byte guards are checked. Watermarks measure actual stack writes, not every
reserved byte in an unwritten frame, so a later console watermark supplements
the static bounds instead of replacing them.

## Descriptor, leases, workload and limits

The distinct 160-byte `KCDDAH1` descriptor has forty little-endian words and
no native pointers. Exact revision, flags, SCI/read-only rights, channel mask
3, TMU1, 12,468,720 Hz reference, sound range `0x100000..0x110000` and reserved
zeros are checked. All six nonempty allocations use canonical aligned P1
addresses, are pairwise disjoint and cover their intended entry points.
Physical/P2 aliases and wrapped or reversed bounds are rejected without
masking addresses. Export/context pointers live in a separate native table.
The descriptor is a controlled allocation contract, not memory protection.

Guard generations and call generations cannot wrap. Stale/busy refusals
precede mutation. Ticket/epoch outputs cannot overlap guard state, including
an eight-byte ticket beginning four bytes before it. Entering a call does
not refresh the lease; completion must precede the limit measured from the
last successful service completion/start. Unsigned subtraction handles one
numerical clock wrap, with regular calls required to exclude a missed full
counter period. The half lease is 2,316,184 ticks; ring policy and bounded
post-I/O observation remain authoritative before audio publication.

The client performs its own integer work and explicitly services the engine
for at least 90 seconds. It tests actual-played-cursor pause/resume, seek
within the original repeating tone range, STATUS, stop/restart, two retired
epoch refusals and one nested refusal. Data jobs read/check at most 2,048
bytes with checked file bounds, independent request identity and admission
from live audio margin. Blocking reads are followed by audio observation;
checked commits alone advance coverage. At least 64 KiB must be verified,
and the no-progress guard refuses continued admission starvation.

Passing numerical gates are seven stages, six completed audio commands,
five STATUS checks, eight descriptor refusals, one nested refusal, two stale
refusals and one deliberately induced deadline, with zero unexpected
service/data failures and stopped audio. The expected gap intentionally
withholds cooperative service; without an interrupt, a lease cannot undo
audio already heard during a stall. Sound ownership remains channels 0/1,
the two 32 KiB rings and the controlled AICA ARM/reset/DMA contract; TMU1 and
the existing framebuffer remain harness-owned. No game-owned allocation is
claimed. The final screen contains fifteen result rows within its existing
display budget.

Root's unified strict ASan/UBSan validation passed nine units, all 28 earlier
integration scenarios and eight new service scenarios. The independent
service simulation also passed its eight cases, including PCM/data errors,
data corruption, slow-read/no-progress refusal, later re-prime failure and
bounded client-clock stall. Native bridge substitutes check control/data
behavior, not SH register preservation, separate physical stacks, oscillator
accuracy or audible continuity. Those remain bounded by the linked audit
and the requested console run.

## Provisional identities

After the final comment/header rebuild, the executable payload and geometry
are unchanged from the audited code. Fixed provisional-ID identities are:

- Runtime SHA-256: `de7dc1209652247be5bcb8e75ecfbc32163d9976bd7966e4abe922d687f5f9f9`.
- ELF SHA-256: `b36e57e2f22128c56f936e9723a0bee10270c163f79543645852f14e6222996a`.
- Payload CRC32: `26c76af8`.

Final publication changes the twelve-byte build ID; use the delivered
`build.json` for final runtime/ELF/map/source identities. Recheck its envelope,
geometry and expected fixed-ID payload difference before packaging. This
report does not label provisional hashes as delivered-package hashes.
