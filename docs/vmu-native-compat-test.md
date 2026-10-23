# Native VMU compatibility and performance test

This update fixes the **Sonic Adventure 2 phase-2 allocator refusal** observed
with build `3cd64df45cfe`. Its first allocation lands near the top of RAM,
where the worker would overlap the old temporary installer stack. The installer
now uses a separate 64 KiB stack inside its protected stage BSS. The original
game heap size and allocator arguments are preserved. The worker also retains
the allocator's original address alias for ownership checks while using
physical addresses for DMA and protected-range comparisons.

Start with **Sonic Adventure 2**, then briefly regress Power Stone 2. The owner
reported successful Skies and Power Stone 2 launches on every boot; those
reports alone do not establish that Skies exercised the virtual VMU route.
The Skies startup predicts the same top-of-RAM allocation as SA2, so this fix
also needs an explicit Skies virtual-card save and power-off/reload result.
New-build console acceptance remains pending.

## Install and select a cached mapping

Extract the ZIP into a new folder. Keep your existing KUI installation and
prepared card. With the SD card mounted, run one command for the game to test:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game sa2
```

Use `--game skies` for Skies of Arcadia or `--game ps2` for Power Stone 2.
`python3 personalize.py --list-games` lists the exact executable hashes.
These mappings were generated from your uploaded startup files; another
revision automatically misses the cache and takes the full discovery path.
The mappings contain metadata, not game files.

The command installs only `KUI/apps/games/retail-boot.kui`, bound to your
existing p0005 preparation. It does not recreate, restore, copy or format the
card, journal, identity or profile. There is no backup/restore step. Merely
copying the generic `base/KUI` folder does not bind your prepared card.

Safely eject. Use the **original GDI**, **SCI / Standard**, Auto/default
encoding, CD music off, background loading off and RAM patches off. Start with
one controller and no physical VMU/accessories. Do not use the `-2048` copies
for this round. On a matching mapping, startup should show
`USING VERIFIED NATIVE MAPPING CACHE`. An uncached or mismatched image shows
`NO MATCHING NATIVE CACHE - SCANNING` and performs the complete discovery.
If the title requests formatting, stop and send the screen.

## Tests for your return

Start with SA2; use Power Stone 2 as a short regression, then test Skies.
There is no need to complete every optional row if an earlier row fails.

| Test | What to do | What to record |
| --- | --- | --- |
| Skies, cached startup | Install `--game skies`; boot the original Disc 1 GDI | Time to bootloader, time after it, cache message, any refusal screen |
| Skies, streaming and play | Watch the intro, play through several scene/area transitions for 15–20 minutes | FMV/audio, input response, scene loads, whether slowdown grows |
| Skies, persistence | Create a test save, wait for save completion, fully power off; reboot and load | Card detection, save completion and correct reloaded progress |
| SA2, cached startup | Install `--game sa2`; boot its original GDI | Same startup timings and cache message |
| SA2, streaming and play | Watch the intro and play several stages/transitions for 15–20 minutes | Audio, video, input response and any intermittent pauses |
| SA2, persistence | Save, fully power off, reboot and load | Correct reloaded progress |
| PS2 regression | Install `--game ps2`; short FMV/gameplay and existing save load | Whether it remains comparable to the smooth prior build |
| Cold-scan comparison | Reinstall without `--game`; repeat one successful title's startup | Scan time versus the matching cached mapping; same behavior afterward |
| Repeatability | Reinstall the matching `--game`; do three cold boots of a successful title | Cache message on every boot; consistent save availability |
| Extra controllers, optional | After single-controller success, boot with two and then four controllers | Detection, responsiveness and save/load; note any refusal |
| Physical accessory query, optional | After normal success, try one physical rumble accessory in A2 | Startup and play; photograph a refusal instead of changing settings |

For each game, report the build shown on screen, title/revision, selected
profile, launch settings, cache message, timings and the earliest failure.
Photograph the complete diagnostic screen. A simple results sheet is included
as `TEST-RESULTS.csv`.

The virtual RESET command and bounded physical accessory information/condition
queries are included. Physical accessory writes, rumble activation commands,
virtual KILL, batches without a port-A parent, and entirely virtual batches
remain outside this adapter. A physical A1 conflict still refuses. CDDA plus
VMU, SCIF/IDE VMU backing, multi-disc handoff and seamless launcher return are
not enabled by this test.

## What is cached, and what still runs

The 1,024-byte mapping binds driver, initializer, allocator, patch and exit
metadata to an exact executable SHA256, size, load address and detector epoch.
Every use hashes the current executable again and checks the independently
pinned record digest. A mismatch runs full discovery; live heap ownership,
layout, hook windows and storage checks are never reused from a previous boot.
This removes repeated address searches, not the game read, card recovery or
all startup verification.

**This test module uses a mapping selected by `--game`.** Automatic first-scan
file writing is also implemented and host-tested in the launcher source, but
the recovered workspace lacks the full UI build dependencies. This ZIP does
not replace your existing shell with an unverified reconstructed UI binary.
Once that frontend is built, it writes per-image mappings under
`KUI/VMU/native-cache/v1/` before shutdown and supplies the same module seed.
See `docs/vmu-native-launch-cache.md` for that integration boundary.

Host instrumentation reduces a steady two-controller poll from 450 to 344
guest-memory/MMIO reads and four controllers from 476 to 366. A fragmented
heap release drops from 1,056 to 543 reads. Fresh-SHA-inclusive mapping replay
uses roughly one-third the host CPU time of full discovery on these three
startups. These are work measurements; console timings and frame rate must
come from your tests. Save journal, readback and storage-restoration checks
remain enabled.

## Current compatibility boundary

| Uploaded startup | Result |
| --- | --- |
| Power Stone 2 | Existing hardware baseline; new build needs regression |
| Skies of Arcadia, Disc 1 | New host-proven family; console candidate |
| Sonic Adventure 2 | New host-proven family; console candidate |
| Resident Evil Code: Veronica | Static discovery passes; live buffer/image overlap correctly refuses |
| Grandia II | Allocator discovery is ambiguous |
| Crazy Taxi 2 | Allocator found; initializer family not yet admitted |
| Phantasy Star Online | Layout and terminal-entry family not yet admitted |
| Evolution 2 | Timer/layout and terminal-entry family not yet admitted |
| Soul Reaver | Allocator/layout and terminal-entry family not yet admitted |

The ZIP includes a source checkpoint, detailed audit report, runtime cost
measurements and the SWAT feature comparison. The source history is a recovered
feature checkout: port focused changes onto the original branch rather than
merging its synthetic recovery root over main.
