# K-UI Tyrfing boot-disc badge

The orange Tyrfing badge replaces the earlier cyan visor for K-UI 2.0 at the
maintainer's request on October 15, 2026. The new rune-etched sword consists of
original editable vector paths in `boot-disc-badge.svg`. The large K-UI wordmark
keeps the earlier badge's exact path geometry, position and size, and the
`github.com/TPMJB` credit keeps its existing position, font and size. Orange
`#ff8a22` and amber `#ffc56a` sit against the existing navy `#080f23` background.

The earlier badge came from TPMJB's `K-UI_DS` revision
[`2a5309298dde8fb100da1e2e4e10517695c9780f`](https://github.com/TPMJB/K-UI_DS/tree/2a5309298dde8fb100da1e2e4e10517695c9780f).
Its origin and unchanged wordmark are documented in
[the original branding record](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/resources/branding/README.md).
The original badge remains in Git history. These project-created assets use
this repository's GPL-3.0-only terms; no DreamShell rendering code is imported.

| File | SHA-256 |
| --- | --- |
| `boot-disc-badge.svg` | `0a19b8992e5192d9a7cdebbc4f0bcfba2ac3876db8f59474a0077036cf45ea51` |
| `boot-disc-badge.png` | `ff30c349c4e0e045ee358844e335015023922240516b6716999927155006eee8` |
| `boot-disc-badge.mr` | `42de4efd6e9401fbd1df17a42e1240a3e0a5b200ed5b7bc573ba29d0d1c77bac` |
| `launcher-brand.png` | `e5ebcba15d3990aef7180e66c1850e24947acbf28e1cd1087954c50d16c88039` |

The badge PNG is the actual 320×90, 32-color MR preview. The two-pixel
`#c0c0c0` border is the console's transparency color. The MR is 4,597 bytes,
within the 8,192-byte reservation. Packaging passes it to mkdcdisc's `-i`
option, which places it at bootstrap offset `0x3820`. Its decoded pixels were
checked against the PNG. Packaging reads it back from the generated CDI and
rejects a missing or substituted badge before publishing the bootstrap artifact.

The same editable SVG supplies the 256×128 launcher header: the sword group
uses `translate(7 21) scale(.95)` and the original wordmark group uses
`translate(12 29) scale(.79)`, plus the amber `by TPMJB` credit. The runtime
displays that header at its existing 128×64 size. App icon assets and embedded
array dimensions are unchanged.

Rebuild the preview, header and MR with Inkscape and Pillow, then regenerate
the embedded shell pixels:

```sh
python3 tools/boot_badge.py --rebuild-branding
python3 tools/generate_shell_art.py
python3 tools/generate_shell_art.py --check
python3 tools/boot_badge.py
python3 tests/test_boot_badge.py
```

The committed rendering used Inkscape 1.2.2 and Pillow 12.3.0, with the existing
DejaVu Sans Mono font. The badge is quantized to 32 colors with median-cut and
no dithering. The independent MR encoder uses indexed literals and runs of
2–127 pixels. Ordinary builds and badge validation consume the pinned output
without Inkscape, Pillow, image decoding or runtime allocation. If intentionally
changing the source, review both preview sizes and update the pins in
`tools/boot_badge.py`, `tools/generate_shell_art.py` and these provenance records.

Format references:
[KallistiOS makeip](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/makeip),
[mkdcdisc license-screen option](https://gitlab.com/simulant/mkdcdisc/-/blob/4d74e40dd2122e14389a305ed1d86dd024201389/README.md).
