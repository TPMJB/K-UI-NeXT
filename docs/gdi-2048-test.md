# Create and select a separate 2048-byte GDI copy

K-UI 1.8.5 supports cooked 2048-byte GDI data tracks and can group an Original
and converted copy into one Games entry. **Keep the verified raw dump.** The
PC converter creates a new folder and never edits the original GDI or tracks.
It checks sector framing/addresses, not EDC/ECC or independent catalogue
agreement; its output is not a replacement archival dump.

## Convert one game

On a computer with Python 3, run from the source or release root:

```sh
python3 tools/gdi_optimize.py "/path/to/Games/Original game/disc.gdi" "/path/to/Games/Original game-2048"
```

The destination must not already exist. Its parent must exist, and the
destination must be outside the original dump folder. A sibling folder is
suitable. The converter publishes the destination only after all tracks and
the new GDI are complete; errors or Ctrl+C remove its temporary staging folder.
It refuses to overwrite files or folders. No additional Python packages are
required.

## Convert a collection

```sh
python3 tools/gdi_optimize.py --batch "/path/to/Games" --report "/path/to/Games-2048-report.json"
```

Choose a new report path outside the Games collection. The converter searches
nested folders and creates a sibling `Game-2048` folder for each supported
game containing exactly one GDI. `Games/Armada` stays unchanged and gains
`Games/Armada-2048`. Put each disc in its own folder.

Every conversion, skip and failure is reported, including reasons. Recognized
completed conversions are skipped on later runs without rechecking their
contents. Folders without a GDI, symbolic links and hidden folders are skipped.
Ambiguous multiple-GDI folders, existing-output collisions and unsupported
tracks are reported; other jobs continue. A batch containing failures exits
with status 1. Inspect the report when Evolution or another title is missing
instead of assuming that every folder was converted.

Ctrl+C keeps completed copies and removes the current incomplete staging
folder. Conversion is sequential, uses bounded memory and needs space for
additional data tracks and unchanged audio.

## What changes

- Every supported raw data sector must have valid sync, Mode 1 and the address
  declared by the GDI. The converter extracts its 2048-byte payload.
- 2352-byte audio and already-cooked 2048-byte data are copied unchanged.
- Track order, LBA starts, sector counts and gaps are preserved.
- Data outputs use `trackNN.iso`; audio outputs use `trackNN.raw`. The original
  GDI filename is retained.

Mode 2 conversion, nonzero file offsets, malformed/partial or overlapping
tracks, unsafe/ambiguous names and symbolic links are refused. The descriptor
is bounded to 32 KiB and 99 tracks; UTF-8 with an optional BOM, common line
endings and blank lines are supported. Unambiguous case-mismatched track names
resolve on case-sensitive hosts.

`conversion.json` records source/output byte counts, CRC32 and SHA-256 for each
track, the source GDI identity and preserved LBA ranges. Data hashes differ
from raw-track hashes; audio and already-cooked hashes should match. These
hashes identify processed bytes, not catalogue verification or game acceptance.

Data tracks shrink by 304 bytes per sector, about 12.9%; audio does not shrink.
That can reduce transfer work, but no universal loading-speed improvement is
promised. Linux, macOS and Windows support exclusive directory publication;
unsupported platforms/filesystems fail rather than overwrite a target.

## Select Original or 2048 on the console

Install the complete matching [1.8.5 runtime and Games payloads](release-v1.8.5.md).
Keep the compatible bootstrap CD and the card's filesystem. Conversion alone
does not update the console UI. Copy each converted folder alongside its
original under `/Games`, then use **X Refresh** if Games already cached that
collection.

Pairing uses sibling names `Game` and `Game-2048`. Each folder must contain
exactly one visible GDI, with matching track count, LBA starts, types and sector
counts; converted data tracks must be 2048 bytes. These are layout checks, not
whole-content verification. Missing, ambiguous or mismatched copies remain
ordinary entries. Pairing is bounded to 512 converted candidates per folder;
if its budget/allocation is exceeded, entries remain standalone.

Select the combined title, choose **Original** or **2048-byte copy** with the
D-pad, press **A** to inspect, then continue to launch confirmation. **B**
returns. Details and confirmation identify the selected copy. For a nested
dump such as `Armada/extracted`, the converter creates `extracted-2048`; open
Armada to find the combined `extracted` entry.

Four recent folder catalogues share at most 2048 cached entries. An individual
listing beyond 1024 rows remains browsable without a snapshot. Page navigation
reuses cached rows, and artwork loads for the selection only. Explicit Refresh
and storage/content changes invalidate caches; reboot rebuilds them. No files
are held open through these snapshots.

Games requesting complete raw sectors from a cooked data track cannot be
satisfied from payload-only data. Use Original for those titles. Audio stays
raw, but resident CDDA playback remains unsupported. Windows CE uses its
background SCI reader with **A**; the regular CE reader remains broken and
is no longer offered. Conversion alone does not establish CE compatibility.

## Compare loading and FMVs

Compare Original and 2048 with the same card, reader and game scene. Time
preparation before bootstrap separately from in-game loading: cooked and
non-GDI images receive an executable checksum pass, which can increase the
prelaunch delay. Shared BIN/CUE preparation can also take longer.

For a useful speed comparison, restart each time and measure the same load
three times, reporting the median and any stutter/audio differences. Test FMVs
separately. The reported repeating-audio stall/skip occurs in Original mode
too, so it has not been established as a conversion defect. Keep the original
for compatibility and reference verification. See
[formats and remaining FMV limits](games-formats.md#fmv-stalls-and-skips).
