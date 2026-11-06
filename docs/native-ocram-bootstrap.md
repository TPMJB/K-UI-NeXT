# Native OCRAM bootstrap and transition diagnostic

## VC2 post-card-check FN6 diagnostic

The owner's `c5854ecb4722` photo reaches the memory-card check, then stops
inside FN6 for command`26`hex (38decimal). Token`11A`, STREAMING3,
pending1 and error0 identify an active scatter-DMA request. PR`01CF1788`
matches the original driver's NEXT_DMA6 slice; packet`088AFE94` is its
caller SP plus28. WHY8 admits that virtual CPU packet. It does not describe
the transfer destination, which the prior screen did not capture.

This diagnostic preserves the transfer rules and352-byte guarded stack. It
uses96 more bytes of previously unused firmware padding below IP.BIN:
stack`8c007e20..8c007f80`, still128bytes before the owner's bootstrap.
The1600-byte reader region and game heap reservation are unchanged. Only
on a rejected FN6, a telemetry hook borrows the current trace's R6/MAP/WHY
words to record the raw destination, byte count and `100`hex plus rejection
reason. No completed history row or live service field is changed. The
terminal page labels that row **R4 R5 AUX VALUE WHY**: for a marked FN6
rejection these are token, packet pointer, raw destination, raw bytes and
rejection code. Other calls retain R6/MAP in AUX/VALUE. It
replaces the terminal-return row with **TOTAL DONE PIECE SECTOR**: whole
request bytes, already delivered bytes, outstanding piece bytes and sector
size. Cached/uncached token, command and state remain visible. Snapshotting
precedes renderer/cache mutation; the page retains its prior line count.

| REJECT (hex) | Failed condition |
| --- | --- |
| 101 | Token or stream command ownership |
| 102 | Pending/current-piece state |
| 103 | Parameter packet mapping |
| 104 | Empty byte count |
| 105 | Delivered cursor exceeds request limit |
| 106 | Piece exceeds remaining request bytes |
| 107 | Command38 byte count is not a multiple of32 |
| 108 | Legacy command28 piece is not whole sectors |
| 109 | Physical DMA destination alignment, address or protected span |

The exact packet is still unknown until the next console test. Existing
linked NEXT_DMA6 tests execute original instructions but author scatter
entries and accounting; they do not prove real PFN/MDL construction, page
translation, scheduler behavior or application compatibility. Replay the
captured packet against the original slice before changing admission rules.
This is a reader diagnostic; emulated VMU and background integration follow
successful VC2 startup and game reads.

## PIO39 streaming candidate

The owner's `40034a198042` report passes Now Loading, then shows a later
REQUEST for command`27`hex (39decimal), CORE0, and original return PR
`01cf30fe`. Stack-fault and I/O-error fields are zero. The preceding ordinary
DMA17 completed. This is a new rejected API operation; the photo alone does
not certify every DMA38 transfer from the preceding build.

The original WinCE driver uses PIO when virtual scatter entries are even
aligned but not suitable for physical32-byte DMA alignment. Multiple entries
use command39; a single entry uses ordinary PIO16. This profile implements
both virtual CPU-copy paths. It keeps destination addresses in the caller's
active MMU context and clips each synchronous transfer to its requested bytes,
including partial sectors. DMA38 keeps its separate physical-address policy.
PIO16 and DMA17 accept the driver's opaque lookahead word only in this MMU profile;
ordinary native read flags remain checked with translation disabled.

PIO_CHECK function13 offers a bounded piece. PIO_TRANSFER function12 fills it
and marks the registered callback due; it never calls that callback inside
the transfer. The original driver advances its scatter entry only after FN12
returns. On a later EXEC, the assembly gateway clears deferred work, unlocks,
publishes all live reader state and restores the owner frame/SR before invoking
the callback on the owner stack. A32-byte owner-stack shim preserves the outer
GD result, return address, exact SR and GBR through nested GD calls. This
ordering is necessary for the original callback to request its next piece.

PIO completion queues drive SYSINTR20, without DMA SYSINTR21. Registration,
unregistration, initialization/reset, abort and error paths retain coherent
callback state. Ordinary PIO16 retains native CHECK word3=4; the supplied
driver then polls EXEC/CHECK with Sleep5ms until completion. No new per-step
ordinary-read interrupt is required for that actual polling branch.

