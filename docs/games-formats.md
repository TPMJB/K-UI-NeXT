# Games image and ripper formats — K-UI 1.8.5

Games supports the direct-launch layouts below, the Original/2048 picker,
RAM-cached lists and selected-game artwork. Disc Ripper also offers multiple
output formats. Container support does not guarantee that every title boots;
see [release notes](release-v1.8.5-notes.md) for scoped hardware results and
remaining compatibility limits.

## Install and try

Merge the complete `KUI` folder from the [1.8.5 release](release-v1.8.5.md)
into the card root, replacing supplied files while keeping your preferences
and game dumps. Keep the compatible bootstrap disc. Runtime and game loader
packages must come from the same build: the current track map is version 3;
version 2 payloads cannot be mixed with it.

Put an image and all its referenced track files in a folder under `/Games`.
Folders with one image select it directly; folders with multiple images remain
browsable. GDI/CUE track payloads are filtered from the list. Standalone BIN/IMG
files are hidden in folders containing a CUE; use a separate folder for an
independent raw image. Discovery reads descriptor relationships without
opening or checking every backing track. Inspection and launch perform the
full file/layout validation. X refreshes the RAM snapshots after card or
collection changes.

## Games navigation

Rows publish before cover reads. Only the highlighted game's artwork loads;
changing the selection cancels obsolete artwork, and a result for an older
page cannot replace the current cover. List, compact and gallery retain their
layouts, with placeholders for unselected games. The ordinary storage worker
warms `/Games` after startup when foreground work and music startup are idle.

Four recently visited folders share a bounded RAM catalogue, and selected
covers have a separate bounded pixel cache. Warm pages and cached positive or
missing covers use no storage I/O. The previous page remains visible during a
new listing request. Explicit Refresh and operations that change card content
or storage invalidate both caches. RAM caches are rebuilt after reboot.

| Input | This build |
| --- | --- |
| GDI | Direct launch; raw 2352 or cooked 2048 data, raw audio, bounded file offsets |
| ISO | Direct launch of a complete Dreamcast data session with IP.BIN and ISO9660 metadata |
| BIN/IMG | Direct launch of a standalone 2352-byte data track; Mode 1 or Mode 2 Form 1 |
| BIN/CUE | Direct launch; shared or separate BINARY files, audio/data tracks, indices and gaps |
| CDI | Direct launch; DiscJuggler v2, v3 and v3.5 layouts |
| CSO | Import to ISO; CISO versions 1 and 2 |
| ZSO | Import to ISO; standard LZ4 or explicitly selected DreamShell LZO |
| CHD | Import CD/GD tracks through an installed MAME chdman |

ISO session addresses are recovered from its PVD and root directory rather
than guessed. Standalone ISO discovery requires the matching root record in
the first 64 physical sectors. A plain ISO without Dreamcast IP.BIN is not a
bootable game. Raw-track addresses come from the sector header. CUE supports
up to two sessions, `REM SESSION`, supported lead/gap directives and GD density
area markers. File references must be safe single-component names; WAV/MP3
CUE tracks and ambiguous DiscImageCreator shared-IMG session padding are
rejected. CDI pregaps and container byte offsets are mapped without extracting
the container or pretending its tracks start on 512-byte boundaries.

The data path handles 2048, 2336, 2352 and 2448-byte sectors, extracting 2048-byte
Mode 1/Mode 2 Form 1 payloads. Full raw reads are available from 2352/2448-byte sources;
2448-byte subchannels are omitted. Mode 2 Form 2, captured subchannel emulation,
CDDA playback, NRG, MDS/MDF, CCD and arbitrary archives are not implemented.
Compressed images do not decode inside the resident game reader.

## Disc-ripper output formats

Choose **Start > Capture settings > Output format**, save, then start New.
Resume and Verify keep the format recorded by that job, regardless of the
current preference. GDI remains the default.

| Output | What it contains |
| --- | --- |
| GDI | Existing raw 2352 tracks and GDI descriptor |
| BIN/CUE | The same raw tracks, with a CUE that describes density areas and declared gaps |
| CSO | Raw-DEFLATE compressed cooked 2048 high-density data-track export |
| ZSO | DreamShell's LZO dialect of ZSO, containing the same cooked data stream |
| CHD | CHDv4 with compressed captured mainchannel data and audio tracks |

Compressed outputs are created after full raw-file verification and checked
by decoding their payload against the capture. Raw tracks, checkpoints and
the internal `.capture.gdi` remain for Resume, Verify and reference hashes.
They are never deleted to save space. Keep enough free space for the raw
capture and the additional export. Compression therefore adds time and card
space during creation.

CSO/ZSO require one high-density data track; their file does not contain CDDA
or low-density tracks, which remain in the raw capture. CHD retains all tracks
captured by the existing `gdi-raw2352-typegap150-v1` profile. Its declared PAD gaps
and zero subchannel padding do not claim that missing pregaps or subchannels
were read from the disc. CHDv4 audio is stored in its canonical byte order;
CUE extraction can recover the captured audio bytes, but older chdman versions
may emit an unusable GD CUE descriptor. Use the import tool for Games.

Export cancellation keeps the raw capture. Resume recreates an interrupted
export. Verify checks the raw hashes and the published export without writing
either. A mismatching existing final export is preserved and reported.

These ripper exports do not add compressed boot support to Games. Use the
computer import tool below for K-UI. ZSO exports require `--zso-codec lzo`.
The resident reader still needs a separate compression/index and protected
memory design before it can read compressed sectors while a game runs.

