<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE batched-proof VMU test

The preceding b5953f7b6d67 first-read test stopped at AUTO reason 6/phase2:
1,200 completion visits, all DMA PRE/POST/lease/restore counts paired, zero
recorded traffic/fence faults, and no virtual-card MEDIA or READ request.
The owner observed an opening FMV at approximately 4 fps with repeating audio,
then skipped it. This was a fallback capture, not a passed block-read test.

## Working theory

The strongest current lead is excessive time spent in interrupt-masked,
uncached helper code during ordinary controller polling. The extra A2 path
performs two ownership queries even for parent-only advertisements, and four
when a target reply is present. The old test repeated expensive coherent
cache-tag/data checks for individual nearby metadata words. The balanced DMA
counters show continued returns, but cannot measure interrupt latency or prove
which missed deadline caused the video/audio behavior.

The new implementation batches nearby words into one query-local 32-byte line
snapshot and validates every used word from a fresh final snapshot after all
page-table rewalks. It accepts legitimate intervening eviction/writeback only
when the current data still matches. It rejects changed mappings/data,
unstable tags and dirty alternate aliases. No saved mapping, line, tag or
buffer ownership survives into another public query.

A representative query drops from 462 to 264 total read callbacks; coherent
cache/data callbacks drop from 360 to 162. That is a 43 percent reduction in this
query's callbacks, not a measured43percent improvement in game performance.
This package does not yet establish the hardware cause or a performance fix.

The original CE DEVINFO handler transitions the new unit to its generic
storage dispatch, but does not itself issue MEDIA/READ. The absence of those
requests is compatible with the application not yet opening the virtual A2.
Explicit card selection/loading is therefore part of this test.

## Install and run

Back up your current `KUI/apps/games/ce-probe.kui`. With power off, replace only
that file using the matching numbered folder. Keep the same original Armada
raw GDI and settings: **SCI SD; Background On; CD music Off; virtual VMU Off**.
Physical VMU in A1; A2 physically empty; controller connected. The module
provides the same generated, empty, read-only A2 card. It opens no SD save
image and performs no emulated save writes.

| Folder | Intended result | Expected AUTO reason/phase |
| --- | --- | --- |
| 01-first-read | Stop after CE returns from the first virtual A2 block reply | 3 / 2 |
| 02-read-finalize | Stop after CE returns from the matching read-finalization ACK | 4 / 2 |
| 03-runtime | Run with the same optimized proof and no automatic stop | None; ABXY+Start shows reports if the game handles reset |

1. Start with **01-first-read**. Compare a few seconds of the opening FMV
   with the very slow prior run; report whether video speed and audio improve.
2. Skip the intro if needed, then open Armada's load/memory-card selection
   screen and inspect/select the blank A2 card. There are no files to load on
   it. Do not create a save or format it at this read-only stage.
3. The module should stop at AUTO reason 3 / phase 2 after the first A2 block
   read. Photograph TRAFFIC, FENCE, A2, MAPPING and DRIVER, including fault rows.
4. If the expected read checkpoint succeeds without a fault, power off and
   try **02-read-finalize** the same way. Its expected reason is 4 / phase 2.
5. If both checkpoints succeed, **03-runtime** is available for checking card
   visibility, empty-card behavior, physical A1 loading and ordinary playback.
   Use ABXY+Start for its report if responsive, then power off.

For automatic variants, reason 5 means a latched A2 fault and reason 6 means
7,200 completion visits occurred without the selected event. Report either
before continuing. A freeze before a checkpoint can still happen; these are
deterministic terminal checkpoints, not an independent watchdog. If A2 is
absent, photograph the card-selection screen too. Exact large counters are
hexadecimal; counters in the prior fallback were 0x4b0 = 1,200.

An all-zero MAPPING/DRIVER page can occur if capture precedes the existing
PLAY-triggered observer. TRAFFIC contains original physical responses,
including a hardware timeout before A2 synthesis; A2 contains the synthetic
response evidence. Restore your backup after testing. No rollback module is
included because the package cannot know the file currently on your SD.

## Remaining work toward the broader goal

Discovery and device information have hardware passes. Block reads and their
finalization, usable mount behavior, sustained performance and SD-backed
write/reload/power-cycle recovery still need proof. A common VMU backend is
already possible, but automatic matching needs separate validated native-SDK
and CE adapters. Physical GD-ROM support additionally needs a retained disc
bootstrap and an independent save-storage provider; the present GD Play BIOS
reboot discards the resident runtime. See the working-theory note in source
for exact implementation anchors and the proposed integration order.