The private region remains1600bytes; three persistent PIO callback words fit
its existing alignment space. The guarded private stack still has352bytes,
now at`8c007dc0..8c007f20`. The additional544-byte low reservation stays wholly
below IP.BIN at`8c008000`, leaving224bytes before it. The owner's bootstrap,
normal game RAM, all64 map slots and the cold-init/receive-buffer lifetime are
retained. Other profiles keep their previous placement with the PIO flag off.

The exact linked-driver test executes the original command39 packet builder,
registration, PIO pump, post-transfer scatter advancement and deferred callback
through the real assembly gateway. It verifies8192bytes across virtual pieces,
including two-byte/cross-page transfers, recursive GD calls, owner-stack/SR/GBR
restoration, outer result preservation and separate physical canaries. The old
release reproduces the photographed39 rejection as a negative control.
Sanitized actual-reader matrices additionally cover PIO/DMA/legacy transfers,
virtual PIO16, CRC/errors/cancellation/quarantine and restored SCI ownership.

These checks do not execute the full CE scheduler, real page tables or complete
VC2 application. Translations, SetKMode, event consumption and SD arrival remain
explicit model boundaries. Hardware startup/gameplay, CDDA and VMU/background
integration still need their own validation.

## Previous command38 scatter-DMA candidate (40034a198042)

The owner's `83db89672c43` screen reaches WinCE, then deliberately stops on
REQUEST function0, command`26`hex (38decimal). CORE0 is a rejected request;
ASM`FFFFFFFE` is the in-flight sentinel, not return-2. FAULT and I/O error
are zero. The supplied driver's command38 return address`01cf2dc2` matches
the photographed PR exactly. Its preceding contiguous DMA17 request completed.

This candidate enables `KUI_NATIVE_GD_DMA_STREAM` on the native foreground
reader. Command38 receives `{FAD,sectors,opaque lookahead FAD}` from virtual
CPU memory, retains one token, and accepts physical DMA pieces aligned to
32bytes. Pieces can begin/end within sectors. The existing SCI cursor rereads
each piece's bounding sectors, clips only its requested bytes, and finishes
that piece inside FN6. SCI ownership is released between pieces. CHECK keeps
STREAMING3 until the whole request completes; FN7 reports the current piece's
remaining bytes. Legacy command28 and ordinary reads retain their contracts.

The actual driver waits indefinitely on its DMA thread's completion event.
After real data delivery, the bridge queues SYSINTR21 in the CE kernel's
pending mask/ring and sets its reschedule byte. Whole-request completion also
queues SYSINTR20. Pending bits are deduplicated and unrelated bits preserved.
The live VBR's IRQ entry is validated against all36 original instruction
contracts; three checked P1 literal pointers supply the kernel bookkeeping
addresses. No game-specific kernel addresses are embedded. Binding or queue
refusal is terminal and reports mapper WHY`B` before proceeding unsafely.

The low entry remains`8c004000`, all64 manifest slots remain, and the guarded
352-byte stack remains`8c007ba0..8c007d00`. The1600-byte private region's first
two receive buffers initially hold cold initialization instructions. `_start`
clears only the following BSS; actual adoption returns before SCI overwrites
that retired prefix. The complete live reader region, including those reused
buffers, is published after every completed gateway call. Only copied launch
metadata is reused for the148-byte trace, three kernel pointers, five existing
gateway words and the terminal row. Geometry and slots remain immutable.

Current tests execute original driver request/FN6/FN7 slices against the linked
resident, compare five physical pieces totaling8192bytes, verify per-piece
DMA21 and final drive20 queue writes, and reproduce the previous release's
rejection as a negative control. A separate linked test executes `_start` and
the real initializer before the first SCI read; it checks the cold prefix is
not overwritten while executing and is never executed after retirement.
The sanitized actual-reader matrix covers272 byte-exact requests,21 new
streams/44pieces/32 partial-sector pieces, cancellation, CRC errors, refusal,
quarantine and SCI restoration. Renderer output matches the previous complete
framebuffers byte-for-byte. Models bind the exact final ELF and source hashes.

The CE scheduler, real TLB handler, full kernel boot, application, CDDA and
VMU/background integration are not executed by these checks. Event consumption
and SD arrival are named model boundaries. Command39/virtual PIO streaming and
nonzero completion callbacks remain unsupported. This is the next concrete
VC2 compatibility increment; complete gameplay still needs hardware testing.

