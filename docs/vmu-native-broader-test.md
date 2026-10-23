# Broader native VMU test

This batch adds **Grandia II** and **Evolution: The World of Sacred Device**
as console test candidates for the automatic native VMU adapter. It retains
the optimized runtime whose SA2 gameplay the owner reported as imperceptibly
different from VMU-off on 23 October 2026. The new games have passed host
discovery, original SH allocator execution, layout transport, live ownership,
stage placement and persistence-model tests. Console save/load acceptance is
still required for this generic adapter.

Grandia's allocator has an unused address copy in an export table. Discovery
now proves which immutable startup call actually uses the initializer. The
layout detector also understands the equivalent R0/R2/R3 call-register
allocations. These are SDK-family rules used during startup. The prepared
runtime guards and durable save protocol retain their existing checks.

Evolution 1 already matches a complete reviewed family; its derived mapping
and independent original-code tests are now included. Evolution 2 uses a
different setup and is not covered by Evolution 1's result.

## Install and test Grandia II

Extract this new ZIP into a fresh folder. From that folder, with the SD card
mounted, run:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game grandia2
```

The command installs only the bound `KUI/apps/games/retail-boot.kui`. It reads
your existing prepared p0005 card/profile and leaves its save files and journal
in place. There is no backup/restore installer step. `base/KUI` is the unbound
template; use the command to personalize it before any manual module copying.

Use the original GDI, SCI / Standard and Auto/default encoding. Keep CDDA,
background loading and RAM patches off. Start with one controller and no
physical A1 VMU. The current generic adapter exposes virtual A1; the requested
physical-A1/virtual-A2 arrangement is a subsequent integration step.

During launch, the matching executable should display
`USING VERIFIED NATIVE MAPPING CACHE`. The catalog pins the exact startup
hash; it does not cache live allocator or SD ownership authority. All live
checks still run. A different game revision may require a cold scan and must
pass fresh discovery.

| Check | What to report |
| --- | --- |
| Launch and card recognition | Correct virtual card detection; photograph the full first refusal/error screen |
| Existing save, if present | Correct game/progress loaded |
| Gameplay | Several minutes of movement, a scene transition, audio and input responsiveness |
| New save | Save completes normally; wait for the game's save-complete indication |
| Power cycle | Fully power off, reboot the same game and load the updated progress |

If asked to format an existing card, stop and photograph the screen instead
of accepting. An earlier exact-title Grandia adapter's success is useful
history, but this automatic adapter needs its own console result.

## Then test Evolution 1

Reinstall using the same prepared card and change only the game selection:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1
```

Repeat recognition, gameplay, save and power-off/reload. Compare any FMV
behavior with this game's existing loader baseline separately from VMU
responsiveness; a working card does not establish disc playback parity.

## Existing games and automatic discovery

The catalog now contains `grandia2`, `evolution1`, `ps2`, `skies` and `sa2`.
Use `python3 personalize.py --list-games` to list the packaged selections.
The supplied Skies Disc 1 and Disc 2 bundles contain byte-identical startup
executables, so the same `--game skies` mapping applies to both; disc-switch
behavior still needs its own test.

Omitting `--game` enables the existing cold discovery path for another native
executable. It does not bypass an unsupported SDK family. First try the new
games with their cached mappings so startup scanning and game behavior are
easy to distinguish. No further SA2 performance test is required to start
this batch.

This package preserves normal, `--control` and `--profile-runtime` modes. The
optional timing instructions are in `docs/vmu-native-optimization-test.md`.
Select normal mode simply by running a command without either mode flag.

## Remaining family work

CT2 and PSO now pass the alternate-register layout proof, but additional
allocator/startup and terminal-lifetime requirements still prevent complete
admission. JGR gains allocator argument recognition but lacks a proved Maple
root transport. Time Stalkers is the next useful older SDK family: its direct
allocator and compact exits need a coherent adapter. Silver, Soul Reaver and
RECV also need explicit segmented memory-ownership proof. These titles remain
outside this console test catalog.

The software sequence remains compatibility, on-console save import/card
selection and A2, shared SCI/SCIF backing, then retained physical-disc play.
G1 hardware work stays after the SCI/SCIF lessons are established. This build
does not add SCIF VMU backing, combined CDDA/VMU or physical-disc VMU service.
