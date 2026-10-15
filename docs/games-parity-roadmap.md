# Games feature-parity roadmap

Source review: 2026-10-15, `release/2.0-tyrfing` integration source. The first
Games-options increment and the initial native LZ4 ZSO slice are implemented
in source, with console acceptance pending. Later increments remain a plan. The historical comparison is in
[the legacy Games audit](audit-legacy-games.md). All future runtime work must
be independently implemented; no DreamShell loader implementation is proposed
for reuse.

## Sequence

| Increment | Deliverable | Current boundary |
| --- | --- | --- |
| 1 — implemented; console acceptance pending | Two bounded RAM patches, detected defaults, persistent launch report | Validate/apply patches after the original executable CRC, before handoff; keep unsafe targets out. Defaults must come from the inspected image. Save launch evidence before KOS/filesystem teardown. Native non-CDDA only; CE/CDDA admission is refused. See [launch options](games-launch-options.md) and the updated audit. |
| 2 — first slice implemented; console acceptance pending | Independent standard LZ4 ZSO block reader, inspection and dedicated native launch path | Development capability only: native SCI Standard, one cooked track, 2048-byte blocks, 32 map slots/31 physical fragments. Separate package/audit gates must pass before a test package is supplied. The development Play route requests an unvalidated-on-console capability; it does not establish compatibility or final-release acceptance. See [ZSO scope](games-zso.md). |
| 3a — planned | CSO v1 DEFLATE, then CSO v2 mixed methods; explicit LZO dialect later | Each decoder needs its own bounded workspace and malformed-input tests. Keep offline import working for all unsupported sizes/codecs. |
| 3b — planned | Direct CHD: portable hunk/track reader, then a protected native runtime | Common CHDv5 CD/GD needs `cdlz`/`cdzl`/`cdfl`, hunk/map decoding and metadata normalization. Default 19584-byte hunks plus codec state exceed the whole low reservation. K-UI's own CHDv4 exports are a distinct first prototype. Prove hunk decoding with MAME oracles before choosing runtime memory; no enabled Play claim or unchecked game-RAM reservation. See [CHD design](games-chd.md). |
| 4 — deferred by user | Emulated VMU prototype | Not part of the current Tyrfing preparation. Future work needs Maple command interception and a safe writable backing store; the existing VMU Manager is not gameplay emulation. No use of unreserved game RAM or KOS after handoff. |
| 5 — planned separately; not implemented | Runtime screenshots and configurable hotkeys | Needs input observation, framebuffer interpretation and safe output ownership before a working screenshot option can be offered. No full-frame allocation in the current low resident. Scope each proven video/reader profile independently. |
| 6 | Last-game selection, category/library improvements, shortcuts, optional game previews | These can be separate shell changes; they need not wait for runtime milestones. Keep a shortcut bound to an inspected image and saved settings. Audio previews must yield to foreground I/O. |
| As needed | Region override, alternate executable/boot modes, placement controls | Start from a concrete failing game and independently established requirements. Old DreamShell numeric options are not equivalent parameters for this loader. |

G1 SD adapter hardware experiments remain excluded. Wireless networking does
not make network shares supported game-launch media. Existing WinCE/CDDA
work retains its own exact-profile admission and recorded console evidence;
the native ZSO slice does not enable it for compressed images.

## Direct compressed images: implemented first slice

The new source reads standard LZ4 ZSO directly through independent bounded C
callbacks. Shell inspection and the detached native reader share the container
and logical-read implementation. The complete matching package still needs
console acceptance; the development launch route is available only inside the
stated first-slice boundary. It is not a published 2.0 capability guarantee.

**Standard LZ4 and historical LZO ZSO can share `ZISO` magic/version.** K-UI's
existing ripper exports the historical LZO dialect. Neither extension nor magic
proves the codec. This live implementation admits standard LZ4 only and does
not guess/fall back to LZO. Keep
`game_image_import.py --zso-codec lzo` as the offline conversion route.

