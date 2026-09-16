# Hardware evidence

These results cover the uploaded logs, storage fixtures and user reports received on
2026-09-16. They establish the specific checks below on the user's console;
they do not complete the full dumping milestone.

## Capture runtime update awaiting console testing

The subsequent SD update implements full-track GDI capture, CRC32/SHA-256 saved
file reread, checkpointed controlled resume and an mstats action. Host FAT32 and
exFAT image tests exercise complete six-track output, independent Python hash
verification, early/middle/late/verification Stop, resumed-versus-uninterrupted
equality, a damaged newest checkpoint, an uncommitted suffix, bounded retries,
media-change and storage-failure handling, corrupt-prefix and wrong-disc refusal,
and preservation of existing completed jobs. These are synthetic host tests.

No full physical capture, hardware resume, independent disc-reference match or
on-console memory measurement is claimed yet. Use [the capture guide](capture-test.md)
with the existing bootstrap CD; the earlier physical evidence below applies to
the diagnostic builds identified there.

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

Disc title/region, console revision, video cable/display and adapter/card model
are not yet recorded. Complete capture, reference verification, and controlled
stop/resume remain later implementation and acceptance stages.
