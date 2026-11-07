# Shared preparation before Bootstrap 1

The owner reports almost twenty seconds from selecting Power Stone 2 to
bootstrap in the first consolidated 72db614 test. That is a launch-preparation
report, not a measurement of the detached stage or gameplay reader. The repair
must live in common preparation code and apply by operation/capability, without
game-title exceptions, new per-game profiles or weakened integrity checks.

## Existing work retained

The delivered GUI already batches raw image extraction in sixteen-sector
windows, hashes in 32 KiB logical windows and reuses a complete executable read
for its source CRC where eligible. Runtime loading and VMU history verification
also use 32 KiB reads. The GUI's immutable program/heap/exit decoders were already
enabled in the actual 72db build; the new recipe/equivalence gate protects that
fact rather than claiming it as another speed improvement.

Do not revive the rejected October 6 read-ahead candidate or attribute a
pre-bootstrap delay to a detached stage's later proof. The accepted collection
modules and worker memory geometry remain preservation boundaries.

## Shared card admission

Image launches and physical-disc launches supply the same optional complete
multi-sector raw-read contract to the common game-card service. This includes
the collection and CE preparation helpers which use the image launcher's
platform. Single-only platforms and low-memory operation retain the former
read path.

The service uses the existing two-copy, sixteen-sector FAT windows during one
fresh ownership scan. Only a fresh volume boot record can select the read-only
FAT ranges. Every CRC, mirror, namespace, incoming-link, allocation and overlap
check remains. A failed multi-read contributes no cache entry and cannot turn
into success by retrying through a different callback. Scratch is discarded
before remount and never occupies game RAM.

After ownership is proved, the same scratch becomes a separate fresh snapshot
window. Each request stops at sixteen blocks, the current file role's useful
length or its first physical discontinuity. Every supplied block still passes
the complete image SHA and per-block CRC, journal-marker and identity checks.
The snapshot starts empty on every invocation and never reads a neighboring
file or allocation slack merely to fill a batch.

A current prepared profile skips this scan. Batching it is a real shared
deployment correction, but cannot by itself explain every warm launch delay.

The real filesystem regression compares the complete prepared profile, logical
admission result and final disk bytes with the single-only implementation. It
also checks batch errors, poisoned output, later allocation corruption,
legacy callbacks and a fresh subsequent operation. The complete collection
fixtures reduced raw commands as follows:

| Filesystem fixture | Single requests | Shared batching |
| --- | ---: | ---: |
| FAT32, fragmented card | 2,356 | 184 |
| exFAT, 4 KiB clusters, fragmented card | 334 | 91 |
| exFAT, 128 KiB clusters, contiguous card | 327 | 86 |

Snapshot requests fall from 259 to 18–20 while still receiving every one of
the 259 useful blocks. These counts are not console elapsed times or a
prediction for a particular SD card.

## Recovery and launch ownership

The frontend can recover a pending session before the card-specific launch
operation. Clearing that receipt first prevents the existing same-card
session-begin path from combining recovery and the pre-launch checkpoint.
A production state-machine reproduction counted two checkpoints, two
allocation walks and two receipt writes, versus one of each in the existing
combined path. This is conditional: idle startup recovery may already have
completed before selection.

Foreground recovery now runs at the start of the launcher's mounted operation,
after selection metadata is stored and before launch/card allocation. This
also covers ordinary CE, CDDA, standalone and other image routes which never
prepare a native virtual card. Idle startup recovery retains its separate
one-shot behavior; no automatic job may race a prepared game handoff.

Within that one exclusive operation, the GUI can carry the just-verified
recovery checkpoint into the exact same card's session-begin. The coordinator
checks the fresh storage identity, partition, selected product/pair, sixteen
identity bytes and unchanged device/write/error generation. Any write attempt,
device replacement, I/O/refusal error, identity difference, cancellation or
ended job prevents reuse. Normal full verification remains the fallback.
The proof is consumed once and cleared on every outcome; it never authorizes
another launch and no on-disk receipt grants this permission.

The production coordinator/receipt test reduces complete checkpoint calls from
two to one while preserving the exact final receipt generations and bytes.
Both fresh allocation walks and both durable receipt publications remain;
this change avoids the duplicate payload/hash work without changing their
ordering. Historical payload verification is retained in the initial fresh
checkpoint. Different cards still receive independent full checkpoints.

## Delivery and measurement

Run `make test-gui-launch-preparation`. Keep host/linked results separate from
console elapsed time. A test GUI can enable the existing shared launch timing
with `KUI_SMASH_LAUNCH_TIMING=1`; ordinary native launches then save
`KUI/apps/games/launch-timing.txt`, recording the product, reader mode, VMU
setting and actual selected module identity. It separates executable I/O and
hashing, extent/SDK proofs, card/history work and the remaining preparation.
The report write occurs outside the recorded action-to-ready interval.

New raw counters distinguish callback attempts (`raw_sessions`) from blocks
received in complete successful requests (`raw_blocks`). Logical ownership
requests include cache hits; neither counter substitutes for console time.

This follow-up replaces the GUI while retaining all twenty non-GUI files in
the first test byte-for-byte, including its accepted Smash modules/helpers and
the exact native CDDA admission manifest. Preserve inherited Standard,
Background and physical-disc workers. This targets common GUI preparation;
it does not change game-time scheduling, reservations or SDK compatibility.
