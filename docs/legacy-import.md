# Importing legacy DS-based raw jobs

The independent repair worker can admit the raw subset of **DreamShell GD
Ripper state v1**, the format written by our improved DS-based ripper. This is
an explicit repair/continuation import. It does not reinterpret `rip.state` as
an independent K-UI checkpoint, rename the old files, or rewrite legacy
sidecars. Imported bytes remain source evidence for the new worker. Select the existing
legacy folder through **Repair / resume a separate copy**. Direct legacy
in-place edits are refused: old CRC receipts would become stale after new
patches, and cannot be silently skipped on a later import. A completed imported
copy can subsequently use independent in-place repair.

The read-only importer checks the exact state header, media class, raw format,
track numbers, starts, declared counts, controls, sector sizes and canonical
filenames against the prepared inserted-disc plan. It rejects cooked legacy
jobs and unsupported or malformed state versions. The old CD data path omitted
two sectors at the end of each data track; where that is the only range
difference, those sectors become missing tails to acquire, not synthesized
bytes. GDI filenames or track sizes alone are never optical identity.

A GD job's retained 2048-byte `rip.disc` must match the payload of two validated
raw optical reads at FAD 45150. If `rip.disc` is absent, a structurally valid
saved raw track-3 boot sector supplies the same payload. Jobs lacking both
cannot be bound safely and are refused. A single-session CD job instead needs
two distinct saved content anchors that each match paired optical reads. The
search is bounded and includes interior sectors; an audio anchor requires exact
raw-byte agreement. Fatal drive results and cancellation stop immediately.
Transient errors and invalid returned data sectors receive bounded retries.
The admitted anchor FADs and SHA-256 values can be rechecked after a safe drive
reinitialization without trusting newly edited source hashes.

`CRC1` journals are parsed with their original track tag, byte count, stream
CRC and record checksum. A valid journal prefix is checked against saved bytes;
an uncommitted file tail is excluded. A torn last record does not publish
progress. Other malformed records are refused. Without a journal, available
whole-sector bytes are imported as saved source observations, not as a proven
historical capture CRC. Every admitted prefix gets CRC32/SHA-256 and a complete
independent reopened reread. A legacy `readback.unstable` marker blocks import.

Saved `.suspect` and `.bad` maps have strict bounds, addresses and offsets.
Their targets remain explicit forced optical rereads, including audio holes
that structural Mode 1 checks could never identify. Published `GDRCV1`
`.recovery-base` records retain the exact old target set. The importer validates
its layout, sorted records and checksum, reconstructs the expected present
whole-track CRC from the baseline and current target bytes, then compares that
with the complete saved track. This protects healthy bytes while allowing a
formerly targeted sector to contain an interrupted old patch. The new worker
must obtain and record its own candidates; no old backup or audio-completion
record is treated as new patch authority.

The import receipt binds state, admitted prefix sizes/hashes, checked metadata,
optical anchors and the target bitmap. A missing source track is an empty
prefix; a truncated raw sector, oversized track, wrong disc or damaged published
baseline is refused. Originals and legacy metadata stay available for review.

## Imported raw BIN/CUE

A separate parser admits a narrow single-session CD subset: one safe ASCII-named `BINARY`
file per consecutive track, `AUDIO`, `MODE1/2352` or `MODE2/2352`, and
`INDEX 01 00:00:00` in each file. Optional `PREGAP` must match the prepared
uncaptured gap exactly. The repair worker still checks every file's byte length
and saved/optical content identity. Shared BIN files, nonzero offsets,
`INDEX 00`, subchannel flags, compressed/cooked tracks and multisession GD cues
are refused rather than having their meanings guessed. Original filenames are
retained. Mode 2 admission does not itself imply Mode 2 reconstruction.

## Host checks

`test_legacy_import_images.py` uses the actual FatFs adapter on FAT32 and exFAT
images. It covers partial prefixes, uncommitted tails, CRC mismatch, target
maps, baseline reconstruction and corruption, audio targets, wrong discs,
transient/invalid/fatal reads, saved-read instability, missing identities,
malformed state, and cancellation. It asserts that the importer issues **zero
storage writes**. The parser unit checks overflow, traversal, FAT name aliases, wrong tracks and
media, cooked formats, truncated/extra state, duplicate CUE files and ambiguous
indexes. These checks do not establish console performance or compatibility
with every historical DS build.
