# Console inventory and verified system backups

System Tools can inspect the console's reported region, BIOS language/audio/
autostart settings and all five settings-flash partition bounds. A failed region
read remains Unknown. The boot BIOS is described as a visible 2 MiB read window;
the app does not infer a programmable chip model from its contents.

Two read-only console backup actions are available:

- **Settings flash:** all 128 KiB, including factory and game/network settings.
- **Visible BIOS:** the currently mapped 2 MiB bank. External bank switches are
  never changed and hidden banks are not claimed to be backed up.

Each uses a new `/KUI/backups/system/flash-NNNN/` or `bios-NNNN/` folder. It reads
each source chunk twice, saves `image.part`, syncs/closes it, then reopens the
file and compares every byte with the console memory again. Exact size, CRC32
and unchanged partition/identity information must agree before publishing
`image.bin`. `verified.txt` records the scope, size, CRC32 and partition map.
Existing backups are never overwritten. A stopped or failed copy keeps its
partial file without publishing it as a verified image. B can stop between
chunks. No multi-megabyte BIOS allocation is required.

Settings-flash files can contain private network/ISP information. Keep the images
on your own card/computer; a diagnostic result and checksum are sufficient for
our test record. BIOS backup does not demonstrate that a chip is writable or
that an image is compatible with another console.

The [32 FAT32/exFAT host image cases](evidence/m15-network-maintenance-host-2026-09-23.json)
cover inventory without SD writes, verified
flash/BIOS output, unique names preserving earlier backups, partition overlap,
identity changes, source failures/unstable reads, SD write/sync/reread/corruption,
and cancellation before/during copy or verification. Source adapters are faked;
console hardware acceptance is still pending.

The first hardware test is inventory, then a settings-flash backup. On a PC,
confirm `image.bin` is exactly 131072 bytes and its CRC32 matches `verified.txt`.
The visible BIOS backup should be exactly 2097152 bytes. Repeat only if you want
to confirm a second backup matches the first; no optical test is involved.

No region changing, flash erase/programming, chip-ID commands, or automatic
rollback is included. Those require the actual flash chip, board revision,
protection-unlock wiring and any dual-BIOS bank wiring to define a supported
hardware profile. The original mask ROM is read-only. These read-only backups
are the prerequisite for that work, not a claim that generic flashing is safe.

Primary references:

- [Pinned KOS flash interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/flashrom.h)
- [Pinned KOS flash implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/flashrom.c)
- [Sega Dreamcast hardware specification, memory map](https://segaretro.org/images/8/8b/Dreamcast_Hardware_Specification_Outline.pdf)
- [System flash partition layout](https://mc.pp.se/dc/flashmem.html)
