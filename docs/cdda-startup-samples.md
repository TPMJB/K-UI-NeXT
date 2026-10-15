<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Quake III Arena and Worms World Party startup samples

These are the next owner-supplied native and Windows CE CDDA candidates.
Use the standalone `kui-cdda-startup-bundle.py` on your computer with Python 3;
there is nothing to install on the Dreamcast for this step.

Extract the helper download, open a terminal in that folder, and run:

```sh
python3 kui-cdda-startup-bundle.py "/path/to/Quake III Arena.cue" \
  --include QUAKE3/Q3_USA.BIN --include QUAKE3/INTRO.BIN \
  --include GINSU.INI --include GINRESET.BIN --include GINXFER.BIN \
  --include QUAKE3/BASEQ3/SYSTEM/SYSTEM.ZIP --output Quake-III-runtime.zip
python3 kui-cdda-startup-bundle.py "/path/to/Worms World Party.cue" --output Worms-World-Party-startup.zip
```

Replace the quoted example paths with the actual `.cue` paths. Leave each
CUE beside its referenced BIN files. Separate track BINs and a combined raw
2352-byte BIN are supported. The helper maps stored indexes and pregaps to
the original high-density sector addresses, checking raw data-sector
addresses independently. Ambiguous or conflicting layouts stop with an
error instead of guessing where the game begins.

GDI input remains supported: pass the `.gdi` path in the same commands.
Original raw 2352-byte DATA and converted 2048-byte DATA are supported;
cooked CUE input requires unambiguous high-density geometry. AUDIO tracks
retain their original 2352-byte format. Prefer the original rip when both
are available. The helper detects native versus Windows CE from the disc
header. No image conversion is needed.

Return **Quake-III-runtime.zip** and **Worms-World-Party-startup.zip** for
analysis. Existing output ZIPs are never overwritten. If extraction stops,
send its error text; a new rip is not needed merely because the helper
reports an unsupported layout.

The inspected US Quake III version boots through a GINSU launcher. Its main
and intro `.BIN` files need the explicit includes above because arbitrary
`.BIN` files can also contain game assets. The original default bundle still
provides useful track, driver and inventory evidence. Other versions may
use different paths; an include-not-found error should be checked against
their inventory. See the [sample findings](evidence/quake3-startup-inventory-2026-10-15.md).

The helper reads source files without modifying them. It collects the boot
header and executable, bounded sound/runtime candidates, original descriptor,
file hashes, track metadata and an ISO directory inventory. Windows CE
external EXEs are also collected. Optional candidates omitted by the byte
budget are recorded explicitly. Audio tracks and ordinary game assets are
not copied by default; candidate suffixes alone do not prove code contents.

The report identifies runtime inputs for adapter analysis. It does not
patch or launch the game, verify a complete disc, or establish K-UI playback
compatibility. Keep the currently working Worms Armageddon loader installed.
The helper download contains authored source and synthetic test evidence;
the startup ZIPs you generate contain your own extracted runtime files.