The actual contract is one cooked ISO DATA track, native executable, Standard
reader, SCI storage and exactly 2048 decoded bytes per independent block.
Header/index geometry, overflow, strictly increasing offsets outside the
index, terminal bounds, raw-block lengths, LZ4 matches/end conditions and
padding are checked. Preparation scans the whole index in bounded pages;
the detached reader rechecks its chosen pair and decodes through a fixed
512-byte physical cache into the requester's sector output. No full index,
compressed input buffer, heap or launcher-owned callback survives handoff.
WinCE, CDDA, background loading and executable RAM patches are outside this
path. A single ISO does not retain its original audio inventory; represent
that as unknown, not proof that the disc had no audio.

The separate `KUIRZS01` manifest covers the entire physical compressed file,
including header/index, and carries its distinct decoded geometry. It has
32 slots: one logical cooked track and at most 31 physical fragments. The
ordinary retail map/reader contract is unchanged. The dedicated outer launch
header is `KUIRZB01`; its shipped manifest is blank and it contains no RAM-patch
capability.

Implemented seams:

- `include/kui/zso.h`, `src/core/zso.c`: independent header/index validation
  and bounded standard LZ4 block decoding.
- `include/kui/retail_zso.h`, `src/core/retail_zso.c`: physical-container
  extent mapping and the shared read-only logical sector adapter.
- `src/core/game_image.c`, Games inspection/artwork and
  `src/apps/games_retail.c`: admitted container discovery, metadata/boot
  inspection, physical mapping, preparation checks and package selection.
- Separately compiled native ZSO stage/resident in `build/retail-zso`: SCI
  Standard only, within the existing low reservation and guarded stack.
- `tools/zso_package.py`, `tools/check_zso_loader.py`: distinct outer contract,
  linked embed/placement checks, final compiler stack reports and mandatory
  integer-instruction audit. Ordinary native/CE gates remain in force.

See [the full ZSO limits and acceptance checklist](games-zso.md). CSO, CHD,
larger blocks and a live historical-LZO reader remain later work. Host
format/inspection tests alone must never be described as successful gameplay.
The requested direct CHD follow-up has a separate [implementation design](games-chd.md);
its current supported route remains PC import with MAME `chdman`.

## Memory proof before adding a resident feature

The ordinary native low reservation is `0x8c004000..0x8c007800`, with its guarded
stack above it (`include/kui/retail_loader_layout.h`). The inspected existing
`build/retail/resident-sci.map` ends BSS at `0x8c0077f4`: only 12 bytes below
that limit. This is an existing local linked artifact, not a fresh size proof
for future changes. Native async and CE have distinct reservations/stacks;
their extra space is not permission to relocate native state or borrow game
RAM.

The separate one-track ZSO resident now uses the 32-slot map, permitting at
most 31 physical fragments and saving 1536 bytes versus the ordinary 160-slot
allocation. Its cooked-only reader omits unused raw-sector paths. The current
linked build must still pass its dedicated placement and stack audits; the
smaller map alone is not proof that a particular binary fits.

Cross-build every proposed package; inspect linked text/data/BSS and compiler
stack call paths, including decoder error paths and callbacks. Pass
`tools/check_retail_instructions.py`, the applicable layout check and stack
check. Keep the existing stack guard and owner-memory boundaries. Refuse an
over-fragmented map rather than borrowing unchecked RAM. Do not add heap
allocation, filesystem callbacks or launcher-owned decoder pointers after
handoff. Compression's claimed speed benefit requires measurements: fewer
stored bytes can still lose to decode cost, index seeks or short card streams.

## VMU and screenshot architecture

**VMU emulation is deferred at the user's request.** Its design below is a
future reference, not active implementation scope for Tyrfing. Runtime
screenshots and their hotkeys remain a separate planned feature; the current
Games options do not implement them. The two manual executable RAM-patch slots
are independent of either feature and are not a patch database or hotkey system.

VMU emulation needs a new independently specified Maple service. Resumable
screenshot hotkeys need reliable controller-condition observation and safe
capture/output scheduling. The current resident hooks GD calls and menu
return; native async interrupt handling is active for
its reader, while CE owns its VBR and admits specific SCI handlers
(`src/loader/retail_resident.c`, `src/loader/retail_async.c`). Neither is an
always-on controller observer or a Maple emulator. `src/apps/vmu.c` uses KOS
for physical VMU management and cannot run after `arch_exec` tears KOS down
(`src/dreamcast/main.c`). `src/loader/retail_storage.h` and `sd_reader.h` expose
read-only storage: runtime output needs a checked freestanding writer and bus
arbitration, not a call back into FatFs.

