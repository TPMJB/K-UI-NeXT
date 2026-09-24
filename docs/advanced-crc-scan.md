# Advanced CRC saved-file scan

This explicit Disc Ripper action reads a **completed K-UI raw-track job** or
**raw-2352 GDI folder** once. K-UI manifests provide the expected saved hashes.
With a supported manifest it checks every saved track against its recorded CRC32, also SHA-256 for older
SHA-enabled jobs. Each data sector receives Mode 1 sync, expected FAD, EDC,
reserved-byte and P/Q parity checks. Audio has no Mode 1 parity; its saved hash
is checked instead. Unsupported sector modes remain an issue, not a pass.
After that same read, the scanner compares every track's measured length and
CRC32 with `/KUI/redump.db` and `/KUI/tosec.db`. This reads only the small
catalogues; it does not read the track files again. A full independent catalogue
match includes the audio tracks and can verify a legacy GDI folder without a
K-UI manifest.

It does not reread the disc, repair sectors, create zero placeholders, alter
normal capture retries, or modify the accepted reader. Saved-manifest comparison
and independent TOSEC/Redump comparison are reported separately. Neither can hide
a detected data-sector problem or saved-hash mismatch. P/Q validation is not
general ECC reconstruction.

## Use

1. Open **Disc Ripper > Advanced > Advanced CRC scan**.
2. Browse into the existing game folder, then choose **Scan this folder**.
   Open the actual game folder containing the GDI and track files, not its
   `/Games` parent. No disc is required. The log now prints the selected folder
   before any metadata check and names missing/unreadable files and FatFs errors.
3. **B** stops the scan. The result distinguishes clean, issues, stopped and
   failed, and structural-only. Choosing the scan folder does not change the new-dump destination.
4. Read the summary or save the normal diagnostic log. The separate report is
   under `/KUI/recovery/scan-NNNN.txt`.

Two clearly separated modes are supported:

| Files in the selected folder | What the result establishes |
| --- | --- |
| Completed K-UI schema 1/2 manifest + matching GDI + tracks | Saved CRC32 (and older SHA-256) comparison plus data-sector checks. Present checkpoints must validate and agree. If both checkpoints were removed, the report explicitly says manifest-only. |
| Exactly one GDI + raw 2352-byte tracks, no manifest | Mode 1 checks plus a catalogue lookup using measured lengths and CRC32. A clean scan with **FULL TRACK MATCH** is clean, including audio CRC verification. Any partial, data-only, missing or unavailable catalogue result remains **STRUCTURAL ONLY**; audio is not fully verified. |

The independent reference result includes its catalogue and title. Missing
databases, catalogue errors and no matching entry are distinct results; none
means the disc is automatically bad. A track-boundary convention or a revision
absent from the database can prevent a match. Existing completed manifests can
still pass their saved-hash checks without a catalogue, as before. Cancellation
during catalogue comparison leaves an incomplete report, never a clean result.

The manifest parser refuses
unknown/duplicate fields, inconsistent track geometry, invalid byte counts,
unsafe filenames, malformed numbers and incomplete jobs. Present checkpoint hashes
and the generated GDI must agree before any track scan begins. This prevents a
malformed job from being reported as successfully checked. It does not make
self-authored metadata an independent reference. A malformed manifest never
silently falls back to structural-only mode.

Imported GDI supports LF/CRLF, quoted filenames containing spaces, consecutive
track numbers, zero file offsets, and safe ASCII filenames within the selected
folder (track names up to 127 bytes; descriptor names up to 100 bytes).
It refuses cooked 2048-byte tracks, nonzero offsets, duplicate/overlapping tracks,
path traversal, ambiguous multiple GDIs, truncated raw sectors and unsupported
geometry. Unsupported sector modes still produce issues. No reference value is
invented from the bytes being scanned.

Folder selection uses the existing browser's 127-byte normalized path limit.
A longer capture path cannot be selected by this version of the browser.

## Reports and original files

Original tracks, manifest, GDI and checkpoints are opened read-only. A scan
creates a new report with exclusive creation; previous reports are never
replaced. The report starts as `.part`, its header is synced before scanning,
and it becomes `.txt` only after its complete result has synced and closed and
its final rename has succeeded. A `.part` file is always incomplete, even if
it contains a COMPLETE line from an interrupted finalization. A `.txt` file
also needs the final COMPLETE line to represent a finished result.