## Previous MMU transition (83db89672c43)

The owner's `b5bf7f5a24fb` test passes the previous stop and remains at VC2's
Now Loading screen with no SD activity. This does not identify its exact PC.
Executing the supplied kernel prefix through all four ASIC-ID branches shows
no GD call or write below `0c008000` before MMU activation. It sets VBR
`8c0120f0` and MMUCR `5`; this is a software boundary, not proof of the console's
freeze location.

This candidate enables `KUI_NATIVE_GD_MMU_BUFFERS`. The GD entry saves and
restores the caller stack under the caller's own SR and masks only interrupt
priority while on the resident stack. It does not newly set SR.BL, allowing
the kernel's translation-miss handler to run. Linked fault-injection checks
cover page misses during saves, return pops, busy/guard paths and exact SR
restoration, with the previous entry as a negative control.

The live entry MMUCR.AT snapshot supplies the CPU-call context. With AT clear,
native aliases and OCRAM behavior are retained. With AT set, CPU parameter and
status pointers in U0/P3 keep their virtual address, including `0c...` process
slots. Physical DMA destinations always use a separate checked main-RAM path.
Virtual PIO/read-payload destinations remain refused: the asynchronous writer
must not normalize a virtual address into unrelated physical memory. Virtual
range/payload refusals receive explicit reasons on the terminal screen.

This implements the post-MMU stack and CPU-buffer contract. It does not add
CE interrupts, callback installation or stream-38/39 support. The actual
hardware stop has not been attributed to a particular later command.

This forward test keeps the accepted native foreground SCI reader and adds
checked CPU parameter/result access to SH-4 operand-cache RAM (OCRAM). The
previous hardware return screen did not show the actual assembly return, the
buffer address, live memory mode or early lock refusals. This build captures
those fields before the C handler, and publishes mutable reader BSS after each
completed gateway call, after releasing the entry lock.

## Hardware result and idle CHECK compatibility

The owner's `564f12e755c3` capture shows the first OCRAM CHECK returned 2,
followed by a successful DRIVE call. The later stop is a CHECK with handle 0,
buffer `7e000f00`, mapper reason 1 and actual/core result -1. Both lock values
and the fault flag are zero. Cached and uncached token/command/status agree:
token `1e`, no owned command, completed status 2. This distinguishes the later
idle probe from the earlier initialization CHECK failure.

The original embedded MAINANA routine at `8ce0946a` deliberately checks handle
0 into a 16-byte OCRAM stack buffer and accepts only result 0. Its caller's
failure path returns to the BIOS at `8ce09720`, matching the captured RETPR.
The new compatibility rule is deliberately bounded: a valid CHECK output
buffer with handle 0 and no owned command receives four zero words and result
NOT_FOUND (0). Handle 0 still fails while a command is owned; mismatched nonzero
handles still fail, and neither can consume the active command. This rule is
based on the actual client and hardware state, not a claim that an emulator
proves every firmware version's zero-handle behavior.

The bounded linked replay compares the previous reader and the new reader
with the same authored idle state and executes the original MAINANA branch.
It establishes progress beyond this specific return path, not complete
metadata parsing, kernel loading, CE startup or gameplay.

## Legacy DMA stream bootstrap

The OCRAM foreground profile enables `KUI_NATIVE_GD_LEGACY_STREAM` for command
28, the older two-word `{FAD,sectors}` DMA stream request. This is distinct from
an ordinary read: no destination exists until function 6 supplies a piece as
`{destination,bytes}`. Function 7 reports that piece's remaining bytes and busy
state. One command token owns the stream across every piece; CHECK reports
STREAMING (3), including the idle interval between pieces. ABORT closes the
owned stream with a successful terminal status, which CHECK acknowledges once.

This implementation accepts finite, validated sector ranges and whole-sector
pieces into checked, 32-byte-aligned main RAM. Invalid handles, malformed
packets, overlapping pieces, excess lengths, OCRAM read destinations and
out-of-range spans are refused. The special `0x1ff` until-disc-end mode is
outside this bootstrap increment. Existing ordinary native reads and the
separate CE stream-38 implementation retain their behavior.

The adapter starts the existing native cursor only after function 6 accepts a
piece. Normal SCI delivery reports progress for that piece, then closes and
releases the bus before the next piece. No extra data buffer, heap lease,
interrupt callback or GD service field is added. Prior-piece bytes are tracked
in the stream-owned outputs slot; CHECK reports the cumulative byte count.
I/O failures remain failures and cannot become successful through ABORT.

