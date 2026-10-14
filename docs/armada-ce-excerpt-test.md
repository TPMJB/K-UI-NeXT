<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE CD music excerpt test

This build attempts audible CD music through Armada's own Windows CE sound
system. It is an initial playback test: a **two-second excerpt, taken from
seconds 2–4 of audio track 4, loops** when Armada requests that track. It does
not yet stream the complete song or support other CE games.

The package also restores `github.com/TPMJB/K-UI-NeXT` beneath the **K-UI logo**,
with normal text spacing. Launch options remain toggleable and save
automatically for each image path. Advanced contains the read tests and
developer controls.

## Install and test

1. With the console off, back up `KUI/runtime.kui`. If
   `KUI/apps/games/ce-cdda-armada.kui` already exists, back it up too.
2. Merge this ZIP's `KUI` folder onto the SCI microSD card, then cold boot.
   The two installed files are the updated menu runtime and the separate
   Armada excerpt loader. Keep the previously installed `ce-probe.kui`,
   native, MDK2 and Toy Commander loaders in place.
3. Select **Original Armada**, open **Options with Y**, and set **CD music
   On**. The option identifies the excerpt test. **START** launches from
   Options; ordinary launch confirmation also uses the saved choice.
4. Stay at the main menu for **30 seconds**, then start a game and play for
   about **one or two minutes**. Listen for the short repeating music segment
   and confirm that guns and other game effects still sound normally. Mention
   any music dropout, disc error, new slowdown or FMV change.
5. Return with **A+B+X+Y+Start** and send **one full-screen photograph of the
   result**, including when no music was heard. The result distinguishes an
   attempted/accepted playback from an owner, context, buffer or voice refusal.

No startup extraction or new private upload is required. Use the same original
raw GDI image already tested. The 2048 variant is not admitted by this test.
If convenient, cold boot once more and confirm that CD music remains On for
that Original image; another image's setting stays separate.

## What the test changes

| Setting | Armada launch route |
| --- | --- |
| CD music Off | Existing `ce-probe.kui`, build `33298cc0bdb8`, unchanged |
| CD music On | New `ce-cdda-armada.kui`, conditional two-second CE excerpt test |

The launcher reads 150 raw audio sectors beginning at LBA **149992** from the
user's own image before boot. The distributed loader contains only an empty
PCM placeholder. After verifying the original admitted kernel, the stage
reserves **512 KiB** outside CE's page allocator and verifies the copied
excerpt. The protected pool cannot be used as a direct GD destination.

The sound binder follows live CE module/process records and checks the game's
sound owner. Playback runs only in that verified owner's restored caller
context, through a software stereo DirectSound buffer. It obtains its own
references and checks for two spare voices under the sound device's recursive
critical section. A failed check reports its refusal; it does not substitute a
fixed AICA channel or reset CE's sound driver. Music acceptance and cursor
observations are diagnostic evidence, while the listening result establishes
whether the excerpt was audible on this console.

Full-song streaming, compatibility with other Windows CE titles, FMV
improvements and console stability beyond this short run remain untested.
The ordinary native, MDK2 and Toy Commander payloads are not replaced.

## Restore

Set **CD music Off** to return Armada to the existing CE loader. For exact
menu restoration, merge `restore-runtime/KUI` onto the card with the console
off and cold boot. That is byte-identical menu build **1e00513dd14a**. Restore
your own excerpt-loader backup if it existed before this package; otherwise
the optional file can remain unused with CD music Off.

Source archives for this build and the pinned prior menu, dependency sources,
licenses, layout/build evidence and per-member checksums are included. The
archive distributes no private game executable, Windows CE kernel, ARM sound
driver or music samples.
