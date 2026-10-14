<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE CD music audio-handoff test

The previous test passed binding, queued seven requests and made seven
deferred attempts. All seven stopped at the caller-context check
(**EXCERPT 0x12 / CONTEXT**, **PLAY 0**, **REFUSED 7**). The recorded current process was `0x8c14e8b4`, while the validated sound owner
was `0x8c14e9ec`. The controller could not safely call that owner's sound
objects from the process that happened to issue the GD request.

This update uses the exact audited CE kernel's native callback mechanism to
run admitted audio work in the validated sound-owner process. The kernel
enters and restores that process context. The adapter retains the owner
check inside the callback, verifies fresh binding identity and the process
handle, and preserves the caller's floating-point state. It does not manually
rewrite the current process, thread, page tables or MMU. The existing cache,
module, object, critical-section and spare-voice checks remain required.

This ZIP also updates the menu runtime to remove the unused **L Memory**
shortcut from the shared menu footer. Saved launch options and the game library remain
available. It installs both files together; installing only the optional
loader would leave the older menu on the card.

## Install and listen

1. With the console off, back up `KUI/runtime.kui` and
   `KUI/apps/games/ce-cdda-armada.kui`.
2. Merge this ZIP's `KUI` folder onto the SCI microSD card, replacing both
   files, and cold boot. The menu and optional audio loader show the same
   new build ID.
3. Select **Original Armada → Y Options → CD music On → START**.
4. Listen for **30 seconds at the main menu**, then play for **two minutes**.
   Expected success is a **repeating two-second track-4 music excerpt**.
   Check that gunfire and other game effects remain audible. Mention any
   disc error, freeze, slowdown or missing effects.
5. Press **A+B+X+Y+Start** and send **one photograph of the whole diagnostic
   screen**, even if no music plays. Include the binding, context, playback
   and cursor rows. The new **CALLBACK** refusal code is `30` if the native
   owner-process handoff fails.

The excerpt comes from seconds **2–4 of Armada audio track 4**, loaded from
this original GDI before boot. No new extraction, private upload or 2048
run is needed. The distributed PCM region is an all-zero placeholder filled
from your image before boot. This is still a conditional excerpt test;
full-song streaming and other CE titles are separate work. Console playback
with this update is unverified.

## Interpret the result

| Result | Meaning |
| --- | --- |
| BIND nonzero; QUEUE and ATTEMPT zero | Binding refused before playback. The failure row locates the check. |
| QUEUE rises; ATTEMPT stays zero | A request was queued, but deferred playback did not run. |
| CALLBACK refusal | The native handoff to the validated sound-owner process failed its checks. |
| ATTEMPT rises; PLAY stays zero | The controller refused the request or received a native API error. |
| PLAY rises with a changing cursor | The CE backend accepted playback; your listening result determines whether it was audible. |

No full-track streaming, universal CE support or FMV improvement is claimed.
The ordinary CE loader and native, Toy Commander and MDK2 loaders remain
as installed. **CD music Off** selects the ordinary CE loader.

## Restore and provenance

To restore both previous installed files exactly, merge
`restore-audio-handoff/KUI` onto the card with the console off and cold boot.
The backup contains menu build **785184b6db05** and optional excerpt loader
build **1524aa62a730**.

Packaging verifies the original 785, module-fix 617, cache-fix b9aa and
loaded-layout 1524 delivery ZIPs, source provenance, and member SHA256 and
CRC32 values. Both new files must match clean compiled ELFs from the same
final commit. Their compiled display IDs and envelope IDs must agree.
Packaging also verifies that the new runtime contains no stale **L Memory**
label and that the two backup files match their exact prior bytes.

Current and preceding 1524 source archives, complete recovered Git history,
dependency sources, licenses, build evidence and output member
size/SHA256/CRC32 records are included. Older source tars are omitted after
verification; their exact trees remain in the Git bundle. This avoids
redundant archives and keeps the deliverable below 50 MiB. No private
executable, CE kernel, ARM firmware or music sample is distributed.
