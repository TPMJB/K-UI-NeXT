# Saved-dump repair, continuation and reread evidence

Repair is explicit. Advanced CRC remains a read-only diagnosis. Ordinary
Capture Resume never silently changes suspect saved bytes.

## Repair modes

**Repair / resume a separate copy** admits completed raw K-UI jobs, matching
checkpointed partial captures, raw2352 GDI folders and the supported separate-file
raw CD CUE subset. It also imports the supported raw legacy DS jobs through the
strict read-only legacy importer. Original files remain read-only; output and
original/candidate evidence live in `/KUI/repair/job-NNNN`. Selecting the same
source resumes the matching checked copy.

**Repair / resume in place** is a separate confirmed action. It supports normal
completed raw K-UI jobs, normal checkpointed partial captures and complete raw
GDI/CUE imports. It avoids another complete track copy: existing good bytes stay
in their files, accepted replacements are applied to targets, and missing tails
of normal partial captures are appended. Legacy partial jobs first use separate
copy import. Derived CHD/CSO/ZSO/cooked jobs cannot be edited in place because the
raw edits would invalidate their separate exported files.

In-place repair checks saved prefixes and exact disc identity before writing.
Every replacement first publishes original bytes and two agreed candidate reads
in durable evidence, then publishes a checked replayable pending patch. A
checkpoint follows saved-target sync and exact readback. A restart verifies the
committed prefix and reconciles an interrupted patch before new recovery.

The source `.kui-inplace-active` guard blocks ordinary Capture Resume/Verify
until finalization. Its partial-source flag makes a restart use original
checkpoint/manifest backups while replacement metadata is being published.
On completion the worker reads back all track bytes, updates raw manifests and
checkpoints, retains original metadata backups, and publishes a checked
`.kui-inplace-complete` receipt binding the exact manifest, checkpoint, source
path and GDI/CUE descriptor bytes. The active guard is retired last. Ordinary
Capture checks this receipt and every saved hash rather than pretending the
repair manifest is byte-identical to the earlier capture's presentation.

## Identity and formats

Completed normal jobs require the original profile/layout and original fixed
start/middle/end content identity. Checkpointed normal partial jobs retain that
same identity. Bounded retries help temporarily unreadable fixed samples. Separate-copy repair
also offers an explicit saved-anchor fallback: it first verifies every admitted
saved prefix against the original recorded CRC/SHA, rejects any readable original
identity sample that disagrees, then creates a distinct recovery binding from
two healthy matching saved/optical anchors. Its immutable receipt is published
before copying/repair and the same exact anchors are rechecked on Resume/reset.
The report distinguishes this new binding from the unreadable original capture
digest. In-place repair retains the original exact identity requirement; use a
separate copy for saved-anchor admission. Ordinary Capture never uses fallback.

Imports discover two distinct healthy matching saved/optical anchors with up to
128 deterministic endpoint/interior/scattered candidates per eligible track.
GD imports exclude shared low-density tracks. CD imports use their actual
single-session layout; audio-only imports require paired equal optical reads,
but do not acquire sector parity or offset correction by doing so. Constant PCM
and repeated identical audio blocks are not independent identity anchors. Raw GDI/CUE
paths are bounded, safe and have zero file offsets. Filesystem object aliases
between track names are rejected before any in-place mutation.

Mode1 and CD Mode2 Form1 candidates pass sync, expected address, EDC and P/Q
checks. Two byte-identical validated optical reads precede replacement. Audio
requires two equal optical reads. Mode2 Form2 and mixed data modes within one
CUE track are refused. Whole-track audio/hash differences cannot automatically
identify individual bad sectors; they remain unresolved unless explicit legacy
bad-sector targets identify them. No generic ECC reconstruction, subchannels,
INDEX00 measurement or audio offset correction is added.

Normal CD captures use their raw internal GDI and BIN/CUE metadata. Successful
CD repair copies publish a CUE as well as raw tracks and the internal geometry.
Single-session CD support remains explicit; no multisession layout is guessed.

## Collection and target recovery

With continuous recovery enabled, a missing source sector that remains
unreadable receives a journaled zero placeholder and collection continues.
`KUIHOL1` declarations live in the existing checksummed `samples.bin` evidence
stream and are committed by the containing copied-chunk checkpoint. An
uncommitted declaration cannot create a completed target.