Deferred VMU sequence:

1. Use an owned homebrew harness to observe controller/VMU commands, then test
   an independent block protocol backend: device info, media info, block read,
   four-phase block write and sync. Observe before synthesizing responses.
2. Preallocate a private image/journal while KOS still owns the filesystem;
   prepare checked immutable physical ranges and card identity. Commit only
   within those ranges, with power-loss recovery. Do not edit FAT metadata
   from the detached reader or overwrite a real attached VMU's slot.
3. Admit one native SDK/profile adapter and an unoccupied logical slot first.
   A 128 KiB image cannot fit in the ordinary low resident; either stream
   bounded blocks with a journal or prove a separately reserved memory lease.
   Start without CDDA/background/CE, which require separate bus and interrupt
   ownership proofs.

Planned screenshot sequence:

1. In an owned runtime, validate framebuffer base/size/stride/pixel format and
   implement bounded BMP output. Check VRAM ranges, double buffers and the
   specifically admitted display modes. A 640×480 RGB565 frame is 614400 bytes;
   no full-frame low-resident allocation is possible.
2. First consider an honestly named **capture on exit**. The menu-return path
   is already intercepted, but capture must precede
   `retail_display_restore()`, which clears VRAM (`src/loader/retail_display.c`).
   The output writer and reserved file space still need implementation.
3. Resumable gameplay hotkeys need passive controller-condition observation,
   debounce, a safe freeze/capture event, writes deferred outside IRQ context,
   and exact restoration of CPU/Maple/render state. Some games bypass BIOS
   Maple calls; support must follow the proven interception profile. Start
   native without CDDA; CE/music require independent admission.

Host VMU tests must cover bad commands, ranges/checksums, out-of-order/duplicate
write phases, incomplete commits, reset, disconnect and recovery after each
journal-write interruption. Hardware acceptance is a real save, power cycle,
then load, while a physical VMU/controller continues working in another slot.
Screenshot tests need format/stride/VRAM-bound cases, exact expected BMP bytes,
full-card/short-write/removal handling and proof that no write occurs in IRQ
context. Console acceptance must verify captured content and, for resumable
capture, continued gameplay/input without corrupted state.

## Required verification

For later compression expansion, extend `tests/test_game_image.c`, `tests/test_retail_image.c`,
`tests/games_retail.c`, `tests/test_games_retail.py`,
`tests/test_games_images.py` and `tests/test_games_covers_images.py`; add a
focused pure codec/map test. The initial ZSO slice adds independent codec/map
coverage and `tests/test_zso_package.py` / `tests/test_zso_loader.py` mutation
gates; keep their bounds and failure coverage when expanding scope. Use the
offline importer's original synthetic
fixtures and independent encoders as byte-for-byte oracles. Cover:

- Raw/compressed mixtures, overlapping LZ4 matches, final literals, index-page
  boundaries, maximum admitted geometry, backward/random and multi-sector
  reads, image beginning/end and compressed spans crossing physical fragments.
- Truncated headers/index/data, integer overflow, index aliases/backtracking,
  offsets into metadata, EOF overruns, invalid matches, excessive padding and
  unsupported versions/block sizes/codecs. Fail before unsafe output writes.
- Short reads, cancellation, changed preparation metadata, SD/CRC failure,
  cache invalidation and retry; no out-of-range card access or stale output
  admitted as success. Existing raw-reader and package tests must still pass.
- Byte equality for shell inspection and detached IP/executable/data reads
  against the uncompressed ISO, with source files unchanged.

Console acceptance uses the same owner ISO and its compressed equivalent on
the same card: boot, gameplay with continued data reads, scene transitions,
FMV/seeking where applicable, repeat launches and return/reset. Record linked
package identity, transport, physical bytes read, logical bytes delivered,
load times and faults. Include a fragmented-file trial within the declared
limit and a clear refusal beyond it. Compare Standard compressed with Standard
uncompressed; passing host tests does not establish console compatibility.

VMU/screenshot milestones additionally require persistence/capture-specific
fault tests and hardware gates before any menu option claims they work.
