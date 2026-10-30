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
| Sword of the Berserk | admitted | variable, zero-mode root | call proved | all structural gates pass |
| Crazy Taxi | admitted | uniform, R5 continuation | no initializer slot | needs a fixed arena (RAM map), as Crazy Taxi 2 did |
| Zombie Revenge | admitted | variable root with 6 inputs (TX bank 6 KiB) | no SDK allocator found | needs a 6-input layout and its own heap route |

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
yet). The combined VMU route still needs their Maple layout (pd 1.46),
allocator (syMalloc 1.35) and terminal exits.