Structural/legacy target declarations and accepted replacement records rebuild
a compact derived bitmap. Subsequent passes visit only unresolved targets,
alternating directions. The worker does not reread healthy track bytes on every
pass. In-place repair combines baseline hashing and structural discovery in one
saved-file pass; Resume combines committed-prefix verification and discovery in
that pass instead. A bounded derived bitmap narrows later storage reads to
suspects, while durable checkpoints and original/candidate evidence remain the
repair authority. Final verification reads saved bytes once and calculates both
whole-track and original-prefix hashes together. Healthy bytes therefore receive
two engine reads in the tested fresh/resumed cases, plus targeted patch checks;
this is a host filesystem read count, not a console throughput measurement.

A patch inside a partial prefix invalidates SHA's running state. Its append
checkpoints retain the checked CRC with an explicit invalid-SHA flag until the
mandatory final read computes SHA-256. No completion metadata is published from
that intermediate state. Zero pass limit means **Until stopped**; finite pass limits retain
unresolved work. B stops with progress retained. Saved target reads must agree
before optical replacement; disagreement retains samples and blocks recovery.

Drive recovery is optional through the native ownership-safe callback between
passes. A successful reset is followed by content-identity revalidation before
writes. A fatal/poisoned command stops immediately; it is never a zero hole or
permission to reinitialize a still-owned drive.

All accepted replacements update track CRCs from their old/new sector CRCs.
One complete saved-byte read establishes final CRC32/SHA-256 and validates
required source-prefix hashes. The previous 8192-target limit is removed, but
free space and filesystem file-size limits remain: repair's three-sample
7120-byte evidence records are larger than salvage's packed candidate format.
Extremely damaged full-disc FAT32 work should use Salvage rather than assume
unbounded repair evidence capacity.

After successful repair, Redump/TOSEC comparison runs automatically and records
its separate grade. Full saved-byte readback, a completed repair and a full
independent catalogue match are different results. No database, partial matches
and mismatches do not become full-match badges.

## Retained state and repeated operations

Repair copies and in-place transactions bind source identity and checked
committed bytes. Changed source data or damaged authoritative state is refused.
Old independent v1 repair copies have no automatic migration to v2 resume.
An interrupted in-place action resumes its existing transaction. After verified
completion, a new action may diagnose later corruption in a fresh transaction,
retaining the earlier job and all of its original metadata/evidence. The prior
completion receipt, state, descriptors and finalized evidence must agree before
a fresh transaction begins.

Each in-place transaction retains original metadata in its own job folder.
A checked original-metadata receipt binds every backup and its absence/presence.
Older fixed `.kui-repair-original` backups remain untouched. Do not remove active guards or edit transaction evidence to bypass a
refusal. FatFs sync/close cannot guarantee physical power-loss atomicity inside
a storage device.

## Storage-only reread evidence

The separate suspect-sector diagnostic requires no inserted disc. It scans raw
saved data, samples at most 64 structural suspects through two independent
reopens, and retains the initial and two repeat samples plus differences under
`/KUI/recovery/readback-NNNN`. It continues counting further suspects beyond the
sample cap. Stable samples do not establish correct disc data.

Advanced CRC independently repeats every saved chunk, stores suspect evidence,
and persists storage-instability quarantine across operations. A complete
consistent scan may clear that quarantine; cancelled/incomplete scans cannot.

The fixed evidence record is a 64-byte little-endian header plus three raw2352
samples. Magic is `KUIRDB1` (saved repeats), `KUIRPT1` (accepted optical pair),
or `KUIHOL1` (missing-source declaration). Header fields retain version1,
sequence, track, FAD, sector, flags, three sample CRCs, aggregate sample CRC and
header CRC. Checkpointed evidence fences distinguish durable records from
uncommitted tails. Pending chunk/patch publication is replayed before the fence
advances.

Host tests run real FatFs against FAT32/exFAT images with ASan/UBSan, including
partial in-place continuation, torn writes, interrupted metadata, CD Mode1/
Mode2 Form1, audio holes, CUE imports, stable-source guards and all existing
copy faults. Console damaged-disc acceptance remains required.
