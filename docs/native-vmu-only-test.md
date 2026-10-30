# Compact virtual VMU test — SCI and SCIF

This candidate gives virtual A2 its own **112 KiB reservation**, including its
private 4 KiB stack. It uses synchronous game-image reads. The VMU allocation
contains no background reader, read-ahead helper or prefetch cache. Existing
Maple handling, physical A1 passthrough and durable SD-backed saves are retained.

The linked audits and host tests are software evidence. Console compatibility
of this candidate is what this test batch will establish.

## Install once for the SCI batch

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
VMU setting **Off** before the batch: this bypasses the current GUI's rule that
assigned cards require the combined New reader. It also prevents optional
title-specific VMU loaders from diverting the test. The selected prepared card
still appears as A2 through this module. Use Background Off (Standard) to select
it; New/Legacy selects the preserved background-loader path.
The launch should show **112 KIB VMU / BACKGROUND OFF**. Keep the installed build
identifier visible in any failure photo.

## Run these cases in one batch

These are candidate checks, rather than requests to repeat old baseline builds.
Stop a game at a convenient point after completing its listed observations.

| Order | Game | What this tests |
|---|---|---|
| 1 | Evolution 1 | Load the existing save, pass the Sofdec/startup transition and enter gameplay. Make a save; power off normally after saving finishes; boot again and load it. |
| 2 | Rayman 2 | Load, play through a streaming/area transition, save, continue playing, then relaunch and load. This checks the smaller heap lease and return from VMU writes to disc reads. |
| 3 | Record of Lodoss War | Load, play/stream, save, continue, and relaunch/load. This exercises its allocator routing and resource-allocation behavior with the smaller reservation. |
| 4 | Sonic Adventure 2 | Watch an FMV, enter gameplay and save/load while physical A1 remains attached. Check controller response and physical A1 behavior when convenient. |
| 5 | Power Stone 2 | Watch the intro, play a match and save/load. Leave physical A1 attached to exercise passthrough alongside virtual A2. |
| 6 | Grandia II | Watch an FMV, enter gameplay and save/load at a convenient point. |
| Optional | Skies of Arcadia | Load, enter gameplay and save/load if convenient. |

An existing save need not exist in every title: detection and a new save are
useful where the game permits them. Do not overwrite a save slot you want to
keep merely for this test. There is no need to seek a distant save point just
to fill every cell in the batch.

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
reload successfully, continue with Lodoss, Sonic Adventure 2, Power Stone 2 and
Grandia II during the same install. SCIF raw transfer speed differs from SCI;
report new hangs or save faults separately from expected loading/FMV performance.
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
support or compressed-image support. DOA2's fixed resource-partition ownership
still needs separate work; reducing the worker size alone does not resolve it.

The complete source history, both compiled transports, linked audits and checksums
are included. `CANDIDATE.json` identifies the exact source/runtime build. See
`SOURCE.md` to recover the source offline with `python3 source/checkout-source.py`.
