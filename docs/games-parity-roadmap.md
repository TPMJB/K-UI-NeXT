# Games feature-parity roadmap

Source review: 2026-10-15, main at `e003836`. The first increment is implemented in source; subsequent increments remain
a plan, not an announcement of supported runtime features. The historical comparison is in
[the legacy Games audit](audit-legacy-games.md). All future runtime work must
be independently implemented; no DreamShell loader implementation is proposed
for reuse.

## Sequence

| Increment | Deliverable | Boundary before enabling it |
| --- | --- | --- |
| 1 — implemented; console acceptance pending | Two bounded RAM patches, detected defaults, persistent launch report | Validate/apply patches after the original executable CRC, before handoff; keep unsafe targets out. Defaults must come from the inspected image. Save launch evidence before KOS/filesystem teardown. Native non-CDDA only; CE/CDDA admission is refused. See [launch options](games-launch-options.md) and the updated audit. |
| 2 | Independently tested compressed-block reader, then direct native ZSO launch | First support standard LZ4 ZSO with 2048-byte independent blocks and a separate Standard-reader package. Inspection alone is not direct-launch support. Pass memory/stack checks and console acceptance before exposing Play. |
| 3 | CSO v1 DEFLATE, then CSO v2 mixed methods; explicit LZO dialect later | Each decoder needs its own bounded workspace and malformed-input tests. Keep offline import working for all unsupported sizes/codecs. CHD remains a separate project. |
| 4 | Emulated VMU prototype | Prove Maple command interception and a safe writable backing store; the existing VMU Manager is not gameplay emulation. First prototype must not depend on unreserved game RAM or KOS after handoff. |
| 5 | Runtime screenshots | Prove input trigger, framebuffer interpretation and safe output ownership. No full-frame allocation in the current low resident. Enable only the demonstrated video/reader profiles. |
| 6 | Last-game selection, category/library improvements, shortcuts, optional game previews | These can be separate shell changes; they need not wait for runtime milestones. Keep a shortcut bound to an inspected image and saved settings. Audio previews must yield to foreground I/O. |
| As needed | Region override, alternate executable/boot modes, placement controls | Start from a concrete failing game and independently established requirements. Old DreamShell numeric options are not equivalent parameters for this loader. |

G1 SD adapter hardware experiments remain excluded. Wireless networking does
not make network shares supported game-launch media. Existing WinCE/CDDA
finishing work stays on its own branch until independently accepted.

## Direct compressed images: bounded first slice

The offline importer already defines and tests CSO v1/v2, standard LZ4 ZSO and
an explicitly selected LZO ZSO dialect (`tools/game_image_import.py`,
`tests/test_game_image_import.py`). Reuse the independent format knowledge and
original test-data generators, not the Python conversion workflow inside the
detached reader. The runtime must read compressed bytes directly.

**Standard LZ4 and historical LZO ZSO can share `ZISO` magic/version.** Extension
or magic does not identify the codec. The first live implementation should
explicitly admit standard LZ4 only, fail clearly on invalid/other dialects and
retain `game_image_import.py --zso-codec lzo` as the conversion route. Do not
retry another codec after arbitrary decoder failure or advertise all `.zso`
files as compatible.

First contract: one cooked ISO data track; native executable; Standard reader;
SCI transport initially; exactly 2048 decoded bytes per block; a documented,
bounded alignment shift and stored span. Use existing ISO session-base
recovery and plain/scrambled boot choices. Refuse WinCE, CDDA/background modes,
larger compression blocks and unsupported codecs before launch. A single ISO
does not retain its original audio inventory: display **unknown**, not “no
audio” (`src/core/game_image.c`, `kui_game_image_audio_info`).

Add a pure C container/decoder module with caller-owned workspace and read-only
range callbacks. Validate the 24-byte header, overflow-safe decoded size,
block count, full index geometry, strictly increasing offsets outside the
index, terminal bound, raw-block length, decoded length and padding. Scan the
index in bounded pages during preparation; never retain a multi-megabyte index
in low RAM. Cache a small fixed number of index entries for sequential reads.
LZ4 input can stream through a 512-byte physical cache; whole blocks can decode
directly into the requester's 2048-byte destination, including overlapping
matches. Shell metadata's partial reads can use its own temporary decoded
sector, which must not become a detached pointer.

