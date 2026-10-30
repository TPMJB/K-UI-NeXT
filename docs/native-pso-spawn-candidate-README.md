<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Combined ripper recovery, PSO card candidate and Spawn RAM-map test

Build: **@BUILD@**. This is a console test package, with the automatic
prefetch mode and automatic per-game cards retained.

This build merges the exact ripper source from **26a9b2193556** with the
native-game changes from **d3ec9affd380**. Both source histories are included.
The two branches changed separate files; the ripper recovery, repair and
capture changes are retained alongside the updated Games UI and VMU runtime.

PSO now has a complete four-entry terminal lifetime proof. Its extra CRT
startup entry is made terminal before any original game code resumes, and
the installed worker checks that entry alongside the ordinary exits. Host
tests and linked-code audits are separate from the console acceptance below.

This package's standalone New reader is the **RAM-map diagnostic**. A game
that falls back to that reader gets no virtual VMU and shows
**RAM MAP TEST - FREE PAGES MARKED**. Spawn is the intended diagnostic run.
The combined reader used by admitted VMU games does not mark RAM.

## Preserve the currently installed build

With the card mounted on the PC, run this Bash block once. It copies only
the three installed modules and their installer receipt to a local archive.
An existing checkpoint with this name is preserved. Continue after the
command finishes without an error.

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

This snapshot restores the immediately preceding installed build. The
installer's older `--restore` command instead restores its first General
backup, which may predate the currently working Code Veronica build.

## Install

Extract the complete ZIP, open a terminal in its package folder, and run:

```sh
python3 install.py /run/media/apollonius/Dreamcast \
  --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin \
  --runtime-checkpoint "$HOME/K-UI-before-@BUILD@.tar"
```

The installer recognizes the exact **26a9b2193556** ripper runtime even when
the older General receipt does not record it. Replacing that runtime requires
the checkpoint above to match all three currently installed modules and the
receipt byte for byte. A different unrecorded runtime or loader is preserved.

The existing shared prepared profile seeds the module; the launcher selects
the game's own card before an admitted launch. Installation reads the
profile and preserves card images, journals, bindings and game settings.
Safely eject the card, then boot K-UI. The runtime build ID must be
**@BUILD@**. Use each game's original GDI, **SCI**, **New (X)**,
**CD music Off**, and **RAM patches Off**.

The detector epoch changes to 6, so older executable-discovery caches are
rebuilt on first use. This does not reset a virtual card or its saves.

## Test 1: existing three-entry game

Boot Code Veronica and load the save you already proved working. This is
a short regression check of the shared runtime/configuration update; the
previous saving, power-cycle and Disc 2 results remain recorded.

## Test 2: PSO

1. Boot PSO with the settings above. It should use the combined virtual-A2
   reader, without the RAM-map banner. Photograph the first refusal screen
   if it stops instead.
2. Select the virtual A2 card if asked. Create or load an offline character,
   reach gameplay, play briefly, then save and quit through the game's menu.
3. Wait for save completion, fully power off, boot again and load that
   character. Report card recognition, gameplay, saving and reloading as
   separate results, with the build ID.

This check does not require an online connection. It establishes the tested
offline path, not every online mode or executable transition.

## Test 3: Spawn

Follow [the Spawn RAM-map guide](evidence/native-spawn-ram-map-test.md).
Run two sessions from a full power-off, with different characters/content
and actual stage or scene transitions. Photograph both complete return
screens, including all four **RAM UNUSED** words, and describe the modes
and approximate durations. This diagnostic does not enable virtual saving
for Spawn. The two captures guide a later reviewed allocation plan.

## Return to the exact previous build

With the card back on the PC, restore the checkpoint created above:

```sh
tar -xf "$HOME/K-UI-before-@BUILD@.tar" \
  -C /run/media/apollonius/Dreamcast
```

This restores only the three modules and their old receipt. Saves created
during testing, card profiles and discovery caches remain on the card.

The package contains the source history, build metadata and SHA-256 sums.
Its RAM-map helper is `tools/decode_native_ram_map.py`; uploading the photos
is sufficient for decoding them.
