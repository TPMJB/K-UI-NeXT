# Offline updates

The updater installs checked `.kup` packages from the SD card without a
Dreamcast internet connection. The initial updater is a **GUI-only addition**:
its ZIP supplies `KUI/runtime.kui` and a second GUI's package in `KUI/updates`.
It installs no `KUI/apps` files. Existing readers, their exact bytes and their
absence remain part of the retained installation.

**The earlier `28cadccf0e71` → `33b04057db86` full-cohort test ZIP is withdrawn.**
Its ordinary `retail-boot.kui` incorrectly replaced the accepted paced Standard
VMU module, and added optional historical files whose presence changes routing.
Do not install it. If it already replaced root software, restore the pre-test
software backup while preserving current VMU images, journals, imports, saves
and preferences. The supplied A/B rollback cannot undo that initial copy:
both withdrawn versions contain the wrong Standard recipe.

`tools/update_release.py` and `tools/package_update_test.py` still refuse before
building or writing output. The GUI-only workflow below does not enable them
or rebuild a worker. See [accepted progress](accepted-progress.md) for the
feature and loader contracts retained by this work.

## Install the initial GUI

With the console off, keep the working `KUI/runtime.kui` on the computer, then
copy only the supplied `KUI/runtime.kui` to that path. Copy the supplied `.kup`
into `KUI/updates`. Keep the existing `KUI/apps` directory and other card files.
Do not replace the entire KUI directory. Existing boot CDs continue to load
the root runtime, which becomes the retained updater baseline and dispatcher.

This new initial GUI is required. Policy 2 extends package version 1;
the withdrawn updater GUIs reject it and cannot install these packages.
An older GUI cannot become an updater merely by copying a `.kup` onto its card.

## Install or switch a package

1. Put trusted `.kup` files or updater-ready test ZIPs in `KUI/updates` or a
   subfolder such as `KUI/updates/test` using a computer.
2. Open **Settings → Updates**. Press **A** to enter a folder or check a file;
   **B** returns to the parent folder, then Settings at the Updates root.
3. Review the target build, channel and replacement/inheritance counts.
4. Confirm, wait for the result, then choose **Restart K-UI**. K-UI reloads the
   selected version directly without returning to BIOS.

The menu shows folders first, then `.kup` and `.zip` files in name order, with
eight rows per page. It keeps the current folder through package review,
installation, refresh and restore. A dozen test packages can stay on the card
and be selected repeatedly without another trip to the computer. Re-selecting an already
installed identical package verifies and selects its retained bank; it need
not copy its runtime again. Completed banks are retained without automatic
pruning.

An updater-ready ZIP contains exactly one `.kup`. Only that member is streamed
into the private `KUI/system/update-stage` cache; source/evidence and any
bundled root runtime are ignored. The contained package then receives the same
inspection, confirmation and pinned installation as a standalone `.kup`.
ZIP checking writes this staging cache; it does not select or install software.
Installation stages the ZIP again and checks the reviewed package pin, so a
different ZIP cannot substitute another update after confirmation.

Stored and deflated ZIP32 archives are supported, including data descriptors.
Encrypted, split, ZIP64, malformed or multi-package archives are refused.
Older manual-install test ZIPs without a `.kup` need an explicit conversion;
the updater does not infer installer scripts, choose among variants, or copy
their old GUI and reader files. Future test deliveries should use
`tools/update_test_bundle.py` to provide the ready-to-select format.

## Restart K-UI and larger updates

Direct restart finishes application I/O, shuts down audio services, fully
checks and loads the selected GUI, releases storage, and hands the image to
the UI thread for execution. It also works when restarting the current build.
An aborted or failed handoff restores the running version's module bindings
and audio services. The separate console/BIOS restart remains available.

Updating the GUI and every declared game/VMU module can use this same direct
restart. The installer verifies replacements before selecting them; the fresh
GUI resolves its new bank. The number of changed files does not require a BIOS
boot. A change to the startup selector/package protocol must retain bootstrap
compatibility or supply an explicit migration. Boot media, firmware and work
requiring a hardware reset remain separate maintenance operations.

