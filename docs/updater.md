# Offline updates

The updater selects checked software from the SD card without Dreamcast
internet access. This add-on changes the GUI only. It delivers no
`KUI/apps/` files, rebuilds no reader and preserves installed readers,
helpers, optional-file absence, cards, journals and preferences.

The integration preserves published main reader sources and the verified
GUI admission for already installed Standard/CDDA readers. See [accepted progress](accepted-progress.md).
A newer GUI must retain those bindings even when reader files stay unchanged.

## The cohort error and initial migration

The ab1 updater expects exactly 34 manifest entries. The earlier folders/restart
GUIs c94129de53a3 and f5b1baf5683b expect exactly 26. A 26-entry package fails in
the former; the 34-entry compatibility repair fails in the latter. Both report
**Incomplete update cohort**. These failures were reproduced with the actual
old decoder and shipped bytes. Inheritance policy 2 exists in both versions;
adding optional reader files to the card cannot repair this count mismatch.

The repaired selector validates allowed software paths instead of requiring a
compiled exact cohort. The builder retains main's 26-role catalog; admission does not depend on that count. These are descriptors, not a requirement that
34 physical files exist.

**Use the manual root-only repair to remove both old package gates.**
Installing only a new version bank leaves the old `KUI/runtime.kui`
dispatcher in charge at the next cold boot.

1. With the console off, back up the current `KUI/runtime.kui` on the computer.
2. Rename SD `KUI/system` to an unused backup name such as
   `KUI/system-before-updater-repair`. This retains installed banks and retires
   their saved activation. Leave `KUI/apps/system` and all of `KUI/apps` in place.
3. If `KUI/boot.kui` exists, rename it to an unused backup name such as
   `KUI/boot-before-updater-repair.kui`. Unattended CD boot otherwise tries that
   override before the root runtime.
4. Copy the repair's `KUI/runtime.kui` to that exact SD path. Keep installed
   readers and user files; merge rather than replacing the whole KUI folder.
5. Boot normally and confirm the repair build. Restart K-UI should return to
   that build. No B timing or activation through the older updater is required.

**Original baseline** means the GUI currently stored at `KUI/runtime.kui`.
After the manual replacement, it means the repair GUI, not the older backup
on the computer. Holding B bypasses selection for one boot without changing
the persistent record. R Original baseline changes that record; it does not
restore files. Restore the pre-migration GUI manually if needed, preserving
all current save data.

The root-only repair ZIP deliberately contains no `.kup`; do not select it in
the Updates menu. The previous 34-entry bridge enters through the old ab1 menu
but fails in the folders/restart GUI. One bridge cannot satisfy both old exact
counts. The manual migration bypasses either decoder and retires stale banks.
The updater recreates `KUI/system` as needed. To undo, restore the backed-up
runtime and renamed system/optional boot paths while retaining current saves.

## Folders, ZIPs and direct restart

Put trusted packages below `KUI/updates`; subfolders such as
`KUI/updates/test/WinCE` are supported. In **Settings → Updates**:

- **A** opens a folder or checks a `.kup` / updater-ready `.zip`.
- **B** returns to its parent; at the Updates root it returns to Settings.
- **X** refreshes the current folder.
- **START** opens Restart K-UI from the folder list or result screen, including
  when the selected version has not changed.

Review the target build, channel and replacement/inheritance counts, confirm
installation, then choose **Restart K-UI**. It fully rereads and verifies the
selected GUI, releases storage and audio services, and hands over directly.
The separate console-restart action still returns to BIOS. Restarting K-UI
does not require a boot-CD trip for each test.

A ZIP must contain exactly one embedded `.kup`. Only that member is staged
in a private cache and passed to the normal package checker/installer.
Bundled source, evidence and old root-runtime files are ignored. ZIP extraction
runs again before installation; replacing the archive after review cannot
silently reuse an earlier checked package. Stored/deflate ZIP32 is supported;
encrypted, split, ZIP64 and unsupported archives are refused.

Old manual-install ZIPs with no `.kup`, or archives containing several
alternative packages, require explicit conversion. The updater does not run
their installer scripts or guess a variant. Prepare a collection of
updater-ready test ZIPs once, then keep them on SD for repeated selections.
The menu shows eight entries per page, so a dozen variants need two pages.

