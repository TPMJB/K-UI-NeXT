# Direct CHD Games milestone

Design review: 2026-10-15. **Direct CHD launch is not implemented.** The existing
Games route is PC import through `tools/game_image_import.py` and MAME
`chdman`; that remains available. CHD capture export is a separate working
capability and does not establish that the detached game reader can decode
CHDs. Do not enable a Play option merely because a header is recognized.

## Formats we actually need to handle

| Profile | Container/map | Hunk decoding and track semantics |
| --- | --- | --- |
| K-UI ripper output | CHDv4, 108-byte header, fixed 16-byte big-endian map entries; own writer emits compressed/raw types 1/2 without a parent | Raw DEFLATE or raw 19584-byte hunks; eight 2448-byte frames. `CHGD` metadata preserves original disc addresses using explicit PAD tails, four-frame storage alignment and zero omitted subchannels. Audio is stored in canonical big-endian sample order. |
| General CHDv4 | Same basic header/map family, additional entry types | Generic files can use mini/repeated, self-referenced, parent-referenced or secondary-compression hunks. Supporting only our writer's types must be labeled a restricted v4 profile. |
| Common modern CD/GD CHDv5 | 124-byte header, up to four codec identifiers, compressed Huffman/RLE map; normalized entries are 12 bytes. Uncompressed v5 has a different map | MAME's default CD/GD codec set is `cdlz` (CD LZMA), `cdzl` (CD DEFLATE), `cdfl` (CD FLAC). Default hunks are also eight 2448-byte frames. Raw/self/parent map entries are distinct from codec slots. |

