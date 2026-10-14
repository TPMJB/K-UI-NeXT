<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE CD music cache-read test

The latest console screen is build **617ab22416df** and records **GD 7,
BIND 9, READ 0x66, QUEUE 0, ATTEMPT 0 and PLAY 0**. Its failure witness is
**WHY 0x10, NODE 0x03cd846c, VALUE 9, EXPECT 0, DETAIL 0x105**. The binder
reached a mapped-word cache check and refused before any native playback
attempt. Silence remains consistent with that refusal; it is not evidence
of an attempted but inaudible buffer.

This update changes the live mapped-word read path only in the optional
Armada excerpt loader. It retains the existing same-color path and adds a
bounded path for the supported CE cache layout. Matching cache lines are
checked before and after reading. A cached target hit is read from the
operand-cache data array; a clean fallback reads direct uncached RAM. Dirty
alternate-color aliases, dirty matches on an uncached page, changed cache
witnesses and changed page mappings are refused. The new path does not flush
or modify cache lines, change the MMU, or reset the game's sound driver.

The array MOV runs in an exact, checked 32-byte P2 helper that saves SR,
masks interrupts briefly, waits eight instructions, and restores SR before
returning. Every fallback uses at most ten array/main-RAM reads and one CCR
sample. Mandatory page-walk reads retain the 4,096-read limit, so the added
operations are bounded separately; the diagnostic READ count remains the
RAM/page-walk count.

Sound-module identity, ownership, object, critical-section, caller-context
and spare-voice checks remain required before playback. Host checks and
build evidence are included. Audible playback on the console is still
unverified.

## Install and listen

1. With the console off, back up `KUI/apps/games/ce-cdda-armada.kui`.
2. Merge this ZIP's `KUI` folder onto the SCI microSD card and cold boot.
   Its only installed file is `KUI/apps/games/ce-cdda-armada.kui`.
3. Select **Original Armada → Y Options → CD music On → START**. The saved
   On choice continues to select the separate excerpt loader.
4. Listen for **30 seconds at the main menu**, then play for **one or two
   minutes**. Expected success is a **repeating two-second music excerpt**.
   Check that gunfire and other game effects remain audible, and mention
   any disc error, freeze, slowdown or missing effects.
5. Press **A+B+X+Y+Start** and send **one photograph of the whole diagnostic
   screen**, even if no music plays. Include **BIND WHY NODE VALUE EXPECT
   DETAIL**, the playback counts and the cursor row.

The excerpt comes from seconds **2–4 of Armada audio track 4**, loaded from
this original GDI before boot. No new extraction or private upload is
required. No 2048 run is needed. A successful excerpt remains the first
audible CE test; full-song streaming and other CE titles are separate work.
The distributed PCM region is an all-zero placeholder filled from your image.

## Interpret the result

| Result | Meaning |
| --- | --- |
| BIND nonzero; QUEUE and ATTEMPT zero | Binding refused before playback. The failure row locates the check. |
| QUEUE rises; ATTEMPT stays zero | A request was queued but the deferred caller path did not attempt playback. |
| ATTEMPT rises; PLAY stays zero | The controller attempted the request and refused or received an error. |
| PLAY rises with a changing cursor | The CE backend accepted playback; your listening result determines whether it was audible. |

No full-track streaming, universal CE support or FMV improvement is claimed.
The menu runtime remains **785184b6db05**, including its saved options and
GitHub link beneath the K-UI logo. The normal CE loader and native, Toy
Commander and MDK2 loaders remain as installed.

## Restore and provenance

Set **CD music Off** to use the existing ordinary CE loader. To restore the
previous excerpt test exactly, merge `restore-ce-excerpt/KUI` onto the card
with the console off and cold boot. It contains byte-identical optional
loader build **617ab22416df**.

Packaging verifies both the original 785 delivery and the preceding 617
module-fix delivery, including their member bytes, SHA256 and CRC32 values.
It verifies the recovered 785 source tree's file bytes and modes, and retains
that exact tree in the complete Git bundle alongside the 617 and current
commits. Current and preceding 617 source archives, dependency sources,
licenses, build evidence and output member size/SHA256/CRC32 records are
included. The redundant standalone 785 tar is omitted to keep the deliverable
below 50 MiB. No private game executable, CE kernel, ARM firmware or music
sample is distributed.
