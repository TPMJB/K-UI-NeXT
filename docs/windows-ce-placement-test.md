# Windows CE placement test

This is the first step toward running Windows CE games. It does **not** start the
game. It loads the Windows CE kernel from the card into the places the Dreamcast
BIOS would put it, checks it, shows what it found, and stops. Nothing runs
Windows CE yet.

## What it needs

- `KUI/apps/games/ce-probe.kui` and `KUI/runtime.kui` from the same build.
- A raw GDI (2352-byte tracks) whose IP selects Windows CE, for example ARMADA.
- Any storage transport. SCI microSD is the planned one for CE.

## How to run it

1. Games, select the CE game, A to inspect it.
2. The detail screen says `A Windows CE placement test`. Press A.
3. The confirmation says the test does not start the game. Press A.
4. The launcher closes. The stage loads IP.BIN, then the kernel file
   (`0WINCEOS.BIN`), checking every sector's header and every SD block's CRC.
5. Photograph the final screen, then power cycle.

## What it does

The kernel file is a 2048-byte load prefix followed by the kernel body. The
prefix's header (one load section: address, file offset, length, entry) says
where the body goes. Following Flycast's independent BIOS (see the
[loader audit](evidence/windows-ce-loader-audit-2026-10-01.md)), the prefix
goes to `0x8ce01000` and the body to `0x8c010000`.

The native stage occupies `0x8ce00000`, which covers the prefix's place, so
this test has its own package, `ce-probe.kui`, whose stage is linked 64 KiB
higher, at `0x8ce10000`. Its header carries the magic `KUIRCE01`; the launcher
refuses to mix it up with `retail-boot.kui`, and each stage checks the IP's
Windows CE flag itself.

The stage then:

1. Reads the prefix sector and builds the load plan
   (`src/core/ce_load_plan.c`) against everything still in use: firmware, IP
   and bootstrap area (`0x8c000000`–`0x8c010000`) and the stage with its stack
   (`0x8ce10000`–`0x8cff0000`). A rejected plan stops with `CE LOAD PLAN
   REJECTED`, its result code and the header fields.
2. Loads the body to `0x8c010000` and copies the prefix to `0x8ce01000`.
3. Shows the checksums, placement, the kernel's ROM header if it has one, and
   the IP's boot bytes.

## The result screen

| Line | Meaning |
| --- | --- |
| `PREFIX CRC32` | CRC32 of the 2048-byte prefix, read back from `0x8ce01000` |
| `BODY CRC32` | CRC32 of the body bytes in place at `0x8c010000` |
| `PREFIX AT` | Where the prefix was put |
| `BODY AT / BYTES / SECTORS / ENTRY` | The body's place, length, sectors read and the entry address |
| `ROMHDR / PHYSFRST / PHYSLAST / RAMSTART / RAMEND` | The CE ROM header found through `ECEC` at body offset 0x40: the kernel image's span and the RAM CE will use |
| `NO CE ROM HEADER AT BODY OFFSET 40` | Shown instead when the body has no such header (Worms Armageddon) |
| `IP F0 / F4 / F8 / FC` | Four words of the IP at 0xF0–0xFF, the boot information the next step needs |
| `STORAGE BLOCKS READ` | SD blocks read by the stage |
| `PLACEMENT CHECKED - CE WAS NOT STARTED` | The test finished |

Expected values, from the kernel files the owner supplied:

| Game | Prefix CRC32 | Body CRC32 | Bytes | Sectors | ROM header | Physlast | RAM start | RAM end |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ARMADA | `6200729C` | `B9F755BC` | `00131800` | `00000263` | `8C141420` | `8C141778` | `8C142000` | `8CFD0000` |
| Bust-A-Move 4 | `70513A3C` | `F6902800` | `001BE800` | `0000037D` | `8C1CE19C` | `8C1CE5B4` | `8C1CF000` | `8CFD0000` |
| Worms Armageddon | `EFFD7677` | `CD686951` | `0010E800` | `0000021D` | none | | | |

All three have body and entry at `8C010000`, and physfirst `8C010000` where a
ROM header exists.

## What it tells us

- That the prefix/body split, the higher stage and the sector checks work on
  the console with a real CE disc image.
- The RAM CE will claim. ARMADA's kernel uses `0x8c142000`–`0x8cfd0000` plus
  its driver globals up to `0x8d000000`, which covers the CE stage. The stage
  must therefore hand over before CE's kernel starts, and keep nothing there
  afterwards. The low resident (`0x8c008300`) sits below the kernel image;
  whether CE leaves it alone is the next step's question.

## Next steps

1. Enter CE: keep the stage alive until its relay finishes, enter the
   bootstrap the way the BIOS would for a CE disc, and report the entry state.
2. Trace CE's first GD calls (wsegacd.dll calls the BIOS GD vector) with their
   addresses before mapping them, and the interrupt waits (IDs 20/21).
3. Implement only what that trace shows: address mapping, the CHECK contract,
   completion for the IRQ waits, and the missing commands.
