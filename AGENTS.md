# K-UI updater-only integration

This branch starts at published main `0045dd96937ee7485ecd4fe0432b28babea8646a`.
Read `docs/accepted-progress.md` before building or packaging. Do not merge the
recovered ab1 loader tree to obtain updater code.

Preserve every main reader/stage source, reader recipe, worker/cache geometry,
game-selection policy and optional-file absence. No `src/loader/` file or
reader-specific Makefile is part of this change. Do not add Smash/VC2/Swirl
routing, session controls or experimental reader profiles.

Permitted GUI glue is limited to three bank-path existence checks in
`src/apps/games_retail.c`, and exact installed-reader recognition in
`src/core/native_vmu_standard.c`/its header and `src/core/games_cdda_profile.h`.
The verified Standard/CDDA headers prevent a GUI-only update from rejecting
already installed readers. They neither rebuild nor install a reader.
`tools/update_addon.py` checks the complete main reader source map and the
reviewed GUI glue before compiling. Do not regenerate it from arbitrary files.

The updater resolves inherited roles to root, including ordinary absence.
Declared replacements are pinned to their bank and never fall back to root.
Admission uses bounded software paths, not an exact catalog count. The current
builder retains main's 26-role catalog; the decoder also accepts valid older
34-role receipts, sparse manifests and allowed new roles. Do not populate
optional modules to satisfy a catalog.

Root-only migration delivers only KUI/runtime.kui, no .kup or KUI/apps files.
Preserve and rename KUI/system and optional KUI/boot.kui before replacing the
root GUI. Leave KUI/apps/system and all readers, saves and settings in place.
Verify the actual rename/root-copy/boot/restart flow on FAT32 and exFAT.
Restart K-UI uses worker action126; updates retain action120. Slots121–125 do
not import the recovered branch's unrelated actions.

Run the focused updater, shell, source-preservation and GUI-admission checks.
Do not repeat settled game compatibility matrices or rebuild readers for this
work. Source/host checks do not establish universal console compatibility.
The withdrawn full-cohort recipes are excluded from this updater-only port;
only GUI-only packaging is included. Future reader experiments need their own
reviewed replacement packages and matching GUI admission.
