# Independent damaged-disc salvage

This explicit app is separate from normal capture. Its first hardware acceptance
is pending. The normal capture engine, GD-ROM adapter and command state machine
are unchanged. It uses their existing bounded raw-read callback, while the app's
single I/O worker continues to own optical and SD access.

Use **New salvage** only for an intentional recovery job. **Zero-fill holes**
starts OFF. OFF stops before committing a chunk containing a sector that still
fails its two individual attempts. ON first records each unresolved address
durably, then writes a zero placeholder and continues collecting good data.
A placeholder remains missing data. Drive fatal/reset requirements and storage
failures stop either mode; they never become zero-filled sectors.

**Resume latest salvage** continues an unfinished first pass. **Retry unresolved
sectors** is separate: it performs 1, 5, 10, 20 or 50 sweeps over holes, alternating
forward and backward direction. A data candidate must pass Mode 1 sync, exact
FAD, EDC, reserved bytes and P/Q. Audio repair needs two identical reads. Matching
audio reads are agreement evidence, not an independent reference match. Mode 2
repair and generic ECC reconstruction are unsupported.

Healthy first-pass batches are read once in groups of at most 32 sectors. Every
data sector gets Mode 1 checks. Only failed sectors receive two individual
attempts. This recovery path is deliberately distinct from the accepted normal
DMA pipeline: its per-commit durability and parity checks are not a new capture
performance setting. Use the normal ripper for healthy discs.

Jobs live in `/KUI/salvage/job-0001`, then increasing numbers. No ordinary capture
folder, checkpoint, manifest or descriptor is overwritten. Latest-job discovery
checks the full track plan plus two saved healthy high-density data-sector SHA-256 anchors.
The anchor search is bounded and happens before creating new files. A wrong or
unreadable anchor refuses resume; it does not weaken identity to the TOC alone.
Two anchors are disc binding, not a full catalogue identity proof.

The fixed target limit is 8,192 unresolved sectors (about 18.4 MiB of missing
payload). Hitting it stops without committing the next chunk. The worker's
bounded allocation is below 256 KiB, plus temporary final metadata. This avoids
an unbounded RAM queue on the 16 MiB console.

## Durable evidence

All integer fields are little endian. Metadata and records carry IEEE CRC32;
these detect accidental damage, not malicious modification.

| File | Role |
| --- | --- |
| `header.bin` | Immutable 4,096-byte versioned track plan, zero-fill consent and two content anchors. Written, synced, closed and compared on readback before capture starts. |
| `journal.bin` | 512-byte sequence/identity/checksum records for target declarations, committed chunks, immutable per-track baselines, first-pass readiness, completed repairs and final completion. Every append is synced and read back exactly. |
| `trackNN.bin` / `.raw` | Raw 2,352-byte sectors; first-pass placeholders are zero and stay explicitly unresolved in the journal. |
| `patch-NNNN.bin` | Immutable 8,192-byte record binding original zero bytes, accepted candidate bytes, candidate evidence type and checksums to the job and target. |
| `salvage.gdi`, `salvage.json` | Published only after every hole is resolved. The manifest explicitly requires a valid final COMPLETE journal and does not claim saved-file verification. |
| `*.part` | Unpublished interrupted output. It is never accepted as a verified patch or complete result. |

Target records are committed before placeholder writes. Track bytes are synced
before the associated progress commit. The baseline freezes the first-pass CRC,
including zeros; it is never rebuilt from patched data.

Before repair, the original target must still be zero. Original and candidate
bytes are published together only after sync, self-validation and exact
comparison with the optical candidate. The track sector is then written,
synced, read back and compared before its REPAIRED event is committed. On restart,
a published candidate makes an interrupted/torn patch idempotent: it is written
and verified again if necessary, and counted once. Already committed targets
must match their retained candidate bytes. Missing or corrupt published
backups stop recovery.

Only an incomplete final journal record may be discarded. Corruption earlier in
the journal or impossible state transitions stop. A tail containing target
records without their chunk commit is discarded, and its uncommitted track tail
is overwritten from the last committed sector. No successful result is produced
from malformed evidence. A stop between durable header publication and initial
journal creation can resume only if **no track file exists**.

CRC replacement computes final track CRCs from the immutable baseline and each
accepted replacement. It does not reread healthy sectors and therefore does not
independently establish their current saved bytes. A manual PC reread remains
available below. FatFs sync and these ordered records do not claim that arbitrary
card/controller failures are physically atomic.

## PC verification

Copy the complete salvage folder, including its journal and retained patches,
then run:

```
python3 tools/verify_salvage.py /path/to/job-0001
```

The verifier uses Python's independent CRC/SHA routines. It requires a valid
final COMPLETE journal, validates every retained patch and reads every saved
track byte. It also replaces target regions with zeros in an independent hash
stream to prove the unchanged bytes still match the original baseline. No
production CRC-replacement algebra is reused for that check. It prints SHA-256
for subsequent comparisons, but does not claim TOSEC/Redump matching.

Normal `verify_dump.py` handles normal captures; the salvage evidence has its own
format and uses `verify_salvage.py`.

## Tests and first hardware check

`make test-images` includes `tests/test_salvage_images.py`. Synthetic raw tracks
are generated independently using the existing polynomial-division parity oracle.
The fixture crosses 32-sector chunk boundaries. The tests use real FAT32 and
exFAT images with injected queue, commit, baseline, backup, patch, sync, readback,
publication and completion failures. They check restart reconciliation, wrong
discs, corrupt published evidence, audio disagreement, wrong data addresses,
invalid P/Q, stop, fatal reads and zero-fill OFF. Every recovered file and its
final CRC is compared with the original fixture. The PC verifier then accepts
that exported job and rejects changed healthy bytes, changed patched bytes,
self-consistently altered backups and incomplete journals.

The console smoke test should first be a short intentional start/stop/resume,
using the existing bootstrap disc and the new SD runtime. Keep normal dumps
separate. For actual damaged media, retain the salvage folder and logs; after
recovery, run the PC verifier and an independent catalogue comparison before
calling optical recovery hardware-accepted. It is not necessary to repeat the
accepted healthy-disc performance benchmarks.
