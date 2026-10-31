# Round-toward-zero ("zr") SDK titles

Crazy Taxi, Sword of the Berserk and Zombie Revenge were linked with Shinobi
1.62zr (`pd Ver 1.50zr`, `syHw Ver 1.07zr`). Every structural gate refused them
before any game-specific code ran.

## What "zr" changes

The SDK interrupt handlers load FPSCR with `0x40001` (round toward zero)
instead of `0x40000`. Each of the four FPSCR set-ups gains one `ADD #1,R0`, so:

- the integer and FPU common handlers grow by 4 bytes each (0x16c/0x1a0);
- the event word moves from vector+0x398 to +0x3a0;
- their three event-literal loads move to handler offsets 0x94, 0x140 and 0x172.

An instruction-by-instruction comparison against the full family showed no
other difference. Every opcode, branch target and literal role is the same;
only the event address and PC-relative displacements move. The vector prefix,
vector, default handler, table initializer and helper windows match the
existing fingerprints exactly, in all three games. The loader's SCI callbacks
are integer code built with the FPU registers reserved, so the rounding mode
does not affect them.

## Source changes

- `native_sdk_sci_family.c` / `native_sdk_sci_route.c`: a third layout beside
  the full and compact families, with its own two handler fingerprints
  (`zero_normal`, `zero_fpu`). All other roles keep the full family's checks.
- `vmu_native_layout.c`: two final-fallback roots, tried only when every
  reviewed root, leaf, tail and R1 root has failed:
  - a variable root with R3 target, R2 count and mode zero, directly after a
    `JSR`/`NOP` (Berserk);
  - the uniform root directly after a `JSR` whose delay slot only loads an
    immediate into R5, which the root's own RX literal then replaces
    (Crazy Taxi).
  Every image that resolved before keeps exactly the same record (all ten
  existing `.nvr` records are byte-identical).
- `vmu_native_heap.c`: the bounded argument evaluator also accepts
  `MOV.W @(disp,PC)` literals and reloads of a stack slot spilled earlier in
  the same window. R15 cannot change in the window and no call precedes the
  target, so a reload only returns the known value stored there.

## Status per title

| Title | SCI family (New reader) | Maple layout | Heap | Combined VMU route |
|---|---|---|---|---|
| Sword of the Berserk | admitted | variable, zero-mode root | call proved | all structural gates pass, but the game saves only to A1 (see below) |
| Crazy Taxi | admitted | uniform, R5 continuation | no initializer slot | needs a fixed arena (RAM map), as Crazy Taxi 2 did |
| Zombie Revenge | admitted | variable root with 6 inputs (TX bank 6 KiB) | no SDK allocator found | needs a 6-input layout and its own heap route |

Console test: Berserk asked for a memory card in A1 and showed none in A2. Its
save code calls the backup library's drive-status check and its file check for
`BERSERK_DATA` with drive 0 (A1) loaded as an immediate. Both library entries
validate that argument as a drive number and index per-drive tables with it.
The combined module's virtual card is A2 and A1 stays physical, so Berserk
needs a virtual A1 to save.

With the SCI family admitted, the launcher's New route selects the standalone
New module for Crazy Taxi and Zombie Revenge (no virtual VMU) once the combined
module refuses them.

## Verification

- SDK SCI discovery with the Crazy Taxi executable as fixture: 815,966 checks,
  including byte/bit mutations of every window, cross-family body swaps in both
  directions (zr/full, zr/compact) and live-model admission of all 21 library
  startups (the 15 earlier-family admissions are unchanged).
- Corpus audit: the same ten titles pass and Berserk is added; Crazy Taxi now
  stops at the heap slot and Zombie Revenge at its layout.
- Bottom-reservation execution of Berserk's original allocator passes.
- Compact Background build: worker, both residents and the read-ahead
  auxiliary are byte-identical. In the stage, only the six edited discovery
  functions changed instructions. All 204 other changed bodies differ only by
  relocated symbol or string addresses, checked word by word. The 35
  compact-profile pins were updated from that review.
