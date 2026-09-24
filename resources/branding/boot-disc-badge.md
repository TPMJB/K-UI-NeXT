# Original K-UI boot-disc badge

The maintainer requested the same K-UI badge below the Dreamcast's SEGA license
screen as the earlier K-UI disc. These three files are reused byte for byte from
`TPMJB/K-UI_DS` revision
[`2a5309298dde8fb100da1e2e4e10517695c9780f`](https://github.com/TPMJB/K-UI_DS/tree/2a5309298dde8fb100da1e2e4e10517695c9780f).
They are TPMJB's independently authored branding, reused under this repository's
GPL-3.0-only terms. No old DreamShell bootstrap or rendering code is imported.

| Local file | Original path | SHA-256 |
| --- | --- | --- |
| `boot-disc-badge.mr` | `resources/boot-disc-badge.mr` | `4f95ef3caac9d449914baa6266b154582363edb7e5b821d5bf18121951fc5fec` |
| `boot-disc-badge.png` | `resources/boot-disc-badge.png` | `88a20c56613f02286b7e9c1c3ace7110a8829c35478288e2715479ad8274c43b` |
| `boot-disc-badge.svg` | `resources/boot-disc-badge.svg` | `d65539440dd3df1b87a546d58043a09c82c2ad3771ad2d4cb2232001b651bce3` |

The [original branding record](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/resources/branding/README.md)
identifies this specific boot badge as native-size vector outlines and text,
separate from the larger illustrated launcher artwork. It preserves the cyan
visor, magenta accent, K-UI lettering, and `github.com/TPMJB` address. The
[original asset manifest](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/resources/next-branding.sha256)
records the same MR checksum above.

The committed MR contains 320×90 pixels and a 32-color palette in 5,220 bytes,
within the 8,192-byte boot-logo reservation. Packaging passes it directly to
mkdcdisc's `-i` option, which places the logo at bootstrap offset `0x3820`.
The existing mkdcdisc bootstrap supplies the rest of the boot disc. No artwork
renderer, conversion dependency, or change to the SD runtime is required.

`tools/boot_badge.py` checks the exact original asset, MR geometry and decoded
pixel count. Packaging also reads the badge back from the generated CDI's
bootstrap and rejects a missing or substituted logo before producing the
`bootstrap-cd` artifact. The PNG is the original preview, not a console capture.

Format references:
[KallistiOS makeip](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/makeip),
[mkdcdisc license-screen option](https://gitlab.com/simulant/mkdcdisc/-/blob/4d74e40dd2122e14389a305ed1d86dd024201389/README.md).
