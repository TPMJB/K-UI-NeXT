<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Extending the WinCE CDDA loader

The SCI audio baseline is RC9, console source `8be0305943c9`, published as
`68736e84fb5f20f1a25307d0354c0da75a1c9f7c`. Its final Armada recording
`62665.mp4` reports one underrun and four complete cached primes with
47–50 ms worker-side restart delay. Occasional audio gaps remain a known
limitation. This extension does not retune that baseline.

## Next candidates

Worms Armageddon and Bust-A-Move 4 are WinCE/CDDA candidates. Pitito's
first-hand test reports list both in the WinCE/CDDA family:
https://www.dreamcast-talk.com/forum/viewtopic.php?t=17407
and https://segaxtreme.net/threads/wince-cdda-fix.24906/ .
Those reports concern CDI conversion fixes, not compatibility with K-UI's
SCI emulation. No patch bytes from those posts are used here.

The proposed first target is Worms Armageddon, whose ordinary CE loading path
has already been exercised in this project's earlier console tests. This is
an ordering decision, not a claim that its kernel matches Armada's.

## Reusable engine and required profiles

The source model already accepts up to 99 validated track intervals. Its
command epochs, cursor-authoritative consumption, progressive checked-source
publication, ring refills, adaptive SCI grants and checked opening cache can
be reused. Each title still needs its actual track inventory admitted by the
launcher and passed to the player.

The current launcher, seed checks, five-entry track array, CE kernel addresses,
application witnesses and native sound-library layout are Armada-specific.
Other games cannot be admitted by merely enabling the menu option or removing
the product check. Compare the selected boot image, ROM module layout, external
DLLs, API exports, process-owner discovery and native object/callback interface
before adding a matching profile. Identical verified runtime components may
share one profile; differing builds need separate checked adapters.

Keep the same saved CD music option in the launcher. Select an admitted profile
from the image/runtime identity. Preserve the ordinary CE loader and existing
Armada profile while adding candidates; unsupported identities must remain
explicit rather than being treated as Armada.

## Collect a small startup bundle

Run the standalone helper against each original, unconverted raw2352 GDI:

```sh
python3 kui-wince-startup-bundle.py "/path/to/Worms-Armageddon.gdi" --output Worms-Armageddon-startup.zip
python3 kui-wince-startup-bundle.py "/path/to/Bust-A-Move-4.gdi" --output Bust-A-Move-4-startup.zip
```

The helper reads the dump without modifying it. It collects IP.BIN, the
IP-selected boot file, external EXEs and bounded DLL/DRV/LIB candidates,
retaining original directory paths. Its JSON includes disc identity, track
ranges, audio candidates, file hashes, a filesystem inventory and explicit
omissions. It does not copy audio tracks or other ordinary game assets.
Audio-track presence alone does not prove when a game uses them. This is
startup analysis, not full disc verification or a console compatibility test.
DLL/DRV/LIB suffixes identify candidates, not verified runtime code; those
selected files may themselves contain embedded assets.

Use distinct output names as above; existing outputs are never overwritten.
The generated ZIPs are inputs for private analysis and are not release assets.
The published helper and its synthetic tests contain no retail game bytes.

## Next implementation step

Compare the two generated bundles with Armada's verified kernel and sound
components. Then extend the appropriate profile and launch admission, build
one console candidate for the first game, and test track changes, pause/resume,
repeated PLAY and sustained delivery. Reuse the RC9 transport and buffering
parameters unless that game's measurements justify a change. No console
compatibility is claimed before that test.
