# Toy Commander main RAM allocation: static evidence

Date: 2026-10-07. This records scalar facts obtained by privately inspecting the
user-supplied boot file. It contains no game payload or game disassembly.

The verified file has length **748444** bytes and SHA-256
`ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd`.
Its expected load address is `0x8c010000`. These findings apply only to that
exact file and load address. They do not establish a general Dreamcast memory
reservation or a completed retail CDDA integration.

## Startup and initialization

| Fact | Address/range | Confidence |
| --- | --- | --- |
| Entry transfers to runtime startup | `0x8c0b0340` | Proven in this boot |
| Startup writes a stack watermark | `[0x8c00c000, 0x8c00f400)` | Proven |
| First BSS clearing range | `[0x8c0c6ba0, 0x8c1164c0)` | Proven |
| Second BSS clearing range | `[0x8c1164c0, 0x8c124320)` | Proven |
| Runtime calls main | `0x8c02796a` | Proven |
| Main calls the system initialization wrapper | `0x8c04e8d6`, call at `0x8c027982` | Proven |
| Heap initialization function | `0x8c0b3e10` | Proven behavior; SDK name supported by embedded version string |
| Heap start passed to initialization | `0x8c124340` | Proven arithmetic |
| Heap size passed to initialization | `0x00edbcc0` | Proven arithmetic |
| Heap exclusive end | `0x8d000000` | Proven arithmetic |

The boot entry/startup does **not** assign an initial value to `r15`; it inherits
the loader's stack pointer. The watermark is evidence of the expected low RAM
stack area, not proof of the actual initial `r15`. The handoff must supply and
verify its own stack contract. No reviewed startup clearing loop reaches high
RAM staging addresses.

Before heap initialization, the reviewed path initializes SDK tables and
exception/MMU infrastructure. The RAM mapping loop covers physical
`[0x0c000000, 0x0d000000)`; mapping that range is not equivalent to clearing or
owning its contents. Runtime library-handle initialization also uses low RAM
tables. Whole-program absence of arbitrary computed stores is not proven.

## Exact early allocation hook

The unique statically identified heap initialization call loads its function
pointer from **`0x8c04e9b4`**, whose original value is **`0x8c0b3e10`**. The load
is at `0x8c04e916`, the call at `0x8c04e920`, and execution resumes at
`0x8c04e924`. The file offset of that pointer word is `0x0003e9b4`.

An exact-profile bootstrap can interpose this pointer, call the original heap
initializer with its original arguments, then obtain an actual allocation
before returning to `0x8c04e924`. Interposing the return from the entire system
initialization wrapper is later: that wrapper already initializes filesystem
buffers through the game allocator.

The initialization call runs after the wrapper has raised interrupt mask level
to 15. The allocator saves/restores the prior interrupt mask. A bootstrap must
preserve the original initialization result and calling convention, its return
address, stack balance, and the original SR; it must not enable interrupts or
start CDDA during this allocation/copy step.

The host must verify the complete boot fingerprint and original pointer word
before patching. At runtime, verify the heap inputs and allocator table, require
the expected returned pointer, and fail before using a worker if those checks
do not hold. An unexpected return must never be treated as an implicit fixed
address reservation.

## Allocator ownership and deterministic first allocation

The initialization creates a circular free list for the entire pool. Its
default allocator allocates in 32-byte units with a 32-byte header, splitting a
larger free block from its upper end. It does not clear the allocated payload.

| Item | Address/value |
| --- | --- |
| Allocation entry thunk | `0x8c0b3ede` |
| Default allocator body | `0x8c0b3ef2` |
| Free thunk | `0x8c0b3fac` |
| Default free body | `0x8c0b3fc0` |
| Allocator function-table pointer | `0x8c117dbc` |
| Default function table | `0x8c0c5ac8` |
| Heap base metadata | `0x8c117de4`, `0x8c117dec` |
| Heap size metadata | `0x8c117de8` |
| Free-list cursor | `0x8c117de0` |
| Free-list sentinel | `0x8c117dc0` |
| Allocator table setter | `0x8c0b3e56` |

For a first allocation with request `N`, the body is
`0x8d000000 - round_up_32(N)`. Its header occupies the preceding 32 bytes. For
example:

| Requested body bytes | Returned body | Header |
| --- | --- | --- |
| `0x20000` (128 KiB) | `0x8cfe0000` | `0x8cfdffe0` |
| `0x30000` (192 KiB) | `0x8cfd0000` | `0x8cfcffe0` |
| `0x60000` (384 KiB) | `0x8cfa0000` | `0x8cf9ffe0` |

These addresses are deterministic only if the interposer allocates immediately
after the original initialization, before any other allocation. Holding the
returned pointer without freeing it excludes that body from ordinary allocator
reuse. It does not prevent independent fixed-address users from writing there.

The game has wrappers at `0x8c04f0d4` (4-byte alignment), `0x8c04f11e`
(32-byte alignment), and `0x8c04f10a` (wrapper free). A separate game arena at
`0x8c04115e` obtains its parent block through the ordinary allocator; its reset
at `0x8c04118e` frees that arena's known block, not all SDK allocations.

Only one static direct heap-initialization call and one static direct allocator
table reset, inside initialization, were identified. The reviewed shutdown
path at `0x8c04e9e8` calls the no-op allocator exit at `0x8c0b3e6a`; it does not
reinitialize the pool. Indirect calls, future external modules, and different
execution paths are not excluded by this observation.

## Why the top of RAM cannot simply be borrowed

The default heap includes all RAM up to `0x8d000000`. In addition, genuine fixed
high RAM references exist:

| Reference | Consumer and implication |
| --- | --- |
| `0x8cfe0000`, pointer word `0x8c0816f4` | SDK transfer/filesystem setup at `0x8c08168a` passes it as a work buffer |
| `0x8cff0000`, pointer word `0x8c0816fc` | Same setup passes it as a second work buffer |
| Physical `0x0cff0000` | SDK board/DMA configuration, including pointer word `0x8c0bf55c` and configuration tables |
| Physical `0x0cffffff` | SDK full-RAM address-range bound at pointer word `0x8c0beb6c`; not itself a RAM write |

The SDK transfer path can load `GINXFER.BIN` and another external payload into
computed destinations. Its callback chain is
`0x8c04c810` → `0x8c0815c0` → `0x8c08168a`. Main's setup at `0x8c04c864`
installs that callback only when the descriptor word at `0x8c0ad764` is nonzero;
the verified boot stores zero there. This supports a bounded normal-launch
candidate, but does not prove the path permanently unreachable: external
handoffs or transfer modules may change the descriptor or destinations.

The boot has no cached/uncached fixed-address literal in
`[0x8cfa0000, 0x8cfe0000)` or its `0xac` alias, and no genuine PC-relative
physical pointer into that interval was identified. A scan of instruction
encodings alone produces false numeric matches; those are not pointer evidence.
Absence of a literal does not rule out computed addresses or future payloads.
Reviewed explicit exception stack changes use low SDK state/stack pointers,
not a literal top-RAM stack; the inherited return stack remains a separate
handoff obligation.

## Smallest concrete candidate

Use the exact heap-init pointer interposer to hold a **192 KiB first allocation**,
requiring body address `0x8cfd0000`. Limit the authored worker, its state, and
private stacks to `[0x8cfd0000, 0x8cfe0000)` (64 KiB). Leave
`[0x8cfe0000, 0x8d000000)` (128 KiB) unused by the worker so the known SDK
scratch locations remain untouched. If the worker does not fit, reject the
layout or choose and separately audit a larger real allocation; do not extend
into the padding.

Copy the worker from transient staging only after allocation validation. Staging
must not intersect the initial heap header, new allocation header, or source
boot/BSS/stack writes. In particular, a full 64 KiB stage at `0x8cfc0000` would
intersect the `0x8cfcffe0` allocation header and is unsuitable without a smaller
bounded length. Stage survival after the copy is unnecessary and must not be
relied upon.

This is an implementable ownership pilot, **not a completed reservation proof**.
It still requires authored link/layout bounds, cache synchronization before
executing copied code, a verified loader stack, protection against later heap
reinitialization/table replacement, and hardware validation of memory pressure
and lifetime across game transitions. Static inspection of the boot alone
cannot certify unknown overlays or external transfer payloads.