The supplied VC2 bootstrap requests 899 sectors and transfers a 2 KiB header to
`8ced4000`, followed by `1c1000` bytes to physical `0c010000`. Verification must
compare both complete outputs with the supplied `0WINCEOS.BIN` and execute the
original MAINANA sequence through its jump to `0c010000`. A fake successful GD
response alone does not establish that the reader delivered those bytes.

To fit this protocol inside the existing protected reservation, this profile
omits its unpublished IRQ entry and unused raw-vector holes. Its private
region was1632bytes before the new scatter profile compaction, with the same two aligned SCI receive buffers and typed
engine/service state. Cross-target layout assertions pin all offsets; no
external SDK code addresses this region. The terminal formatter uses indexed
rows with identical captured fields and pixels. Unshown legacy service
counters occupy one permanently-zero union; unshown GD counters are omitted
from the MMU profile. Displayed assembly history, core return and I/O error are
retained. The 148-byte trace reuses only private copied launch-string metadata
at manifest offset 68 and is cleared after adoption. All geometry fields and
64 map slots remain intact. The Games menu uses its own title metadata.

## Install and test

Install the module as `KUI/apps/games/retail-physical.kui`. Keep the current
Games/VMU menu. Use Virtual VMU Off, Background Off / Standard, CD music Off,
RAM patches Off, and Native / Default boot. Open Sega Smash Pack, then Virtua
Cop 2. Photograph the entire screen if RET or GD STOP appears. Report whether
Smash Pack opened, whether selecting VC2 changed the display, and whether a
black screen kept video sync, rebooted, or remained stopped. If VC2 continues,
report the furthest visible stage. A photo is the intended trace export.

## What changes

CPU OCRAM admission requires translation off, operand-cache RAM enabled,
normal operand-cache indexing, word alignment and a span contained in one
4 KiB half. The pointer remains a raw CPU address. OCRAM is refused as a
read-payload destination before MMIO: the native asynchronous writer owns
main-RAM destinations. Native RAM aliases and GD command statuses are retained.

The assembly entry records arguments and memory mode even when a lock or
stack-guard check refuses a call before C. It records the actual returned R0
on every return path. The terminal screen captures cached and uncached service
views before rendering changes cache occupancy. Four completed calls appear
oldest first; the last-call snapshot can also show an unfinished call.

Each completed gateway call purges the aligned BSS cache-line span after its
final lock update. This publishes reader state if the next executable discards
dirty operand-cache tags. It uses no additional private-stack space. A bounded
cache model exercises an authored dirty-tag discard; this boundary is
synthetic. SH7750 non-associative F400 tag writes first write back dirty valid
lines, according to Renesas manual section 4.5.3. The retail F400 loop must
not be described as an unconditional dirty-data discard. No cache-loss root
cause is established. Reference:
https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware

Unpublished SDK callback exports are omitted only in this foreground profile
to make room. The SCI engine policy, native E0 function-2 leaf and 352-byte
private-stack geometry are retained. No GUI runtime is replaced.

## Screen key

All values are hexadecimal. `FFFFFFFF` is -1. `FFFFFFFE` is an entry sentinel:
the latest gateway call has not recorded its assembly return. Initial unused
history rows are zero. `CORE` may belong to an earlier C call when assembly
refuses entry; `ASM` is the actual latest gateway result or the sentinel.

| Row | Meaning |
| --- | --- |
| HIST FN R4 R5 RET CCR | Four completed calls, oldest first: function, R4, R5, actual returned R0 and entry CCR. |
| SEQ FN SRC ASM CORE | Entry counter, function, gateway source, assembly return, last C-core result. |
| R4 R5 AUX VALUE WHY | Latest arguments, mapper result and reason code. For CHECK, R4 is the handle and R5 the result-buffer address. For marked FN6 failures AUX/VALUE are raw destination/bytes. |
| PR SP SR VBR MMU | Latest caller return address, caller stack, caller SR, VBR and live MMUCR at entry. |
| CCR CB LOCKW LOCKN FAULT | Live entry CCR, most recent entry CCR with copy-back enabled, lock before that call, lock at terminal capture, stack-guard fault. |
| TOKEN CMD STAT PEND ERR | Cached service token, command, status, pending flag and error. |
| TOTAL DONE PIECE SECTOR | Request bytes, delivered bytes, outstanding DMA piece bytes and sector size, captured before rendering. |
| P2 TOKEN CMD STAT LOCK | Uncached aliases of token, command, status and entry lock captured before rendering. |

