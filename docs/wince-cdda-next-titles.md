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

Bust-A-Move 4 was ported first because its supplied runtime closely matched
Armada. Worms Armageddon supplies a different older sound-runtime family;
its second console candidate now has audible playback with recovered
underruns documented in its test guide.

## Reusable engine and required profiles

The source model already accepts up to 99 validated track intervals. Its
command epochs, cursor-authoritative consumption, progressive checked-source
publication, ring refills, adaptive SCI grants and checked opening cache can
be reused. Each title still needs its actual track inventory admitted by the
launcher and passed to the player.

The common engine now has separate Armada, Bust-A-Move 4 and Worms profiles.
CE kernel addresses, application witnesses and native sound-library layouts
remain specific to the admitted runtime.
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

## Opt-in availability

The saved CD music toggle is offered to inspected WinCE GDIs with high-density
audio tracks, including converted 2048-byte DATA with original raw AUDIO.
New images default to Off. The menu does not
use a title whitelist to hide the option. Actual launch still requires a
matching native runtime adapter; an unrecognized profile is refused before
handoff with a clear option to turn CD music Off. An available toggle does
not claim universal WinCE playback compatibility.

The diagnostic candidate removes the all-raw DATA admission restriction for
the three exact profiles while retaining raw AUDIO and executable CRC checks.
It also freezes a fourth terminal page at the last underrun, using existing
clock samples and source/ring/transport progress. See
[the diagnostic test guide](wince-cdda-diagnostic-test.md). This records
event-correlated evidence without retuning playback or reconstructing discarded
raw DATA sectors.

## Supplied runtime comparison and first port

The supplied US Bust-A-Move 4 runtime (T-8117N, 26 tracks) shares Armada's
SNDCORE, audio firmware and kernel instructions. A separate BAM4 profile
selects its rebased kernel globals, DirectSound method addresses and verified
game device holder. Its first console candidate reuses the RC9 streaming
engine; see [the BAM4 test guide](bam4-ce-cdda-test.md). On 2026-10-15 the
owner reported successful audible playback with build `ed0a2c809bda`. Recording
`62683.mp4` shows ten successful starts, zero refusals/HRESULT, and one
recovered underrun. Keep that build as the working BAM4 baseline; it is not
a claim of zero-dropout operation or other-revision compatibility.

The supplied Worms Armageddon runtime uses a different DirectSound callback
that does not take Armada's refill critical section. Its separate adapter
protects each short host copy against the callback's current in-flight source
slice and handles asynchronous Stop cleanup before re-priming. It reserves
a lower source/controller pool below that kernel's OEM region. See
[the Worms console test](wince-cdda-worms-test.md). The owner heard music with
build `5be6d79357b3` and noticed a minor cut; recording `62685.mp4` contains
three recovered underruns, five successful starts and no refusals/HRESULT.
This is the working Worms baseline, with the residual buffer-supply limitation
still open. Armada and BAM4 modules are retained unchanged by that delivery.