Each experiment needs a distinct admitted build ID. Renaming a package does
not create a new identity: banks use the 12-character build ID. The current
builder derives that ID from the clean source commit. Different bytes under
an existing ID are refused rather than replacing that bank.

The format can name deliberate reader replacements for future WinCE/CDDA
experiments. Such a package must identify exactly which paths it changes and
verify their intended selection. The GUI-only builder inherits every reader;
it does not manufacture reader experiments or imply that different filenames
change a game's route. Optional-file absence is part of that route.

## Restore and recovery

**Restore previous** verifies the previous bank or retained baseline before
selecting it. Holding **B** when the updater-enabled GUI starts requests the
root baseline for that boot. This is a manual escape, without an automatic
hang watchdog. Restart is required after changing the persistent selection.

The original pre-updater GUI can be restored manually with the console off.
Preserve card data when doing so. Software rollback does not reverse saves,
preferences, card assignments, exports or other user changes made meanwhile.

Games, VMU data, preferences, credentials, covers and custom assets are never
update destinations. They are shared across versions. Reader paths inherited
from root are also shared: a later manual reader change affects all banks that
inherit that path. Inheritance preserves the current installation; it is not
an immutable or hashed snapshot of those readers.

Use a supported writable FAT32/exFAT volume and keep the same device connected
through the operation. Cancellation before activation leaves the selection
unchanged. A failed selection sync/readback can leave its outcome uncertain;
restart and check. Two records and readback do not make filesystem allocation
metadata power-loss atomic or certify a new GUI's behavior.

## Version banks and verification

Each bank lives at `KUI/system/<build-id>/`. The installer writes and rereads
replacement members, then writes the checked `package.bin` receipt last.
Explicitly inherited members have no bank file and are not opened, copied or
hashed during installation. Missing root readers remain missing.

The fixed ordered [catalog](../include/kui/update_catalog.inc) has 26 entries.
Each must declare one policy:

| Policy | Meaning | Resolution |
| --- | --- | --- |
| 0 — Replace | Current-build `.kui`, checked length and SHA-256 | Its bank file; missing or corrupt never falls back to root |
| 1 — Approved legacy | Exact historical path/build/length/SHA pin | Its bank file; no arbitrary mixed build |
| 2 — Inherit root | Non-GUI path, size zero, SHA all zero, no payload | Original root path, including an ordinary missing-file result |

The GUI always uses policy 0. The additive builder uses policy 2 for all other
entries. It includes no legacy adapter payload. Root inheritance is explicit;
a missing replacement is never reinterpreted as inheritance. Replacement
SHA verification consumes the same stream used to load the module, before
launch-specific patching. Silver/Toy presence checks use the same resolver;
GUI-only inheritance deliberately preserves their installed root inventory.

CRC-protected `active0.bin` and `active1.bin` records retain selected and
previous IDs. A new record uses the older slot. A bank without a valid receipt
is incomplete. The selector may use an older checked selection or baseline
when update files are missing or corrupt. A physical read failure invalidates
the mounted session and blocks module loading until restart or Recovery.

The root GUI verifies a selected runtime, propagates the boot storage transport
and hands it control. Its staged RAM copy receives a one-use handoff record.
The launched GUI rechecks selection, receipt, disk header and the source's
still-empty slot before adopting it. This path reads 256 GUI bytes instead of
loading the full runtime twice. Missing or stale context takes the full
verification path; baseline startup rereads its 64-byte header. These are
bounded-I/O checks, not measured console startup timings.

## Build the GUI-only test

With the pinned Dreamcast toolchain configured and a clean source commit:

```sh
python3 tools/update_addon.py build --output build/update-addon/A
```

Build the second GUI from a different clean admitted commit, then package the
two build outputs:

```sh
python3 tools/update_addon.py build --output build/update-addon/B
python3 tools/update_addon.py package \
    --baseline build/update-addon/A --candidate build/update-addon/B \
    --output build/K-UI-Updater-GUI-TEST.zip
```

The helper checks clean-source provenance, actual runtime bytes, capability
and empty handoff markers, and the GUI-only destination allowlist. It creates
a sparse `.kup` whose only payload is the GUI. Package inspection can also
be run with `python3 tools/update_package.py verify path/to/update.kup`.
Passing codec checks alone does not approve an arbitrary loader cohort.

