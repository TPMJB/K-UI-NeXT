# CHD loading test

This candidate adds **on-console CHD preparation and loading**. Select a CHD
in Games, choose **Prepare CHD**, confirm the required space, and wait for
conversion and verification. Return to the detail screen and choose Play.
Later visits reuse the completed cache.

Preparation expands the image into a GDI and tracks under
`KUI/Cache/CHD/<source identity>/`. CD images also receive a checked CUE
descriptor so the normal reader preserves CD media and sector modes.
The existing game reader then owns in-game reads.
This is **not direct compressed streaming during gameplay**: the card
needs room for the CHD plus its expanded tracks. First preparation can take a
long time on serial storage. The displayed expanded size excludes allocation
overhead; preparation checks that additional space too.

## Preserve the current VMU installation

The CHD runtime overlay contains `KUI/runtime.kui`, guides and licenses. Copy
that overlay onto the existing installation. Keep the personalized
`KUI/apps/games/retail-boot.kui`, `KUI/tools.kui`, and VMU profile/card files.
The ordinary GDI manifest and relocation ABI are unchanged. The runtime
accepts each existing module's own identifier and checked layout; module and
shell build identifiers do not have to match.

A separate full candidate package replaces Games modules. Use it only on a
separate test installation. The overlay neither installs nor activates a new
VMU engine. CSO support still needs the previously supplied `cso-boot.kui`
and its original SCI Standard restrictions.

## Initial supported profile

| Container or feature | Admission |
| --- | --- |
| Parentless CHDv4 / CHDv5 optical images | Supported through the pinned libchdr decoder |
| Default CD codecs `cdlz`, `cdzl`, `cdfl` | Tested against independently generated MAME images |
| Raw and self-reference hunks | Supported; reference chains are bounded and iterative |
| `cdzs`, general DEFLATE/LZMA/Huffman/FLAC/Zstd | Built; the current oracle matrix targets the default three CD codecs |
| GD metadata `CHGD`, legacy `CHGT`; CD `CHTR` / `CHT2` | Explicit normalization; PAD, disc addresses and storage alignment stay distinct |
| MODE1 / MODE1_RAW, MODE2_FORM1 / MODE2_RAW, AUDIO | Cooked 2048-byte or raw 2352-byte output; little-endian GDI audio |
| Parent/delta images, stored subchannels, nonzero pregaps/postgaps, unknown session extensions | Refused with a profile error |
| Size bounds | Optical 2448-byte frames, hunks at most 512 KiB, logical content at most 4 GiB; bounded GDI disc geometry |

K-UI's restricted v4 CHD exports also retain independent reader tests. Game
compatibility still depends on the ordinary native/WinCE/CDDA admission rules.
A CHD does not expand those rules. This candidate has host and target-build
validation; console acceptance is still required.

## Publication, cancellation and reuse

Inspection is read-only. Preparation creates an exclusive work directory,
decodes each exported track, syncs it, reads every output byte back and compares
SHA-256, then closes it. The source must also close successfully. After the
GDI and manifest pass sync/readback, one directory rename makes the cache
visible. Sources, existing caches, settings and VMU files are preserved.
B stops preparation; cleanup removes only files created by that attempt.
Power loss can leave an unpublished `.workNNN` folder, removable with Files.

The key binds source path, file size, FAT date/time, complete CHD header and
normalized geometry. Reuse checks manifest CRC, identity, track lengths and
GDI SHA-256 and exact regenerated CUE content for CD images.
It **does not rehash every track on each launch**. Creation
verification is not fresh full-media verification. The ordinary reader repeats
its IP/executable checks before handoff. If a completed cache is damaged,
remove that source's cache directory and prepare again. An incomplete existing
final directory is never overwritten.

## Console acceptance sequence

1. Keep the previous runtime for rollback. Install only the CHD overlay.
2. Check that a known-working original GDI still boots normally.
3. Select its matching CHD. Confirm tracks/expanded size and prepare it.
   B during another attempt should stop without a selectable partial cache.
4. Play with Standard, CD music Off and RAM patches Off first. Compare boot,
   a real gameplay load and sustained reads with the original image.
5. Reboot and select the CHD again: Play should be available directly.
   Record the exact build, image release and any failure screen.
6. Try optional CDDA/WinCE only for an image already admitted by that loader.
   The exact-owner VMU profile retains its image fingerprint; re-encoding can
   change that fingerprint.

Original synthetic console tests, when included in the bundle, exercise the
codec/cache/normal-read handoff without commercial game data. Their guide
states the expected screens. Host results do not establish console success.

## Automated checks

Run `make test-chd` with MAME `chdman` on PATH. Set
`KUI_REQUIRE_CODEC_ORACLES=1` in CI to make missing independent codecs fatal.
`KUI_CHDMAN` selects the modern-reader oracle; `CHDMAN` selects the filesystem
oracle. AddressSanitizer and UndefinedBehaviorSanitizer are enabled.

The matrix compares canonical storage and exported data/audio with MAME and
original synthetic tracks, including GD PAD, the 45000-LBA density boundary,
alignment, raw/self/codec entries and cache seeks. Real FAT32/exFAT tests cover
prepare/reuse, changed identity, full readback, storage failures, cancellation,
cleanup and unchanged VMU sentinels. Core tests inject faults at every callback
and cancellation boundary. Dreamcast game readers retain placement, stack and
instruction audits.

## Direct streaming remains separate

The ordinary native service has 14,336 bytes for code and BSS at
`0x8c004000..0x8c007800`. A default CHD hunk alone is 19,584 bytes, before codec
state. This shell-side decoder is closed before game handoff and takes no new
reservation from game RAM. A future direct reader needs a demonstrated
protected workspace and separate console acceptance.

The independently pinned decoder, licenses and hardening changes are recorded
in [the dependency record](../third_party/chd-codecs.md). No DreamShell decoder
implementation is used.
