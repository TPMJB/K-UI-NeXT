# Scalable salvage evidence, version 2

New GD salvage jobs use a version-2 header and a packed candidate file. The normal
capture/checkpoint format is unchanged. Version-1 salvage jobs retain their
original immutable header, journal identity and individual patch files, and can
still resume. No version-1 metadata is silently rewritten into version 2.

## Capacity and memory

The worker no longer stores an array of 8,192 targets. Its allocation remains
statically bounded below 256 KiB. A disposable `targets.cache` file supplies
checksummed target records by index; it is recreated from the authoritative
journal after every exact disc-identity check. Status updates use counters,
without rescanning every target for each optical attempt.

Targets are bounded by the admitted track layout. Journal capacity is derived
from the layout: at most three target/candidate/repair events per sector, one
chunk event per sector, per-track baselines and final records. The conservative
bound is `(4 * total_sectors + track_count + 4) * 512` bytes. This replaces the
old fixed 64 MiB ceiling. Actual first-pass chunk events normally cover 32
sectors apiece.

At most one candidate is published per original target. Candidate records are
5,120 bytes each in one `patches.bin`, rather than a separate 8 KiB file for
each sector. Even the maximum representable admitted layout has fewer than
720,000 sectors, so this packed file fits within FAT32's file-size limit.
Available filesystem space and storage failures remain operational limits.

## Header and anchors

The 4,096-byte header retains version 1's track-plan and anchor layout. New
GD headers have magic `KUISALV2` and version 2 at byte 8. CD headers use
`KUISALV3`/version 3 and store profile `1` at byte 4,064; all other reserved
bytes remain zero. Versions 1/2 are GD-only. CD keeps the same packed-candidate
and journal layouts as version 2, but publishes `salvage.cue`. The IEEE CRC32 at byte
4,092 covers the preceding bytes. The SHA-256 of all 4,096 bytes binds every
journal/candidate record to the exact immutable header.

New anchor discovery still requires two distinct, valid high-density data
sectors. It tries endpoint alternatives, deterministic samples across the data
interior, middle-region alternatives and further scattered samples, bounded to
128 candidates per eligible track/anchor. A new candidate gets one probe
(two matching reads for audio), independent of the saved-anchor retry setting.
Discovery stops if the first anchor cannot be found. Once stored, the exact saved anchors
must match on Resume. Exact anchor reads have a bounded retry budget, including
latest-job discovery; a valid different payload is not retried into acceptance.
CD anchors can be any admitted track, with matching audio rereads and distinct,
nonconstant audio anchor payloads. Fatal reads and Stop end discovery immediately.

## Journal and candidate publication

The 512-byte `KUISJNL1` journal record layout is retained. Every record has an
ordered sequence, header identity and CRC32. Target declarations still precede
placeholder chunk commits. Chunk commits follow saved-track sync. A target's
baseline is always the original zero placeholder, and per-track baseline CRCs
remain immutable.

Version 2 adds event **7, CANDIDATE**, using the existing fields:

| Field | Meaning |
| --- | --- |
| track/index at byte 20 | Original target index |
| sector at byte 24 | Sequential packed candidate slot |
| count at byte 28 | Zero |
| CRC at byte 32 | Accepted candidate sector CRC32 |

A packed candidate is written, synced and validated on exact readback
before its CANDIDATE event is committed. Slot order is sequential; duplicate
publication for a target or reused slots are invalid. The published record must
bind the original target index, track, sector and header identity. Only then may
the saved target be replaced. A REPAIRED event follows target sync and exact
sector readback, including independent checks that the candidate RAM buffer
and readback still have the expected candidate CRC.

On restart, the journal reconstructs both repaired state and candidate slots.
A published candidate makes an interrupted/torn target write idempotent. An
unpublished candidate tail is discarded only after header identity and journal
validation; no track mutation can depend on such a tail. Published candidate
corruption, changed repaired bytes or a shortened candidate file fail closed.

## Packed candidate record

Each record occupies exactly 5,120 bytes:

| Offset | Meaning |
| --- | --- |
| 0 | `KUISPAT2` |
| 8, 12, 16 | Original target index, zero-based track, zero-based sector |
| 20 | Evidence type: 1 for validated data (Mode1, or CD Mode2 Form1); 2 for matching audio reads |
| 24, 28 | Original and candidate sector CRC32 |
| 32 | 32-byte immutable header SHA-256 |
| 64 | 2,352 original zero bytes |
| 2,416 | 2,352 accepted candidate bytes |
| 4,768–5,115 | Zero reserved bytes |
| 5,116 | CRC32 of the preceding 5,116 bytes |

Version-1 `KUISPAT1` records remain 8,192 bytes and retain their original paths.

## Derived target cache

The cache is an accelerator, never recovery authority. Each 32-byte record has
`KUITGT2` plus NUL, original target index, sector, candidate CRC, track/repaired
state, candidate slot/presence word and a CRC32 over the preceding 28 bytes. Every read checks
the checksum, requested index and layout bounds. Missing/corrupt cache data
stops the current operation; Resume recreates it from the journal.

For version-1 jobs, one directory scan discovers canonical `patch-%04u.bin`
files, including ASCII case variants. The derived slot word is zero when a
published file is present and `UINT32_MAX` when absent. The snapshot is used
only after the complete directory scan, checked directory close and index
sync succeed. Duplicate names, directories or wrong lengths at an admitted
candidate path fail closed. Temporary `.part` files and noncanonical or
out-of-range names do not publish candidates. Resume rebuilds these markers
from the journal and directory every time; interrupted scans are disposable.

A positive marker only identifies a file to validate: its complete record,
header identity, sector checks and exact saved bytes still receive the existing
checks. Newly published version-1 candidates update the marker after verified
publication. The immutable version-1 header, journal and patch files retain
their original formats. Packed jobs keep their original candidate-slot meaning.

## Acquisition and completion

The collection pass probes each range of up to 32 sectors once. Valid returned
data sectors retain their full address, EDC and parity checks; unverified slots
are deferred to recovery without collection subdivision or singleton retries.
Every unresolved sector is durably declared before its zero placeholder is
committed. Later recovery uses checked data batches and bounded singleton
fallbacks; audio candidates retain their matching-read requirement.

Completion reconstructs each track CRC from the immutable baseline and repaired
target CRCs. This loop remains cancellable. Only after all targets resolve and
the COMPLETE journal event succeeds does the engine expose `final_crc[99]` and
the immutable header identity in its successful receipt. Completion checks can
use that receipt for independent saved-file readback and catalogue comparison;
the reconstructed CRC itself is not a saved-file reread.

`tools/verify_salvage.py` independently supports both versions. It checks the
complete journal, published candidates, every saved track byte, the original
zero baseline and the final descriptor/manifest. Packed candidates are read
individually; all candidate payloads need not be held in memory at once.
