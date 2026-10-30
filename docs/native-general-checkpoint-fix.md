# General installer: newer GUI checkpoint upgrade

This installer-only update fixes the upgrade path to the lean General
`34dc7b5e15ee` reader when a later ripper or wizard has replaced `KUI/runtime.kui`.
It includes no console binaries and does not change VMU emulation or RAM usage.

The older installer accepted only package-declared GUI hashes, even when the
supplied four-file checkpoint exactly matched the card. The corrected installer
accepts another valid KUI runtime envelope only with that exact checkpoint.
Both loader modules must still match their existing General receipt, original
backup, or the intended package. A checkpoint does not bypass loader ownership
checks. The original General backup and the external checkpoint remain unchanged.

## Install

Extract this small ZIP **into the existing extracted
`K-UI-Native-General-Prefetch-Auto-34dc7b5e15ee` folder**, beside `general.json`,
`personalize.py`, and the original `install.py`. It adds
`install-checkpoint-fix.py`; it does not replace the original installer or payloads.

From that folder, reuse the checkpoint that already passed validation:

```bash
python3 install-checkpoint-fix.py /run/media/apollonius/Dreamcast \
  --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin \
  --runtime-checkpoint "$HOME/K-UI-before-34dc7b5e15ee.tar"
```

After it succeeds, enter the existing extracted
`K-UI-VMU-Toggle-6096398d3373` folder and run:

```bash
python3 install.py /run/media/apollonius/Dreamcast
```

The first command installs the expected lean General modules. The second installs
the toggle UI/module pair and copies the installed card profile automatically.
Neither command modifies card images, journals, profile files, game assignments,
or launch settings. The toggle remains an experimental hardware-test candidate
with the validation limits stated in its README.

The checkpoint must still contain the exact current `KUI/runtime.kui`,
`KUI/apps/games/retail-native-dual.kui`,
`KUI/apps/games/retail-native-standalone.kui`, and
`KUI/native-general/state.json`. A stale checkpoint, damaged GUI, or unexpected
reader-module change is refused before installation. No checkpoint is extracted
by either installer.

## Rollback

The toggle's `--restore` restores the exact pair it replaced. To also return to
the GUI, modules and General receipt from before the first command, first restore
the toggle from its extracted folder, then restore the already verified external
checkpoint:

```bash
python3 install.py /run/media/apollonius/Dreamcast --restore
tar -xf "$HOME/K-UI-before-34dc7b5e15ee.tar" -C /run/media/apollonius/Dreamcast
```

That four-file checkpoint contains no VMU saves, journals or settings, so saves
made after the upgrade remain in place. The General installer's own `--restore`
retains its historical meaning: restore the files from before the first General
installation, which can be older than this external checkpoint.