- The complete compact audit passes with only the private-corpus bottom-heap
  section skipped. That section needs the eight reviewed originals, including
  Grandia II and Lodoss, so it must run where those files are.

## Delivery steps

1. Rebuild the compact Background module, the standalone New module and the
   GUI from this commit.
2. Run `tools/audit_vmu_native_corpus.py` and
   `tools/prove_vmu_native_bottom_corpus_heap.py` with the eight reviewed
   originals. Then run `tools/check_native_background_compact.py` on the build.
3. Console checks:
   - Berserk on Background (New) with Virtual VMU On: boot, play, save,
     power-cycle and reload.
   - Crazy Taxi and Zombie Revenge on Background (New): boot and play through
     loading-heavy sections.

# Early Shinobi (1.41-1.43) titles

Sonic Adventure, The House of the Dead 2, Time Stalkers, Elemental Gimmick Gear
and Silver share one earlier interrupt family. Every byte that matters is
identical across all five:

- Two 0x34-byte stubs. The EXPEVT one is copied to VBR+0x100 and +0x400, the
  INTEVT one to VBR+0x600. Each records its event code in one word and jumps to
  one common handler (0x14c bytes) that saves the FPU in round-toward-zero.
- The handler table is the full family's: VBR+0x200+(code-0x200)/8, filled by
  the same reviewed initializer, with SCI ERI/RXI at VBR+0x25c/+0x260.
- The default handler has no debug helper. It unwinds the common frame and
  takes the same system return path (0xac000010) the full family's default takes
  after its helper. Forwarding a foreign SCI event therefore behaves as the
  original SDK does.

`early_family()` in `native_sdk_sci_family.inc` admits it only when no
full/compact vector exists. It requires a unique INTEVT stub, its adjacent
EXPEVT stub, the common handler and the default, each matching its reviewed
fingerprint. Their event/common/default literal relations must agree, and the
table initializer must be unique. The live route additionally checks the copied
INTEVT stub (only its R0 scratch may differ), the live common and default code,
and both still-default SCI slots. The standalone New resident's late bind
compares the 13-word early stub (scratch 11, common 12) instead of the 24-word
full vector.

Blue Stinger (Shinobi 1.28) has the same stubs and handler but another table
initializer, so it is still refused. Silver also fails its executable-extent
proof before the launcher's New fallback is considered.

With this, the New route can use the standalone reader for Sonic Adventure,
House of the Dead 2, Time Stalkers and Elemental Gimmick Gear (no virtual VMU
yet).

## Virtual VMU route for the early titles (not yet admitted)

All five early titles share one Maple driver build (pd 1.46). Its submit,
resume, consumer, gate and ready code already passes the reviewed driver
discovery. The rest differs from the pd 1.50 builds in these ways, identical
across all five:

- Constructor: the loop that fills the 48 receive slots stores the upper 24
  through a second literal (manager+0x1048, which is 0xfe8+96) instead of adding
  96. The geometry is the same: slot k is the RX base plus k KiB. The rest of
  the function matches the pd 1.50 code shifted by 4 bytes.
