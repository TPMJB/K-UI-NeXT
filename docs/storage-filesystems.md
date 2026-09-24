# SD and IDE/CF filesystem support

**Future work; not implemented.** This assessment covers direct serial SD and
IDE/CF storage. K-UI currently uses FatFs for FAT32/exFAT. It has no lwext4
backend or implemented IDE/CF adapter.

## Current boundaries

[media.h](../include/kui/media.h) already separates block reads, writes, capacity
and synchronization from the hardware. However,
[diskio.c](../src/core/diskio.c) owns one global device/volume,
[data.c](../src/core/data.c) accepts only the supported FAT32/exFAT layouts, and
[storage_probe.c](../src/core/storage_probe.c) mounts through FatFs. Many file
operations call FatFs directly. The Games preparation code also extracts physical
file locations through `FATFS.csize`, `FATFS.database` and `FIL.sect` in
[games_retail.c](../src/apps/games_retail.c).

## Proposed architecture

1. Give each storage device its own block-I/O context and capabilities. Adapt
   serial SD and, separately, an IDE/CF driver to that boundary.
2. Validate a partition view, then inspect its filesystem at mount time.
   Validate filesystem structures and supported features; a partition type or
   magic value alone is insufficient.
3. Select FatFs for supported FAT32/exFAT volumes or lwext4 for a supported ext
   volume. Keep that selection in the mounted-volume context; do not redetect
   the filesystem on each read.
4. Expose common file/directory operations and a backend-specific physical
   extent exporter. Replace direct FatFs dependencies progressively, starting
   with Games browsing and preparation. Do not reuse the existing globals for
   concurrent mounts.

Upstream [lwext4](https://github.com/gkostka/lwext4) provides ext2/3/4 support
with configurable features and a
[block-device callback interface](https://github.com/gkostka/lwext4/blob/master/include/ext4_blockdev.h).
It still needs a pinned configuration, SH4 integration and testing. Its published
memory estimates are for Cortex-M4, not Dreamcast measurements. Initial ext
support should be read-only, with an explicit supported-feature set; writable
operations and recovery need their own validation.

Pinned KOS supplies a
[generic block-device interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/blockdev.h)
and [G1 ATA PIO/DMA and device/partition adapters](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/g1ata.h).
Those are implementation foundations, not evidence that IDE/CF already works in
K-UI or remains usable after its runtime shuts down.

## Game handoff and performance

Both filesystem backends should export validated file-offset-to-physical-sector
runs into the existing [retail image manifest](../include/kui/retail_image.h).
An ext backend must convert filesystem blocks to device sectors and initially
reject sparse or unwritten image mappings that the manifest cannot represent.
Files and their allocation must remain stable through handoff. The filesystem
libraries stay outside the game resident; it reads the prepared physical runs
through the selected device transport. IDE/CF additionally needs its own
independent resident transport.

Adding lwext4 does not inherently slow existing FAT32/exFAT SD reads: those
volumes would continue using FatFs. Detection adds mount-time work, while code,
cache memory and backend-dispatch costs need measurement. It does not add a
filesystem lookup to each resident game read.

There is **no promised gameplay speedup from ext4**. Identical physical extents
use the same SD transfer path. Any benefit from directory handling, allocation
or reduced fragmentation must be measured separately from transport speed.
Compare mount/browse/preparation times, physical I/O counts and memory use first;
then compare fixed game loads using equivalent image data and allocation.
