# Independent damaged-disc salvage

This explicit app is separate from normal capture. Console acceptance of this
expanded candidate is pending. It uses the existing bounded raw-read callback;
the GD-ROM adapter and command state machine retain their ownership/deadline
contracts. The app's single I/O worker owns optical and SD access. Normal
capture retains its DMA pipeline and has separate integrity-check improvements.

Salvage uses a dedicated PIO policy: up to 30 seconds for a read, then up to
5 seconds to confirm an abort has released the command. Normal capture retains
its 5-second read and 1-second abort windows. This gives damaged-media firmware
recovery more time to return an ordinary read error instead of forcing an early
abort. It does not guarantee recovery from a scratched disc. An unconfirmed abort
still poisons the adapter and requires restart; no later read, drive INIT or
placeholder is issued from that fatal result. Cancellation also waits for bounded
abort cleanup. Firmware calls themselves cannot be preempted by these deadlines.
Failed read reports include FAD/range, elapsed time, policy deadlines, last command
status and sense/transfer/ATA details, without logging every successful read.

After a fatal stop, let the worker reach READY and save its report, then restart
and use **Resume latest salvage** with the same disc. Resume revalidates the saved
identity and committed prefix. A later fatal read can stop it again without
discarding that prefix. The screen requests restart only for the native reset
latch; other fatal conditions retain a generic drive-stop message. Reports live
in `/KUI/probes/pNNNN/diagnostics.txt` and identify the failing optical request.

If normal Resume repeatedly blocks on its first collection range, restart the
console, highlight **Resume latest salvage**, then press **Y** for **Resume past
next range**. A confirmation explains that the next range of at most 32
uncommitted sectors will become unresolved placeholders without an optical
attempt. The worker still validates the disc, journal and existing recovery
evidence first. It keeps the committed prefix, defers exactly one remaining
range, then resumes normal collection. The range comes from the replayed job
frontier, never a hardcoded disc address or imported text log.

This is an explicit request; ordinary **A Resume** is unchanged. Requests on
already-collected jobs or with no collection bytes remaining are refused before
repair reconciliation. New jobs and repair-only actions cannot use this flag.
The skipped range is missing data until later validated individual repairs;
neither the bypass nor an unverified partial firmware transfer counts as recovered data.
The same existing target/chunk commit protocol records it, so a stop or failed
commit can be resumed without duplicating committed targets. There is no new
sidecar format and no automatic clearing of the fatal drive state. Individual
repair may still encounter a sector that wedges the firmware.

Use **New salvage** for an intentional recovery job. Collection probes each range
once. A safely released medium-error buffer can retain individual data sectors
that pass every supported sector check, even though the whole range did not
complete. Its remaining unverified sectors are deferred. A failed transport
without a released partial buffer defers that entire range. Audio requires a
successful full transport and is never accepted from a failed partial read.
Salvage records the unresolved addresses durably,
writes zero placeholders and moves on to the next range. The placeholder
remains missing data until a validated repair replaces it. Drive fatal/reset
requirements and storage failures stop the operation; they never become holes.

**New salvage** and **Resume latest salvage** automatically begin targeted repair
when collection finishes. **Recovery passes** defaults to **Until stopped**: it
keeps sweeping unresolved sectors in alternating forward/backward directions,
even when a pass makes no progress, until every target resolves or B stops the
operation. The existing bounded drive commands, fatal/reset handling and storage
checks still apply to every attempt. A finite limit of 1, 5, 10, 20 or 50 passes
can also be selected; exhausting it leaves an explicitly incomplete job.
**Retry unresolved sectors** runs further passes on a collected job. B retains
committed good data and repair evidence; Resume continues collection and recovery.
The screen shows the current pass and the actual optical range with its local
attempt budget. Collection has one range attempt (`1/1`), without subdivision
or individual retries; those reads belong to the later recovery phase.
Audio agreement reads are shown separately (`1/2`, then `2/2`) within the same
logical attempt. Failed collection ranges are marked **deferred**, and later
recovery passes try those sectors individually. The most recent problem retains
its range and final local try
while healthy reads continue; a successful retry updates that history to
**checks passed**, including the attempt that succeeded. The diagnostic `status.attempts` counter remains
the cumulative number of actual optical callback invocations; it is not a
per-sector retry count. These display fields are transient and do not alter
the journal, stored receipt or read schedule.

Collection progress counts committed output, including unresolved placeholders.
Its average rate uses only bytes newly committed during this invocation; resumed
bytes are excluded. The collection clock starts after identity checks and journal
replay, includes retries, stalls and durable writes, and freezes when collection
finishes or stops. It does not include later targeted recovery. Without a usable
clock or new committed bytes, a rate or remaining-time estimate is unavailable.
Reaching the end of collection still leaves unresolved targets incomplete.
The collection screen shows a percentage, average KiB/s and estimated scan time
remaining after two seconds and some newly committed output. Percentage is
floored to tenths and reaches 100% only at the end of the planned output. The
time estimate uses the unrounded average and remaining scan bytes. Once targets
exist or a read problem is active, ETA is explicitly uncertain instead of
projecting a local damaged-area rate across the entire remaining disc. Values
refresh with worker progress, including
before optical reads; a blocking drive command can hold the last snapshot.
These metrics disappear for individual repairs, which keep their pass and target
counts. The scan estimate excludes all subsequent recovery and saved-file checks.

