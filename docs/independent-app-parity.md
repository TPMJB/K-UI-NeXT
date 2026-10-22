# Independent app parity and package sizes

Source review: 2026-09-23. This document records implementation boundaries,
not new measurements of the finished GD-ROM reader.

## GD Play

GD Play hands an inserted game back to the console's normal BIOS boot sequence.
The confirmation must make clear that this exits K-UI. The caller stops the I/O
worker, closes/unmounts storage and shuts down audio before the main thread calls
`kui_gd_play_boot()`. The implementation configures `ARCH_EXIT_REBOOT`, then
calls `arch_exit()`.

This is the supported shutdown path in the pinned KOS source:
`arch_exit -> exit -> arch_exit_handler -> arch_shutdown -> arch_reboot`.
KOS shuts down initialized peripherals and filesystems before entering the BIOS.
The app does not jump directly to a guessed BIOS address and does not import the
legacy GD Play executable-loader code.

Normal BIOS region and autostart behavior still apply. A BIOS with autostart
disabled may require choosing Play. This is not a region-free loader, a software
replacement for BIOS authentication, or an SD image loader. Booting a retail disc
through this handoff remains a console acceptance test.

Primary sources:
[KOS architecture interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/arch.h),
[KOS shutdown implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/init.c).
KOS also documents `syscall_system_cd_menu()`, but that enters the BIOS CD
menu; its name is not evidence of a direct game-start syscall.
[System calls](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/syscalls.h).

## Region and BIOS maintenance

Neither capability inherently requires DreamShell. The previous apps combine our
maintenance workflow with inherited device drivers and application-framework code.
Their independently authored validation and backup behavior can guide new apps;
that does not make the inherited flash drivers independently reusable.

| Capability | Independent foundation | Hardware boundary and next gate |
| --- | --- | --- |
| Read console region and system settings | Upstream KOS `flashrom_get_region()` and `flashrom_get_syscfg()` | Read-only inspection can work on ordinary consoles. Unknown/failed reads must remain unknown; KOS explicitly says region detection is not guaranteed on every Dreamcast. |
| Save factory/settings flash backups | Upstream KOS `flashrom_info()` and `flashrom_read()` | Validate reported partition bounds, read exact lengths, save to a new file and verify the reopened SD file. Do not export private flash contents into project evidence. |
| Permanently change factory region | A new, independently sourced implementation using the actual flash chip's documented protocol | The factory region area is protected. The previous project's hardware procedure requires a physical protection-unlock modification; a settings screen alone cannot enable it. Board revision, actual flash part and wiring must be identified before defining support. |
| Back up or compare the visible boot BIOS bank | Read-only access plus an explicit validated hardware profile | A backup app must distinguish the boot BIOS from settings flash and respect the physically selected bank. A successful read does not establish that the chip is programmable. |
| Write a replacement/dual BIOS chip | Independently implemented chip-specific identification, erase/program and verification | The original Sega mask ROM is read-only. A programmable replacement/dual-BIOS installation is required. Support depends on exact chip ID, operating voltage, bank wiring, sector layout and command protocol, not simply “VA1.” |

The previous BIOS driver lists the Sega MPR device as read-only and supports
specific programmable AMD, ST, Macronix, AMIC and ESMT parts. That is evidence of
the old build's supported devices, **not a support list for the independent
build**. Its region-change documentation describes a hardware unlock and was
tested on particular PAL boards; it must not be generalized into instructions
for an unidentified board or replacement PSU.

Sources:
[KOS flash interface and caveats](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/flashrom.h),
[legacy region hardware manual](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/region_changer/doc/Manual.txt),
[legacy BIOS chip capabilities](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/modules/bflash/devices.c),
[legacy maintenance workflow](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/bios_flasher/modules/module.c).

Implementation sequence:

1. Add read-only system identity, partition inspection and verified SD backups.
2. Audit provenance of the original maintenance model and tests; extract only
   independent contributions. Preserve exact-size images, bank-layout validation,
   backup-before-erase, unchanged-bank checking and full post-write comparison.
3. Define supported hardware from chip datasheets and actual board/bank wiring.
   Write host tests for wrong IDs, image size, protected regions, timeouts,
   failed backup, changed bank, partial programming and verify mismatch.
4. Enable writes only for an explicit supported profile after hardware
   acceptance with a recoverable programming setup. Unsupported hardware retains
   read-only inspection/backup.

No region writes, boot-flash programming or imported DreamShell flash driver is
added in this app round.

## Why the distributions have different sizes

These retrieved values describe **compressed downloadable packages**, not
resident RAM, executable size, app quality or feature equivalence.

| Retrieved package | Bytes | MiB | Contents/scope |
| --- | ---: | ---: | --- |
| Legacy K-UI 1.0.1 release ZIP | 100,333,734 | 95.69 | Complete DreamShell-based distribution, CD images and core variants |
| NeXT `sd-update`, run 35903321199 | 4,864,696 | 4.64 | Runtime, five music loops, reference catalogues, guides and notices for an existing boot disc |
| NeXT `diagnostic`, same run | 153,472,368 | 146.36 | Bootstrap image, binaries/maps, test fixtures, SD update and source/dependency records |

The first and second rows have different scopes; their approximately 20.6:1 ratio
does **not** measure code or RAM efficiency. The third row is larger than the old
distribution because it includes source/dependency archives. An apples-to-apples
installed-file or executable comparison has not been measured here.

The legacy build system explains significant additional content:

- It places the complete SD directory in the release alongside a self-contained
  CDI containing that directory again, plus a separate bootstrap CDI.
- It ships normal, debug and emulator core variants, executable modules,
  commands, application assets and several loader/firmware targets.
- Its core links SDL and its GUI/image/font/RTF/graphics support, FreeType, Lua,
  XML, Tsunami/Parallax, JPEG, PNG, zlib and C++ support. The independent shell
  uses a smaller fixed renderer and only the components it currently needs.
- NeXT does not yet replace the full ISO-loader compatibility layer or every
  feature of the older applications. Similar visible menu choices do not
  establish feature parity.

The five NeXT music WAVs alone total **4,674,286 uncompressed bytes**. Assets and
package scope can dominate a download without being resident together in RAM;
the menu player caches only the selected track.

Package evidence:
[legacy release metadata](https://api.github.com/repos/TPMJB/K-UI_DS/releases/tags/k-ui-1.0.1),
[NeXT run artifact metadata](https://api.github.com/repos/TPMJB/K-UI-NeXT/actions/runs/35903321199/artifacts),
[NeXT build run](https://github.com/TPMJB/K-UI-NeXT/actions/runs/35903321199).
Composition:
[legacy Makefile](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/Makefile),
[legacy package checks](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/utils/package_release.py),
[NeXT package code](../tools/package.py),
[music inventory](../resources/music/manifest.json).
