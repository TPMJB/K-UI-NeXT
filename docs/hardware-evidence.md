# Hardware evidence

These results cover the uploaded logs, storage fixtures and user reports received on
2026-09-16 and 2026-09-17. They establish the specific checks below on the user's console;
they do not complete the full dumping milestone.

## First completed capture: Sword of the Berserk

The user reports that **SWORD OF THE BERSERK GUTS RAGE** completed capture and
saved-file verification on SD runtime `2657a97031e3`. The uploaded manifest
identifies that build/title and declares `complete` and `saved_data_verified`.
This build publishes the final manifest/GDI only after the complete SD reread
passes. Both checkpoints and the GDI layout support the reported completion.
The uploaded raw bytes independently establish the checks for tracks 1 and 2
below; track 3 was intentionally omitted.

| Track | Sectors | Bytes | CRC32 | Independent PC result |
| --- | ---: | ---: | --- | --- |
| `track01.bin` | 776 | 1825152 | `bcec7767` | Length, CRC32 and SHA-256 match the manifest and both checkpoints |
| `track02.raw` | 526 | 1237152 | `0ff934e3` | Length, CRC32 and SHA-256 match the manifest and both checkpoints |
| `track03.bin` | 504150 | 1185760800 | `2cfb5dcb` | Not supplied; values are recorded by the console, not independently recomputed |

The three declared tracks total **1188823104 bytes**. Their bounds, sector sizes,
type-change gap exclusion and `disc.gdi` entries agree with the declared profile.
This verifies metadata consistency, not agreement with an independent disc catalog.

Both 4096-byte checkpoints pass independent Python CRC32 and structure checks,
including identity/build, reserved bytes, track bounds/order and alternating
sequence parity. `checkpoint-b.bin` is sequence 126 with 503808 track-3 sectors;
`checkpoint-a.bin` is sequence 127 with all 504150 track-3 sectors and hashes
matching the completed manifest. Both record **zero retries**. The earlier
track-3 prefix hash could not be checked without track 3. Valid checkpoints on
their own do not demonstrate a hardware Stop/reboot/Resume cycle.

`diagnostics(7).txt` is from **addd439aaea5**, not the capture build. Its bytes
are identical to the previously examined `diagnostics(6).txt`; it contains the
earlier nine-sample disc probe and storage probe, with no capture/mstats/timing
output. `storage(3).bin` and `storage(3).json` are also identical to the previous
storage uploads. Rechecking them passes all 2097325 expected bytes and CRC32
`a70f77ca`, but establishes no additional independent console session.

The user reports capture around 210-220 KiB/s. Earlier photos show capture near
209 KiB/s and saved-file verification near 311 KiB/s, with estimated main RAM
around 860 KiB and a sampled peak around 869 KiB. These are spot observations,
not whole-operation averages or detailed allocator measurements. The original
capture build has no timing instrumentation. Timing build `0ef58878ccdd` remains
the next SD-only measurement update; this evidence change adds no runtime code.

[Machine-readable results](evidence/sword-of-the-berserk-2026-09-17.json) retain
the original upload names, lengths, SHA-256 fingerprints, per-track checks and
decoded checkpoints. No game track bytes are stored in the repository.

Still needed: local PC verification of track 3, a compatible independent
reference comparison, controlled hardware Stop/reboot/Resume and timing results.
If the original capture log remains available, save/upload the report whose
header says `K-UI SD runtime 2657a97031e3`; do not repeat the full capture just
to replace a lost log. Keep the completed dump for later hash comparisons.

## Capture runtime host coverage

The SD capture runtime implements full-track GDI capture, CRC32/SHA-256 saved
file reread, checkpointed controlled resume and an mstats action. Host FAT32 and
exFAT image tests exercise complete six-track output, independent Python hash
verification, early/middle/late/verification Stop, resumed-versus-uninterrupted
equality, a damaged newest checkpoint, an uncommitted suffix, bounded retries,
media-change and storage-failure handling, corrupt-prefix and wrong-disc refusal,
and preservation of existing completed jobs. These are synthetic host tests.

The first physical completion is documented above; full PC track verification,
hardware resume and an independent disc-reference match remain pending. Use
[the capture guide](capture-test.md) with the existing bootstrap CD; the earlier
physical evidence below applies to the diagnostic builds identified there.

## SD runtime session and confirmed startup selection

