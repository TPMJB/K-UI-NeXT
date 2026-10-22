# Standalone SCIF, SCI and IDE/CF storage

Release 1.8.5 uses the selected transport for the CD bootstrap, runtime and
native Games reader. SCIF and SCI have scoped console storage/game results;
IDE/CF remains untested synchronous PIO. Read the
[current release notes](release-v1.8.5-notes.md) for compatibility evidence.
Applications and Games use exFAT/FAT32 through FatFs. The refreshed CD also
has a [read-only ext4 runtime loader](ext4-bootstrap.md), but runtime ext4 app
and game-library support is not enabled. Keep your existing card filesystem.

## Install on the selected device

1. Back up KUI. Merge the release's complete `KUI` folder into the card root,
   preserving preferences/dumps and any working `recovery.kui`. Runtime and
   all game payloads must come from the same build.
2. Keep the compatible bootstrap CD. SCI requires the SCI-capable graphical
   bootstrap from `6af5e11` or later; an old SCIF-only CD cannot gain drivers
   from a runtime file update. The release supplies a current CDI if needed.
3. Change adapters or wiring only with power off. Boot normally and verify
   1.8.5, its build ID and the selected storage transport.

Use `/KUI`, `/Games` and optional `/Music` at the device root; there is no
`/SCI` directory. IDE/CF uses the same layout. The planned CF board presents an
ATA slave beside the GD-ROM master; physical design/coexistence are unverified.
See [installation](release-v1.8.5.md).

## Selection and ownership

Auto discovery order is SCIF, SCI, then IDE/CF. The graphical boot menu starts
automatically after three seconds without input; any input pauses it. Startup
tries normal then recovery images on each device. Recovery or X on Home requests
recovery only. Home Left/Right can select one explicit transport for this
session. B returns/stops and still permits a later retry; Diagnostics opens the
built-in checks, and Card tools loads only optional `/KUI/tools.kui`.
If a FAT boot partition exists, files there are authoritative and the CD never
substitutes a runtime from ext4 data. Startup searches for a valid image;
the selected source is passed to the new runtime. Normal operations keep that
device selected. A failed read/write does not silently redirect the operation
onto another card. An idle boot menu can retry after SD card insertion in a
suitable socket. Power off before changing adapters, wiring, boards or IDE/CF;
do not remove media during an operation.

Games records the selected transport in its validated physical-sector map.
The high stage and resident reader use that same transport after the launcher
shuts down; filesystem code and launcher callbacks do not survive into games.
Only the selected transport's reader remains resident during gameplay. Native
readers and their private stacks now stay below the IP image; CE keeps
its separate placement. Guest checks protect firmware and all resident memory.
The temporary high stage installs the matching transport reader. See
[placement evidence](evidence/native-low-resident-2026-10-06.md).
Existing zero-valued transport maps mean SCIF. Unknown device IDs are rejected.

This is a **standalone-device implementation**. SCI microSD occupies the SCI
port and GPIO7 chip select. W5500 network operations are unavailable while SCI
storage owns that port. Extra chip selects do not enable simultaneous SCI
storage and networking;
a shared-bus implementation and hardware validation are still required.
The existing W5500 plus SCIF-card configuration remains supported.

SCI now has an [original bounded sector-DMA implementation](evidence/sci-dma-design-2026-10-01.md),
with block polling for ineligible buffers or a channel owned by another user.
It does not use the upstream DMA helper that previously stalled W5500 reads.

## Historical SCI measurements

The following rates belong to their recorded development builds, not new
1.8.5 performance measurements.

The initial polled runtime soak measured 522 KiB/s writes and 529 KiB/s reads; these are
filesystem-call measurements, not the bus clock rate or retail Games results.
The first DMA build passed a [416 MiB soak](evidence/sci-dma-soak-2026-10-01.md)
at 1,005 KiB/s writes and 926 KiB/s reads, with zero errors or DMA faults.
Later SCI and background-game work superseded the initial polled build.
Gameplay remains a separate measurement from filesystem throughput. IDE/CF
uses bounded PIO; optical reads and CF writes take turns on their common G1 bus.

Diagnostics > Storage tests provides Quick, Compare and Soak for the selected
device; see [storage testing](storage-testing.md). The owner's existing
SCIF/SCI soaks do not need repeating for an ordinary update. A title screen
is not a complete compatibility result. No formatting or bulk reripping is
required. Physical IDE/CF validation remains a separate future task.

## Future filesystem work

The block-device boundary is shared, while Games continues to consume validated
physical extents. IDE/CF therefore starts with the existing exFAT support.
The CD's readers load `/KUI/runtime.kui` or `/KUI/recovery.kui`; full ext4 use still requires
application filesystem operations and game extent export separately. It is not
enabled by detecting an ATA device. See
[the filesystem design](storage-filesystems.md).
