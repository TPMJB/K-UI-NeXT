# Toy Commander sound contract: static evidence

Date: 2026-10-07. Status: **candidate contracts, no enabled retail audio patch**.
This document contains numeric addresses and independently written observations,
not game code, driver bytes or disassembly. Inspection was read-only. The ordinary
1.8.5 reader is outside the candidate scope and must retain its existing behavior.

## Input and limits

The supplied `1GUTH.BIN` is 748,444 bytes, CRC32 `cdc493b3`, SHA-256
`ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd`.
All executable addresses assume load address `0x8c010000`. The disassembly and
the supplied executable remain private outside the repository.

“Observed caller” means a direct relative call or a literal target reference
found in this executable and its inspected local control flow. It does not
prove absence of unrecognized indirect calls or dynamically loaded code.
Facts below are separated from interpretations that require the external driver
or a bounded console observation. No inactive-channel or unused-address probe
constitutes a resource lease.

## Initialization and the external driver

| Item | Executable evidence |
| --- | --- |
| Game sound initialization | `0x8c04a0fa`; initialized flag `0x8c0a7318` |
| Driver selection | `0x8c06a9b0`; game passes selector `1`, choosing 64 ports and `audio64.drv`; selector `2` chooses 32 ports and `MidiDa.drv` |
| Driver/port state | `0x8c10f514`: port count, selector, filename |
| SDK sound initialization | `amInit` at `0x8c06aa86`; initialized flag `0x8c0a8940` |
| Driver installation | `acSystemInstallDriver` at `0x8c0833b8`; image size is rounded to 32 bytes before installation |
| Sound heap creation | `amInit` obtains first free driver sound memory, then calls `amHeapInit` at `0x8c06a3ba` |
| Driver cursor table | Game reads sound-RAM word `0xa08000e8` until stable, adds sound-RAM base, and stores the resulting pointer at `0x8c0d9064` |

The game allocates a temporary ordinary-heap buffer, passes it to `amInit`,
which loads and installs the external driver, and frees that temporary buffer.
Its later initialization path sends an all-port stop and initializes its host
and voice tables before publishing its initialized flag. Therefore a playback start
inserted immediately after `amInit` would be stopped by the rest of the game
initializer. Allocation may occur there, but playback must wait until the game
initializer has completed successfully.

The boot file contains ac/am host-side wrappers, not the external ARM sound
driver. Reusing those interfaces avoids replacing the game's ARM program or
resetting AICA. Exact external `audio64.drv` identity and semantics remain an
admission input, as described below.

## Actual game voice allocator

The game does not use the embedded generic `amVoiceAllocate` entry
`0x8c083a12` on any observed call path. Its custom allocator operates directly
over 64 ports and calls the ac interface. A generic am voice allocation therefore
does not exclude a port from the game.

| Table or function | Address / shape |
| --- | --- |
| Occupancy bytes | `0x8c0d9068`, 64 bytes |
| Committed port state | `0x8c0d90a8`, 64 records of 36 bytes |
| Desired port state | `0x8c0d99a8`, 64 records of 36 bytes |
| Generation handles | `0x8c0da2b4`, 64 words; low byte identifies port |
| Round-robin updater index | `0x8c0a7324` |
| Picker | `0x8c04a868` |
| Setter | `0x8c04a708`; fills desired record and marks occupancy |
| Updater | `0x8c04a270`; sends stop/close/open/play/pan/volume/rate commands |
| Normal game sound service | `0x8c04ab7a`; calls updater, then retires invalid handles |

The picker returns a free port first. When stealing, it considers occupied
desired records whose byte 33 is nonzero and chooses an older generation. Game
play code maps byte 33 nonzero to loop-off; zero maps to loop-on. A reservation
made only by setting occupancy can disappear on table reset, and the updater
would then close or modify that port. Capping the actual picker and updater is
the stronger candidate exclusion.

| Fingerprinted candidate site | Current numeric bound | Candidate bound |
| --- | --- | --- |
| Picker bound `0x8c04a8ac` | 64 | 62 |
| Updater initial work counter `0x8c04a29a` | 64 | 62 |
| Updater wrap comparison `0x8c04a5d2` | Port 63 | Port 61 |

The work counter is decremented before the first processed step; its initial
value is not exactly the number of visited records. The wrap comparison and
picker bound establish the proposed port range 0–61. The updater's saved start
index must already be within that range when admission occurs. The candidate
must be installed before any game sound allocation and retain SDK capability
of 64 ports; reducing SDK capability would reject the adapter's own ports.

Both movie port allocations at `0x8c06d826` and `0x8c06d8c2` use the same
picker. Movie paired play at `0x8c06da0a` calls game pairing helper
`0x8c04a80e` with those two previously picked ports. The ordinary effect creation
entry `0x8c04ac72` also uses the picker. The only observed `acDigiOpen` target
reference is from the custom updater. These facts support, but do not by
themselves admit, a reservation of ports 62 and 63.

## Stop, mask, clear and teardown paths

