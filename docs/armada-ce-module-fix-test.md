<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE module-binding and CD music test

The previous console result showed **GD 2, BIND 4, READ 0x4a, QUEUE 0,
ATTEMPT 0 and PLAY 0**. The live sound binder returned its MODULE refusal
before the excerpt was queued. That accounts for the lack of an audible change;
it does not establish whether playback will succeed after binding is corrected.

This test updates the optional Armada excerpt loader and adds a precise
binding-refusal witness. The saved options and menu runtime stay at build
**785184b6db05**, including the GitHub link beneath the K-UI logo.

The audited CE kernel tries a filesystem DLL before its ROM copy. The earlier
binder admitted only ROM-backed sound DLLs. It now also admits the exact
disk-backed DSOUND and SNDCORE profiles, checked against four section shapes,
flags and sizes plus three immutable 32-byte CRC guards per DLL. Those guards
and the existing owner, object, method, critical-section and mapping witnesses
are repeated before publishing a binding. Only the fourth discardable `.reloc`
section may have a zero adjusted address. The bounded read budget is 4,096,
with at most 64 module records and 64 mapped pages.

The missing disk-backed admission is consistent with the previous refusal;
that screen did not identify which record or branch failed. The new witness
row provides that evidence if this test remains silent. Playback still depends
on passing all binding and restored caller-context checks.

## Install and listen

1. With the console off, back up `KUI/apps/games/ce-cdda-armada.kui`.
2. Merge this ZIP's `KUI` folder onto the SCI microSD card and cold boot. The
   only installed file is `KUI/apps/games/ce-cdda-armada.kui`.
3. Select **Original Armada**, open **Y Options**, keep **CD music On**, and
   launch with **START**. The saved On choice selects the new excerpt loader.
4. Wait **30 seconds at the main menu**, then play for **one or two minutes**.
   Listen for a **two-second repeating music excerpt** and check that gunfire
   and other game effects remain audible. Mention any disc error, freeze,
   slowdown or missing effects.
5. Press **A+B+X+Y+Start** and send **one photograph of the whole result screen**,
   including if no music plays. Include **BIND WHY NODE VALUE EXPECT DETAIL**;
   it identifies the exact record check if binding still refuses.

The excerpt comes from seconds **2–4 of Armada audio track 4**, read from the
same original raw GDI before boot. No extraction, new private upload or 2048
test is required. A successful excerpt is the first audible CE test; full-song
streaming and other Windows CE titles are still separate work.

## How the result is interpreted

| Result | Meaning |
| --- | --- |
| BIND nonzero, QUEUE and ATTEMPT zero | Sound binding refused before native playback. The new witness locates the failed check. |
| QUEUE rises, ATTEMPT stays zero | A request was queued but the deferred caller path did not attempt playback. |
| ATTEMPT rises, PLAY stays zero | The deferred controller attempted the request and refused or received an error. |
| PLAY rises with a changing cursor | The CE backend accepted playback; the listening result determines whether it was audible. |

No full-track streaming, universal CE support or FMV speed improvement is claimed.
The PCM bytes in the distributed loader are an all-zero placeholder; the launcher
fills them from the owner's own image. Kernel admission, the reserved excerpt
pool, caller-context checks and the checks for spare sound voices remain required.

## Restore and provenance

Set **CD music Off** to use the existing ordinary CE loader. To restore the
previous excerpt test exactly, merge `restore-ce-excerpt/KUI` onto the card with
the console off and cold boot. It contains the byte-identical excerpt loader
from build **785184b6db05**.

The source was recovered from the pinned archive distributed with that build.
Packaging verifies every archived file's bytes and mode against the recovered
Git baseline before adding the new committed source archive. The previous source
identity and archive hash, current build evidence, dependency sources and licenses,
and all output member sizes, SHA256 values and CRC32 values are included. Private
game binaries, the Windows CE kernel, ARM firmware and music samples are absent.