- Initializer wrapper: it never converts the lock pointer to its uncached P2
  alias. The lock cell keeps its cached P1 value (the cell's own address + 4),
  while the resident and the runtime proof currently require the P2 form.
- Maple server: it does not save and mask SR. It sets a busy flag on entry and
  clears it on exit instead. Its entry is 0x3a bytes before the completion call
  (0x4a in pd 1.50), so the gun-flag wrapper, which has the reviewed shape,
  branches there. The API functions that call the server directly when a
  manager flag is set do not mask interrupts either. The server, and with it
  the completion hook, then runs at the caller's interrupt level, while the
  resident accepts only levels 6 and 15 and refuses others.

### Driver support (done)

Discovery now tells the two drivers apart. `KUI_VMU_NATIVE_FAMILY_EARLY_V1` is
chosen only when the exact pd 1.46 server entry (completion_call-0x3a) and its
lock helper are present: the helper returns -3 or -1 unless the driver is idle
and Maple DMA has stopped, then takes the lock with `tas.b` (-2 if it was
held). Both are byte-identical in all five titles. Every other image keeps
`UNIFORM_V1`, and the corpus probe output of the 16 other recognized titles is
unchanged.

- Layout: the early constructor (its loop reaches the upper 24 slots through
  manager+0x1048), the 44-byte wrapper without the P2 lock store, and the
  gun-flag wrapper calling completion_call-0x3a. The layout now resolves in all
  five titles; the SH transport model runs each real initializer path and
  finds the same 48 slots.
- Runtime: the early lock cell keeps the image's own cached pointer
  (`kui_vmu_native_lock_pointer`), U0 in The House of the Dead 2 and P1 in Sonic
  Adventure. The early server runs at its caller's interrupt level: an
  interrupt-time run that finds the lock held sets manager+0x10e8, and the
  next API call runs the server inline. The runtime proof and the resident
  therefore accept any inherited IMASK for this family and rely on the lock
  byte, which they already require to be held. The hook gateway still raises
  IMASK to 15 for the worker's own work.
- The SDK coherence gate (it retires stale P1 lines of pd 1.50's P2 lock
  alias) is neither prepared nor installed for the early family.

### Remaining

- Lease. The House of the Dead 2 does not link syMalloc at all: it manages
  its own memory, so its lease needs a fixed arena outside any allocator, a new
  lease mode. Sonic Adventure, Time Stalkers, Elemental Gimmick Gear and Silver
  link syMalloc 1.35, which the heap model does not know yet.
- Exits. The early SDK has two adjacent BIOS exits: an 8-byte one (r4=1) and a
  cleanup one (r4=2) right after it, both through the literal 0x8c0000e0. An
  8-byte joint head at the first, loading its target from the second's
  standard veneer, covers both. Sonic Adventure also has an executable-loader
  consumer of 0x8c008300 that needs its own proof.
- Silver's program extent, which depends on the allocator proof.

### RAM maps (compatibility test 3)

The RAM-map standalone reader filled every free 128 KiB page from 0x8c000000
with a pattern before the game started. On A+B+X+Y+Start it listed the pages
that still held it: bit i of word w is the page at 0x8c000000+(32w+i)*0x20000.
Pages 112-113 (the stage, 0x8ce00000-0x8ce40000) and 127 are never marked.

| Title | Words | Untouched |
|---|---|---|
| Sonic Adventure (two sessions, the same) | 00000000 00000000 80000000 02000010 | single pages only: 0x8cbe0000, 0x8cc80000, 0x8cf20000 |
| The House of the Dead 2, session 1 | 00000000 00000000 00000000 681CFFE0 | 0x8cca0000-0x8ce00000 (1.375 MiB), 0x8ce40000-0x8cea0000, 0x8cf60000-0x8cf80000, 0x8cfa0000-0x8cfe0000 |
| The House of the Dead 2, session 2 | 00000000 00000000 FFFF0000 681CFFFF | 0x8ca00000-0x8ce00000 (4 MiB) and the same three upper pieces |
| Time Stalkers (two sessions) | all zero | none |

- The House of the Dead 2 left 0x8cca0000-0x8ce00000 untouched in both
  sessions, room for the 284 KiB module as a fixed arena. Session 1 used 2.625
  MiB more than session 2, so a long session should confirm the range first.
- Sonic Adventure has no two adjacent untouched pages, so the 284 KiB module
  cannot sit in a fixed arena there.
- Time Stalkers wrote every page, probably clearing RAM at boot. Marking RAM
  before the game starts cannot tell cleared pages from used ones; marking
  after the game's own startup could.
