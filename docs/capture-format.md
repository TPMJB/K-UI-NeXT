# Capture format and invariants

Profile: `gdi-raw2352-typegap150-v1`. This is a declared logical GDI capture
profile, not a subchannel or forensic image of every physical region.

## Track plan

The planner accepts both density TOCs, consecutive track numbers starting at 1,
low-density start FAD 150, high-density first data track at FAD 45150, and
control values 0 (audio) / 4 (data). Other layouts fail before output creation.
All address ranges are half-open: `[start_fad, end_fad)`. GDI starts subtract
150 once. Raw files are `(end_fad - start_fad) * 2352` bytes; any individual file
above the FAT32 limit is refused even on exFAT.

For an intra-session change between data and audio, exclude the 150 sectors
immediately preceding the next TOC start. Same-type boundaries and each session
leadout use the full TOC range. The manifest retains `toc_end_fad` and
`excluded_tail_sectors`, making those exclusions explicit. The space between
the low-density leadout and high-density start is outside both session ranges.

This convention was checked against the independent BSD `httpd-ack` track/GDI
writer at [the pinned reference](https://github.com/sega-dreamcast/httpd-ack/blob/f5891519a01902d50b675ff38660ac87108de293/src/httpd-ack.c).
No source code is copied from that project. It is evidence of an established
GDI convention, not proof that every GD-ROM pregap matches it. Physical complete
captures and reference comparisons remain the acceptance gate.

## Reads and identity

Read at most 32 raw sectors (75,264 bytes) per PIO command. Firmware-visible
buffers and parameters have static lifetime. Capture reads each range once;
identification uses two reads with different fills and compares the payload.
Both paths inspect the guards and unused tail. Failed aborts and guard corruption
poison further drive access.
Sense 2/6 stops capture and requires a fresh prepare/identity check on resume.

Each data sector also passes its Mode 1 or Mode 2 Form 1 EDC check. This detects
errors; it does not repair ECC or validate independent reference content.
Audio bytes are preserved as returned by the drive, without byte swapping,
offset correction or subchannels. Repeat equality is not an audio reference.

The disc fingerprint is SHA-256 over the profile name, canonical track plan and
three raw content samples per track (start, midpoint, end-1). It includes the
high-density boot metadata, whose Dreamcast signature is required. Full hashes
are still necessary to establish whole-disc identity against a reference.

A failing batch shrinks to single-sector reads for the affected window. The
budget is ten additional attempts after the initial failed request; shrinking
does not replenish it. Capture attempts use one guarded raw PIO read, check the
firmware's transferred-byte count and retain data-sector EDC validation. The
small identification samples and diagnostic probes still use paired reads.
Capture services PIO continuously with a runnable scheduler yield every 2 ms,
rather than sleeping after each busy firmware status. Deadlines and Stop checks
remain active on every command-loop iteration. CDDA has no sector EDC; a single
successful audio transfer is accepted without repeat comparison or offset/jitter
correction. Final saved-file CRC32/SHA-256 readback proves storage consistency,
not that the drive returned an independently correct audio sample.
No data is written from a failed/invalid request and no sectors are zero-filled.

## Files and checkpoints

New jobs are exclusively created at `/KUI/dumps/d<first-16-identity-hex>-NNNN/`.
The complete 32-byte identity is checked in checkpoints; the short directory
prefix is only an index. New capture never replaces an existing job. Resume
selects the greatest existing NNNN for that fingerprint and refuses corrupt
state instead of guessing an older job.

Track writes must return the exact requested length. Hash/accounting advances
only then. Every 4,096 sectors (about 9.2 MiB), at track end, and on controlled
Stop/read failure, synchronize the current track before committing a checkpoint.
SD errors never advance committed progress. Alternate two 4,096-byte records;
each has a sequence number, profile/content identity, track sector counts,
CRC32/SHA-256 prefix hashes and a record CRC32. Stored SHA digests are finalized
copies, not compiler-dependent serialized internal hash state.

Checkpoint schema 1 uses little-endian integers:

| Offset | Field |
| --- | --- |
| 0 | `KUICKP1` and NUL |
| 8 / 12 | Version 1 / record size 4096 (u32) |
| 16 | Nonzero sequence (u64) |
| 24 | Full disc fingerprint (32 bytes) |
| 56 / 60 | Track count / cumulative retries (u32) |
| 64 | Original capture build ID (12 hexadecimal ASCII bytes) |
| 76–95 | Reserved, zero |
| 96 | Up to 99 entries: sectors (u32), CRC32 (u32), SHA-256 (32 bytes) |
| After final entry through 4091 | Reserved, zero |
| 4092 | CRC32 of bytes 0–4091 |

Complete tracks precede at most one partial track; later counts/hashes are zero.
Decode checks checksum, identity, version, reserved fields, sequence, bounds and
ordering. Resume rereads and validates every committed prefix before **any**
track mutation. Short or mismatching committed data causes refusal. Only the
first incomplete file can have an uncommitted suffix; after successful prefix
checks, truncate that suffix and append from the checkpoint. Unexpected later
track files or excess sizes cause refusal. Corrupt/incomplete jobs remain for
inspection. A valid older record can recover a torn newer checkpoint when its
file invariants still hold; arbitrary FAT/exFAT power-loss recovery is not promised.

## Completion and verification

After all tracks are synchronized and checkpointed, unmount/remount FatFs and
reread every saved byte. Compare exact sizes, CRC32 and SHA-256 to the capture
stream. Only then publish `disc.gdi` and `manifest.json` through flushed temporary
files and checked renames. A final-file readback checks metadata too. If stopped
between publishing those files, Resume revalidates and completes finalization;
the PC verifier requires both. Existing final metadata must match and is never
silently overwritten. Verify performs no writes.

`SAVED DATA VERIFIED` establishes saved-versus-captured consistency. It does not
mean an independent disc-reference match. Python's `hashlib` and `zlib` recompute
the hashes independently in `verify_dump.py`, validate the GDI/manifest profile,
and distinguish complete, partial and mismatching supplied references.

CRC32 and SHA-256 are new project implementations of published algorithms;
SHA-256 follows [FIPS 180-4](https://doi.org/10.6028/NIST.FIPS.180-4). The CD EDC
uses reflected polynomial `0xd8018001`, zero initial state and no complement.
No new linked library or copied DreamShell code is introduced.
