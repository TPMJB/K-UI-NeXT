<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native ZSO: experimental SCI Standard reader

This first slice adds a separate resident reader for **standard LZ4 ZSO**
images. It is an opt-in development candidate for console testing, not a
published K-UI 2.0 release or a claim that compressed games are now generally
compatible. Ordinary uncompressed images keep their existing reader path.

## Which ZSO format

The supported container is **ZISO version 1 with independent 2048-byte LZ4
blocks**. Each logical block is either stored raw or decoded as one bounded
LZ4 block. No LZ4 frame, dependent dictionary or codec guessing is used.

K-UI's existing Disc Ripper **ZSO export uses the historical DreamShell-LZO
dialect**. Both dialects can use `.zso` and the `ZISO` magic, so the extension
and header do not reliably identify their compression algorithm. The new
resident reader does not provide LZO fallback. Do not assume an existing
ripper export is suitable or rename a different format to `.zso`.

Use an image produced explicitly as standard LZ4 ZSO from a complete Dreamcast
data-session ISO, and retain its original image. The existing
[PC import workflow](games-formats.md#compressed-image-import) remains available
for other supported compressed containers. This reader does not add CSO or CHD
resident decoding.

## Current launch scope

| Setting or image feature | First-slice requirement |
| --- | --- |
| Game | Native Dreamcast boot, with valid IP metadata and boot executable |
| Storage | SCI microSD |
| Reader | Standard; Background loading Off |
| DATA | One cooked 2048-byte logical track from the decoded ISO |
| Audio | No image-backed CDDA |
| Windows CE | Not admitted by this reader |
| Optional executable RAM patches | Off; the ZSO package has no patch capability |
| File allocation | At most 31 physical file fragments in the separate 32-slot map |

Selecting a ZSO image explicitly requests this experimental path. Unsupported
storage, reader or feature combinations must be refused before handoff. A
recognized container and successful preparation do not prove a particular
game will run. No broad compatibility or performance figure is available.

The physical `.zso` file includes its header, block index and compressed data.
Its fragment map covers that entire physical file; it is not a map of the
decoded ISO's logical bytes. The 32-slot manifest reserves one slot for the
logical track and leaves 31 for physical fragments. A file that exceeds this
fragment limit needs another supported layout before it can be launched.

The admitted decoder bounds are:

- Positive decoded length, an exact multiple of 2048 bytes, up to 2 GiB.
- Physical file length fitting an unsigned 32-bit byte count.
- ZSO alignment shift no greater than 9 and encoded block spans no greater
  than 4096 bytes, with only the admitted alignment padding.
- A fully checked index before the source is published for launch, followed
  by a rechecked index pair and bounded exact decode for each logical block.

The selected file remains read-only and must stay unchanged while the game
runs. Launch preparation and the resident reader do not rewrite, expand or
replace it. Do not alter or remove the card during play. These structural
bounds are admission limits, not recommended image sizes or speed promises.

## Separate package and audits

The dedicated build directory is `build/retail-zso`, containing one Standard
SCI resident, its high stage and its entry wrapper. Its outer relocation
header is **`KUIRZB01`**, while the private prepared map is **`KUIRZS01`**.
Ordinary native and CE packages retain their own contracts; a ZSO payload
cannot be substituted for either by changing its filename.

The wrapper uses the fixed native low reservation at `0x8c004000` through
`0x8c007800`, with the existing guarded service stack above it. Its high stage
is linked at `0x8ce00000`. Shipped packages contain a completely blank map and
no executable RAM-patch seed. Only the launcher fills the owner's checked
map before handoff.

`tools/zso_package.py` checks the runtime envelope, exact relocation tuple,
blank manifest and absence of patch capabilities. The dedicated
`tools/check_zso_loader.py` gate additionally checks all three linked ELFs,
resident and stage bounds, BSS and stack reservations, embedded byte identity,
the uncached relay trampoline, required codec/SCI entry points and the fixed
SCI Standard build profile. It requires final LTO stack reports, counts every
emitted frame conservatively and runs the linked integer-instruction audit.
The one initializer frame is charged to the existing high stack only after
linked wrapper/bootstrap bytes, its unique entry reference, the final call
graph and initialization-before-owner-boot call sites establish that domain.
Every shared helper remains charged to the resident service stack; neither
stack limit is enlarged. There is no audit-skip mode for this profile.

Generated host fixtures and mutation tests exercise these package and audit
gates. They do not supply retail images or establish console acceptance.
The codec and map contracts are defined in
[zso.h](../include/kui/zso.h) and
[retail_zso.h](../include/kui/retail_zso.h).

## Console acceptance still required

Before promoting this first slice, the complete matching package needs a
physical SCI test with an admitted native image: successful boot, a real
gameplay load, continued data reads and a return or recorded failure. Compare
the same title's known-working uncompressed image when assessing timing or
compatibility. Record the exact image variant, build and launch options.

Until that evidence exists, treat ZSO as experimental. The work does not
resolve native FMV stalls, WinCE music compatibility, ATA DMA or IDE console
validation, and does not change the release status of 2.0 Tyrfing.
