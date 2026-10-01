# SD and IDE/CF filesystem support

**The CD bootstrap can now read a runtime from clean, supported ext4.** Normal
apps and Games preparation still use FatFs for FAT32/exFAT. Keep the working
card's filesystem until a later runtime implements ext4 application access.
The [ext4 bootstrap guide](ext4-bootstrap.md) defines the pinned format, partition
layouts and read-only limits. SCI and IDE/CF still need console validation;
see [installation and test scope](storage-transports.md).

The agreed future same-card layout is **128 MiB FAT32 boot/recovery plus ext4
data**, documented in [boot-recovery.md](boot-recovery.md). The boot volume
stores `/KUI/runtime.kui` and a retained `/KUI/recovery.kui`, allowing startup
independently of dirty ext4 data. This fixes the CD interface now; do not format
the working card yet. Current app mounting intentionally keeps its original
single-volume policy, and no ext4 repair program is bundled.

## Current boundaries

[media.h](../include/kui/media.h) already separates block reads, writes, capacity
and synchronization from the hardware. However,
[diskio.c](../src/core/diskio.c) owns one global device/volume,
[data.c](../src/core/data.c) accepts only the supported FAT32/exFAT layouts, and
[storage_probe.c](../src/core/storage_probe.c) mounts through FatFs. Many file
operations call FatFs directly. The Games preparation code also extracts physical
file locations through `FATFS.csize`, `FATFS.database` and `FIL.sect` in
[games_retail.c](../src/apps/games_retail.c).

The new [boot-only ext4 reader](../src/core/ext4_boot.c) uses a separate validated
partition view and does not replace these application APIs. It loads normal or
recovery runtime images, with no writes or journal replay. The FAT boot reader
has its own explicit read-only extent view; it does not relax normal app
mounting. Compatible future ext4
runtimes can therefore be installed as card updates under the new CD; changes
to the CD's supported format or fixes to its own reader may still need a reburn.

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
5. For split media, bind ordinary apps to the validated ext4 data partition.
   Never retry a failed data operation on the FAT boot partition. Boot-image
   updates must be explicit, and preserve the working recovery image until a
   replacement has passed hardware checks.

Upstream [lwext4](https://github.com/gkostka/lwext4) is now pinned and configured
for the read-only CD bootstrap. Application integration, writable operations,
recovery and game extent export still need their own implementation and
validation. Bootstrap host fixtures do not establish application behavior or
Dreamcast memory/performance measurements.

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
through the selected device transport. The standalone storage build supplies
independent SCIF, SCI and IDE/CF resident readers; the latter two still need
hardware validation.

Adding lwext4 does not inherently slow existing FAT32/exFAT SD reads: those
volumes would continue using FatFs. Detection adds mount-time work, while code,
cache memory and backend-dispatch costs need measurement. It does not add a
filesystem lookup to each resident game read.

There is **no promised gameplay speedup from ext4**. Identical physical extents
use the same SD transfer path. Any benefit from directory handling, allocation
or reduced fragmentation must be measured separately from transport speed.
Compare mount/browse/preparation times, physical I/O counts and memory use first;
then compare fixed game loads using equivalent image data and allocation.
