# Repair copies and saved-sector reread evidence

These are separate, explicit workflows. Advanced CRC remains read-only and
ordinary Resume does not repair saved tracks. No DreamShell implementation
code is used.

## Repair to a new copy

The worker accepts a completed normal K-UI raw-track job or an imported raw
2352-byte GDI folder. Imported filenames must be safe single components, file
offsets must be zero, and the track file lengths must match the inserted disc's
prepared capture ranges exactly. Ambiguous folders, traversal, truncated files,
overlaps and incompatible layouts fail before a repair folder is created.

Normal completed jobs require the same optical content identity stored by
capture: the complete track layout and the start/middle/end raw samples of
every track. Imported jobs have no stored optical identity. They require two
distinct healthy high-density data anchors, chosen with bounded alternatives
near the ends of game data. Both must match the inserted disc byte for byte.
The shared low-density warning/data tracks are never used as imported identity
anchors. Identity checks are not a catalogue match.

Every original file is opened read-only. Output goes into a newly and
exclusively created `/KUI/repair/job-NNNN`. Each supported Mode 1 suspect sector
gets two independent saved-file open/seek/read/close passes. A difference from
the initial sample blocks optical repair and preserves the evidence.

A replacement requires two byte-identical optical reads, each passing sync,
expected FAD, Mode 1 EDC, reserved-byte and P/Q checks. Fatal/reset-required
results stop immediately. The selected 1/5/10/20/50 attempt limit counts
candidate pairs per sector; it does not change normal acquisition retries.
Original and candidate bytes are written, synced and read back in the binary
evidence file before candidate bytes reach the copy. Up to 8192 suspects can be
handled in one copy operation.

All tracks, including unchanged audio, are copied and hashed with CRC32 and
SHA-256. Every saved copy byte is then reread and compared with both digests.
When a source manifest provides expected track hashes, those must also match
the final tracks. A whole-track hash cannot identify an individual corrupt
audio sector or a structurally valid but incorrect data sector. Those
mismatches remain unresolved. Mode 2 is unsupported for optical repair.

The final GDI is published only after all tracks pass their complete readback,
all available source hashes match, and evidence files close successfully. The
folder also contains `copy.txt` with track sizes and final CRC32/SHA-256 values,
`samples.bin` and `evidence.txt`. An imported copy can have unverified audio;
that qualification is recorded and displayed. Completion is not independent
archival verification or a Redump/TOSEC match.

Stopped, failed or unresolved operations retain a partial copy and
`evidence.part`, without a published GDI. A subsequent attempt creates another
unique folder; partial repairs are not silently resumed or overwritten. An
error during the final descriptor publication can leave completed data and
evidence beside `disc.part`. Original files remain unchanged in every case.
FatFs sync/close cannot guarantee physical power-loss atomicity inside a card.

## Saved-sector rereads

This storage-only diagnostic needs no inserted disc. It scans the saved raw
data tracks for structural Mode 1 issues. At most 64 suspect sectors receive
two independent reopen/read/close passes in addition to the initial sample.
The full scan still counts later suspects and records when the sample limit
was reached. Audio and whole-track hash mismatches cannot be localized by
this workflow; use Advanced CRC for saved hashes and catalogue comparison.

Each run creates a separate `/KUI/recovery/readback-NNNN` containing
`samples.bin` and a completed `evidence.txt`, or incomplete `evidence.part`.
The log records byte difference counts, first differing offsets and CRC32 for
each pass. All three raw samples are retained, including disagreements.
Agreement means the sampled reads were stable, not that the original defects
are repaired or that the storage device is fully verified.

## Binary evidence v1

All integers are little-endian. A record has a 64-byte header followed by
exactly three raw 2352-byte samples. Records append sequentially. Header and
payload CRCs detect torn/truncated or changed records; a partial tail is never
evidence of a completed operation.

| Header offset | Meaning |
| --- | --- |
| 0 | Eight bytes `KUIRDB1` plus NUL for saved rereads, or `KUIRPT1` plus NUL for an optical candidate |
| 8 | Version, 1 |
| 12 | Record sequence, starting at 1 |
| 16 | One-based track number |
| 20 | FAD |
| 24 | Zero-based raw sector within the saved track |
| 28 | Structural flags for a reread; zero for an accepted optical candidate |
| 32 | Sample count, 3 |
| 36, 40, 44 | CRC32 of each sample |
| 48 | CRC32 of all three concatenated samples |
| 52–59 | Reserved zero |
| 60 | CRC32 of the preceding 60 header bytes |

Reread records contain initial saved bytes and the two reopened reads.
Candidate records contain the original saved bytes and both identical,
validated optical reads. Appending and syncing a candidate record is followed
by exact saved-byte readback before any candidate is used in the new track.

Host validation: `make test-repair-copy` runs the real FatFs implementation
against FAT32 and exFAT images with synthetic sectors and ASan/UBSan. It checks
unchanged originals, valid and wrong-disc identities, optical disagreement,
unlocalizable hashes, bounded forensic sampling, cancellation, write/sync/
readback/close failures and final publication failures. Console acceptance is
still required.
