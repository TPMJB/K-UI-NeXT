<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native ZSO: experimental SCI Standard reader

Separate native residents decode **standard LZ4 ZSO** and explicitly selected
**historical LZO1X ZSO** images. It is an opt-in development candidate for console testing, not a
published K-UI 2.0 release or a claim that compressed games are now generally
compatible. Ordinary uncompressed images keep their existing reader path.

## Which ZSO format

Standard mode admits **ZISO version 1 with independent 2048-byte LZ4 blocks**.
Historical mode admits **ZISO version 0 or 1 with independent 2048-byte LZO1X
blocks**, including K-UI Disc Ripper exports. Each logical block is stored raw
or decoded using the one explicitly selected codec. No frame, dependent
dictionary, codec guessing or failure-triggered fallback is used.

K-UI's existing Disc Ripper **ZSO export uses the historical DreamShell-LZO
dialect**. Both dialects can use `.zso` and the `ZISO` magic, so the extension
and header do not reliably identify their compression algorithm. The launcher must select historical LZO1X before inspecting or preparing an
LZO file; the default remains standard LZ4. In Games, use Options > ZSO codec,
or Y after a failed inspection, then A to save and reinspect. The exact image
choice is loaded before metadata reads. Selecting the wrong codec refuses
malformed decoded blocks rather than switching decoders. Raw-only blocks
cannot identify a codec. Do not rename another container to `.zso`.

Use a known codec and a complete Dreamcast data-session ISO, and retain the
original image. The existing
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

Selecting a ZSO image requests the selected codec's experimental path. Unsupported
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

Each dedicated directory contains one Standard SCI resident, its high stage
and entry wrapper:

| Codec | Build directory | Installed package | Outer header | Prepared map |
| --- | --- | --- | --- | --- |
| LZ4 | `build/retail-zso` | `zso-boot.kui` | `KUIRZB01` | `KUIRZS01` |
| LZO1X | `build/retail-lzo` | `lzo-boot.kui` | `KUIRLB01` | `KUIRLS01` |

The LZ4 version1 wire ABI is unchanged. Each detached binary admits only its
own codec and rejects a map or package typed for the other.
Ordinary native and CE packages retain their own contracts; a ZSO payload
cannot be substituted for either by changing its filename.

The wrapper uses the fixed native low reservation at `0x8c004000` through
`0x8c007800`, with the existing guarded service stack above it. Its high stage
is linked at `0x8ce00000`. Shipped packages contain a completely blank map and
no executable RAM-patch seed. Only the launcher fills the owner's checked
map before handoff.

`tools/zso_package.py` (explicit `--codec lzo` for the LZO package) checks the runtime envelope, exact relocation tuple,
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

Both decoders stream through the existing 512-byte physical-sector cache
directly into the caller's 2048-byte output; there is no compressed-input
buffer, heap or borrowed game RAM. LZO validates every literal/match length,
lookbehind and the canonical `11 00 00` hexadecimal end marker. The bundled
public miniLZO compressor/safe decoder is a host test oracle, never linked
into the detached resident.

At the pinned SH-4 toolchain, the first LZO reader ends at `0x8c007688`, leaving
376 bytes in the existing reservation. Its conservative runtime stack sum is
1216 of 1232 bytes; LZ4 remains 1208. These are build measurements, not console
speed results. Compression reduces card bytes but adds decoding and index
reads; compare the same image/card against uncompressed ISO to measure the
net loading or gameplay-stall cost. No SH-4 throughput promise is made.

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