Compressed bytes require a distinct physical-container map. The existing
retail v3 validator equates logical track bytes with extent coverage and
requires wire bytes 272–319 to be zero (`src/core/retail_image.c`,
`kui_retail_manifest_validate` / `kui_retail_manifest_decode`). Do not place
compressed offsets into an ordinary track map. Define explicit compressed
wire/package admission with container bytes, codec/block geometry and bounded
physical extents; validate it before copying the resident prefix. Preserve
ordinary v3 decoding and readers. The high stage and detached resident must
share the same logical-to-compressed read implementation and retain IP/boot
CRC verification.

Integration seams:

- `include/kui/game_image.h`, `src/core/game_image.c`: explicit compressed
  format/storage metadata, read adapter, ISO normalization; no retained pointer
  to an opener's stack. Keep physical and decoded sizes distinct.
- `src/apps/games.c`, `src/apps/games_covers.c`: bounded inspection/art extraction
  through that adapter. Remove the offline-only message only for admitted
  formats; preserve it for everything else.
- `src/apps/games_retail.c`: validate the physical compressed file and index,
  build its extent map, fingerprint preparation metadata and select the new
  package. Its current `map_track` assumes uncompressed file geometry.
- New compressed map/read module alongside `src/core/retail_image.c`, plus
  explicit stage/resident selection in `src/loader/retail_stage.c` and
  `src/loader/retail_resident.c`. The existing background
  `src/core/retail_cursor.c` is not compression aware; leave it out of this slice.
- `Makefile`, `Makefile.dc`, `tools/retail_package.py`,
  `tools/check_retail_loader_layout.py`, `tools/package.py`: separate native
  package, audited embedded resident, dependencies and installed files.

This is a moderate-to-large feature spanning format parsing, detached mapping
and package validation. Split it into reviewable decoder, shell inspection,
native runtime and acceptance commits. Decoder/inspection commits alone must
not be labeled direct launch.

## Memory proof before adding a resident feature

The ordinary native low reservation is `0x8c004000..0x8c007800`, with its guarded
stack above it (`include/kui/retail_loader_layout.h`). The inspected existing
`build/retail/resident-sci.map` ends BSS at `0x8c0077f4`: only 12 bytes below
that limit. This is an existing local linked artifact, not a fresh size proof
for future changes. Native async and CE have distinct reservations/stacks;
their extra space is not permission to relocate native state or borrow game
RAM.

For the first one-track compressed resident, a proposed 32-slot map permits at
most 31 physical fragments and saves 1536 bytes versus the ordinary 160-slot
allocation. `kui_retail_resident_init` already copies a stage-validated map
prefix. A specialized cooked-only reader can also omit unused raw-sector
paths. These are feasibility options, not proof that the decoder fits.

Cross-build every proposed package; inspect linked text/data/BSS and compiler
stack call paths, including decoder error paths and callbacks. Pass
`tools/check_retail_instructions.py`, the applicable layout check and stack
check. Keep the existing stack guard and owner-memory boundaries. Refuse an
over-fragmented map rather than borrowing unchecked RAM. Do not add heap
allocation, filesystem callbacks or launcher-owned decoder pointers after
handoff. Compression's claimed speed benefit requires measurements: fewer
stored bytes can still lose to decode cost, index seeks or short card streams.

## VMU and screenshot architecture

Both need a new independently specified Maple service. The current resident
hooks GD calls and menu return; native async interrupt handling is active for
its reader, while CE owns its VBR and admits specific SCI handlers
(`src/loader/retail_resident.c`, `src/loader/retail_async.c`). Neither is an
always-on controller observer or a Maple emulator. `src/apps/vmu.c` uses KOS
for physical VMU management and cannot run after `arch_exec` tears KOS down
(`src/dreamcast/main.c`). `src/loader/retail_storage.h` and `sd_reader.h` expose
read-only storage: runtime output needs a checked freestanding writer and bus
arbitration, not a call back into FatFs.

VMU sequence:

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

Screenshot sequence:

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

For compression, extend `tests/test_game_image.c`, `tests/test_retail_image.c`,
`tests/games_retail.c`, `tests/test_games_retail.py`,
`tests/test_games_images.py` and `tests/test_games_covers_images.py`; add a
focused pure codec/map test. Use the offline importer's original synthetic
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
