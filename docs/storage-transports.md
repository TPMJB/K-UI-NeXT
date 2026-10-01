# Standalone SCIF, SCI and IDE/CF storage

This development build adds storage discovery to the CD bootstrap, runtime and
native Games reader. SCIF is the existing hardware-tested path. SCI microSD and
IDE/CF need console validation; a successful build is not a hardware pass.
The filesystem remains exFAT/FAT32 through FatFs. ext4 is future work.

## Install for one-card SCI testing

1. Keep the existing card's filesystem, games and preferences. Copy this run's
   `KUI/runtime.kui` and `KUI/apps/games/retail-boot.kui` onto that card. Updating
   the complete `KUI` folder from the SD-update artifact also supplies the
   matching application assets. Do not mix runtime and game-loader builds.
2. Burn **this run's `kui-bootstrap.cdi`** from the bootstrap artifact once.
   Older boot CDs only look at SCIF; replacing a file cannot update the burned
   CD's storage driver. Later compatible runtime updates can reuse the new CD.
3. With power off, move the same card to the standalone SCI microSD board and
   connect it in place of the W5500. No second microSD card is required.
4. Boot normally. Check that the runtime build matches the download and the
   diagnostic storage line identifies **SCI**, not SCIF or CD fallback.

Keep the directories at the card root: `/KUI`, `/Games` and optional `/Music`.
There is no `/SCI` directory. IDE/CF uses the same directory layout.
The CF board is intended to present an ATA slave beside the GD-ROM master.
Its physical design and the new ATA driver remain unverified on hardware.

## Selection and ownership

Discovery order is SCIF, SCI, then IDE/CF. Startup searches for a valid runtime;
the selected source is passed to the new runtime. Normal operations keep that
device selected. A failed read/write does not silently redirect the operation
onto another card. Power off before changing devices.

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

SCI uses bounded hardware-clocked, polled transfers for this initial build.
It does not use the upstream DMA path that previously stalled W5500 reads.
Actual SD speed, card/module compatibility and gameplay behavior need console
measurements. IDE/CF initially uses bounded PIO; optical reads and CF writes
must take turns on their common G1 bus.

## First console check

Use a card with free space. First confirm startup, Games browsing, and a saved
diagnostics report on the selected medium. Then use Diagnostics' existing
write/reread check to verify data integrity before trying a new rip. For the
optional SD throughput benchmark, copy [t13-sci-storage.cfg](bench-cfgs/t13-sci-storage.cfg)
to `/KUI/bench.cfg` on the same card before pressing R; its explicit SCI setting
avoids the benchmark's default SCIF target. An absent
or unsupported device should report failure and return to recovery, not hang.

Try DOA2 and Evolution 2 first, recording the build and selected transport,
time to gameplay, transitions, FMV/audio behavior and VMU save/load. Compare
against the accepted SCIF build using the same game files. A title screen is
not a complete compatibility result. Faster storage does not add Windows CE
or image-backed CD audio support.

After that, check one known-good disc rip and its verification on the new
medium before expanding the game list. Keep the previous SCIF build available
for comparisons. No formatting or bulk reripping is required for this check.

## Future filesystem work

The block-device boundary is shared, while Games continues to consume validated
physical extents. IDE/CF therefore starts with the existing exFAT support.
Adding ext4 will require filesystem operations and extent export separately;
it is not enabled by detecting an ATA device. See
[the filesystem design](storage-filesystems.md).