A report lists per-track saved hashes, measured sizes, FAD bounds and track types,
the independent catalogue result, suspect data-sector FADs and flags,
unsupported sectors, audio/data counts and final mismatch counts. At most
4,096 individual suspect lines are written; summary counts still include all
sectors. The bounded report is diagnostic evidence, **not a durable complete
repair queue**. The separate salvage format will own any future patch targets.
Storage errors remain failures even when B is pressed at the same time.

The worker has one bounded heap allocation, a 16-sector input buffer and no
allocation per sector. It frees its scratch allocation on completion, failure
or cancellation. It uses the single existing storage worker, so it cannot run
alongside a capture.

## Short console acceptance without another full game scan

The SD package generates synthetic fixtures under `/KUI/tests/scan/`. They
contain generated data and audio patterns, no game data, and are not playable.
All three folders are small; no full-disc scan or new burn is needed to test the UI.

| Folder | Expected result |
| --- | --- |
| `clean` | CLEAN; 10 data + 4 audio sectors; no suspect sectors or hash mismatches. |
| `damaged` | ISSUES; 2 suspect data sectors, 0 unsupported sectors, 3 CRC mismatches. |
| `gdi-only` | STRUCTURAL ONLY; 10 data + 4 audio sectors, no suspect data sectors. The synthetic tracks have no catalogue entry, so audio remains unverified. |

The catalogue integration has FAT32/exFAT host coverage for a full match including
audio, audio/length mismatches, partial and absent entries, malformed catalogues,
cancellation, and references which match tracks with structural or saved-manifest
problems. A file-read counter proves catalogue lookup adds no second track read.
Hardware acceptance of the new lookup remains pending.

The damaged fixture changes P/Q at FAD 150, data payload at FAD 45152, and one
byte of the audio track. Audio damage appears as a track CRC mismatch; there
is no false audio-sector parity claim. The reports should name those two data
FADs. Run the clean fixture twice to confirm it produces two separate reports.
Stop during a longer scan when convenient; it must leave `.part` and preserve
all original files. Runtime `cc2320bb6d3a` passed the clean fixture, correctly
rejected the damaged fixture (two suspect sectors and three CRC mismatches),
and completed the GDI-only fixture plus the user's ARMADA folder. The older
runtime reported the latter two as structural-only because it did not yet use
the bundled independent catalogues. ARMADA's five reported CRCs and aggregate
data/audio lengths match the bundled TOSEC entry for
`Armada v1.000 (1999)(Metro3D)(US)[!]`. The full per-track size check is part of
the newly added automatic lookup, and the older report did not record each
size separately. This finding does not require another full scan to explain
the old result. [Hardware evidence](evidence/m15-app-round-five-2026-09-24.json).

To regenerate the fixtures from source:

```
python3 tools/make_scan_fixtures.py build/scan-fixtures
```

Keep generated fixture/dump folders outside version control.

## Automated checks

[The host result record](evidence/m15-advanced-crc-host-2026-09-23.json) records
the original 40 passing filesystem cases, fixture expectations and source SHA-256 hashes. The [round-five record](evidence/m15-advanced-crc-round-five-host-2026-09-23.json) adds named schema 1/2 jobs, absent checkpoints, exact missing-GDI errors, wrong parent folders, longest selectable folder, and imported GDI cases.

`tests/test_recovery_manifest.c` covers parser truncation, duplicate keys,
integer overflow, unsupported schemas, escaped NUL, unsafe paths, malformed
track geometry and required fields. The synthetic Mode 1 oracle generates
P/Q by polynomial division independently of the production checker.

`tests/test_recovery_scan_images.py` exercises real FAT32 and exFAT images:
clean/damaged/older SHA jobs, repeated reports, unsupported modes, malformed
manifest/GDI, valid fallback and conflicting/invalid checkpoints, truncated
tracks, pre-start and in-scan cancellation, read faults, simultaneous read
fault + Stop, report write/sync/final-sync/close/rename failures. Every case
compares the original files before and after. These tests establish app and
storage behavior; they are not new optical-reader benchmarks.

Catalogue integration adds ten cases per filesystem, for 84 passing image
cases total. The optional separate salvage worker is documented in
[salvage-worker.md](salvage-worker.md); the owner has deferred its console tests.
