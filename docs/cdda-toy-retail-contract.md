# Toy Commander retail CDDA contract candidate

Status: verified executable facts and an implementation plan, **not an admitted
retail profile or an audio-playing build**. Prepared 2026-10-07. No patches below
are enabled. The ordinary 1.8.5 reader and its memory reservation remain unchanged.

The supplied boot file matches the preflight: `1GUTH.BIN`, 748,444 bytes,
CRC32 `cdc493b3`, SHA-256
`ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd`.
Addresses below apply only to that executable loaded at `0x8c010000`.
The original executable, its disassembly and external game drivers are private
inputs, not repository contents or distributed loader payloads. The planned adapter
will call the game's existing interfaces, without reproducing their implementation.
The detailed [memory evidence](evidence/cdda-toy-memory-static-2026-10-07.md) and
[sound evidence](evidence/cdda-toy-sound-static-2026-10-07.md) distinguish verified
static facts from the remaining driver and hardware obligations.

## Main RAM: make a real first allocation

Toy initializes its ordinary heap over `[0x8c124340,0x8d000000)`, after clearing
startup BSS through `0x8c124320`. Its default allocator uses 32-byte units and
carves allocations from the high end. Raw allocation at `0x8c0b3ede` returns the
body; its 32-byte header precedes that body. A successful allocation remains
unavailable to subsequent ordinary heap allocations until it is freed.

The early init call at `0x8c04e920` obtains its `syMallocInit` target
`0x8c0b3e10` from the unique observed literal at `0x8c04e9b4`; its return point
is `0x8c04e924`. This is earlier than the return from the larger system-init
wrapper, which also performs game filesystem allocations. Interposing the
literal offers a concrete bootstrap point: call original heap init, make one
checked allocation, then copy the authored helper into that allocation before
allowing ordinary startup allocations to resume.

For illustration, a first raw allocation of `0x30000` bytes returns
`0x8cfd0000`, with its header at `0x8cfcffe0`. A fixed-linked helper could use
only its lower `[0x8cfd0000,0x8cfe0000)` portion, provided the measured code,
state and private stack all fit. This is a sizing example, not a selected
release layout. Admission must check the actual allocator result and all
segment bounds before copying or calling code; a different result must refuse
the fixed-linked profile, rather than use an unallocated address.

The executable also contains direct SDK references to `0x8cfe0000` and
`0x8cff0000`. Therefore neither a heap-end reduction nor an allocation permits
placing helper bytes there. Any upper padding in a first allocation must remain
outside helper code, state, stacks and buffers. The presence of an initially
zero descriptor on one such path does not prove that path can never run later.
The remaining audit must cover fixed-address users, heap reinitialization and
the helper lease's lifetime.

The temporary loader stage begins at `0x8ce00000` and its binary ends below
`0x8cf00000`. Its bytes may be the early copy source only while they remain
untouched. No pointer into that temporary stage may survive installation. The
bootstrap must preserve the original init return value, arguments, integer ABI
and exact interrupt state, publish copied code through coherent caches, and
retain a checked private stack within the allocated helper body.

## Sound: use the game driver and reserve the game's custom ports

Toy loads external `audio64.drv` and has host-side ac/am sound interfaces in
the executable. The ARM driver itself is not in this boot file. The detached
test's AICA reset, global mixer writes, all-channel clearing and TMU1 setup are
not usable in the retail adapter.

Toy's own 64-port allocator/updater bypasses `amVoiceAllocate`. Allocating two
voices through that generic SDK API would therefore not reserve them against
the game. A fingerprinted candidate can restrict the custom game code to ports
0–61 while leaving the SDK's 64-port capability intact:

| Site | Candidate restriction |
| --- | --- |
| Picker `0x8c04a868`, bound at `0x8c04a8ac` | Scan 62 ports instead of 64 |
| Updater `0x8c04a270`, work counter at `0x8c04a29a` | Candidate counter reduction 64 to 62; predecrement permits at most 61 processed steps |
| Updater wrap at `0x8c04a5d2` | Wrap after port 61 instead of port 63 |

The picker bound and index wrap perform the port exclusion; the work counter
limits processing effort, rather than selecting ports, and must be evaluated
separately to preserve effects behavior. These sites are identified, but
reservation is not yet admitted: startup,
shutdown, global stops, masks, other direct port users and sound reinitialization
must also respect the exclusion. Movie helpers reach the same custom picker;
that fact alone does not establish every movie/reset path.

