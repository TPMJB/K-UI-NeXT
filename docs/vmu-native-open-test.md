<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Open native virtual-A2 test

This is the ordinary native Standard reader with the current automatic
virtual-A2 adapter. Use it to test ordinary non-Windows-CE, non-CDDA games on
SCI independently of the Background-reader experiment. Physical A1 remains
available; leave physical A2 empty.

## Install without a title selection

Run from this folder:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

Omitting `--game` leaves the startup cache empty and runs fresh SDK-family
discovery on the launched executable. This is an existing supported route,
included here expressly for broader testing. The six optional packaged cache
entries accelerate discovery for their exact images; they do not limit which
titles may attempt a cold scan.

Use SCI, Background loading Off, Auto/default encoding, CD music Off and RAM
patches Off. Safely eject and cold boot. Existing prepared image, journal,
identity and profile files are not rewritten by the installer.

For each game, record recognition, existing-save load, gameplay/scene change,
new save, full power-off, then reload. Include the title, image format and
displayed build. Photograph a complete refusal or error screen.

Automatic admission still needs a unique supported Maple/allocator/layout
and lifetime proof. A cold scan may refuse an unknown SDK family; it does
not guess offsets or install partial hooks. Passing one title does not prove
all revisions or all games. The current generic route has reviewed fixtures
for Power Stone 2, Skies, Sonic Adventure 2, Grandia II and both Evolutions.
Known remaining family work includes the CT2/PSO/JGR cases, Time Stalkers'
older allocator, and segmented ownership used by titles such as Silver,
Soul Reaver and RECV. Legacy title-specific success is separate from this
generic adapter's coverage.

If a new game's family is refused, the included standalone collector can
prepare a small analysis bundle from its original raw 2352-byte GDI:

```sh
python3 collect-native-startup.py /path/to/game.gdi --output Game-startup.zip
```

It collects the boot metadata and executable, not the whole disc. Its input
is an original native GDI, not a converted 2048-byte image or a CE disc. The refusal
photo and that bundle provide the next family adapter's inputs.

For one of the six catalog images, an optional `--game` selection still works;
run `python3 personalize.py --list-games` to list them. The runtime-profile
option also remains available, with its usual trigger gesture and intentional
stop, but ordinary compatibility testing does not require timing mode.
