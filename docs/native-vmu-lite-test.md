# Compact virtual VMU lite test — SCI and SCIF

This candidate gives virtual A2 a **72 KiB reservation**, including its private
4 KiB stack. The previous VMU-only candidate needed 112 KiB. Like that build, it
uses synchronous game-image reads. The VMU allocation contains no background
reader, read-ahead helper or prefetch cache.

Only two buffers changed from the 112 KiB build:

- **One staged virtual reply instead of 31.** The game's Maple driver sends at
  most one frame to A2 in each batch, because A2 has a single receive slot. The
  112 KiB build already refused a second A2 frame; this build refuses it before
  any card access, the same way.
- **4 KiB working copies of the game's Maple command list instead of two 12 KiB
  copies.** A longer list stops the game with a visible fault; nothing is read,
  written or sent. Evolution's measured polling lists are 36–84 bytes. Four
  controllers with eight VMUs all updating their screens stay under about
  2 KiB.

Maple validation, the save journal, durable SD-backed saves and physical A1
passthrough are unchanged. The linked audits and host tests are software
evidence. Console compatibility of this candidate is what this test batch will
establish.

## Install once for the SCI batch

If the 112 KiB VMU-only test is still installed, restore it first. Run
`python3 install.py /path/to/Dreamcast --restore` from **that** package's
extracted folder. Otherwise this installer refuses and changes nothing.

Extract the complete ZIP. With the Dreamcast microSD mounted on the PC, run
from the extracted directory, replacing `/path/to/Dreamcast` with its mount path:

```sh
python3 install.py /path/to/Dreamcast --transport sci
```

The installer reuses an existing prepared virtual card. If several prepared
cards are available, it prints their profile paths; repeat with the desired
existing profile explicitly selected. For example, **only if that profile is
actually present**:

```sh
python3 install.py /path/to/Dreamcast --transport sci --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

No new card needs to be created or formatted. The selected profile backs virtual
A2 across this test batch. Test saves go to that existing virtual card normally.
The installer preserves its card, journal, identity and profile bytes, the recent
recovery GUI, native background-loader module, assignments and launch settings.
It backs up and replaces only the ordinary `retail-boot.kui` loader.

Safely eject the microSD. For every game in this batch select:

| Setting | Value |
|---|---|
| Storage | SCI |
| Background reader | **Off (Standard)** |
| Saved Emulated VMU setting | **Off for this test**; the installed module supplies A2 itself |
| CD music | Off |
| RAM patches | Off |
| Physical A1 | May remain attached |
| Physical A2 | Empty |

Virtual A2 is forced on by this installed test module. Set the saved Emulated
VMU setting **Off** before the batch, for the same reasons as the 112 KiB test:
it bypasses the GUI rule that assigned cards require the combined New reader,
and it keeps title-specific VMU loaders from diverting the test.
The launch should show **72 KIB VMU LITE / BACKGROUND OFF**. Keep the installed
build identifier visible in any failure photo.

## Run these cases in one batch

Stop a game at a convenient point after completing its listed observations.

| Order | Game | What this tests |
|---|---|---|
| 1 | Evolution 1 | Load the existing save, pass the Sofdec/startup transition and enter gameplay. Make a save; power off normally after saving finishes; boot again and load it. |
| 2 | Rayman 2 | Load, play through a streaming/area transition, save, continue playing, then relaunch and load. |
| 3 | Record of Lodoss War | Load, play/stream, save, continue, and relaunch/load. |
| 4 | Sonic Adventure 2 | Watch an FMV, enter gameplay and save/load while physical A1 remains attached. Check controller response and physical A1 behavior when convenient. |
| 5 | Power Stone 2 | Watch the intro, play a match and save/load. Leave physical A1 attached to exercise passthrough alongside virtual A2. |
| 6 | Grandia II | Watch an FMV, enter gameplay and save/load at a convenient point. |
| Optional | Skies of Arcadia | Load, enter gameplay and save/load if convenient. |
| Optional | Power Stone 2 | With a second controller and its own physical VMU attached, play a 2-player match and save/load. This sends the longest Maple lists in this batch. |

An existing save need not exist in every title: detection and a new save are
useful where the game permits them. Do not overwrite a save slot you want to
keep merely for this test.

Record results in `test-results.csv` or report them in chat. Useful details are:
boot/transition result, virtual A2 detection, save result, loading after relaunch,
and any new hang or controller/physical A1 issue. For a failure, include the
game, transport, build identifier and the exact last screen or message.

## SCIF batch

Mount the microSD again and switch only this test's ordinary loader:

```sh
python3 install.py /path/to/Dreamcast --transport scif
```

Use the same `--profile` argument if one was needed for SCI. Safely eject, use
your SCIF hardware, and change Storage to **SCIF**. Keep Background Off (Standard),
the saved Emulated VMU setting Off, CD music Off and RAM patches Off.

Start with **Evolution 1 and Rayman 2**. If both can save, resume game reads and
reload successfully, continue with the rest of the list during the same install.
Do not select the SCIF variant while the card is connected through SCI, or vice
versa; each variant admits only its selected transport.

## Restore the original ordinary loader

From the same extracted package:

```sh
python3 install.py /path/to/Dreamcast --restore
```

The exact pre-test loader is restored. Keep this ZIP and
`KUI/native-vmu-only/` until restoration is complete. Repeating an install or
restore is supported. If the ordinary loader has been changed by another
installer since this test, this installer refuses to overwrite that change.
Restore your previous saved Emulated VMU setting afterward (On if it was On).

## Scope and source

This batch covers native, uncompressed game images through the Standard reader.
It does not add original physical-disc launching, DOA2 VMU support, WinCE/CDDA
support or compressed-image support.

The complete source history, both compiled transports, linked audits and checksums
are included. `CANDIDATE.json` identifies the exact source/runtime build. See
`SOURCE.md` to recover the source offline with `python3 source/checkout-source.py`.