Each experiment needs a distinct admitted build ID. Renaming a file does not
change its identity: banks use the twelve-character build ID. Different bytes
or manifests under an existing ID are refused. Reader experiments must declare
their intended software replacements and verify actual game-route selection;
the GUI-only builder does not create such experiments.

For CDDA experiments, prepare one updater-ready package per test with the
intended reader replacements and a GUI that admits their exact identities.
Place them in `KUI/updates/test`, choose a package, install, and use
**START -> Restart K-UI -> A** before launching the game. Repeat from the GUI
to switch tests without a BIOS/boot-CD trip. The root-only repair and GUI-only
packages retain the installed CDDA readers; choosing those does not change the
CDDA implementation. Old manual-copy test ZIPs still require conversion.

Returning from a running game is separate. **A+B+X+Y+Start** can invoke the
existing BIOS-menu reboot hook on supported games, sometimes after returning
to the title screen first. The hook returns through ROM and the boot CD and
can display its report first. Direct return to the SD GUI exists only in
optional native session-control modules; this GUI repair does not install
them. Universal CDDA/WinCE direct return remains unimplemented, and terminal
diagnostic builds may still require a reset. See
[native session controls](native-session-controls.md).

The startup **K-UI / Selecting installed version** screen covers SD setup,
mounting and saved-version checks. It has no programmed wait. With the root
GUI selected, the selector reads the root header instead of rereading its
whole payload. Its duration on console is not established by host tests;
the owner reported a longer-than-desired pause after the successful repair.

## Software paths and banks

The decoder accepts one to 64 unique entries with `runtime.kui` first.
Additional `.kui` roles may use flat lowercase filenames under `apps/games/`
or `apps/disc/`, with letters, digits, dots, underscores and hyphens.
Traversal, nested module directories and user-data paths are refused.
Three retained raw CE-helper `.bin` paths are allowed for inheritance only.

The [default26 role list](../include/kui/update_catalog.inc) is a builder/bridge
profile, not a mandatory exact list embedded in admission. A package may omit
allowed non-GUI roles; those roles retain their root resolution.

| Policy | Contents | Resolution |
| --- | --- | --- |
| 0 — Replace | Current-build `.kui`, exact size and SHA-256 | Checked bank file; missing/corrupt never falls back |
| 1 — Approved legacy | Exact path/build/size/SHA pin | Checked bank file; arbitrary old binaries are refused |
| 2 — Inherit root | Size zero, SHA all zero, no payload | Existing root file, including its ordinary missing-file result |
| Omitted role | No manifest entry or payload | Same root inheritance for an allowed non-GUI path |

The GUI is always a replacement. The add-on payload contains only that GUI;
all other default26 entries inherit root. Inherited readers are shared mutable
files, not immutable snapshots: a later manual change affects every bank that
inherits them. Optional-file absence is preserved rather than filled in.
Raw helpers retain their separate production validation; they are not loaded
as `.kui` envelopes.

Each bank lives in `KUI/system/<build-id>/`. The installer verifies the reviewed
package again, writes/synchronizes/rereads replacements and writes its checked
`package.bin` receipt last. It does not open or hash inherited root readers
during installation. Module replacement SHA verification uses the same stream
that loads the module, before launch-specific patching.

CRC-protected `active0.bin` / `active1.bin` records retain selected and previous
IDs. **Y Restore previous** verifies its target before selecting it. Missing or
corrupt banks can fall back to a retained checked selection or root. Physical
read failure invalidates the mounted session and blocks module loading until
restart or Recovery.

Games, VMU data, settings, credentials, covers and custom assets remain shared
between GUI versions. Software rollback does not undo changes to those files.
Cancellation before activation leaves selection unchanged; a failed activation
sync/readback can leave its outcome uncertain. Records and readback do not make
filesystem allocation metadata power-loss atomic or certify that a GUI runs
correctly. Keep the same device connected through the operation.

## Build and validation

With the pinned toolchain configured and a clean source commit:

```sh
python3 tools/update_addon.py build --output build/update-addon/A
```

