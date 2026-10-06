# Create a separate 2048-byte GDI test copy

Keep the verified raw dump. This experimental conversion creates a new folder;
it never edits the original GDI or track files. It does not verify EDC/ECC or
match a disc catalogue, and its output is not a replacement archival dump.

On your computer, with Python 3 and no additional packages:

```sh
python3 tools/gdi_optimize.py "/path/to/Games/Original game/disc.gdi" "/path/to/Games/Original game-2048"
```

The destination must not already exist. Its parent folder must exist, and the
destination must be outside the original dump folder. Use a sibling folder as
shown above. The tool publishes the destination only after every track and the
new GDI are complete; a normal error or Ctrl+C removes its temporary staging
folder. It refuses to overwrite existing files or folders.

## Convert a collection

To convert all game folders beneath a collection directory, run:

```sh
python3 tools/gdi_optimize.py --batch "/path/to/Games"
```

The converter searches nested folders and creates a sibling `Game-2048` folder
for each game with exactly one GDI. `Games/Armada` stays unchanged and gains
`Games/Armada-2048`. Keep both on the card. The selector test build groups
matching sibling folders into one Games entry: select **Armada**, then choose
**Original** or **2048-byte copy** before the usual inspection and launch flow.
Older builds list the folders separately. The experimental 2048 runtime and
its matching apps are still required.

Completed conversion folders are excluded from later searches. A rerun leaves
recognized copies unchanged and does not reverify their contents. Other output
collisions and folders containing multiple GDIs are reported as failures;
remaining games continue. Put each disc in its own folder for batch conversion.
Symbolic links and hidden folders are skipped. The summary lists converted,
skipped and failed jobs; a batch with failures exits with status 1.

Ctrl+C keeps already completed copies and removes the current incomplete staging
folder. Conversion remains sequential, with bounded memory, and needs enough
free space for the additional data tracks and unchanged audio.

The converter:

- Checks every raw data sector's sync, Mode 1 marker and address against the GDI.
- Extracts the 2048-byte payload from each supported 2352-byte data sector.
- Copies 2352-byte audio unchanged, and copies already-2048-byte data unchanged.
- Preserves track order, LBA starts, sector counts and gaps in the new GDI.
- Writes `trackNN.iso` for data and `trackNN.raw` for audio, avoiding source-name
  collisions. The original GDI filename is retained.

Mode 2 sectors, nonzero file offsets, malformed/partial tracks, overlapping
tracks and symbolic links are refused. Inputs are bounded to 99 tracks and a
32 KiB ASCII GDI descriptor. Runtime memory stays bounded while processing
large tracks. Linux, macOS and Windows support exclusive directory publication;
unsupported platforms/filesystems fail safely rather than overwrite a target.

`conversion.json` records source and output byte counts, CRC32 and SHA-256
separately for each track, plus the source GDI identity and preserved LBA ranges.
Converted data hashes will differ from the original raw-track hashes. Audio
and already-cooked tracks should have matching source/output hashes. These
hashes identify the bytes processed; they do not assert that the source was
previously catalogue-verified or that a game has been tested successfully.

Data tracks shrink by 304 bytes per sector, about 12.9%. Audio does not shrink.
Launch testing requires the matching experimental K-UI loader; public 1.7 does
not support these cooked backing tracks. On-console conversion and an automatic
ripper conversion option are not part of this first test.

## Install and compare on the console

The current package's console header reads **K-UI 2048 picker**. A ZIP
containing only the Python converter does not update the console UI.

Pairing uses the folder names `Game` and `Game-2048` in the same parent. Each
must contain exactly one visible GDI, with matching track count, LBA starts,
types and sector counts. The converted data tracks must be 2048-byte tracks.
These layout checks are not a full content verification. If one copy is absent,
ambiguous or mismatched, it remains accessible as an ordinary folder/image.
The pairing index is bounded to 512 candidate converted folders per directory;
if that budget or its memory allocation is exceeded, all entries remain
standalone for that listing.
Selecting a pair opens the version picker; **D-pad** selects, **A** inspects,
and **B** returns. The detail and launch screens identify the selected copy.
If your dump is nested under `Armada/extracted`, batch conversion creates
`Armada/extracted-2048`: open Armada to reach the grouped `extracted` entry.

This is a hardware test build based on 1.7, retaining its accepted SCI reader
fix. It has not yet demonstrated retail-disc loading speed. Back up your
current `KUI` folder, then install this test's `KUI/runtime.kui` and matching
`KUI/apps` together. Keep your existing compatible boot CD and your card's
filesystem. Restore the saved folder to return to public 1.7.

Copy the converted folder alongside the original under `Games`. Open its GDI
through the normal Games launch flow. The map log should show `2048-byte
sectors` for converted data. Audio remains raw; existing K-UI CD audio playback
limitations still apply. Games that request complete 2352-byte data sectors
from a cooked track are refused by this prototype; keep the original raw copy
for those games. Conversion alone does not establish Windows CE compatibility.

Use three versions of the same game: raw GDI on this test build, converted GDI
on this test build, and its original retail disc through the normal disc boot.
Keep the card, storage connection, reader mode, clock and game settings the
same for both GDI tests. First use the standard reader. Once that comparison
works, repeat with the background SCI reader as a separate comparison.

For each version, restart the console and time the same loading event three
times. For example, use the same DOA2 characters and stage, timing from the
confirmed selection to the first playable frame. Record the three times and
their median. A phone recording is useful for identifying those frames and
checking FMV audio/video sync. Use the same capture method for all versions.
Test an FMV separately, and then your Windows CE title with the existing CE
boot test flow if it already works with a raw image.

Measure initial K-UI launch time separately: cooked executables receive an
extra preparation checksum pass because their raw address headers were
removed. That pass affects launch time but is not part of in-game loading.
Record failures, stutter and sync changes alongside timings. The useful result
is whether converted in-game loads approach the disc, with acceptable playback.
