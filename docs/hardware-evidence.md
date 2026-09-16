# Hardware evidence

These results cover the uploaded logs and storage fixtures received on
2026-09-16. They establish the specific checks below on the user's console;
they do not complete the full dumping milestone.

## First SD runtime session

Tested source: `addd439aaea504d765498f214a1e5409d5862fee`.
The [successful combined build](https://github.com/TPMJB/K-UI-NeXT/actions/runs/35147067592)
contains the CD bootstrap and SD runtime used for this session. Keep using that
disc; this evidence update does not require a new binary or another burn.

The uploaded `diagnostics(6).txt` starts with `K-UI SD runtime addd439aaea5`
and records `Running SD runtime build addd439aaea5`. This confirms execution
reached the separate SD program. Disc and storage operations then completed
from that runtime.

| Check | Observed result |
| --- | --- |
| SD execution handoff | Confirmed in the runtime log |
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

These are unconfirmed by the supplied SD runtime log. If they were already run,
record the observed outcome rather than asking for another burn or repeating
successful work.

1. Hold B at startup with a valid package present; confirm usable CD fallback.
2. Boot with the runtime file temporarily renamed; confirm usable fallback.
3. Try the five supplied rejection fixtures, then restore the good package and
   confirm the SD runtime starts again.
4. Confirm five successful power-off/power-on boots with controller input.

Follow the [one-disc test guide](sd-bootstrap.md) for the file changes. All
these checks use the existing bootstrap CD and accessible exFAT card. A second
known-good retail disc with a different TOC layout would broaden disc coverage.
FAT32 PC verification can follow when a reader is available.

Disc title/region, console revision, video cable/display and adapter/card model
are not yet recorded. Complete capture, reference verification, and controlled
stop/resume remain later implementation and acceptance stages.
