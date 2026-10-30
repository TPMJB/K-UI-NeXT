# Skies next: 72 KiB baseline and current launch GUI

The **72 KiB virtual-VMU worker is the next baseline**. This package combines
that worker with the current launch GUI and a matching 72 KiB full-submit
comparison. All are built from the same source revision. An older-sized 112 KiB
split worker is included only as a fallback reference. There is no request to
repeat the already accepted game batch or begin by retesting 112 KiB.

This package retains the recovery and launch preparation work. Console testing
will establish whether the smaller worker changes Skies' freeze or save behavior;
host tests alone do not establish a fix.

## Inspect the cards without changing them

Extract the complete ZIP on the PC. With the Dreamcast microSD mounted, run:

```sh
python3 inspect-cards.py /run/media/apollonius/Dreamcast > ~/kui-skies-cards.json
```

Send that PC-side report back. It lists every prepared card, filename, VMS
description/application ID, installed module profile and effective card contents
after committed journal replay in RAM. It does not create a card, write saves,
change profiles or reset journals. Card numbers do not establish which game's
saves are present; the report establishes their current contents.

Choose one existing prepared profile and keep it for all mode comparisons.
The examples use p0005 only if that profile exists and is the intended card.
If the report identifies another card, substitute its exact profile path.

## Install the current GUI and the 72 KiB baseline

```sh
python3 install-ui.py /run/media/apollonius/Dreamcast
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

The ordinary loader installer defaults to **baseline: 72 KiB, split controller**.
It can also be selected explicitly with `--mode baseline`. The GUI installer
changes only `KUI/runtime.kui`; the loader installer changes only the ordinary
`retail-boot.kui` module.

Safely eject. Use **SCI**, **Background Off (Standard)**, saved **Emulated VMU
Off**, CD music Off and RAM patches Off. This module supplies virtual A2 itself.
Keep physical A2 empty; physical A1 may remain attached. Record the build/mode
shown at launch and repeat the Skies scene or action where the freeze occurred.
Report whether A2 is detected, whether the old save loads, and attempts or minutes
played. Time from pressing A to Bootstrap 1 if convenient. The GUI retains the
CRC/SHA source-pass reuse that removes the duplicate executable read; the
reduction in reads does not establish an exact console time saving.

The 72 KiB worker retains virtual A2 and durable card handling with one staged
virtual reply and 4 KiB working command-list copies. A Maple command list longer
than 4 KiB is a compatibility limit: the loader stops visibly before storage
access or partial physical submission. It does not truncate the list. Photograph
any visible refusal with its complete message and build identifier.

## Matching 72 KiB controller comparison

To compare the retained full-submit path without changing the reservation or
prepared card:

```sh
python3 install.py /run/media/apollonius/Dreamcast --mode full-submit --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

Keep the same card, game image, settings and accessories. This mode also reserves
**72 KiB**. Compare the same scene and report any change in freeze/save behavior.
The included CSV has one row per available mode; the reference row is optional.

Physical A1 and, if convenient, an additional controller/physical VMU provide
optional passthrough coverage. Keep accessories unchanged while comparing a
freeze, then vary one condition at a time.

If a read fails, photograph the entire page. The compact VMU-only fault page
shows `FN CMD SD IMG STATE` followed by `LAST LBA CNT DST DONE`. SD command and
response bytes are encoded in STATE along with storage-health bits. These fields
help distinguish an SD, image-map or request failure without changing the read,
retry or acceptance rules.

## 112 KiB fallback reference, only if needed

| Mode | VMU reservation | Controller submit | Role |
|---|---:|---|---|
| baseline, default | 72 KiB | Split capture/completion | Next baseline |
| full-submit | 72 KiB | Retained full submit | Matching comparison |
| reference-112 | 112 KiB | Split capture/completion | Fallback reference |

The reference is available in this same package if a comparison or compatibility
fault makes it useful; it is not the first test:

```sh
python3 install.py /run/media/apollonius/Dreamcast --mode reference-112 --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

## Independent exact restores

Restore this package's ordinary loader and GUI separately:

```sh
python3 install.py /run/media/apollonius/Dreamcast --restore
python3 install-ui.py /run/media/apollonius/Dreamcast --restore
```

They restore the exact bytes present before this package was installed. New
backup/receipt namespaces are `KUI/skies-next-loader/` and `KUI/skies-next-ui/`.
Earlier `KUI/native-vmu-only/`, `KUI/native-vmu-only-skies/` and
`KUI/skies-launch-ui/` backups and receipts remain untouched. Keep this ZIP and
its new backup directories until restoration is complete. Restore this newer
layer before using an earlier package's restore command. An external loader or
GUI change is preserved by refusing to overwrite it.

The installers leave card images, journals, profiles, assignments and launch
settings unchanged. Saves written by a game remain normal saves; restoring a
loader does not roll the card back.

This package covers SCI native uncompressed image launching. It adds no
physical-disc launch, DOA2 VMU admission, WinCE/CDDA or compressed-image support.
Complete source history and linked artifacts/audits are included; see `SOURCE.md`.