`SRC`: 0 = BC gateway; 1 = C0; 2 = direct 1000; 3 = direct 10F0.
`FN`: 0 = request; 1 = check; 2 = execute; 3 = initialize; 4 = drive status.
`CB` is sticky observation, not a count; zero means no entry has yet observed
CCR's copy-back bit set. `LOCKW` is the entry snapshot; `LOCKN` is terminal state.
For a busy entry, CHECK returns 4 without calling C. Other busy results are
request 0, execute -1, initialize -1. These differ from a C rejection.

| WHY | Meaning |
| --- | --- |
| 0 | Mapper not called during the latest entry; includes early assembly refusal or a guest precheck. |
| 1 | CPU OCRAM pointer admitted. |
| 2 | OCRAM refused for read-payload validation. |
| 3 | OCRAM refused because MMUCR.AT enables translation. |
| 4 | OCRAM refused because CCR.ORA/OIX mode is unsuitable. |
| 5 | OCRAM alignment or one-half span check failed. |
| 6 | Main-RAM pointer admitted. |
| 7 | Main-RAM range or protected-range check failed. |
| 8 | CPU virtual parameter/result pointer admitted with MMU enabled. |
| 9 | CPU virtual span/range refused. |
| A | CPU virtual read-payload destination refused. |
| B | Live CE interrupt/event binding or queue refused. |
| C | CPU virtual PIO payload admitted by the active PIO profile. |

The last map operation of the latest call is shown; this is not a separate
history of every mapping. If the screen says GD STOP while a call is still in
C, the entry is current and its assembly result remains `FFFFFFFE`. After
drawing all four history rows, the terminal formatter reuses the first row's
storage as scratch. The on-screen history is preserved; this terminal path
does not resume gameplay or export an intact in-memory ring.

## Validation and limits

Build with `make -f Makefile.native-ocram-bootstrap`. Run `ocram-host-tests`
and `ocram-diagnostic-tests` with that Makefile. The stream protocol and actual
SCI reader are checked by `ocram-legacy-core-tests` and
`ocram-legacy-reader-tests`. `ocram-mmu-buffer-tests` checks virtual CPU versus
physical DMA semantics and AT0 controls. `ocram-dma-stream-tests` checks the
new command38 core and legacy regression under its enabled flag.
`ocram-pio-stream-tests` checks the PIO39 callback state, virtual CPU buffers,
ordinary PIO16 hints and DMA/legacy regressions with PIO enabled. The actual
reader and renderer targets include the enabled PIO profile.
`tools/run_native_ce_dma_events.py --kernel OWNER_0WINCEOS.BIN --output OUT`
checks actual-kernel binding and queue mutations. Check final artifacts with
`tools/check_native_standalone_foreground.py --require-ocram --require-clean`.
The renderer tests use the actual font and framebuffer routines against
guarded host mappings, with authored policy-refusal, stale-token and early-busy
fixtures. The formatter runs under ASan/UBSan; the fixed-address renderer uses
UBSan. Previews are labeled FIXTURE and are not console screenshots.

Separate linked checks execute actual SH gateway, native GD dispatch and
mapper instructions with authored registers, memory and cache behavior. The
original embedded MAINANA CHECK branch reaches DRIVE for an admitted buffer.
The entry checks cover normal, busy and guard returns, real R0, sentinel,
history, exact BSS publication span and stack/register preservation. Reports
bind the tested ELF and source hashes. Original retail input bytes are not
included; the linked MAINANA check requires the owner's external SAMPLE.BIN.

The low private stack remains 352 bytes. The checked C worst path uses at most 272
bytes (the current measured release uses260); guards reserve16 bytes at each end and the aggregate reserve includes
48 bytes for compiler-library work. The pinned linked division helper uses
4 bytes of that reserve. No initialized mutable section exists outside BSS.

Actual hardware eviction, SD/SCI timing, complete CE startup and gameplay are
not proved by these tests. CDDA and VMU/background integration remain
subsequent work. This
is a forward bootstrap and diagnostic build, not a claim of complete VC2 or
universal compatibility.
