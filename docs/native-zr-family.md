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

The install routine runs from 0x15a bytes before the table initializer to its
end, and the initializer is its last part. Its first 0x84 bytes are identical in
all five titles. It chooses the stubs' common handler from its first argument
and writes that choice into both stub templates in the executable itself, at
+0x30. With 0x80000000 it writes the nesting handler that directly follows the
plain one, clears the nesting count (event+4) and stores its second argument,
the game's interrupt stack, at event-4. Otherwise it writes the plain handler
and clears both words. Only then does it copy the templates to VBR+0x100, +0x400
and +0x600 and fill the handler table with the default.

The nesting handler (0x180 bytes) counts levels. On the first level it saves SP
at event-8 and switches to that stack. It then saves the same frame, masks to
IMASK 15 and dispatches through the same table, so an SCI callback runs in the
same state under either handler. Its only literal is the event word, and it
never branches outside itself.

- The House of the Dead 2 selects it: just before the SDK hardware init that
  calls the install routine, its init calls a setter (0x8c03b570) that stores
  the mode 0x80000000 and its interrupt stack, 0x0c573700, far below the
  virtual card's arena.
- Sonic Adventure, Elemental Gimmick Gear and Silver have the SDK's
  interrupt-stack setter. A small wrapper calls it with a fixed buffer. Time
  Stalkers has none.
- The Background stage binds the route on the running game's memory: the
  executable it proves is the one the install routine has just changed.
  `early_family()` therefore accepts stub templates that both name one handler:
  the plain one, or the nesting one when four things match exactly: the
  install routine, its four role literals (both stubs' common words, the
  nesting handler and the plain one), the nesting handler, and its event word.
  The live INTEVT copy must then equal its template word for word, apart from
  its R0 scratch. The selected handler's live code is checked: 0x180 bytes for
  the nesting one, 0x14c for the plain one. The image-only finder still reports
  the plain handler.
- Compatibility tests 7 and 7b stopped The House of the Dead 2 at this bind
  (phase 10, detail 7), after the virtual card's worker had installed. Test 7
  knew only the plain handler. Test 7b accepted the nesting handler in the live
  copy, but still required the plain one in the templates. Its host test had
  checked the untouched file as the image, which is never what the stage sees.
- The early-family test now executes each title's own install routine, in
  both modes, in a small SH-4 interpreter over one main-RAM model. The route
  then reads that memory as the image and as live state. It accepts all five
  titles in both modes; test 7b's code refuses all five in nesting mode, as
  on console.
- The bind's refusals are now distinct: detail 0x0B for a live table that
  overlaps loader memory, 6 or 7 for the admission (7: the route check
  refused), and 0x0C for a plan with unexpected fields.
- The standalone New resident's late bind still accepts only the plain
  handler, in the templates and in the copy. For a title in nesting mode it
  therefore never installs the SCI callbacks, and reading stays in the game's
  own GD calls. That is a follow-up.

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

## Virtual VMU route for the early titles (The House of the Dead 2 admitted for test 7)

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

### No-allocator arena (done for The House of the Dead 2; console test 7)

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
- Worker: with an arena lease it hooks the submit entry, the borrowed
  literal (it must hold the low heap hook), the completion slot and the exits,
  and no release or initializer entry. Its release check refuses an arena
  lease outright. The release resume cell holds release+20 as for every lease,
  which is 20 here; nothing jumps to it. With the early exit pair and no
  allocator hooks the worker witnesses 10 words instead of 16.
- Early driver, live path: the worker's live check, used for every transfer
  after the first, still required IMASK 6 or 15, unlike the full proof. The
  House of the Dead 2 would have stopped at its second transfer whenever its
  server ran at another level. It now takes the same pd 1.46 exception and
  still requires the held lock byte. A host session runs the early driver,
  the exit pair and the arena lease together at all 16 inherited levels, and
  checks that the pd 1.50 driver is still refused at levels other than 6
  and 15, and that an unheld lock or a P2 lock pointer is refused, by both
  the full proof and the live check.
- Controller-only frames of the early driver always take the full submit:
  the split capture requires the P2 lock pointer, which only pd 1.50 stores.