| Path | Behavior and implication |
| --- | --- |
| Game initialization, all-port stop at `0x8c04a1cc` | `acDigiMultiStop` with both masks all ones; own playback must begin afterwards |
| Game sound release `0x8c049f98` | All-port stop, then `amShutdown`, then game initialized flag becomes zero; adapter must retire its lease before original release |
| SDK shutdown `0x8c06aa0e` | Calls all-port stop/close path `0x8c06a92e`, shuts down sound system, zeroes heap state via `0x8c069b60`, and clears initialized flags |
| Updater paired stop `0x8c04a55a` | Masks are built from the current port and its paired partner; movie partners come from the capped picker |
| Desired-record clear `0x8c04a666`, `0x8c04a6ce` | Clears one 36-byte host record; actual stop/close occurs through updater |
| Desired-table clear `0x8c04a67e` | Clears all 64 desired records; no direct ac command |
| Host-table reset `0x8c04a95c` | Clears all desired records and copies 0x900 bytes to committed state; no direct ac command |
| Game all-handle clear `0x8c04ae3c` | Clears desired table and invokes generation-checked retirement over host handles |
| Generic SDK voice close | Embedded generic SDK paths also contain port close; no observed allocator/free caller establishes these as normal effect paths |

The game's paired masks use `acDigiSetMask` at `0x8c06898a`: ports 0–31 select
the lower word, ports 32–63 select the upper word at bit `port - 32`. Therefore
ports 62/63 select upper mask `0xc0000000`, lower mask zero. Capped picker and
updater paths must keep game-generated masks clear of those bits. Global stops
and sound shutdown are real lease boundaries, not masks to hide indefinitely.

All-port host table clears can still cover 64 records if the updater never
services ports 62/63 and adapter state remains separate. A later generic driver
reset, all-port stop or heap clear cannot be treated that way: stop and invalidate
owned playback before the original operation, then reacquire on a complete new
initialization. No stale driver cursor, heap address or register snapshot may
survive a new generation.

## Sound heap ownership

| Interface / state | Observed contract |
| --- | --- |
| `amHeapAlloc`, `0x8c069c00` | `(out_address, size_bytes, alignment, memory_type, cleanup_callback)`; first four arguments in r4–r7, fifth on caller stack; returns boolean |
| Alignment | 4 or 32 only |
| Memory type 1 | Fixed allocation from heap's high end; internal entry `0x8c06a02c` |
| Memory type 2 | Purgable allocation from heap's low end; internal entry `0x8c06a1fe` |
| Returned address | Absolute sound-RAM/G2 address, not a byte offset |
| `amHeapFree`, `0x8c069e7c` | Fixed allocations must be freed in reverse order; purgable blocks are tracked separately |
| Heap state | `0x8c10e55c`; fixed metadata region begins at offset `0x7e8`, records are 20 bytes |
| Heap upper bound | `0xa09f4000` exclusive; final 0xc000 bytes of 2 MiB sound RAM are excluded by `amHeapGetInfo` |
| `amHeapClear`, `0x8c06a46c` | Bit mask in range 1–3; bit 2 clears purgable blocks, bit 1 fixed blocks; no observed direct caller in this executable |
| Heap shutdown `0x8c069b60` | Clears heap state during `amShutdown` |

The game bank allocation wrapper `0x8c049ece` uses memory type 2, alignment 4
and null cleanup callback. Movie helper `0x8c06d80e` does likewise. Both free
through `amHeapFree`. No observed ordinary game allocation uses the fixed-top
allocator. A candidate can request one fixed, aligned ring with null callback,
hold it for the entire sound-driver generation, and validate its actual metadata.
That reduces interference with ordinary bank/movie frees. It still consumes
real heap capacity and must refuse if the allocation fails. Do not select a
sound address merely because it appears idle.

Do not free the adapter's fixed block while later fixed allocations remain above
it in allocation order. A fixed-heap clear must terminate the lease before the
clear, rather than rely on noticing a reclaimed block after game reuse.

## Existing ac host interfaces

| Interface | Numeric contract |
| --- | --- |
| `acDigiOpen`, `0x8c068b28` | `(port, sound_address, size_bytes, audio_type, sample_rate)`; fifth argument on caller stack; returns boolean; port <64, nonzero address/size, type 0–3 |
| `acDigiPlay`, `0x8c068492` | `(port, start_position, aica_loop_flag)`; game simple caller uses start 0; loop-off 0, loop-on 0xff |
| `acDigiMultiPlay`, `0x8c068a04` | `(aica_loop_flag, upper_mask, lower_mask)`; one 16-byte queued command for both selected ports |
| `acDigiMultiStop`, `0x8c068a96` | `(upper_mask, lower_mask)`; zero mask rejected |
| `acDigiSetVolume`, `0x8c0690d4` | `(port, volume)`; clamps to 15 |
| `acDigiSetPan`, `0x8c069158` | `(port, pan)`; clamps to 31 |
| `acDigiGetCurrentReadPosition`, `0x8c068420` | `(port, out_position)`; checks installed flag `0x8c0af74c`; reads per-port word in driver-published table |
| Host command enqueue, `0x8c083d34` | Queues 16-byte packet into 32-slot sound-RAM ring; boolean success indicates enqueue success, not proven applied playback |

