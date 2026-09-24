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
mean an independent disc-reference match.

**Schema 2 and the capture options.** A job records SHA-256 and CRC32 per track
(schema 1, as always) unless it was started CRC-only (`capture_hash=crc32` in
`KUI/bench.cfg`), in which case its checkpoints carry a flag (header bytes 76-79,
bit 0, zero in every older record) and its manifest is schema 2: `"hashes":["crc32"]`,
no `sha256` fields, and **no verification claim** (no `saved_data_verified`, no
`reference`). The claim is left out on purpose: published metadata is never
overwritten, so a claim in it could not be upgraded by a later Verify, and how a
run verified is recorded in that run's report instead. A job keeps the mode it
started with, because SHA-256 state cannot be resumed from a digest. `verify_dump.py`
accepts both schemas, always recomputes CRC32 (and SHA-256, for reference) from the
files, and rejects a schema 2 manifest that carries a claim or a SHA field.

Related options, all defaulting to the old behaviour: `end_readback=off` skips the
re-read after capture (CRC-only jobs only; the report then says `CAPTURED`, never
`SAVED DATA VERIFIED`, and Verify still works afterwards); `resume_check=size` checks
file sizes on resume instead of re-reading the committed bytes and continues the
CRC32 from the checkpoint (CRC-only jobs only; it catches a wrong size but **not** a
corrupted prefix, which only the end read-back or the PC verifier will find);
`sample_readback=N` re-reads and byte-compares 1 chunk in N while capturing, so a
bad write stops the capture where it happens.

**Reference check.** If `KUI/redump.db` and/or `KUI/tosec.db` are on the card
(`data/known-dumps/`; the SD update includes them), the finished capture's
per-track sizes and CRC32s, already in the checkpoint, are compared with those
catalogues in one streaming pass over two small files, about a second whatever
the disc's size. Nothing is re-read from the saved tracks. The report says
`Reference check (Redump|TOSEC): <grade>` and names the matching entry:

| Grade | Meaning |
|---|---|
| `FULL TRACK MATCH` | Every track equals one catalogue entry, size and CRC32. |
| `DATA TRACKS MATCH` | Every data track matches; an audio track differs or is unlisted. |
| `IDENTIFIED BY DATA TRACK` | The data tracks the entry lists match, but it lists fewer than the capture has. The bundled Redump catalogue lists one identifying track per game. |
| `PARTIAL MATCH ONLY` | Some data tracks match. |
| `NO CATALOGUE MATCH (INCONCLUSIVE)` | Another revision, a disc the catalogue lacks, a track-boundary convention, or a read error all look the same. Not a failure. |

A match is stronger than a re-read: it shows the bytes equal the canonical dump,
where a re-read only shows the card holds what the drive returned. It never
changes whether the capture succeeded, and with no catalogue on the card the
report says `No independent reference compared`. Both catalogues are consulted
and the better grade is reported. The format is one header line
(`DREAMSHELL_REDUMP_CRC_V1`) then `G<TAB>tracks<TAB>name`, `T<TAB>number<TAB>bytes<TAB>crc32`
and `E` per game.

Python's `hashlib` and `zlib` recompute
the hashes independently in `verify_dump.py`, validate the GDI/manifest profile,
and distinguish complete, partial and mismatching supplied references.

CRC32 and SHA-256 are new project implementations of published algorithms;
SHA-256 follows [FIPS 180-4](https://doi.org/10.6028/NIST.FIPS.180-4). The CD EDC
uses reflected polynomial `0xd8018001`, zero initial state and no complement.
No new linked library or copied DreamShell code is introduced.


## Named output metadata extension

The shell now selects a destination (default `/Games`) and creates a sanitized
IP.BIN title folder before the first checkpoint: `Title`, `Title (2)`, etc.
The GDI is `Title.gdi` in each folder. Named manifests add the optional
`gdi_file` string to schema 1/2; the updated PC verifier validates this as a
single filename and opens it explicitly. Manifests without that key retain
`disc.gdi`. Checkpoint bytes, track filenames and capture hashes are unchanged.
Resume/Verify discover the highest numbered folder in the selected parent whose
checkpoint matches the complete disc identity and track plan, falling back to
legacy `/KUI/dumps` jobs if no named match exists. See
[ripper-controls.md](ripper-controls.md).