Native controls retain A standard, X background 20, Y background 25. Native CD
confirmation uses L/R triggers to choose **Plain** or **Scrambled** executable
encoding. Default is Plain; a CUE `REM KUI SCRAMBLED 1` supplies a Scrambled
default. Both choices are explicit at launch, so Plain overrides that marker.
Choose Scrambled for an executable known to use the Dreamcast MIL-CD transform;
the container extension alone cannot identify that encoding. Commercial Katana
CD rips commonly retain a plain executable and even a GD-ROM IP media field.

Windows CE confirmation uses A for the background SCI reader. Its regular
reader is no longer offered. A CE map that exceeds the 64 background slots now
fails with an explanation instead of falling back to the broken regular path.
CE still requires SCI microSD and the existing CE boot layout.

The legacy selected-image read probe remains a raw, zero-offset GDI diagnostic.
Games of other formats launch through the new retail readers; their details do
not offer that legacy probe.

## Compressed image import

Run on a computer with Python 3. The destination must be a new folder outside
the source image's directory. Copy the entire resulting folder to `/Games`.

```sh
python3 tools/game_image_import.py "Images/Game.cso" "Ready/Game"
python3 tools/game_image_import.py "Images/Game.zso" "Ready/Game"
python3 tools/game_image_import.py "Images/Game.zso" "Ready/Game" --zso-codec lzo
python3 tools/game_image_import.py "Images/Game.chd" "Ready/Game" --chdman /path/to/chdman
```

DreamShell LZO ZSO requires an installed liblzo2; `--lzo-library PATH` selects
its shared library/DLL explicitly. Standard ZSO uses the included bounded LZ4
decoder. CSO/ZSO preserve the exact expanded ISO bytes. CHD uses chdman verify
before extractcd, choosing GDI for GD metadata and CUE for CD metadata. For
CHDv3/v4 GD images it also restores the extracted audio byte order. Use a
current chdman for multi-track CD extraction and session markers. Delta CHDs need `--parent`.
CHDs with stored or unknown subchannel data are rejected unless explicitly
allowed with `--drop-subchannels`; their export cannot preserve that data.

The importer writes `import-report.json` with source/output hashes, the launch
file and its limitations. It validates all exports and publishes the new folder
atomically without overwriting a destination. Originals remain unchanged.

## Batch 2048 conversion and skipped games

```sh
python3 tools/gdi_optimize.py --batch "/path/Games" --report "/path/Games-2048-report.json"
```

Choose a new report path outside the Games collection. The report records each
conversion, skip and failure, including paths/reasons and cancelled jobs. The
console repeats failures at the end. UTF-8/BOM descriptors, blank lines and
common line endings now normalize safely; unambiguous case-mismatched source
track names resolve on case-sensitive hosts. No-GDI image folders are explicitly
reported as skipped. This tool still refuses unsafe/ambiguous names, malformed
raw sectors, Mode 2 conversion, nonzero source offsets and overwrite collisions.
If Evolution or another title is skipped, the report names the reason;
its exact input/report is needed to distinguish unsupported geometry from a
malformed image. A successful batch does not establish game compatibility.

## FMV stalls and skips

K-UI contains no movie-specific skip logic. A game's read-error or playback
timeout can move it past a movie after a stall. Possible distinctions include
raw-sector requests that cannot be satisfied by a cooked 2048 track, synchronous
request pacing, storage retries, and unavailable disc audio. These are candidate
causes, not a diagnosis of the reported titles.

Owner feedback on 2026-10-06 confirms the skip also happens in Original mode:
roughly half a second of audio repeats for several seconds before the movie
is skipped. That makes cooked conversion unlikely as the common cause and
is consistent with a buffer no longer refilling while a read or completion
notification stalls. It does not establish whether the failure is physical
storage, request completion, or a timing/compatibility issue.

Compare the same scene on Original and 2048 with the same reader. Note title,
scene, selected version/reader and any return/error counters. A cooked-only
failure suggests a sector-format requirement; a failure in both copies needs
reader/transport or game compatibility investigation. Version 1.8.5 includes
no speculative movie timeout increase. The native placement correction addresses startup collisions; it does not establish a
fix for these FMV stalls.

## Verification and sources

Host tests use independent generated IP/ISO/track data, never commercial bytes.
FAT32/exFAT launch preparation compares both standard physical reads and the
background cursor with expected bytes, validates full IP/exact boot CRCs, checks
card hashes remain unchanged and runs filesystem checks. Parser tests cover
truncated/malformed CDI/CUE, shared offsets, session geometry and Form 2 refusal.
Resident slot size stays 12 bytes; the native link/layout/stack audits enforce
each reader's existing memory limits. Physical acceptance remains format- and
title-specific. The 1.8.5 release notes distinguish owner-tested launches from host validation.

Format references, independently implemented:

- [MAME CD/CUE geometry](https://github.com/mamedev/mame/blob/master/src/lib/util/cdrom.cpp)
- [Flycast CDI geometry](https://github.com/flyinghead/flycast/blob/master/core/imgread/cdi.cpp)
- [Flycast CDI footer fields](https://github.com/flyinghead/flycast/blob/master/core/deps/chdpsr/cdipsr.cpp)
- [KallistiOS scrambling tool](https://github.com/KallistiOS/KallistiOS/blob/master/utils/scramble/scramble.c)
- [CSO specification](https://github.com/unknownbrackets/maxcso/blob/master/README_CSO.md)
- [ZSO specification](https://github.com/unknownbrackets/maxcso/blob/master/README_ZSO.md)
- [MAME chdman](https://docs.mamedev.org/tools/chdman.html)
- [Historical DreamShell LZO reader](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/modules/isofs/ciso.c)

No current DreamShell loader code is imported. LZO tests use a temporary
historical miniLZO oracle outside the repository; that oracle is not shipped.
