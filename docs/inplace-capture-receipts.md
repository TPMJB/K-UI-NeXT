# Admitting finalized in-place repairs to normal capture verification

An active `.kui-inplace-active` or `.kui-inplace-active.part` blocks ordinary
Resume and Verify before checkpoint admission. Only the explicit repair worker
may continue those original tracks. The completed worker refreshes source
metadata and checkpoints, retains the original metadata backups, publishes an
independently readable completion receipt, and then retires the active guard.

A repaired manifest can retain different title/build provenance or descriptor
names from normal capture's original formatter. Normal capture therefore does
not require its regenerated JSON string to equal the repaired JSON byte for
byte. Instead it validates a precise receipt before any possible truncation or
capture write, and verifies every saved byte before accepting completion.

The `.kui-inplace-complete` receipt is 512 bytes:

| Offset | Value |
| --- | --- |
| 0 | `KUIIPD1` plus NUL |
| 8, 12 | Little-endian version 1 and record length 512 |
| 16 | SHA-256 of the exact published manifest bytes |
| 48 | SHA-256 of the exact refreshed checkpoint bytes |
| 80 | SHA-256 of the normalized card-root source folder including its terminating NUL |
| 112 | SHA-256 of the exact GDI descriptor bytes |
| 144 | SHA-256 of the exact CUE descriptor bytes for BIN/CUE; otherwise zero |
| 176–507 | Zero |
| 508 | IEEE CRC32 over the first 508 bytes |

The normal engine hashes the literal selected checkpoint before adding this
run's retry diagnostics. It then strictly parses the receipt-bound manifest and
compares its profile, format, identity, full track geometry and track hashes
with the prepared optical layout and selected checkpoint. Safe GDI/CUE names
come from that parser. The descriptor bytes must match both their receipt
hashes and independently generated geometry/mode descriptors. A receipt alone
cannot establish correctness or bypass saved-file hash checks.

A receipt-bound checkpoint must already cover every track. A `.part` completion
receipt, a missing receipt beside retained repair-original metadata or the
permanent `.kui-inplace-history` sentinel, invalid
checksums, changed folder binding, substituted checkpoint/manifest/descriptor,
or inconsistent fields refuses the operation read-only. Quick Resume cannot
skip saved-byte verification for a finalized repair.

`tests/test_capture_repaired_images.py` exercises actual normal capture, saved
sector damage, explicit in-place repair and subsequent normal Verify/Resume on
real FAT32/exFAT images. Negative cases include a re-checksummed wrong-folder
receipt and an otherwise valid partial checkpoint; both must fail before any
normal Resume writes.
