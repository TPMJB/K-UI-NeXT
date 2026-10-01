# Standalone SCIF, SCI and IDE/CF storage

This development build adds storage discovery to the CD bootstrap, runtime and
native Games reader. SCIF is the existing hardware-tested path. SCI microSD
has now passed a [15-minute runtime storage soak](evidence/sci-soak-baseline-2026-10-01.md)
with zero errors, but measured throughput is below SCIF in this initial build.
SCI retail Games and IDE/CF still need console validation.
Application filesystems remain exFAT/FAT32 through FatFs. The refreshed CD also
includes a [read-only ext4 runtime loader](ext4-bootstrap.md), preparing for a
later runtime with ext4 app support. Keep the existing working card filesystem.
The [boot/recovery plan](boot-recovery.md) recommends a future 128 MiB FAT32
boot partition plus ext4 data on the same card. The CD supports that loading
interface now; current apps still intentionally reject two-partition media.

## Install for one-card SCI testing

1. Keep the existing card's filesystem, games and preferences. Copy this run's
   `KUI/runtime.kui` and `KUI/apps/games/retail-boot.kui` onto that card. Updating
   the matching `KUI` application assets keeps the runtime and game loader
   together. Preserve an existing known-working `KUI/recovery.kui` when merging
   the package; do not replace both runtime copies during an ordinary update.
2. Use the SCI-capable **`6af5e11` boot CD** (or a later compatible bootstrap).
   Older boot CDs only look at SCIF; replacing a file cannot update the burned
   CD's storage driver. The diagnostics runtime update does not require another
   burn when that SCI-capable CD is already in use.
3. With power off, move the same card to the standalone SCI microSD board and
   connect it in place of the W5500. No second microSD card is required.
4. Boot normally. Check that the runtime build matches the download and the
   diagnostic storage line identifies **SCI**, not SCIF or CD fallback.

Keep the directories at the card root: `/KUI`, `/Games` and optional `/Music`.
There is no `/SCI` directory. IDE/CF uses the same directory layout.
The CF board is intended to present an ATA slave beside the GD-ROM master.
Its physical design and the new ATA driver remain unverified on hardware.

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
Only the selected transport's reader remains resident during gameplay. Each
reader keeps the original protected low-memory and stack limits; the temporary
high stage contains all three and installs the matching one.
Existing zero-valued transport maps mean SCIF. Unknown device IDs are rejected.

This is a **standalone-device implementation**. SCI microSD occupies the SCI
port and GPIO7 chip select. W5500 network operations are unavailable while SCI
storage owns that port. Sharing the wires between SD and Ethernet/Wi-Fi, extra
chip selects, and simultaneous-device arbitration are outside this change.
The existing W5500 plus SCIF-card configuration remains supported.

SCI now has an [original bounded sector-DMA implementation](evidence/sci-dma-design-2026-10-01.md),
with block polling for ineligible buffers or a channel owned by another user.
It does not use the upstream DMA helper that previously stalled W5500 reads.
The initial polled runtime soak measured 522 KiB/s writes and 529 KiB/s reads; these are
filesystem-call measurements, not the bus clock rate or retail Games results.
The first DMA build passed a [416 MiB soak](evidence/sci-dma-soak-2026-10-01.md)
at 1,005 KiB/s writes and 926 KiB/s reads, with zero errors or DMA faults.
DOA2 improved substantially but retains a little lag. The next processing
optimization and broader card/module compatibility remain hardware pending.
Gameplay needs its own console measurement. IDE/CF initially uses bounded PIO; optical reads and CF writes
must take turns on their common G1 bus.

## First console check

Use a card with free space. Diagnostics → R → Storage tests provides Quick,
Compare and Soak on the device selected at boot; see the
[storage testing guide](storage-testing.md). The owner's SCIF and SCI 15-minute
soaks are now complete; no repeat is needed before the first game test.
The legacy benchmark remains under Storage tests → Advanced, or Benchmarks in
the CD menu. It uses `/KUI/bench.cfg`; [t13-sci-storage.cfg](bench-cfgs/t13-sci-storage.cfg)
explicitly selects SCI. The new Storage tests presets do not use that file.
An absent or unsupported device should report failure and return to recovery,
not hang.

Start with one DOA2 run using the existing image: record character selection
to first-stage load time, the first ten seconds of fighting, one FMV and return
to K-UI with A+B+X+Y+Start. Copy the SCI-capable `retail-boot.kui` from build
`3a368ddcfaff` into `/KUI/apps/games/` first; a runtime-only update leaves the
previous Games reader in place. The launcher validates checksums and layout,
but does not reject every older SCIF-only payload before handing off to it.
Evolution 2, further transitions and VMU save/load can follow. Compare
against the accepted SCIF build using the same game files. A title screen is
not a complete compatibility result. Faster storage does not add Windows CE
or image-backed CD audio support.

After that, check one known-good disc rip and its verification on the new
medium before expanding the game list. Keep the previous SCIF build available
for comparisons. No formatting or bulk reripping is required for this check.

## Future filesystem work

The block-device boundary is shared, while Games continues to consume validated
physical extents. IDE/CF therefore starts with the existing exFAT support.
The CD's readers load `/KUI/runtime.kui` or `/KUI/recovery.kui`; full ext4 use still requires
application filesystem operations and game extent export separately. It is not
enabled by detecting an ATA device. See
[the filesystem design](storage-filesystems.md).
