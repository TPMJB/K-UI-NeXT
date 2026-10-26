<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution component isolation @BUILD@

These two diagnostic builds separate virtual-card operation from background
reading. They retain the current SDK startup corrections, virtual A2 address,
384 KiB bottom reservation and existing card profile. Neither is a claimed fix.
The current combined New-reader result is a black screen; Legacy passes that
point but immediately reports that a memory card is required.

| Variant | Disc reader | Virtual VMU | What to check |
| --- | --- | --- | --- |
| `reader-only` | Current New (X), full 192 KiB read-ahead cache | Disabled | Opening FMV and title/gameplay transition |
| `vmu-only` | Standard synchronous reader, Background Off | Current A2, saving and loading enabled | Card recognition, opening FMV and existing save load |

The New reader uses the controller-completion support hook for its read-ahead
heartbeat. Reader-only retains that dependency and the current ownership and
memory guards, but does not advertise A2, synthesize card replies or call the
in-game VMU storage backend. It is a virtual-card-off control, not a hook-free
reader. If it still fails, shared support hooks remain a possible cause.

VMU-only uses the current VMU runtime with the independently linked Standard
reader. It does not enable New, Legacy, asynchronous SCI callbacks or the
read-ahead producer. Its extra reserved tail stays unused so that the RAM
reservation and bottom placement match the current combined build.

## Test reader-only first

Extract this archive into a fresh folder and run there:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

Safely eject and cold boot. For the same Evolution image, explicitly select
**SCI / Background reader New (X) / Auto encoding / CD music Off / RAM patches
Off**. Confirm the startup label identifies **NEW READER / VIRTUAL VMU OFF**.
A missing-memory-card warning is expected in this variant. Check whether the
opening movie plays and whether the game reaches its title/menu and gameplay.
Do not assess virtual saving in this variant.

## Test VMU-only

Run from the same extracted folder:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant vmu-only
```

Safely eject and cold boot. Explicitly select **Background reader Off** for
Evolution, retaining SCI and the other settings above. Confirm the startup
label identifies **STANDARD READER / VIRTUAL A2 ON**. Check card detection and
the opening movie. If it reaches the menu, try the existing A2 save. A new
save is unnecessary for this first comparison.

For each variant, report the startup label, whether the card warning appears,
whether the FMV plays, and the last working point. Photograph an automatic
error page if one appears. A full timing/PVR diagnostic recording is not
requested.

## Restore the installed loaders

```sh
python3 install.py /run/media/apollonius/Dreamcast --variant restore
```

This restores the exact two loader files backed up before the first isolation
installation, including whether either file was absent. Then select **New
(X)** again for normal combined-reader testing. The installer leaves the GUI,
saved launch choices, prepared profile, card, journal, identity and game images
in place. It verifies the expected current GUI before installing a variant.
Ordinary on-console prepared-card validation/recovery still runs at launch.

The VMU-only variant temporarily replaces the Standard game loader globally;
restore before testing other games. Switching to reader-only also restores
the original Standard loader. Keep the on-card isolation backup until restore
has completed successfully.

## Interpretation

If both isolated variants pass, the combined interaction becomes the main
lead. If VMU-only fails, investigate the current VMU/Standard path, including
the transition from the historical Standard/A1/192 KiB configuration. If
reader-only fails, investigate New plus its required support hooks. Neither
result alone proves a specific root cause.

Only the owner can establish the console result. Software checks verify the
selected paths, reserved geometry, disabled card backend, retained New cache,
ordinary Standard VMU behavior, startup corrections and reversible installer.