The ac wrappers themselves serialize their command operation using original
interrupt-mask and host-lock behavior. Some G2 wait loops are unbounded. A
retail adapter must not call them from an arbitrary interrupt or the current
masked GD frame and must not represent every wrapper as a time-bounded operation.

Type 0 as PCM16 is a **strong static interpretation**: the game converts the
type-0 byte length to samples by dividing by two; type 1 is unchanged; types 2/3
double the byte length. Stereo movie playback uses two mono ports and distinct
sound-RAM regions. Left/right pan encoding, physical playback phase and exact
end-point interpretation require confirmation in the actual external driver.

The game movie cursor wrapper `0x8c06d42c` forms a 16-bit unsigned delta between
successive `acDigiGetCurrentReadPosition` results. Its fill helper uses two bytes
per unit. This is strong evidence for a modulo-65,536 PCM16 sample cursor. The
host wrapper only exposes an ARM-published word, so publication granularity,
latency, reset marker, ring-end behavior and stereo phase cannot be proven from
this boot file alone. These are required before cursor positions authorize
inactive-ring writes. A single multi-play command supports a common start
request, but is not proof of synchronized physical channel positions.

## Cooperative service and existing clock

The normal game sound service `0x8c04ab7a` calls the updater at `0x8c04ab86`.
Observed callers are normal SH function calls/tail calls, not exception entries:

| Caller | Static context |
| --- | --- |
| `0x8c027488` | Normal main-loop path after optional scene update `0x8c01910c` |
| `0x8c0191d2`, `0x8c0192be` | Two branches of ordinary scene/frame update |
| `0x8c04be04`, `0x8c04be3e` | Ordinary effect-management tail calls |

A checked interposer can call the original service and then a small adapter step
on its own private stack. Static evidence establishes a candidate normal-context
opportunity, not a maximum interval through FMV, loading or waits. Multiple
service calls within a frame are possible; use measured time/position and a
reentrancy gate rather than assume one call per frame.

Timer read `0x8c0b497e` is a no-argument, read-only function returning
`0xffffffff - TMU0_TCNT`. Game initializer `0x8c0b4958` configures TCR0=2,
TCOR0=TCNT0=`0xffffffff`, and starts TMU0; game timer initialization
`0x8c04f2fa` calls it. The game's unsigned-count conversion at `0x8c04f5a2`
multiplies a constant equal to approximately 1.28 microseconds per tick.
The adapter can read that existing clock after validating running state and
configuration. It must detect counter reinitialization and use bounded unsigned
deltas; it must never reinitialize or change the game's TMU configuration.

The ordinary game copy helper `0x8c049e24` copies main RAM into sound RAM and
waits on the existing G2 status between groups of eight 32-bit writes. It does
not replace a bounded adapter copy contract: its status waits are unbounded,
zero/unaligned sizes are outside the observed normal calling convention, and
elapsed transfer time must be measured. Refilling a complete large half in one
service call may still stall FMV. A candidate should stage incremental fills,
serialize SCI/card ownership with game data, and enforce actual deadlines.

A frame-service watchdog cannot stop a looping ring while the entire game or
its interrupts are stalled. It can only notice the missed deadline after control
returns, by which time old audio may already have repeated. Safe continuous
playback therefore requires an enforceable service-gap contract or a verified
sound-side finite/end-event mechanism. The observed basic play flag is only
off/on; it is not an arbitrary repeat count. `acDigiRequestEvent` is present in
the host library, but event semantics or scheduled port chaining are not proven
by its packet wrapper. These are additional specific questions for the exact
external driver, not grounds to invent a queued playback protocol.

## Bounded next implementation and remaining gates

Implement an exact-executable admission validator and a separate retail adapter
that uses real main-RAM and sound-heap allocations. Validate every original patch
site before any candidate mutation. Apply the custom picker/updater restriction
before game voice allocation; leave every ordinary target untouched. Keep own
state outside game tables. Submit GD audio actions to ordinary-context service
rather than perform heap/driver calls from the GD frame.

Before playback is enabled, complete these specific gates:

1. Extract and privately inspect the exact `audio64.drv` selected by this image;
   record its hash and verify PCM16 command, cursor, loop-end, multi-start and
   completion semantics. Calling existing wrappers is possible without copying
   driver code, but wrapper discovery alone is insufficient for safe refill.
2. Admit two excluded ports and one fixed sound-heap lease across all observed
   initialization, global stop, fixed clear, release and reinitialization paths.
3. Verify normal service context/ABI/cache preservation, maximum service gap,
   incremental refill time, cursor progression and card serialization during
   actual movie, menu and loading transitions.
4. On a lost lease, cursor/timer contract or refill deadline, stop only owned
   ports while ownership remains valid and refuse further writes; reacquire only
   after a complete new sound-driver generation. Do not reset AICA or mute the
   game's master mixer to hide failure.

No count of further successful generic transport tests resolves these ownership
and lifecycle gates. The next console run should exercise the checked retail
contracts together with effects and FMV only after those gates are implemented.