For a test with a reviewed, explicit replacement set, first build its testing
`.kup` using `update_package.py` with `--inherit-missing` for untouched paths.
Give each variant a distinct source/build identity and verify its actual game
route. Then wrap it for direct ZIP selection:

```sh
python3 tools/update_test_bundle.py variant.kup variant.zip \
    --source-commit FULL_SOURCE_COMMIT --label "Reader experiment"
```

The wrapper includes exactly one checked package, its source, a replacement
list and checksums. It refuses implicit conversion of arbitrary old test ZIPs.

For the focused folder/restart check, build two admitted GUI-only versions,
then produce an upgrade package and an example test ZIP without changing the
installed root runtime:

```sh
python3 tools/package_update_switch.py --upgrade build/update-addon/C \
    --test build/update-addon/D --output build/K-UI-Updater-Switch-TEST.zip
```

The existing older GUI uses its old BIOS restart once to enter the upgrade.
The upgraded GUI's Restart K-UI command then handles subsequent switches.

The actual-release host test installs A → B on FAT32 and exFAT, exercises
handoff/adoption, baseline bypass and rollback, and checks retained reader
hashes **and absent paths**. Its separate test fixture can contain accepted
reader bytes to exercise the production Standard recognizer; those fixture
readers are not files installed by the delivered GUI-only ZIP.

Console acceptance is limited to checking the displayed A build, package
review/install, restart to B, restore/restart to A and the held-B escape.
Keep a saved log for a failure. Existing game compatibility results remain
their existing results; this updater does not request another compatibility
matrix or establish new reader compatibility. Host tests and native linking
do not establish physical-console acceptance of the updater.

## Wire format, version 1 with inheritance

All integers are unsigned little endian. A package contains a 128-byte header,
`count × 128` manifest bytes, then replacement contents in catalog order.
Limits are 64 entries and 128 MiB payload; `.kui` envelope limits also apply.

| Header offset | Field |
| --- | --- |
| 0 | `KUIUPD1` followed by NUL, 8 bytes |
| 8 / 12 | Version 1 / header length 128, 32-bit each |
| 16 / 20 | Entry count / manifest length, 32-bit each |
| 24 | Payload length, 64-bit |
| 32 | Build ID, 12 lowercase hexadecimal ASCII bytes |
| 44 | Channel: 0 stable, 1 testing |
| 45–63 | Zero |
| 64 | Manifest SHA-256, 32 bytes |
| 96–123 | Zero |
| 124 | CRC32 of preceding 124 bytes |

| Manifest-entry offset | Field |
| --- | --- |
| 0 | Exact catalog-relative ASCII path, NUL padded to 80 bytes |
| 80 | File length, 64-bit; zero for policy 2 |
| 88 | Full-file SHA-256, 32 bytes; all zero for policy 2 |
| 120 | Policy 0, 1 or 2, as defined above |
| 121–127 | Zero |

The GUI contains exactly one initialized 16-byte capability marker: ASCII
`KUIBANK1`, protocol 1 and its 32-bit inverse. It also contains exactly one
192-byte empty handoff slot: `KUIHOF1\0`, version 1 and size 192, remaining
bytes zero on disk. The RAM record carries transport, selection, runtime and
manifest digests, original header fields, a token and CRC. It establishes
transfer coherence, not authenticity. A filled or duplicate source slot is
refused; the running GUI consumes its RAM slot once before storage I/O.

Selection records are 128 bytes: `KUIACT1\0`, version/length, nonzero 64-bit
sequence at 16, selected/previous IDs at 24/36, selected manifest SHA at 48,
zero 80–123 and CRC32 at 124. Empty selected ID plus zero manifest SHA means
baseline. Sequence overflow is refused. Future catalog/protocol migration
requires a deliberate compatible baseline upgrade.

There is no package signature or online downloader in this first updater.
An eventual downloader can obtain trusted published packages from
[TacitusVoxt/K-UI-NeXT Releases](https://github.com/TacitusVoxt/K-UI-NeXT/releases)
and use the same checker; it must not install arbitrary `main` builds.