Sources: current `src/core/capture_chd.c`; MAME
[CHD format definitions](https://github.com/mamedev/mame/blob/master/src/lib/util/chd.h),
[CD geometry](https://github.com/mamedev/mame/blob/master/src/lib/util/cdrom.h),
and [chdman CD defaults](https://github.com/mamedev/mame/blob/master/src/tools/chdman.cpp).
These are format/defaults evidence, not a census of the user's game files.
An admitted v5 reader must examine actual codec identifiers and map entry types.

CD codecs are more than ordinary DEFLATE/LZMA/FLAC on a raw hunk. The CD layer
separates mainchannel and subcode streams and can reconstruct omitted sync/ECC
bytes. A reader must restore the declared frame representation before checking
the hunk CRC or returning raw sectors. Declared FLAC support cannot be replaced
by assuming all audio is unused. See libchdr's
[CD transform](https://github.com/rtissera/libchdr/blob/master/src/libchdr_cdrom.c)
and [CD FLAC decoder](https://github.com/rtissera/libchdr/blob/master/src/libchdr_codec_cdfl.c).

Track normalization needs separate physical-storage and disc-LBA coordinates.
Parse `CHTR`/`CHT2` CD metadata, `CHGT` legacy GD metadata and `CHGD` current GD
metadata explicitly; account for pregaps/postgaps, four-frame storage padding,
PAD tails, the GD density boundary and audio byte order. Newer CD session
metadata also needs an explicit policy. Track 3 is not universally a sufficient
boot-session rule. Reject unsupported legacy binary metadata, ambiguous
sessions, stored subchannels or parent/delta dependencies clearly until their
semantics are implemented. Do not silently discard tracks or claim original
subchannels were absent merely because a game read requests 2048 data bytes.

The PC importer already has bounded header/metadata checks and GD PAD geometry
validation, but `chdman verify` and `extractcd` do the actual container/hunk
decoding. Its supported versions 3/4/5 are not evidence of a built-in decoder.

## The memory gate

Vendored `third_party/miniz` provides a usable raw-DEFLATE decoder, documented
in `third_party/capture-codecs.md`. The current export wrapper has shared static
worker state and cannot be called from a departed KOS runtime. No libchdr,
LZMA or FLAC CHD decoder is presently vendored.

An isolated size check against the existing SH-4 compiler measured
`sizeof(tinfl_decompressor) = 8364` bytes. One 19584-byte hunk plus that state
already needs **27948 bytes**, before input buffers, code, metadata, map/cache
or stack. This is a concrete estimate for the existing miniz-based path, not a
universal theoretical lower bound. It exceeds the **14336-byte entire ordinary
native code/BSS reservation**, `0x8c004000..0x8c007800`.

Unlike ZSO's independent 2048-byte LZ4 block, a CHD hunk spans multiple sectors.
A game can request only one sector from its middle. Ordinary DEFLATE and LZMA
back references can need earlier output within that hunk, so direct decoding
into an arbitrary 2048-byte game destination does not supply a valid history
buffer. FLAC and the CD transforms add their own working state. Measure those
workspaces with the exact pinned implementation and SH-4 configuration.

The v5 map also needs a plan: materializing it costs
`12 * ceil(logical_bytes / hunk_bytes)` bytes, around a megabyte for a large GD
image at default geometry. Fixed-entry v4 maps can be read on demand. For v5,
evaluate bounded decode checkpoints or a validated normalized index sidecar
prepared while KOS is alive; neither removes the hunk/codec memory requirement.
Current libchdr has low-memory map techniques worth auditing, but its heap and
stdio implementation is not a freestanding drop-in:
[container reader](https://github.com/rtissera/libchdr/blob/master/src/libchdr_chd.c).

Before detached launch, choose and prove a protected reservation for code,
decoded hunk, codec state and map/cache. The high staging area becomes ordinary
game RAM after handoff and is not automatically free. A smaller supported hunk
profile, a verified owner memory lease, or another established loader memory
contract needs its own design and acceptance. Do not hide an arbitrary
128 KiB reservation in game RAM. The larger CE reservation does not solve the
native case and is not permission to change the WinCE/CDDA finishing branch.

## Next implementation: portable hunk reader and oracles

The next source change should be a callback-based CHD reader prototype, with
caller-owned workspace, independent of `capture_chd.c` and detached placement.
Proposed files: `include/kui/chd.h`, `src/core/chd.c` and focused metadata/codec
modules as needed. Keep physical file offsets, normalized hunk entries and
disc-track geometry distinct. Do not allocate a complete map implicitly.

1. Implement the restricted K-UI CHDv4 profile first: strict header/parent
   refusal, streamed fixed-entry map validation, exact raw/DEFLATE hunk output
   and CRC32. Decode a hunk once, then return bounded requested frame slices.
   This gives an independently testable real reader for our own CHD exports.
2. Normalize CHGD tracks and compare data/audio/addresses with the original
   capture and MAME extraction. Preserve canonical bytes for hunk CRC; convert
   audio sample order only at the disc-reader boundary. Treat synthetic PAD
   separately from captured sectors.
3. Add v5 map decoding and raw/self entries with bounded reference resolution,
   cycle detection, map CRC16 and hunk CRC16. Start controlled `cdzl` fixtures,
   then add `cdlz` and `cdfl` before describing common default CHDs as supported.
   Parent files remain an explicit later capability, not an implicit search.
4. Run a KOS-side benchmark with caller-owned heap workspace to measure
   worst-case memory, random-read latency, sequential throughput and corruption
   handling. This is inspection/benchmark capability, not game launch.
5. Only then design a separate CHD manifest/package and protected runtime
   workspace. Preserve the native/CE/CDDA/ZSO readers and their layout gates.

Codec libraries may be independently sourced from their original maintainers
with a pinned revision, retained licenses and a dependency record; no
DreamShell implementation is needed. Existing miniz is already documented.
Do not copy the complete capture worker, its compressor or its shared globals
into the resident to obtain one inflate operation.

## Integration and acceptance

`game_image.c` currently normalizes physical files into tracks and supports the
separate ZSO adapter. CHD adds multi-track hunk storage and a larger mutable
decode cache. Define an explicit caller-owned reader/workspace lifetime before
integrating it; no borrowed opener-stack context or hidden heap allocation
without a close/release path. Audit Games inspection, artwork, cancellation
and every error exit. Runtime manifest preparation must map the physical CHD
file plus any normalized index, not pretend logical sector bytes are file
extents. Detached IP/executable CRC checks remain mandatory.

Use the current original capture fixture in `tests/test_capture_chd_oracle.py`
as an independent baseline; extend it with real `chdman createcd` v5 fixtures
using each default codec and their mixture, raw and self hunks, CD and GD
geometry and hunk-boundary seeks. Compare decoded canonical hunks and returned
sectors byte for byte. Existing PC-import tests remain in place.

Required negative cases include truncated/oversized headers/maps/hunks,
metadata cycles/ranges/duplicates, integer overflow, invalid codec slots,
out-of-range and cyclic references, wrong parents, CRC failures, invalid
pregap/PAD/storage geometry, unknown subchannels, short reads and cancellation.
Require bounded work, unchanged publication on failed open, invalidated caches
after failed reads and intact output redzones under ASan/UBSan.

A detached prototype additionally needs linked code/BSS/stack and instruction
audits, proven workspace ownership, file-fragment bounds, read-error cleanup
and console comparison against the same uncompressed owner image. Test native
data loading first; container audio inventory does not enable CDDA emulation.
WinCE and CDDA are later admission milestones. Publish the actual version,
codec, hunk-size, metadata and transport scope only after those gates pass.