A data candidate must pass Mode 1 sync, exact
FAD, EDC, reserved bytes and P/Q. CD data additionally supports Mode2 Form1
with matching duplicate subheaders, EDC and address-zeroed P/Q. Mode2 Form2 and
generic ECC reconstruction remain unsupported. Audio repair needs two identical
reads; their agreement is not an independent reference match.

First-pass batches are read once in groups of at most 32 sectors. There is no
recursive split or second attempt at a failed singleton. The adapter's salvage-only
`PARTIAL` result means that a medium-error command released its guarded buffer,
not that any byte is trustworthy. The worker checks each data sector at its
expected FAD, including EDC and both P/Q parity sets, and retains only sectors
that pass all supported profile-specific checks. A reported transfer byte count
does not authorize a prefix, change sector alignment, or establish audio coverage.
Normal capture retains its existing all-or-nothing acceptance rules.
Unverified bytes are cleared before the existing target/chunk commit protocol
records their addresses. The target count
therefore includes deferred healthy neighbours, not only proven bad sectors.
Recovery after collection retries those targets with the existing acceptance
and durable repair rules. This recovery path
is deliberately distinct from the accepted normal
DMA pipeline: its per-commit durability and parity checks are not a new capture
performance setting. Use the normal ripper for healthy discs.

Jobs live in `/KUI/salvage/job-0001`, then increasing numbers. No ordinary capture
folder, checkpoint, manifest or descriptor is overwritten. Latest-job discovery
checks the full track plan plus two saved healthy high-density data-sector SHA-256 anchors.
The new-job search tries endpoint alternatives and data-interior/middle/scattered
samples, bounded to 128 candidates per eligible track and anchor. Each new
candidate gets one probe before the search moves on; an audio probe still
requires two matching reads. A large saved-anchor retry preference does not
multiply these new-candidate probes. If no first anchor is found, discovery
stops instead of running a second futile search. It runs before creating files
and displays the current identity-search activity. Track-end
damage alone no longer exhausts the search. Resume still requires the exact
saved anchors, retrying transient failures at those exact addresses. A valid
different payload or an anchor still unreadable after its bounded budget refuses
resume. These exact saved-anchor checks retain their stronger bounded retry
budget, including latest-job discovery. Two anchors are disc binding, not a full catalogue identity proof.

Single-session CD plans use version-3 profile-aware headers and publish BIN/CUE,
not a fabricated GDI. They admit the supplied TOC INDEX01 ranges only; INDEX00,
subchannels and multisession capture are not inferred. CD anchors may come from
data or audio; audio anchors require matching repeated reads, reject constant
PCM and require different content hashes to avoid binding a disc to two silence
sectors. An all-silence disc without suitable anchors is refused before files
are created. CD data tracks must finish with one consistent supported mode.

After eight wholly unverified collection ranges, the worker checks its saved
identity anchors before probing another range. Each anchor gets one attempt
(two matching reads for audio) and must match its saved SHA-256 digest.
If at least one matches, scanning continues without INIT; another health check
is due only after 64 more wholly unverified ranges. Any accepted sector resets
this interval to eight. An explicit skipped range does not count as a failed
probe. A valid different anchor payload stops immediately. If neither anchor
can be verified, the worker requests one safe recovery with the usual exact
identity revalidation, or stops when recovery is unavailable or unsuccessful.
Health checks run only between committed collection ranges, so their reads
cannot replace accepted bytes awaiting a commit. They never run after FATAL.

When the adapter offers safe recovery, the worker also requests reinitialization
between recovery passes and after eight consecutive retryable failures outside
collection. Invalid `PARTIAL` candidates enter that counter only after their
sector checks fail; valid partial data resets it after acceptance. Collection
failures do not seed that separate recovery counter.
The wider health-check spacing avoids repeated spin/identity cycles while
scanning a damaged band whose saved anchors remain readable.
Firmware may still seek or retry internally during each bounded range request.
The adapter must
first establish released command/buffer ownership and an identical track plan.
Then the worker rechecks its exact content anchors before any additional track
mutation. Active acquisition buffers are preserved across this check. Recovery
refusal, disc changes and fatal/poisoned reads stop; a fatal result is never
passed to recovery or converted to a hole. This optional strategy has host fault
coverage but still needs console/damaged-media acceptance.

