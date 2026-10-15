<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE CD music loaded-layout test

The latest console screen is build **b9aa5c70a431** and records **GD 6,
BIND 4, READ 0xcc, QUEUE 0, ATTEMPT 0 and PLAY 0**. Its failure witness is
**WHY 0x0e, NODE 0, VALUE 0, EXPECT 0x8cf4e5f4, DETAIL 0x10**. It reached
the module walk, then reported a required sound module missing after shape
filtering. No native playback was attempted.

The audited CE kernel extends a module's loaded `.data` section with its
own metadata: its ASCII basename and 28 bytes per section-table record.
The binder previously compared the sizes stored in the original module
file. This update checks the canonical sizes after that kernel operation:

| Module | Filesystem-backed loaded `.data` | ROM-backed loaded `.data` |
| --- | --- | --- |
| DSOUND | `0x4dc` | `0x4c0` |
| SNDCORE | `0x1208` | `0x11ec` |
| COREDLL | — | `0x1e0` |

These exact sizes are checked alongside the existing section count, flags,
RVA, address and immutable code guards. The section table must also be at
the exact location in the loaded data section established by CE. Raw and
partly expanded layouts are refused. Module identity, mapping, owner,
object, critical-section, caller-context and spare-voice checks remain
required. The previous bounded cache-read fix remains in place. No game
binaries, sound-driver reset, MMU change or cache flush is introduced.

The module missing result is consistent with the old size filter. Host
regression checks reproduce the layout mismatch; they do not establish
audible playback on the console. This remains a conditional two-second
excerpt test.

## Install and listen

1. With the console off, back up `KUI/apps/games/ce-cdda-armada.kui`.
2. Merge this ZIP's `KUI` folder onto the SCI microSD card and cold boot.
   Its only installed file is `KUI/apps/games/ce-cdda-armada.kui`.
3. Select **Original Armada → Y Options → CD music On → START**.
4. Listen for **30 seconds at the main menu**, then play for **one or two
   minutes**. Expected success is a **repeating two-second music excerpt**.
   Check that gunfire and other game effects remain audible. Mention any
   disc error, freeze, slowdown or missing effects.
5. Press **A+B+X+Y+Start** and send **one photograph of the whole diagnostic
   screen**, even if no music plays. Include **BIND WHY NODE VALUE EXPECT
   DETAIL**, the playback counts and the cursor row.

The excerpt comes from seconds **2–4 of Armada audio track 4**, loaded from
this original GDI before boot. No new extraction, private upload or 2048
run is needed. The distributed PCM region is an all-zero placeholder,
filled from your image before boot. Full-song streaming and other CE titles
are separate work.

## Interpret the result

| Result | Meaning |
| --- | --- |
| BIND nonzero; QUEUE and ATTEMPT zero | Binding refused before playback. The failure row locates the check. |
| QUEUE rises; ATTEMPT stays zero | A request was queued but the deferred caller path did not attempt playback. |
| ATTEMPT rises; PLAY stays zero | The controller attempted the request and refused or received an error. |
| PLAY rises with a changing cursor | The CE backend accepted playback; your listening result determines whether it was audible. |

No full-track streaming, universal CE support or FMV improvement is claimed.
The menu runtime remains **785184b6db05**, including saved options and the
GitHub link beneath the K-UI logo. The normal CE loader and native, Toy
Commander and MDK2 loaders remain as installed.

## Restore and provenance

Set **CD music Off** to use the existing ordinary CE loader. To restore the
previous excerpt test exactly, merge `restore-ce-excerpt/KUI` onto the card
with the console off and cold boot. It contains byte-identical optional
loader build **b9aa5c70a431**.

Packaging verifies the original 785, module-fix 617 and preceding cache-fix
b9aa delivery ZIPs, their source provenance, and member SHA256 and CRC32
values. It verifies every file's bytes and mode in the recovered 785 source
tree. Current and preceding b9aa source archives, complete recovered Git
history, dependency sources, licenses, build evidence and output member
size/SHA256/CRC32 records are included. Older 785 and 617 source tars are
omitted from this ZIP after verification; their exact trees remain in the
Git bundle. This avoids redundant archives and keeps the deliverable below
50 MiB. No private executable, CE kernel, ARM firmware or music sample is
distributed.
