# Advanced CRC saved-file scan

This explicit Disc Ripper action reads a **completed K-UI raw-track job** once.
It checks every saved track against its recorded CRC32, also SHA-256 for older
SHA-enabled jobs. Each data sector receives Mode 1 sync, expected FAD, EDC,
reserved-byte and P/Q parity checks. Audio has no Mode 1 parity; its saved hash
is checked instead. Unsupported sector modes remain an issue, not a pass.

It does not reread the disc, repair sectors, create zero placeholders, alter
normal capture retries, or modify the accepted reader. It does not perform a
catalogue lookup. A clean saved-file scan and an independent TOSEC/Redump match
are different evidence. P/Q validation is not general ECC reconstruction.

## Use

1. Open **Disc Ripper > Advanced > Advanced CRC scan**.
2. Browse into the existing game folder, then choose **Scan this folder**.
   The folder must contain its `manifest.json`, generated GDI, valid checkpoint
   and complete `trackNN.bin`/`trackNN.raw` files. No disc is required.
3. **B** stops the scan. The result distinguishes clean, issues, stopped and
   failed. Choosing the scan folder does not change the new-dump destination.
4. Read the summary or save the normal diagnostic log. The separate report is
   under `/KUI/recovery/scan-NNNN.txt`.

Only supported schema 1/2 manifests from K-UI are accepted. The parser refuses
unknown/duplicate fields, inconsistent track geometry, invalid byte counts,
unsafe filenames, malformed numbers and incomplete jobs. The checkpoint hashes
and generated GDI must agree before any track scan begins. This prevents a
malformed job from being reported as successfully checked. It does not make
self-authored metadata an independent reference.

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

A report lists per-track saved hashes, suspect data-sector FADs and flags,
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
Both folders are small; no full-disc scan or new burn is needed to test the UI.

| Folder | Expected result |
| --- | --- |
| `clean` | CLEAN; 10 data + 4 audio sectors; no suspect sectors or hash mismatches. |
| `damaged` | ISSUES; 2 suspect data sectors, 0 unsupported sectors, 3 CRC mismatches. |

The damaged fixture changes P/Q at FAD 150, data payload at FAD 45152, and one
byte of the audio track. Audio damage appears as a track CRC mismatch; there
is no false audio-sector parity claim. The reports should name those two data
FADs. Run the clean fixture twice to confirm it produces two separate reports.
Stop during a longer scan when convenient; it must leave `.part` and preserve
all original files. Console acceptance of these new app paths is pending.

To regenerate the fixtures from source:

```
python3 tools/make_scan_fixtures.py build/scan-fixtures
```

Keep generated fixture/dump folders outside version control.

## Automated checks

[The host result record](evidence/m15-advanced-crc-host-2026-09-23.json) records
the 40 passing filesystem cases, fixture expectations and source SHA-256 hashes.

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

The hole-aware first pass, durable unresolved map, immutable baseline,
per-patch backups/readback and bounded targeted recovery passes remain the next
salvage implementation stage in [salvage-plan.md](salvage-plan.md).
