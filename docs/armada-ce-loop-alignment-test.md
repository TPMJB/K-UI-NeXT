<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE CD music loop-alignment test

The previous **bd6b94787375** test entered the correct sound-owner process
and created the native sound buffer, but **Play** returned `0x887800ff`.
The screen reported four attempts, four refusals and **PLAY 0**. Current and
owner process were both `0x8c14e9ec`; the buffer size was `0x56220` bytes
(**352,800**). No music played.

The audited stereo driver requires a native loop length divisible by
**64 bytes**. A full two-second stereo PCM excerpt is 352,800 bytes, which
leaves a 32-byte remainder. This update allocates and plays **352,768 bytes**,
omitting the last **eight stereo frames**, about **0.18 ms**, from playback.
The complete **352,800-byte source excerpt**, seed layout, placeholder and
source PCM CRC checks remain unchanged. The audible test is still a roughly
two-second loop of Armada track 4.

The sound-owner callback, fresh identity and handle checks, caller-context
preservation, cache and mapping checks, and spare-voice admission remain in
place. The native driver still manages its sound objects. This does not
change the game's original sound-effect handling or introduce a driver reset.
Console playback with this update is unverified.

## Install and listen

1. With the console off, back up `KUI/runtime.kui` and
   `KUI/apps/games/ce-cdda-armada.kui`.
2. Merge this ZIP's `KUI` folder onto the SCI microSD card, replacing **both
   files**, and cold boot. The menu and optional audio loader show the same
   new build ID. The unused **L Memory** Diagnostics shortcut remains removed.
3. Select **Original Armada → Y Options → CD music On → START**.
4. Listen for **30 seconds at the main menu**, then play for **two minutes**.
   Expected success is a **roughly two-second repeating track-4 excerpt**.
   Check that gunfire and other game effects remain audible. Mention any
   disc error, freeze, slowdown or missing effects.
5. Press **A+B+X+Y+Start** and send **one photograph of the whole diagnostic
   screen**, even if no music plays. Include the binding, context, playback,
   buffer and cursor rows.

The source excerpt comes from seconds **2–4 of Armada audio track 4**, loaded
from your original GDI before boot. No new extraction, private upload or 2048
run is needed. The distributed PCM region is an all-zero placeholder filled
from your image before boot. Full-song streaming and other CE titles remain
separate work.

## Interpret the result

| Result | Meaning |
| --- | --- |
| BIND nonzero; QUEUE and ATTEMPT zero | Binding refused before playback. The failure row locates the check. |
| CALLBACK refusal | The native handoff to the validated sound-owner process failed its checks. |
| ATTEMPT rises; PLAY stays zero | The controller refused the request or received a native API error. |
| PLAY rises with a changing cursor | The CE backend accepted playback; your listening result determines whether it was audible. |
| Buffer size `0x56200` | The native buffer has the corrected 352,768-byte loop length. |

No full-track streaming, universal CE support or FMV improvement is claimed.
The ordinary CE loader and native, Toy Commander and MDK2 loaders remain
as installed. **CD music Off** selects the ordinary CE loader. Saved launch
options and the game library remain available.

## Restore and provenance

To restore both previous installed files exactly, merge
`restore-loop-alignment/KUI` onto the card with the console off and cold boot.
Both backup files are byte-identical build **bd6b94787375**.

Packaging verifies the original 785, module-fix 617, cache-fix b9aa,
loaded-layout 1524 and audio-handoff bd6 delivery ZIPs, source provenance,
and member SHA256 and CRC32 values. Both new files must match clean compiled
ELFs from the same final commit, with matching compiled and envelope build
IDs. The exact previous runtime and optional loader are retained together.

Current and preceding bd6 source archives, complete recovered Git history,
dependency sources, licenses, build evidence and output member
size/SHA256/CRC32 records are included. Older source tars are omitted after
verification; their exact trees remain in the Git bundle. This avoids
redundant archives and keeps the deliverable below 50 MiB. No private
executable, CE kernel, ARM firmware or music sample is distributed.
