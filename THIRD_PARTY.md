# Source provenance and licenses

New code is GPL-3.0-only under [LICENSE](LICENSE), except files explicitly
licensed otherwise, including the MIT Wi-Fi firmware and shared link library.
No DreamShell framework or
reader code is used as an input to this build. Selected original TPMJB artwork
from the earlier K-UI project is reused with the owner's authorization. This is source-aware
development, not a claim of a formal clean-room process.

| Input | Exact selection and use | License/notice |
| --- | --- | --- |
| KallistiOS | Official upstream `fcfa7d869471591ca1c777543261a7bfea7cb726`; kernel, hardware APIs, minifont and `arch_exec` execution trampoline | KOS BSD terms and per-file exceptions; [LICENSE.KOS](LICENSES/LICENSE.KOS), upstream `AUTHORS` and `doc/license/` |
| Original K-UI artwork | TPMJB's launcher badge and three original app icons, unchanged source images from `K-UI_DS` revision `2a5309298dde8fb100da1e2e4e10517695c9780f`; encoded as embedded RGB565 pixels | Owner-authorized reuse under this project's GPL-3.0-only; exact origins and hashes in [artwork record](resources/branding/README.md) |
| DejaVu Sans / Sans Bold | Unmodified font files and a generated ASCII raster atlas for the independent shell; no font engine linked at runtime | [Bitstream Vera/DejaVu license](LICENSES/DejaVu-fonts.txt); [source hashes and generation](resources/fonts/README.md) |
| Bus activation helper | `src/dreamcast/drive_bus.c` adapts the activation portion of upstream KOS `cdrom_init` at that pin | Original KOS copyright holders and BSD terms retained in the file |
| Resident probe SD reader | `src/loader/sd_reader.c` adapts the SCIF pin sequence and SD/CSD protocol from upstream KOS `hardware/scif-spi.c`, `hardware/sd.c`, and TMU register definitions from `kernel/timer.c`, all at the same pin; a separate read-only implementation owns its state and timer after kernel shutdown | Original per-file copyright holders and [KOS BSD terms](LICENSES/LICENSE.KOS) retained; no DreamShell source input |
| stb_vorbis 1.22 | `stb_vorbis.c` with documented short-read guards from `nothings/stb` commit `2c980bb59875b0d32144a71867fbdebb2f77cd20`; bounded, RAM-only Vorbis decoding in the Music app | Upstream alternative A, [MIT notice](LICENSES/stb_vorbis.txt); upstream/local hashes and adaptations in `dependencies.json` |
| stb_image 2.30 | `stb_image.h` with a documented zero-length read guard, from the same `nothings/stb` commit; PNG and JPEG box art that the owner places in `KUI/covers` | Upstream alternative A, [MIT notice](LICENSES/stb_image.txt); upstream/local hashes and adaptation in `dependencies.json` |
| lwext4 | Pinned `include/` and `src/` from `gkostka/lwext4` commit `58bcf89a121b72d4fb66334f1693d3b30e4cb9c5`; read-only CD bootstrap with documented checksum/bounds and read-only fixes; upstream/local per-file SHA-256 pins in `dependencies.json` | BSD-3-Clause and GPL-2.0-or-later per file; upstream [notice](LICENSES/lwext4.txt), all file headers retained in `third_party/lwext4` |
| FatFs | ChaN R0.16, official patches 1 and 2; SHA-256-pinned downloads, original license and patched source retained | [FatFs notice](LICENSES/LICENSE.FatFs) |
| K-UI Wi-Fi link library | This project's own `firmware/kui-wifi/components/kwlink` (`kwlink.c`, `kwhost.c`): the Dreamcast's side of the Wi-Fi board's link, shared with the board's firmware | MIT rather than GPL-3.0-only, so the firmware, which links Espressif's closed-source Wi-Fi libraries, can use it too; [notice](LICENSES/kui-wifi-kwlink.txt) |
| Known-dump catalogues (`data/known-dumps/`) | Track names, sizes and CRC32s only, no game or disc data. `redump.db` is adapted from the Libretro database's Redump Dreamcast DAT (CC BY-SA 4.0, share-alike; this file stays under that licence); `tosec.db` is a factual index of TOSEC's 2025-03-13 DAT pack | [Sources and licences](LICENSES/known-dumps-README.txt) |
| GCC/Binutils/Newlib | KOS stable profile at the pinned KOS revision | Component licenses; GCC runtime exception and Newlib component notices apply to runtime code |
| mkdcdisc | Canonical Simulant GitLab repository `4d74e40dd2122e14389a305ed1d86dd024201389`; separate host image-writing tool | MIT for its own code, with separate third-party components; upstream `THIRD-PARTY-NOTICES.md` |
| IP.BIN in the CDI | mkdcdisc MIL-CD template, including the LiENUS homebrew bootstrap | See the upstream IP.BIN provenance below; no DreamShell bootloader |
| Linux test/build utilities | Distribution `mkfs`, `fsck`, Meson, Ninja, libisofs and mtools | Host tools only; not linked into the Dreamcast executable |
| KiCad 7 libraries (CF board) | Symbols and footprints used by `hardware/cf-board`; `cf-board.pretty/CF-Card_3M_N7E50-E516xx-30_SmallRing.kicad_mod` is KiCad's `CF-Card_3M_N7E50-E516xx-30` with its board-lock rings cut from 3.99 to 3.2 mm | CC-BY-SA 4.0 with the KiCad libraries' exception for designs that use them; the adapted footprint file stays under CC-BY-SA 4.0 |
| iceGDROM riser board (reference) | Only the signal on each pin of the Dreamcast's GD-ROM connector, taken from `zeldin/iceGDROM` `pcb/riser`; no files copied | GPL-3.0 upstream; facts only |
| KiCad, Freerouting, kiutils | Host tools that draw, route and check the CF board (`hardware/cf-board/tools/build.sh`) | Host tools only; nothing from them is in the board files beyond their output |

