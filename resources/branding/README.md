# Original K-UI launcher assets

These selected TPMJB-created K-UI assets are reused at the maintainer's request
under this repository's GPL-3.0-only terms. The original input files are retained
byte for byte. This record covers the listed artwork, not inherited DreamShell
code or the other assets in the legacy repository.

Source repository: `TPMJB/K-UI_DS` (formerly `DreamShell_NeXT`), revision
[`2a5309298dde8fb100da1e2e4e10517695c9780f`](https://github.com/TPMJB/K-UI_DS/tree/2a5309298dde8fb100da1e2e4e10517695c9780f).

## Origins and exact inputs

The badge was introduced with the original K-UI branding in
[`7d5b4477a9d4f8ea406c6ec58d590f6eaafa770a`](https://github.com/TPMJB/K-UI_DS/commit/7d5b4477a9d4f8ea406c6ec58d590f6eaafa770a).
Its [origin record](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/resources/branding/README.md)
identifies artwork created with image-generation assistance for TPMJB. The
imported `launcher-brand.png` is the original 256x128 launcher texture, derived
from that badge by the legacy `utils/build_kui_artwork.py` conversion step.

The original icon SVGs were introduced in
[`f7fab643c3727a90a739a4d8249a6c184aee1c74`](https://github.com/TPMJB/K-UI_DS/commit/f7fab643c3727a90a739a4d8249a6c184aee1c74)
and recolored in `7d5b4477`. They contain geometric paths/shapes, with no embedded
third-party artwork. Their original 64x64 PNG renderings are retained alongside
the SVGs so regeneration needs Pillow only and preserves the original textures.
The original Speedtest icon represents the independent Diagnostics entry.

| Local file | Source path at the pinned revision | SHA-256 |
| --- | --- | --- |
| `launcher-brand.png` | `applications/launch_app/images/brand.png` | `a5d98f17ae66cc549760e98a7907a2c680d4af0d57ea209bab07df7cb7e50355` |
| `../icons/disc-ripper.svg` | `applications/gd_ripper/images/icon-next.svg` | `8263c8ef8efe9b779863303f90d921f90b9374c734255cb8f119463990eaa683` |
| `../icons/disc-ripper.png` | `applications/gd_ripper/images/icon.png` | `1c3bb0ed7ffc95949a06339b16b87f8392de6ed443cd92e4913a4b8d70c1283b` |
| `../icons/settings.svg` | `applications/settings/images/icon-next.svg` | `772f5912638c1e0996edd51be50ec2c2d5c9bd6c4761f76b892da5a60cf13ccd` |
| `../icons/settings.png` | `applications/settings/images/icon.png` | `7d5577f04247961016fd04ab620ebef998ae14982967372af8a766b1bf61f74b` |
| `../icons/diagnostics.svg` | `applications/speedtest/images/icon-next.svg` | `b23ed82505824f2c43ba3b05262533d27d358cc68ceeaf9496c61708e7ab662d` |
| `../icons/diagnostics.png` | `applications/speedtest/images/icon.png` | `8b1119e261aa24b8ac5aaef898f736f738fdee9efcb2a1f4622193682097b863` |

The original launcher uses a flat `#080F23` background, `#121A31` panels,
`#472958` selection, `#F07DDC` selection edge, `#65E8F2` accent and
`#343354` dividers. Its home layout has an app list on the left and selected-app
details on the right. The inherited `images/bg.png` DreamShell logo is unused in
that layout and is not imported. Neither the boot splash, bootstrap, old fonts,
XML app definitions nor the legacy rendering/event code is imported here.

## Rebuilding the embedded pixels

```sh
python3 tools/generate_shell_art.py
python3 tools/generate_shell_art.py --check
```

Run from the repository root with Pillow installed. The generator validates the
seven hashes above and writes `src/dreamcast/shell_art.inc`. Ordinary Dreamcast
builds use that committed include and do not need image conversion libraries.

The opaque brand is 128x64 RGB565 pixels against the original navy background.
Icons are emitted at 128x128 for the detail pane and 24x24 for the app list, in
disc-ripper/settings/diagnostics order. Bilinear scaling follows the original
launcher's texture scaling. Fully transparent icon pixels use `0xf81f`, which the
renderer skips; antialiased edges are precomposed against `#121A31`. The arrays
contain 118,144 bytes of pixel data. They are fixed assets, with no runtime
decoding, file access or allocation.
