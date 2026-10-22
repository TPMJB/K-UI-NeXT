# Silver: prepare a separate card for the next save test

Both imported Silver saves have loaded successfully through the read-only
virtual card. This package prepares a separate Silver-only card with **36 used
blocks and 164 free**. It retains the three original Silver files and their
allocated payload bytes. The full white-card backup (`p0002`) and ABC card
(`p0001`) remain in place.

This is preparation only. The included Card tools program reads the new
image, identity and clean journal, checks their live allocations, and creates
a new profile file. It does not enable retail saving, recover a journal or
change those three source files.

## Copy and run

1. Back up your usual `KUI/tools.kui` outside `KUI`.
2. Check that `/KUI/vmu-test/p0003` does not already exist on the SCI card.
   The `NEW-CARD/p0003` folder in this ZIP must be copied as a **new** folder
   to `/KUI/vmu-test/p0003`. Do not merge it over an existing folder or rename
   it: the supplied identity is bound to number 3. If that folder already
   exists, keep it and report the conflict so a fresh pair can be supplied.
3. Copy this ZIP's `KUI/tools.kui` over the installed copy. Keep the current
   retail loader in place. No physical VMU is needed for this preparation.
4. Power-cycle, open **Card tools**, select **SCI**, then press **Y** to prepare
   the latest pair's profile. A/X are disabled. Keep power on during the
   ownership and readback checks.
5. At **PROFILE / READY**, check that the reported source is **p0003** and
   image SHA prefix is **36a1275ac94b9826**. Photograph the full screen and
   upload the exact `.bin` file named there. The first export is normally
   `/KUI/vmu-test/p0003/retail-persistent-profile-0001.bin`; an existing output
   makes the tool choose another unused profile filename.
6. Power off and restore your usual `tools.kui`. Keep all of `p0003` in place;
   moving or replacing it changes the allocation needed for the next test.

If the report names another source pair or stops with a refusal, photograph
that screen. Do not replace or repair the old pairs. No game write test is
included in this package; the new profile is needed to build that test.

## Contents and checks

The new pair contains `card.vmd` (131,072 bytes), `card.journal` (1,024 zero
bytes) and `identity.bin` (16 bytes). Its image SHA-256 is
`36a1275ac94b9826778d09116864ba4740a4ad50dbc9f3ff49bff65236f7754b`.
Its identity follows the existing preparation tool's exact number-3 format.
The new pair gets a separate image file and a number-3 identity. No old
journal binding, allocation map or profile is reused for it.

The image was constructed from the verified full white-card backup by retaining
directory slots 0, 1 and 2: `_SILVER_.SYS`, `_SILVER_.001`, `_SILVER_.002`.
The complete source and resulting filesystem were validated. Every retained
file's original metadata and allocated payload bytes were preserved. New
physical allocations, card identity and live ownership are checked by Y after
the files are copied; this archive does not claim to know those addresses.

The preparation runtime is the existing dedicated preparation mode. Its native
audit requires the linked preparation flag, source-bound module/ELF and absence
of the legacy image writer, formatter and recovery functions. Real FAT32/exFAT
fixtures verify generic save images, unchanged source files and create-new
profile outputs.