The host interface supports opening mono ports with
`acDigiOpen` at `0x8c068b28`, querying a per-port read position with
`acDigiGetCurrentReadPosition`, and starting both through a single
`acDigiMultiPlay` command at `0x8c068a04`. The latter takes loop flag, upper
32-port mask and lower 32-port mask; ports 62/63 select upper mask
`0xc0000000`, lower mask zero. Loop-on is `0xff`; loop-off is zero. These are
interface facts. PCM16 type zero is strongly inferred from the game's byte/sample
conversions, but driver confirmation remains open, as do played-position units
and synchronized hardware phase. Driver/cursor semantics still need review before using them to authorize
inactive-half writes.

Allocate the stereo ring through the game's actual sound heap after successful
sound initialization. `amHeapAlloc` at `0x8c069c00` is a concrete allocation
interface; its exact heap pointer, arguments, returned sound-address convention
and reset/free lifetime must be enforced by the adapter. An idle sound-RAM
range or idle ports do not substitute for that allocation and exclusion.
Stop, pause and failure handling must affect only the two reserved ports; never
mute the master mixer, reset the ARM or restore stale game register settings.
Both game initialization and shutdown issue an all-port stop. The original
`amShutdown` path also closes ports, resets the driver and destroys the sound
heap. The CDDA lease must end before that original shutdown runs, and a new
sound initialization requires fresh ring allocation and port ownership. Clearing
the game's host tables alone is not equivalent to stopping the ARM ports.

## Service: a normal game call, one shared card owner

The game's service function `0x8c04ab7a` calls the custom port updater
`0x8c04a270`; the normal main loop calls that service after its scene update.
Other ordinary calls and tail calls also reach it. A checked interposer on this
path can call the original game service and then run a bounded helper step in
ordinary game context. This avoids introducing a new interrupt vector or timer
owner. Static call sites do not prove the maximum gap during movies, loading
or other stalls; that gap remains a measured admission condition.

The existing game timer read at `0x8c0b497e` returns the inverted TMU0 down
counter without writing the timer. The game's conversion uses 1.28 microseconds
per tick, and its initializer selects `TCR0=2`. The adapter must verify running
state and configuration, use bounded unsigned deltas, detect resets or stopped
progress, and stop owned playback on a lost timing contract. It must not
initialize or reconfigure TMU0 or borrow the standalone test's TMU1 setup.

The current GD entry masks interrupts and uses a nearly full private stack.
Do not invoke the game sound allocator/command interface from that masked
GD frame or an arbitrary interrupt. Instead, GD PLAY/PAUSE/STOP submit small
audio actions that the ordinary-context helper applies. Preserve the GD
handle's completion/error behavior and report position from samples actually
played, independently of the raw-audio prefetch cursor.

Game data and audio must share one SCI/card/receive-buffer owner. A data step
may yield to audio only after a complete CRC-checked block and stream stop;
audio STOP/PAUSE must not cancel the unrelated game GD request. The helper
needs its own raw-audio extent cursor, frame carry and incremental inactive-half
fill state. A 64 KiB stereo ring has a 185.8 ms half window. Refilling an entire
half in one long game call would undermine video timing; use bounded chunks
and check played positions and time before publishing.

**A frame callback alone cannot prevent stale audio during a whole-game stall.**
With a looping ring, a late callback can detect the missed deadline and stop
its ports only after stale samples may already have replayed. Guards and time
checks on return do not provide an earlier stop. Before enabling looping audio,
the adapter therefore needs an enforceable maximum service gap or an independent
driver/hardware service or stop mechanism that remains effective through the
relevant game stall. Merely measuring a small gap in a passing run is not that
guarantee. Driver inspection may establish finite-block playback or a suitable
sound-side event/watchdog contract; no such mechanism is currently proven.

The paced observation currently ends at `0x8c007660`, below its `0x8c007800`
cap, leaving 416 bytes; its audited stack uses 1,160 of 1,232 available bytes.
The bootstrap, action bridge and extent callbacks require a separate measured
candidate build, with candidate-only diagnostic reduction if necessary. The
bulk helper and its private stack belong in the real game allocation. No
ordinary target may gain the candidate's patch logic or altered sound behavior.

## Immediate implementation gate

Implement a strict exact-image patch/lease validator and separate bootstrap,
then a retail adapter around the proven game allocator and sound interfaces.
Enable music only after the remaining sound reset/mask, cursor, memory lifetime
and enforceable service-or-stop contracts are resolved. Validation must cover refusal before
partial installation, exact first-allocation bounds, cache/ABI preservation,
serialized card transitions and loss of sound/timer ownership. The first
console candidate should test these specific integration contracts together
with actual music, effects and loading, rather than repeat the passing generic
transport or standalone audio tests.
