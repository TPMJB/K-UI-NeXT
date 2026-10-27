# Fixed-extent virtual VMU store

`src/core/vmu_store.c` implements an allocation-free, 512-byte-block backing
store. It does not intercept Maple, discover filesystem allocation, access
arbitrary game RAM, or enable in-game saving by itself.

The image remains an ordinary **131,072-byte VMD**: 256 blocks of 512 bytes,
with no private header or changed VMU filesystem layout. A separate **1,024-byte
sidecar** holds one redo transaction. The caller owns a 1,624-byte store object
on the host ABI (compile-time limit: 2 KiB); it never mirrors the card in RAM.
The implementation reuses K-UI's small-table IEEE CRC32 routine,
`kui_retail_crc32` from `src/core/retail_image.c`.

## Ownership and admission

Before opening the store, the owner must:

1. Allocate and fully synchronize the exact-size image and sidecar files.
2. Obtain the actual card capacity and prove that their allocation belongs to
   those files. Supply ordered absolute-LBA extents, a stable nonzero 128-bit
   image/sidecar identity, and the independent card capacity.
3. Exclude filesystem metadata, game-file sectors, and every other owner from
   these ranges; relinquish filesystem/cache ownership before raw handoff.
4. Pin the card identity and allocation until handoff ends, and serialize the
   store with all game-reader and storage operations.
5. Supply 512-byte I/O callbacks whose successful sync is both a durability
   barrier and an ordering barrier for all preceding writes.

This module copies up to 16 image extents and 16 sidecar extents. It rejects
zero-length, out-of-capacity, overflowing, overlapping, or wrong-total-length
maps before issuing any I/O. Logical order may be physically noncontiguous or
unsorted. A mapping fingerprint includes the identity, capacity, counts, and
ordered extents. A committed marker made for another identity or map is rejected
without replay.

Extent ownership and card identity cannot be inferred from a numeric map alone.
They are caller/transport admission responsibilities; accepting a structurally
valid map is not proof that its sectors are free to overwrite. The transport
must reject unsupported LBA widths before dispatch, rather than truncate them.
A FatFs proof harness can supply a synthetic address space over two files, but
that does not establish a physical in-game extent map.

## Durable block transaction

Sidecar logical sector 0 holds the complete proposed block; sector 1 holds its
commit marker. Every write follows this sequence:

| Step | Operation | Reason |
|---|---|---|
| 1 | Write an all-zero marker; sync | The previous redo payload may be reused only after its old marker cannot survive. Its base was already synchronized. |
| 2 | Write the new 512-byte payload; sync | An interrupted payload cannot be mistaken for a committed write. |
| 3 | Write its complete CRC-protected marker; sync | Establish the durable commit before touching the base image. |
| 4 | Write the image block; sync | Materialize the committed block in the ordinary VMD before returning success. |

The marker is retained after success. The next transaction clears it with a
barrier before overwriting the payload. Thus the only complete redo copy is
never replaced while its base image is still vulnerable.

The API returns success only after all four barriers succeed. Any read, write,
or sync error blocks the instance until a fresh `open` succeeds. An error may
have happened after a transaction reached durable storage; callers must report
failure or uncertain completion, and must never acknowledge a successful save
for that request. An ordinary sync retains the redo record.

Writes are atomic **per VMU block**, not across an entire save file. A game may
update multiple data and VMU filesystem blocks; interruption between those
updates can leave the same application-level incomplete save possible on a
physical card. No claim of whole-save transactional consistency is made.

## Recovery and clean management handoff

Open reads and checks the marker. A CRC-invalid/torn marker is uncommitted: the
ordering rules ensure this state cannot have an in-progress base overwrite.
A CRC-valid marker with incompatible fields, wrong identity/map, or corrupt
payload fails closed. A complete valid record is synchronized **before** base
replay, then its indicated image block is written and synchronized.

The extra pre-replay barrier matters when reopening after an ambiguous I/O error
in the same powered session: a valid marker read through a driver cache may
still be volatile. Replaying the base before making that cached record durable
would introduce a power-loss window. Recovery can itself be interrupted and
repeated; its committed redo is never changed by replay.

Never expose, move, replace, or edit a potentially interrupted base image
without its original sidecar and successful recovery. `checkpoint` first
synchronizes the base, then clears and synchronizes the marker. After successful
checkpoint and relinquishing the store, management tools may treat the VMD as a
standalone image, relocate it, or recreate/rebind its sidecar. Failure requires
recovery under the original mapping before management continues. Ordinary
`sync` alone does not authorize sidecar rebinding.

## Sidecar marker version 1

Integers are little-endian; all reserved bytes are zero. CRC32 is reflected IEEE
CRC32, initial/final XOR `0xffffffff`, matching zlib and K-UI's retail CRC.

| Byte range | Field |
|---|---|
| 0–7 | ASCII `KUIVMUJ1` |
| 8–11 | Version, 1 |
| 12–15 | Sector size, 512 |
| 16–31 | Persistent image/sidecar identity |
| 32–39 | Card sector capacity |
| 40–43 | Canonical mapping fingerprint CRC32 |
| 44–47 | VMU block index, 0–255 |
| 48–51 | Full 512-byte payload CRC32 |
| 52–55 | Committed record flag, 1 |
| 56–63 | Diagnostic transaction sequence |
| 64–507 | Zero reserved bytes |
| 508–511 | CRC32 of bytes 0–507 |

The fingerprint CRC processes the identity first, then capacity as u64, image
extent count as u32, sidecar extent count as u32, then every image extent and
every sidecar extent in logical order as u64 first LBA + u32 sector count. It
excludes C structure padding. The sequence is diagnostic only; no comparison or
ordering between competing records is needed.

CRC detects torn sectors probabilistically; it is not authentication or a
protection against maliciously changed files. Recovery's torn-marker rule
assumes exclusive ownership and this ordered protocol. Arbitrary post-commit
media corruption can exceed the fault model. Successful callbacks must never
silently lose or reorder previously synchronized data or damage another sector.
In particular, a driver exposing only an SD "accepted" response without waiting
for programming completion cannot meet the contract.

## Host verification

`tests/test_vmu_store.c` uses an independent write-back block-device model and a
separate bitwise CRC oracle. It checks:

- Every logical block through three noncontiguous, physically unsorted extents.
- Copies of input maps, exact card endpoints, 64-bit arithmetic edges, overlap,
  malformed counts, unknown record versions, wrong identities/capacities,
  corrupt payloads, blocked instances, and untouched surrounding sectors.
- 4,120 interrupted write/barrier cases: fail before persistence, persist fully
  but report failure, or tear at every one of 513 byte boundaries at each of
  the eight write/barrier steps. Previously acknowledged data must survive;
  any unacknowledged block must recover as exactly old or exactly new.
- Every recovery read/write/barrier failure and idempotent retry.
- 513 torn pre-replay barriers with a valid but still volatile commit in the
  same powered session; no base write may precede a successful redo barrier.
- Ambiguous errors at every transaction step without a device reset, followed
  by a successful next transaction and a real reset.
- Successful clean checkpoint and all 1,539 checkpoint interruption boundaries.

Compile and run with the normal host warnings and ASan/UBSan, linking
`vmu_store.c` and `retail_image.c`. These tests verify the store protocol under its
callback contract; they do not establish the Dreamcast transport's durability,
Maple interception, filesystem ownership proof, or game compatibility.
