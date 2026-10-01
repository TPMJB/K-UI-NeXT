# Boot and recovery on one card

The refreshed CD can load a normal image or a separate known-working image
from the same card. For the future ext4 application runtime, the recommended
layout is **128 MiB FAT32 for boot/recovery, with the remaining space as ext4
data**. Keeping the boot files outside ext4 lets recovery start even when the
data filesystem is dirty or uses a feature the CD's ext4 reader cannot handle.

**Keep the existing exFAT card unchanged for current use.** This change settles
the CD's loading interface; the current app runtime intentionally still rejects
two-partition cards. Full ext4 application access, writes, journal replay and a
filesystem-repair interface remain future card-delivered development. There is
no bundled repair program yet. `recovery.kui` is a separately retained runtime
image, not a claim that the image can repair ext4.

## Future card layout

| Volume | Files and purpose |
| --- | --- |
| 128 MiB FAT32 boot partition | `/KUI/runtime.kui` for normal startup; `/KUI/recovery.kui` for the retained working image |
| Remaining space, ext4 data partition | Games, music, settings, rips and other app data once the runtime supports ext4 |

Both partitions are on the same card/device. Partition names and filesystem
labels are optional conveniences; the loader never relies on them. The future
runtime must bind ordinary app operations to the validated ext4 data partition
and must never redirect failed data writes onto the boot partition. Updating
boot images must be an explicit operation, separate from ordinary app writes.

## Startup controls and file selection

| At startup | Behavior |
| --- | --- |
| No button | Try `/KUI/runtime.kui`, then `/KUI/recovery.kui` on that device if the first image is missing, unreadable or rejected |
| Hold X from power-on through the startup prompt | Try only `/KUI/recovery.kui`; do not substitute the normal image |
| Hold B | Stay in the built-in CD tools; B overrides loading |

The device search order remains SCIF, SCI, then IDE/CF. Each device's normal
and recovery attempts happen before advancing to another device. Cancellation
or allocation failure stops loading. Every accepted image passes the same
header, length and payload checksum checks and receives the selected transport.

When a FAT boot candidate exists, it is authoritative. The CD mounts only its
validated extent for loading, through a read-only view, and does not inspect or
mount the ext4 data filesystem. Missing/corrupt files on FAT never cause an
automatic switch to files on the Linux data partition. Whole-device exFAT/FAT32
and direct clean ext4 boot remain supported; direct ext4 is used only when no
FAT boot candidate exists.

Checksums detect damaged packages, not bugs in executable code. A package can
pass every check and then hang. Power off and boot holding X to bypass that
normal image. If no usable recovery image is found, the CD tools remain
available. X's meaning after the CD tools appear is their existing storage
check, not the startup selection control.

## Partition policy

The scanner uses 512-byte sectors and limits total capacity to `UINT32_MAX`
sectors. All candidates still require actual filesystem validation.

- Raw whole-device filesystems remain accepted.
- MBR permits at most one FAT candidate (`0x0B`, `0x0C` or `0x07`) and one Linux
  candidate (`0x83`), with non-overlapping, in-bounds extents. Extended, hybrid
  and unknown MBR partition types are rejected.
- GPT recognizes an EFI System or Microsoft Basic Data partition as a FAT
  candidate, and the Linux filesystem GUID as a Linux candidate. At most one
  of each is allowed; ESP plus Basic Data is ambiguous even if their labels
  differ. Unrelated GPT types are ignored for boot selection but their extents
  and GUIDs are still validated.
- GPT requires matching valid primary/backup headers and arrays, their CRCs,
  non-overlapping extents and unique partition GUIDs. Accepted headers are
  revision 1.0 and 92 bytes; entries are 128 bytes, with at most 128 entries.
  A damaged copy is rejected rather than repaired or silently substituted.

## Updating without replacing the recovery copy

1. Retain a backup on the computer. Install a hardware-tested, compatible
   runtime as `/KUI/recovery.kui` when first setting up recovery.
2. For a normal update, replace `/KUI/runtime.kui` and its matching app files.
   **Keep the working `recovery.kui` untouched**; exclude it when merging a
   package that includes a new recovery copy.
3. Finish writes, safely unmount/eject the card, then cold-boot and test the
   new runtime. Keep the older recovery image until the replacement is proven.

Renaming or replacing FAT files is not guaranteed atomic across power loss.
Keeping a second file reduces dependence on a bad runtime; it does not make
the FAT filesystem, partition table or card immune to corruption. If either
filesystem or the partition table becomes unreadable, repair or restore the
card on the computer and keep using the CD. Damage to card data alone does not
require reburning the CD.

A saved runtime by itself is not a complete application rollback: external
Games payloads and other app files may need the matching version. A future
dedicated repair image must start independently of those files and the ext4
data volume; all code needed to diagnose and recover that volume must be in
the recovery image or accessible on the boot partition.

## Read-only loading and later recovery software

The [Linux ext4 documentation](https://www.kernel.org/doc/html/latest/admin-guide/ext4.html#options)
explains that a read-only mount can still replay a journal; suppressing replay
also leaves an unclean filesystem inconsistent. K-UI therefore does not treat
"read-only" as permission to load arbitrary dirty ext4 data. Its direct ext4
bootstrap rejects recovery-needed volumes and disables journal operations in
[its configuration](../config/lwext4/generated/ext4_config.h), independently of
the [pinned library's journal implementation](../third_party/lwext4/src/ext4_journal.c).
The separate FAT boot path does not need ext4 recovery to start an image.

The 1.5 version-1 `KUIRUN1` envelope stays unchanged: a 64-byte header and a
validated payload of at most 4 MiB. Future recovery or application improvements
can be supplied as compatible card images. A future small second-stage image
could also retain this envelope while loading a larger program; that is a
possible extension, not a second stage included here. Fixes to the CD itself or
changes outside its supported transport/package contract may still require a
replacement CD.