Targets now scale to the admitted disc layout. The fixed 8,192-target array and
64 MiB journal ceiling are removed. A disposable checksummed disk index keeps
the worker's allocation below 256 KiB, plus temporary final metadata. Journal
capacity is derived from the disc's sector count, and new jobs pack candidate
evidence into one file. Filesystem space and storage failures remain limits;
memory does not grow with the number of damaged sectors. Version-1 jobs can
still resume with their original header/journal/patch format. See the exact
[version-2 storage contract](salvage-format-v2.md).

A resumed job marked as having unstable saved-storage reads is refused before
the journal or target cache is changed. Complete a consistent saved-dump reread
to clear that condition; optical rereads are not a substitute for stable storage.

## Durable evidence

All integer fields are little endian. Metadata and records carry IEEE CRC32;
these detect accidental damage, not malicious modification.

| File | Role |
| --- | --- |
| `header.bin` | Immutable 4,096-byte versioned track plan, legacy placeholder flag and two content anchors. New jobs use `KUISALV2`/version 2 with the flag set to 1. Existing version-1 0/1 jobs resume without rewriting their header or identity; old OFF does not prevent durable holes. Written, synced, closed and compared on readback before capture starts. |
| `journal.bin` | 512-byte sequence/identity/checksum records for targets, committed chunks, immutable baselines, readiness, published candidate slots, completed repairs and final completion. Every append is synced and read back exactly. |
| `targets.cache` | Derived 32-byte checksummed target records, rebuilt from the authoritative journal on every resume. It is not recovery evidence and can be recreated. |
| `trackNN.bin` / `.raw` | Raw 2,352-byte sectors; first-pass placeholders are zero and stay explicitly unresolved in the journal. |
| `patches.bin` | Version 2: append-only 5,120-byte records binding original zero bytes, accepted candidate bytes, evidence type and checksums to the job/target. A durable journal publication precedes any target mutation. |
| `patch-NNNN.bin` | Version 1 only: retained immutable 8,192-byte records with the original format and filenames. |
| `salvage.gdi`, `salvage.json` | Published only after every hole is resolved. The manifest explicitly requires a valid final COMPLETE journal and does not claim saved-file verification. |
| `*.part` | Unpublished interrupted output. It is never accepted as a verified patch or complete result. |

Target records are committed before placeholder writes. Track bytes are synced
before the associated progress commit. The baseline freezes the first-pass CRC,
including zeros; it is never rebuilt from patched data.

Before repair, the original target must still be zero. Original and candidate
bytes are published together only after sync, self-validation and exact
comparison with the optical candidate. The track sector is then written,
synced, read back and compared before its REPAIRED event is committed. Expected
candidate CRCs are retained independently across writes, so a changed RAM buffer
cannot validate its own changed readback. On restart,
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

An unpublished version-2 candidate tail is discarded only after identity and
journal validation. Published candidates, reused slots and target transitions
remain checked. Target-cache corruption stops the attempt; Resume rebuilds the
cache from the journal instead of treating its bytes as authority.

CRC replacement computes final track CRCs from the immutable baseline and each
accepted replacement. It does not reread healthy sectors and therefore does not
independently establish their current saved bytes. After a completed engine run,
the frontend automatically compares those CRCs with its available catalogue and
can optionally reread all saved files. See [completion checks](ripper-controls.md#salvage-completion-checks).
The independent PC check below also remains available. FatFs sync and these
ordered records do not claim that arbitrary
card/controller failures are physically atomic.

## PC verification

Copy the complete salvage folder, including its journal and retained patches,
then run:

```
python3 tools/verify_salvage.py /path/to/job-0001
```

The verifier uses Python's independent CRC/SHA routines. It requires a valid
final COMPLETE journal, validates every retained patch and reads every saved
track byte. It supports both version-1 individual patch files and version-2
packed candidates. It also replaces target regions with zeros in an independent hash
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
invalid P/Q, stop, fatal reads, automatic recovery and legacy OFF-job resume.
Continuous-mode tests recover after more than 50 passes and exercise stop/resume
while passes make no progress. Finite limits retain unresolved targets without
publishing completion. Every recovered file and its
final CRC is compared with the original fixture. The PC verifier then accepts
that exported job and rejects changed healthy bytes, changed patched bytes,
self-consistently altered backups and incomplete journals.

The expanded fixtures also collect 8,208 unresolved sectors, stop during repair,
resume to byte-exact completion and independently verify the packed candidate
file. They cover damaged endpoint regions with healthy interior anchors,
single-probe collection and coarse deferred ranges, candidate-publication faults, cache recreation,
write-buffer changes, quarantine refusal and cancellation at finalization.
Historical version-1 jobs generated by the previous production engine are
resumed separately; their immutable headers remain unchanged. These are host
fault models, not measurements of scratched-disc recovery on the console.

The console smoke test should first be a short intentional start/stop/resume,
using the existing bootstrap disc and the new SD runtime. Keep normal dumps
separate. For actual damaged media, retain the salvage folder and logs; after
recovery, run the PC verifier and an independent catalogue comparison before
calling optical recovery hardware-accepted. It is not necessary to repeat the
accepted healthy-disc performance benchmarks.
