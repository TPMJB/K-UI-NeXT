# Games disc formats test

This development build extends the Games browser, metadata reader, covers and
detached game readers. It retains the Original/2048 picker and cached paging.
It is not a final 1.7 release or a promise that every game boots.

## Install and try

Copy the complete `KUI` folder from the SD update onto the card, replacing its
files. Keep the existing bootstrap disc. Runtime and game loader packages must
come from this same build: the new track map is version3, not version2.

Put an image and all its referenced track files in a folder under `/Games`.
Folders with one image select it directly; folders with multiple images remain
browsable. GDI/CUE track payloads are filtered from the list. Standalone BIN/IMG
files are hidden in folders containing a CUE; use a separate folder for an
independent raw image. The first collection scan still validates candidate
Original/2048 pairs; subsequent pages reuse its snapshot. X refreshes it.

| Input | This build |
| --- | --- |
| GDI | Direct launch; raw2352 or cooked2048 data, raw audio, bounded file offsets |
| ISO | Direct launch of a complete Dreamcast data session with IP.BIN and ISO9660 metadata |
| BIN/IMG | Direct launch of a standalone2352-byte data track; Mode1 or Mode2 Form1 |
| BIN/CUE | Direct launch; shared or separate BINARY files, audio/data tracks, indices and gaps |
| CDI | Direct launch; DiscJuggler v2, v3 and v3.5 layouts |
| CSO | Import to ISO; CISO versions1 and2 |
| ZSO | Import to ISO; standard LZ4 or explicitly selected DreamShell LZO |
| CHD | Import CD/GD tracks through an installed MAME chdman |

ISO session addresses are recovered from its PVD and root directory rather
than guessed. Standalone ISO discovery requires the matching root record in
the first64 physical sectors. A plain ISO without Dreamcast IP.BIN is not a
bootable game. Raw-track addresses come from the sector header. CUE supports
up to two sessions, `REM SESSION`, supported lead/gap directives and GD density
area markers. File references must be safe single-component names; WAV/MP3
CUE tracks and ambiguous DiscImageCreator shared-IMG session padding are
rejected. CDI pregaps and container byte offsets are mapped without extracting
the container or pretending its tracks start on512-byte boundaries.

The data path handles2048,2336,2352 and2448-byte sectors, extracting2048-byte
Mode1/Mode2 Form1 payloads. Full raw reads are available from2352/2448 sources;
2448-byte subchannels are omitted. Mode2 Form2, captured subchannel emulation,
CDDA playback, NRG, MDS/MDF, CCD and arbitrary archives are not implemented.
Compressed images do not decode inside the resident game reader.

Native controls retain A standard, X background20, Y background25. Native CD
confirmation adds Left/Right to choose **Plain** or **Scrambled** executable
encoding. Default is Plain; a CUE `REM KUI SCRAMBLED 1` supplies a Scrambled
default. Both choices are explicit at launch, so Plain overrides that marker.
Choose Scrambled for an executable known to use the Dreamcast MIL-CD transform;
the container extension alone cannot identify that encoding. Commercial Katana
CD rips commonly retain a plain executable and even a GD-ROM IP media field.

Windows CE confirmation uses A for the background SCI reader. Its regular
reader is no longer offered. A CE map that exceeds the64 background slots now
fails with an explanation instead of falling back to the broken regular path.
CE still requires SCI microSD and the existing CE boot layout.

The legacy selected-image read probe remains a raw, zero-offset GDI diagnostic.
Games of other formats launch through the new retail readers; their details do
not offer that legacy probe.

## Compressed image import

Run on a computer with Python3. The destination must be a new folder outside
the source image's directory. Copy the entire resulting folder to `/Games`.

```sh
python3 game_image_import.py "Images/Game.cso" "Ready/Game"
python3 game_image_import.py "Images/Game.zso" "Ready/Game"
python3 game_image_import.py "Images/Game.zso" "Ready/Game" --zso-codec lzo
python3 game_image_import.py "Images/Game.chd" "Ready/Game" --chdman /path/to/chdman
```

DreamShell LZO ZSO requires an installed liblzo2; `--lzo-library PATH` selects
its shared library/DLL explicitly. Standard ZSO uses the included bounded LZ4
decoder. CSO/ZSO preserve the exact expanded ISO bytes. CHD uses chdman verify
before extractcd, choosing GDI for GD metadata and CUE for CD metadata. Use a
current chdman that preserves session markers. Delta CHDs need `--parent`.
CHDs with stored or unknown subchannel data are rejected unless explicitly
allowed with `--drop-subchannels`; their export cannot preserve that data.

The importer writes `import-report.json` with source/output hashes, the launch
file and its limitations. It validates all exports and publishes the new folder
atomically without overwriting a destination. Originals remain unchanged.

## Batch2048 conversion and skipped games

```sh
python3 gdi_optimize.py --batch "/path/Games" --report "/path/Games-2048-report.json"
```

Choose a new report path outside the Games collection. The report records each
conversion, skip and failure, including paths/reasons and cancelled jobs. The
console repeats failures at the end. UTF-8/BOM descriptors, blank lines and
common line endings now normalize safely; unambiguous case-mismatched source
track names resolve on case-sensitive hosts. No-GDI image folders are explicitly
reported as skipped. This tool still refuses unsafe/ambiguous names, malformed
raw sectors, Mode2 conversion, nonzero source offsets and overwrite collisions.
The Evolution failures cannot be identified without their actual report/input.

## FMV stalls and skips

K-UI contains no movie-specific skip logic. A game's read-error or playback
timeout can move it past a movie after a stall. Possible distinctions include
raw-sector requests that cannot be satisfied by a cooked2048 track, synchronous
request pacing, storage retries, and unavailable disc audio. These are candidate
causes, not a diagnosis of the reported titles.

Owner feedback on2026-10-06 confirms the skip also happens in Original mode:
roughly half a second of audio repeats for several seconds before the movie
is skipped. That makes cooked conversion unlikely as the common cause and
is consistent with a buffer no longer refilling while a read or completion
notification stalls. It does not establish whether the failure is physical
storage, request completion, or a timing/compatibility issue.

Compare the same scene on Original and2048 with the same reader. Note title,
scene, selected version/reader and any return/error counters. A cooked-only
failure suggests a sector-format requirement; a failure in both copies needs
reader/transport or game compatibility investigation. No driver timing changes
or speculative timeout increase are included in this format build.

## Verification and sources

Host tests use independent generated IP/ISO/track data, never commercial bytes.
FAT32/exFAT launch preparation compares both standard physical reads and the
background cursor with expected bytes, validates full IP/exact boot CRCs, checks
card hashes remain unchanged and runs filesystem checks. Parser tests cover
truncated/malformed CDI/CUE, shared offsets, session geometry and Form2 refusal.
Resident slot size stays12 bytes; the native link/layout/stack audits enforce
each reader's existing memory limits. Physical hardware tests are still needed.

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