The builder checks preserved main reader source blobs, reviewed GUI glue, and the verified headers
under `resources/updater-bindings`. It passes both
`NATIVE_STANDARD_MANIFEST` and `NATIVE_CDDA_MANIFEST` to the GUI build,
checks their binding evidence and emits a GUI-only package. Do not rebuild
readers merely to obtain a matching GUI label.

Two distinct clean GUI builds can exercise switching:

```sh
python3 tools/update_addon.py build --output build/update-addon/B
python3 tools/update_addon.py package \
    --baseline build/update-addon/A --candidate build/update-addon/B \
    --output build/K-UI-Updater-GUI-TEST.zip
```

`tools/update_package.py verify path/to/update.kup` checks a package.
Its explicit `--members` software-path list supports deliberate sparse/new-role
packages; the default remains the current34 bridge. The withdrawn full-cohort release/build recipes are excluded from this port.

Host validation must include actual older 26/34 decoder compatibility,
the repaired decoder, real FAT32/exFAT staging/installation, unchanged reader
hashes and absent paths, current Standard recognition, raw-helper preservation,
changed-ZIP pin refusal, restart reload/abort, handoff and rollback. Report each
old decoder's result separately: old34 success does not prove old26 success.
The manual repair must also verify the exact system/optional-boot rename and
root replacement sequence with a previously selected bank on both filesystems.
Reader fixtures used for these checks are not installed by the add-on.

Console acceptance is the changed updater flow: root migration/selection,
folder and ZIP review, installation, direct restart and restore. Do not repeat
settled game, save/import, CDDA or WinCE matrices for a GUI updater change.
Host and linked checks do not establish console acceptance.

## Wire format

Version 1 remains a 128-byte header, `count × 128` manifest bytes and declared
replacement payloads in manifest order. The namespace rules replace the old
exact-count requirement without relabelling or rewriting existing receipts.

| Header offset | Field |
| --- | --- |
| 0 | `KUIUPD1` followed by NUL |
| 8 / 12 | Version 1 / header length 128, 32-bit each |
| 16 / 20 | Entry count / manifest length, 32-bit each |
| 24 | Payload length, 64-bit; at most 128 MiB |
| 32 / 44 | Twelve lowercase hexadecimal build bytes / channel 0 stable or 1 testing |
| 45–63 | Zero |
| 64 | Manifest SHA-256, 32 bytes |
| 96–123 / 124 | Zero / CRC32 of preceding 124 bytes |

| Entry offset | Field |
| --- | --- |
| 0 | Allowed relative software path, NUL padded to 80 bytes |
| 80 | File length, 64-bit; zero for inheritance |
| 88 | Full-file SHA-256, 32 bytes; zero for inheritance |
| 120 / 121–127 | Policy / zero |

All integers are little endian. The GUI contains one initialized sixteen-byte
`KUIBANK1` capability/protocol/inverse marker and one empty 192-byte
`KUIHOF1\0` handoff slot, version 1. Only the checked RAM copy receives its
transport, selection, digests, token and CRC; populated/duplicate source slots
are refused. Matching startup transfer context permits receipt/header/slot
adoption instead of rereading the whole GUI. Explicit Restart K-UI always takes
the full verification path, even for the same build.

Selection records are 128 bytes: `KUIACT1\0`, version/length, nonzero 64-bit
sequence at 16, selected/previous IDs at 24/36, selected manifest SHA at 48,
zero 80–123 and CRC32 at 124. Empty selected ID plus zero manifest SHA selects
root. Sequence overflow is refused.

Checksums establish integrity, not publisher authenticity. There is no package
signature or online downloader in this first updater. A future downloader can
use trusted published packages from
[TacitusVoxt/K-UI-NeXT Releases](https://github.com/TacitusVoxt/K-UI-NeXT/releases).

## Withdrawn full-cohort test

The `28cadccf0e71` → `33b04057db86` ZIP is withdrawn. It substituted an
ordinary reader for paced Standard and introduced presence-sensitive optional
loaders. If it replaced root software, restore the pre-test software backup
while preserving current cards, journals and saves. Its supplied A/B rollback
cannot undo that initial replacement; both versions had the same bad recipe.
