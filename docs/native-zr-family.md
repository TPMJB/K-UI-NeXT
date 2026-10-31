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
yet). On console all four play normally on it: the first three since
compatibility test 2, and Elemental Gimmick Gear since a fresh rip in test 4b.
Its earlier "Invalid or unsupported data sector" came from a damaged copy of
track03.bin on the card, where the executable occupies the track's last 486
sectors.

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

### Exits (done for The House of the Dead 2 and Time Stalkers)

The early SDK (syBt 1.04) has two adjacent BIOS exits, byte-identical in all
five titles: an 8-byte one that jumps through the vector cell 0x8c0000e0 with
R4=1, and right after it a cleanup exit that calls the SDK cleanup routine and
then jumps through the same cell with R4=2. They share one literal pool.

- Discovery: `KUI_VMU_NATIVE_EXIT_EARLY_PAIR` (family 7, two entries, the
  second exactly 8 bytes after the first). Every opcode, the vector literal
  and the linker fill are exact; the cleanup literal must be an even address
  inside the image. The pair must be the image's only firmware consumer: its
  own two loads of the vector cell in any alias, and no load of the boot
  entry 0x8c008300. No branch or literal may enter either window's interior.
- Patch: the first entry gets an 8-byte head, `mov.l @(3,pc),r0; jmp @r0;
  nop; nop`, which loads its target from the second entry's ordinary 12-byte
  veneer; both go to the same low terminal. The veneer's literal is 4-byte
  aligned at either entry alignment, so the head is constant
  (`KUI_VMU_NATIVE_PATCH_HEAD`). The installer's exit loop is unchanged: it
  compares each exit's 12 original bytes, then patches it. For the early pair
  the first entry's turn writes nothing; the second entry's writes its veneer
  and then the first entry's head, so each exit keeps working at every step.
  The worker witnesses all 20 bytes of the joint before every transfer, with
  the head's window 8 bytes and the veneer's 12. The terminal does not read
  R4, so how each exit was entered does not matter.
- Results on the library: The House of the Dead 2 resolves (entries
  0x8c0afeb0/0x8c0afeb8, cleanup 0x0c0bea30), and so does Time Stalkers
  (0x8c026ce8/0x8c026cf0). Every other title's exit result is unchanged.
  Elemental Gimmick Gear is refused because two literal-pool words read as
  branches into the windows; Sonic Adventure because it also loads 0x8c008300
  in its executable loader; Silver because it has more consumers of the vector
  cell, including a 0xac0000e0 alias.
- Size: with the exit pair the compact worker filled its 72 KiB exactly, so
  its owned stack went from 4 KiB to 3 KiB. The linked call-graph audit puts
  the deepest path (the split submit) at 1912 bytes including its 256-byte
  guard border and 80-byte assembly allowance, leaving 1160 bytes spare, and
  executes the compiled stack check at the new bounds. The 72 KiB worker, the
  284 KiB lease and the 192 KiB cache are unchanged; 1 KiB is now free for
  code and state. Other profiles keep 4 KiB stacks.

### No-allocator arena (in progress, for The House of the Dead 2)

The House of the Dead 2 links no SDK allocator, so its lease is a reviewed
fixed arena that is the whole lease (`KUI_VMU_NATIVE_HEAP_ARENA`), pinned to
the exact executable (bytes and SHA-256), like Crazy Taxi 2's plan:

- Borrowed call: the game's init calls the SDK hardware init once
  (MOV.L/JSR/NOP at 0x8c01b112, literal 0x8c01b184, target 0x0c0ae9c4). That
  routine copies the interrupt vector the SDK SCI route needs (0x8c0bf044 loads
  it at 0x8c0bf0a2). The borrowed literal has one consumer and no immutable
  word holds its address.
- Arena: 0x8cdb9000, 284 KiB, ending where the stage begins. The owner names
  only the image and the borrowed literal; every allocator role is zero. The
  lease is begin == payload, the complete arena, phase 2, one initialization.
  Its validity is that exact geometry, and every allocator-only proof
  (identity, release, bottom transaction) refuses it.
- Lifetime plan: no release is ever admitted. Its identity is the worker's own
  first eight header words at the arena's start, so the per-transfer poll stops
  the worker if anything overwrites the arena's beginning; no protected SDK span
  may lie inside the arena.
- Still to do: the worker's arena mode (no release or initializer hooks), the
  stage (discovery, installer without allocator calls), the native cache record
  and the GUI's routing.

### Remaining

- Lease for the syMalloc 1.35 titles: Sonic Adventure, Time Stalkers,
  Elemental Gimmick Gear and Silver link syMalloc 1.35, which the heap model
  does not know yet. Silver's own exact-image module (built in October) took
  its lease from that allocator and is a reference for its control block.
- Exits for Sonic Adventure (its executable-loader consumer of 0x8c008300
  needs its own proof), Silver and Elemental Gimmick Gear.
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
| The House of the Dead 2, session 3 (test 6, to midway through the second chapter) | 00000000 00000000 00000000 681CFFFC | 0x8cc40000-0x8ce00000 (1.75 MiB) and the same three upper pieces |
| Time Stalkers (two sessions) | all zero | none |

- The House of the Dead 2 left 0x8cca0000-0x8ce00000 untouched in all three
  sessions, room for the 284 KiB module as a fixed arena. Its top 284 KiB,
  0x8cdb9000-0x8ce00000, is the reviewed arena: about 1.1 MiB above the highest
  page any session wrote (session 1's 0x8cca0000). Later chapters and the other
  modes should still be mapped before relying on it.
- Sonic Adventure has no two adjacent untouched pages, so the 284 KiB module
  cannot sit in a fixed arena there.
- Time Stalkers wrote every page, probably clearing RAM at boot. Marking RAM
  before the game starts cannot tell cleared pages from used ones; marking
  after the game's own startup could.
