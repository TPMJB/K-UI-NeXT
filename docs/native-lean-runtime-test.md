<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Lean native VMU runtime

Build: **@BUILD@**. This candidate reduces the native combined reader/virtual-A2
runtime's memory reservation while keeping the full 192 KiB prefetch cache,
Prefetch Auto, physical A1 handling, per-game cards and durable saving.

| Game-heap reservation | Previous c7 build | This candidate |
| --- | ---: | ---: |
| Worker, including private stack | 128 KiB | 112 KiB |
| Maps, reader state, auxiliary and its private stack | 64 KiB | 20 KiB |
| Disc prefetch cache | 192 KiB | 192 KiB |
| Total payload | 384 KiB | 324 KiB |

The original allocator also uses its unchanged 32-byte allocation header.
The 60 KiB reduction is real capacity returned to games. Worker code/data and
state were reduced by removing an unnecessary intermediate WRITE copy,
compacting response metadata, and excluding diagnostic collectors which the
General build already refused to enable. Normal failure reports remain.
Maximum Maple batch sizes, reply publication checks, CRC checks, save-journal
ordering and the private 4 KiB stacks are retained.

General also removes the inherited 180,000-transaction diagnostic stopping
limit. Submitted/completed counters retain their exact pending-pair checks
across unsigned wrap; boundary tests include real WRITE/SYNC/READ and journal
recovery. Explicit diagnostic builds keep their finite test budgets.

The linked-code and host evidence in this package establishes software checks,
not a full-game hardware pass. This candidate changes memory layout and needs
console confirmation. Spawn remains on its standalone reader without a virtual
card; its terminal SD counters are retained. The earlier PSO/Spawn intro
playback investigation remains open. The old Lodoss/GameShark crash described
by the owner occurred around twenty years ago and is not a K-UI regression.

## Preserve the currently installed build

With the card mounted on the PC, run this Bash block once. Continue when it
finishes without an error. The no-clobber setting preserves an existing archive.

```bash
(
  set -C
  tar -c -C /run/media/apollonius/Dreamcast \
    KUI/runtime.kui \
    KUI/apps/games/retail-native-dual.kui \
    KUI/apps/games/retail-native-standalone.kui \
    KUI/native-general/state.json \
    > "$HOME/K-UI-before-@BUILD@.tar"
)
```

## Install

Extract the complete ZIP, open a terminal in its package folder, and run:

```bash
python3 install.py /run/media/apollonius/Dreamcast \
  --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin \
  --runtime-checkpoint "$HOME/K-UI-before-@BUILD@.tar"
```

The installer verifies the exact checkpoint and updates the matching GUI and
two native modules. It preserves card images, journals, profiles, settings and
the first General backup. If another task changed a managed file, the normal
refusal guard preserves it. Safely eject before booting the console.

## Short console checks

Use each game's original GDI, **SCI**, **New (X)**, **CD music Off** and
**RAM patches Off**, with the same p0005 profile.

1. **Code Veronica:** load an existing virtual-A2 save, play briefly, save to
   an available slot, power off fully, then boot and reload that save.
2. **PSO:** compare the same intro and ordinary gameplay with the previous
   working build. Note any change in video/audio or card detection. Its save
   and full-power-cycle behavior remain separate checks if not yet performed.
3. **Physical A1:** when present, confirm it remains visible alongside virtual
   A2. No new card or formatting is required.

A long Lodoss playthrough is not required for this first check. Ordinary
Lodoss/Rayman gameplay and save/load remain useful additional coverage.
Report the build ID and exact game/action if anything differs. A complete blue
failure screen is useful; reboot rather than repeatedly retrying a failure.

## Return to the exact previous build

With the card mounted on the PC:

```bash
tar -xf "$HOME/K-UI-before-@BUILD@.tar" \
  -C /run/media/apollonius/Dreamcast
```

This restores the three modules and receipt while keeping new saves, journals,
cards and caches. The installer's `--restore` returns the first General backup,
which may be much older than the checkpoint above.

The package includes full source history, build/validation evidence and SHA-256
sums. The ripper sources already incorporated into c7 are preserved.