Tested source: `addd439aaea504d765498f214a1e5409d5862fee`.
The [successful combined build](https://github.com/TPMJB/K-UI-NeXT/actions/runs/35147067592)
contains the CD bootstrap and SD runtime for this build. Keep using the existing
disc; this evidence update does not require a new binary or another burn.

The uploaded `diagnostics(6).txt` starts with `K-UI SD runtime addd439aaea5`
and records `Running SD runtime build addd439aaea5`, with successful disc and
storage operations. After initially reporting a CD bootstrap heading, the user
clarified that holding B selects CD bootstrap and booting without touching any
buttons reaches SD runtime. This resolves the apparent discrepancy and confirms
the normal SD handoff and manual fallback selection.

Source inspection shows both the title and saved report header use the same
compile-time role. The CD shows its own role during startup even on a successful
load; it remains there when loading is skipped or fails. The user also reports
that cold boots work. No count was supplied, so this is recorded as a successful
user-reported check rather than a documented five-boot sequence.

The user then completed the three-step missing-file/checksum/restoration check
on the same disc. With the runtime renamed, the console reported that
`runtime.kui` failed or was missing (wording recalled approximately). The
`bad-checksum.kui` test produced `Runtime Checksum mismatch`. Renaming the good
file back restored normal operation. These are user-reported hardware passes
for missing-file detection, payload-checksum rejection and good-runtime
restoration. No new log or screenshot was supplied, and diagnostic probes or
controller responsiveness during automatic fallback were not separately reported.

| Check | Observed result |
| --- | --- |
| SD execution handoff | Confirmed by the runtime log and the user's no-buttons startup report |
| Hold B at startup | User confirms CD bootstrap selection; fallback probe results were not separately supplied |
| Missing runtime file | User-reported pass: failed/missing-file explanation shown |
| Bad payload checksum | User-reported pass: `Runtime Checksum mismatch` shown |
| Restore good runtime | User-reported pass: normal operation returns after renaming the original file back |
| Cold boots | User reports they work; count unspecified |
| Video mode | 640x480 NTSC interlaced, buffered |
| Disc TOCs | Both density regions parsed; three tracks |
| Raw disc reads | All nine samples passed repeat and buffer-guard checks |
| Data-sector comparison | All six data samples matched independent cooked reads |
| Audio samples | Three passed repeat/guard checks; offset and subchannels were not validated |
| Storage layout | exFAT, MBR volume start 2048, 249997312 volume sectors, 131072-byte clusters |
| Console storage probe | `/KUI/probes/p0009`, 2097325 bytes; write, remount and reread passed |
| PC storage verification | Exact length, every expected byte and CRC32 `a70f77ca` passed |

The initial `CMD 24 FAILED` with sense `6/40` was followed by successful
initialization and the complete disc probe. The TOCs, sample addresses and
sample CRC32 values match the prior session; this does not establish a second
disc layout or independent full-disc accuracy.

The PC check ran `tools/verify_probe.py` on temporary copies of the uploaded
`storage(2).bin` and `storage(2).json` using the standard fixture filenames.
The original uploads were unchanged. The storage pattern is deterministic,
so its hash matching previous successful fixtures is expected.

| Uploaded file | Bytes | SHA-256 |
| --- | ---: | --- |
| `diagnostics(6).txt` | 2514 | `d2a93a39de88d08c3395d40c6031e8cbb29427c4d7301eec5cb029303d5310b3` |
| `storage(2).bin` | 2097325 | `e19146d42e4161db7890e39427fc5ccb95ef1da2a97b910c224d03d8cda6daff` |
| `storage(2).json` | 115 | `618b9b5553f0932666bcf0363e235ba7d5c1884974106cc353afd886f4b44849` |

## Earlier evidence

- `99a069777c7e`: both TOCs, nine raw samples, six data comparisons and the
  exFAT console/PC storage checks passed. The first five uploaded diagnostic
  files are progressively longer snapshots of one session, not five boots.
- `cf8210bc5442`: the user explicitly confirmed the rendering fix removed the
  visual errors. Disc samples and exFAT console/PC checks passed again.
- The user reported a FAT32 console pass on a 32 GB card. Its log and fixture
  remain unavailable because they lack a compatible reader. FAT32 SD runtime
  loading has not been established on hardware.

## Remaining checks on the same bootstrap disc

These are unconfirmed by the supplied logs and user reports. If they were already run,
record the observed outcome rather than asking for another burn or repeating
successful work.

1. The other four supplied rejection fixtures (`bad-magic.kui`, `truncated.kui`,
   `oversized.kui` and `wrong-version.kui`) still lack hardware results. Their
   host checks pass. Record fallback usability when testing them; missing-file,
   bad-checksum and restoration checks need not be repeated.
2. Confirm the cold-boot count and controller response for the planned five-boot
   check. Existing successful boots count; do not repeat them just for a log.

Follow the [one-disc test guide](sd-bootstrap.md) for the file changes. All
these checks use the existing bootstrap CD and accessible exFAT card. A second
known-good retail disc with a different TOC layout would broaden disc coverage.
FAT32 PC verification can follow when a reader is available.

The capture title is now recorded above. Its region, console revision, video
cable/display and adapter/card model are not yet recorded. Independent full-dump
verification and controlled stop/resume remain hardware acceptance checks.