- Stage (dual builds): when heap discovery finds no allocator, the relay
  binds the arena plan for this exact image and shows `NO SDK ALLOCATOR -
  REVIEWED RAM ARENA`. The arena takes the fixed arena's route: the borrowed
  literal gets the trampoline, which gives it the low heap hook, makes the
  borrowed call, then calls the hook with the arena. The hook's initializer is
  the stage's no-op (`RTS; NOP` after the trampoline). The installer then
  takes the arena itself as the lease: the hook's arguments must be exactly
  the planned arena, entered from the trampoline on an owner stack, and no
  allocator is called. Placement has its own check
  (`kui_vmu_native_stage_arena_safe`): 32-byte aligned main RAM outside the
  live stage and the executable. The digest kept from discovery to
  installation covers the borrowed call site instead of allocator code. Exit
  discovery is guarded by the submit window alone; the installer compares,
  replays and patches no initializer or release entry. In lease-only mode a
  borrowed literal gets back the call target it held, for Crazy Taxi 2's
  fixed arena too (it previously got the allocator initializer).
- Only the compact Background profile's lease is exactly the arena (284
  KiB); the General profiles' larger leases refuse it, so only the compact
  modules (A2 and A1) may be routed to it.
- Checks: a host test runs the stage's whole route on the owner's executable
  (program extent, driver and digest, plan, placement, lease, exit pair with
  the submit window alone, and all 60 spans the worker protects, 48 of them
  receive slots, outside the arena). The fixed-arena audit executes the
  compiled arm for both kinds, now also checking which line it shows; its
  mutation test covers the arena tests. The game's crt0 keeps the boot stack
  (0x8c00f400, guard-filled down to 0x8c00c000), inside the owner range the
  hook and the installer check.
- Cache record (kind 3): when heap discovery finds no allocator, the
  resolution record holds the arena plan's owner and call, with the exits
  guarded by the submit window alone. Its owner may hold no allocator role;
  the stage replays it only through the plan bound again for the exact
  image, and kind-1 and kind-2 readers refuse it as a cache miss. On the 21
  executables of the library only The House of the Dead 2's result changes
  (refused before, kind 3 now); every other record is byte-identical.
- GUI: the launcher seeds a kind-3 record only into a combined module whose
  own lease (its dual descriptor's lease size) is exactly the arena, that is
  the compact Background modules in A2 and A1. Any other module is not
  seeded; with New it then takes the standalone reader, as for an executable
  without any proof. So with this change The House of the Dead 2 runs on the
  Background module, with Virtual VMU On or Off, instead of the standalone
  New reader. Host tests cover the routing for all three readers and four
  lease sizes, and on the owner's executable.

### Console test 7c (The House of the Dead 2 on the Background module)

- The SDK SCI route binds (`SDK SCI ROUTE READY / LEVEL 1`), the game finds
  the virtual card in A2 and saves, and the save loads after a power cycle.
- With the card On it froze once during the first boss battle in Original
  mode, with the sound still playing and no failure page. There was also some
  lag that test 6's standalone reader did not show.
- A RAM map through the first boss in Original mode (test 6's reader) gave
  `00000000 00000000 00000000 681CFFF0`: 0x8CC80000-0x8CE00000 untouched,
  the arena included. Arcade mode gave the same.
- With Virtual VMU Off on the same module, with the same reader, prefetch and
  arena, the boss was passed without a freeze. The cause therefore lies in
  the card-On path: the per-transfer overlay on the early driver's full submit
  path, or the card's SD loans, each answered "send again" while the reader is
  busy.
- Test 7d adds a report gesture to the worker: both triggers fully pressed
  with A, B, X and Y in port A's condition reply, held for 180 consecutive
  completed full transfers. A controller-only fast transfer restarts the
  count, so it serves drivers whose every transfer takes the full path, such
  as the early driver. Its read fails closed like every completion read. The worker then shows its failure page with detail 0x4F
  and its counters: transfers, card reads, write phases and saves, SD reads,
  busy loans, faults, the last loan result and block, and the last card
  request and reply.

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
