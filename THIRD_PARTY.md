# Source provenance and licenses

New code is GPL-3.0-only under [LICENSE](LICENSE). No DreamShell source or
noncommercial license is used as an input to this build. This is source-aware
development, not a claim of a formal clean-room process.

| Input | Exact selection and use | License/notice |
| --- | --- | --- |
| KallistiOS | Official upstream `fcfa7d869471591ca1c777543261a7bfea7cb726`; kernel, hardware APIs and minifont | KOS BSD terms and per-file exceptions; [LICENSE.KOS](LICENSES/LICENSE.KOS), upstream `AUTHORS` and `doc/license/` |
| Bus activation helper | `src/dreamcast/drive_bus.c` adapts the activation portion of upstream KOS `cdrom_init` at that pin | Original KOS copyright holders and BSD terms retained in the file |
| FatFs | ChaN R0.16, official patches 1 and 2; SHA-256-pinned downloads, original license and patched source retained | [FatFs notice](LICENSES/LICENSE.FatFs) |
| GCC/Binutils/Newlib | KOS stable profile at the pinned KOS revision | Component licenses; GCC runtime exception and Newlib component notices apply to runtime code |
| mkdcdisc | Canonical Simulant GitLab repository `4d74e40dd2122e14389a305ed1d86dd024201389`; separate host image-writing tool | MIT for its own code, with separate third-party components; upstream `THIRD-PARTY-NOTICES.md` |
| IP.BIN in the CDI | mkdcdisc MIL-CD template, including the LiENUS homebrew bootstrap | See the upstream IP.BIN provenance below; no DreamShell bootloader |
| Linux test/build utilities | Distribution `mkfs`, `fsck`, Meson, Ninja, libisofs and mtools | Host tools only; not linked into the Dreamcast executable |

Source URLs and downloaded-file hashes are in [dependencies.json](dependencies.json).
The build artifact includes applicable KOS and FatFs license texts and source
records. Build-host package versions may change within Ubuntu 24.04; pinned
source inputs do not mean the entire compiler/image build is bit-for-bit
reproducible. `build.json` records the actual compiler and revisions, and
`SHA256SUMS` identifies the produced files.

## Bootstrap provenance

The image packager's [upstream notice](https://gitlab.com/simulant/mkdcdisc/-/blob/4d74e40dd2122e14389a305ed1d86dd024201389/THIRD-PARTY-NOTICES.md)
distinguishes the LiENUS replacement bootstrap from fixed Sega license-screen
code that the retail BIOS expects. Its interoperability rationale is an upstream
explanation, not blanket legal clearance from this project. The diagnostic uses
the MIL-CD template, disables the optional MR artwork, and does not distribute a
Sega BIOS image or a commercial game's extracted bootstrap.

The packager also contains libisofs, libedc, an ELF parser, a scramble routine and
makeip-derived code under their respective notices. They run on the build host;
their object code is not linked into K-UI. We retain the packager's notice so its
status is not reduced to its top-level MIT label.

## Independent references

The BSD `sega-dreamcast/httpd-ack` project informed the earlier feasibility
research. No code from it is copied into this implementation. The command
adapter uses KOS's documented firmware structures and status values. It does
not patch or call the KOS CD-ROM command wrappers.

FatFs source changes consist of the two official patches. `config/ffconf.h` is
new project configuration; the block-device adapter and storage probe are new
project code. A source inventory must be revisited when any new dependency,
font, bootstrap, catalog or runtime component is added.