The Wi-Fi board's firmware ([firmware/kui-wifi](firmware/kui-wifi/README.md))
is a separate program for the ESP32-C5 under its own MIT licence. It is built
with Espressif's ESP-IDF, including its closed-source Wi-Fi libraries, by its
own workflow, and is not part of the Dreamcast builds. Its host tests, and
K-UI's (`tests/wifi_model.c`), run its bridge core on the build computer only.

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
research and the declared GDI gap/address convention. No code from it is copied
into this implementation. The SHA-256/CD EDC routines are new implementations
of published algorithms; see [the capture format](docs/capture-format.md).
Games box art is also new code. The `.PVR` texture reader
(`src/core/pvr_texture.c`) follows the file layout, layout codes and Morton
(twiddled) texel order documented by KallistiOS's own texture tool at the
pinned commit (`utils/pvrtex/file_pvr.c`, `utils/pvrtex/pvr_texture.c`); no
code is copied from it. `0GDTEX.PVR` is found with the existing independent
ISO9660 reader. No DreamShell code, cover scanner or decoder is used. The command
adapter uses KOS's documented firmware structures and status values. It does
not patch or call the KOS CD-ROM command wrappers.

FatFs source changes consist of the two official patches. `config/ffconf.h` is
new project configuration; the block-device adapter and storage probe are new
project code. A source inventory must be revisited when any new dependency,
font, bootstrap, catalog or runtime component is added.

## Isolated CDDA harness

The experimental `Makefile.cdda` target links K-UI's own freestanding SCI SD
transport, a privately configured read-only copy of pinned FatFs, the existing
font renderer and new PCM/timeline/standalone harness code. It links no KOS
kernel and no DreamShell source or binaries. `src/loader/cdda_aica.c` uses the
BSD-licensed register contracts from the same pinned official KallistiOS
revision: `sound/arm/aica.c`, `hardware/spu.c`, `hardware/g2bus.c`, and
`include/dc/g2bus.h`/`fifo.h` under `kernel/arch/dreamcast`. The original per-file
notices are retained in its header and `LICENSES/LICENSE.KOS` applies.

The startup follows K-UI's existing detached cache/stack handoff; the new clock
uses the documented SH7091 TMU register layout already used by the independent
SD probe. PCM sector stripping, planar conversion and deadline state are new
project implementations, not translations of DreamShell refill machinery.
`include/kui/cdda_clock.h` uses the same pinned KOS `kernel/timer.c` reference:
measured main clock 199,499,520 Hz, peripheral clock one quarter of that, and
the owned TMU peripheral-clock/4 selection. The resulting 12,468,720 Hz is a
documented reference, not an absolute measurement of the running console or
a feedback adjustment of AICA pitch. The bounded conversion and cooperative
job/state helpers are independent project implementations.
The controlled profile 07 descriptor, cooperative service guard, isolated
client program and integer SH C-call stack bridge are new project code.
They exercise explicitly owned homebrew stacks and ordinary integer C ABI;
they do not install a retail interrupt handler or copy a loader's CDDA hook.
Profile 08's request queue, separately linked BIOS client and owned-vector
wrapper are new project implementations. Command numbers, parameter layouts,
handle/status meanings and the r4/r5/r6/r7 calling convention are taken from
the same pinned official KallistiOS revision's `include/dc/syscalls.h`,
`hardware/cdrom.c` and `hardware/syscalls.c` under `kernel/arch/dreamcast`.
Its BSD notice remains in [LICENSE.KOS](LICENSES/LICENSE.KOS). No KOS syscall
implementation is linked or copied into this freestanding harness. Unsupported
commands and ambiguous PLAY2 ranges are refused rather than inferred from a
third-party loader. This remains an explicitly owned homebrew vector test.
Profile 09's incremental multi-sector queue, chunk-identity checks and batch
client extend K-UI's own controlled implementation in new files. The count
parameter follows the same pinned KOS `cd_read_params_t`/PIOREAD interface;
the sector-by-sector scheduling and confirmed-prefix publication rules are
new project code. Each EXEC performs at most one 2,048-byte data chunk.
Cancellation is admitted between chunks, with no active-transfer abort,
retail interrupt or uncached-alias coherence claim.
Profile 10's separate controlled client intentionally waits beyond the same
cooperative lease and verifies the resulting stopped audio and confirmed
prefix. Its expected deadline is tracked separately from unexpected faults;
it introduces no interrupt watchdog or automatic background servicing.
`tools/cdda_fixture.py` generates original sine-tone test data. The user's Toy
Commander audio remains external and is excluded from source and test packages.
