# VC2 staged-helper writer diagnostic

The VMU integration builds reached the original MAINANA handoff, but the
immutable helper no longer matched the bytes staged before native startup.
Build `921cd40e182e` reported a zero first instruction word and a changed code
CRC. The next useful evidence is the native instruction that overwrites this
memory, so this build watches that write before attempting VMU integration.

This is a diagnostic build. It presents no virtual card, performs no VMU or
other SD writes, and stops permanently after a captured watch event. It keeps
the working foreground reader, full track list and 48-slot mapping budget.

## Install and test

Copy the ZIP's `KUI` folder onto the existing SD installation, replacing the
matching files. The package includes the matching UI, experimental
`apps/games/retail-physical.kui`, and `apps/disc/native-disc.kui`. It incorporates
main `a2b23b3e3d8c1769f1a78ae897aa84e8cc9f4cd7`, including the GD-Play,
GD-ripper and recovery-comparison changes.

1. Remove physical VMUs while leaving the controller connected.
2. Use Native/Default boot. Leave virtual VMU, background reading, CD music
   and RAM patches OFF in the launcher.
3. Launch the same Sega Smash Pack SD image, then select Virtua Cop 2 if the
   menu appears. A watch stop before the menu is also a useful result.
4. Photograph both pages headed `VC2 WRITE WATCH 1 OF 2 REGS` and
   `VC2 WRITE WATCH 2 OF 2 CONTEXT`. They alternate every eight seconds.
5. Power off after collecting the photographs; the game does not resume.

`VC2 WATCH NO TRAP - ONE PAGE` is a separate single-page result. Photograph it
once. An earlier launch/GD STOP screen also needs one photograph. If there is
no diagnostic, record the last visible stage and whether the SD LED is active.

## What the watch establishes

The SH-4 user-break controller watches native CPU writes at the helper's first
address: P1 `8cfc4000` and P2 `acfc4000`, while SR.BL is clear. It compares the
exact address, rather than the complete helper interval. A captured event
records all 36 CPU words described below. The low resident's guarded memory
budget permits this complete register snapshot without following PC, PR or
stack memory pointers. The supplied original binaries can be used to resolve
PC and PR and identify the writer and caller afterwards.

For an ordinary UBC data-write break, saved SPC is the next PC after the
watched access. A delayed branch can require decoding its branch and delay
slot rather than simply treating SPC as the writer's address.

## Captured word indices

Each row starts with its hexadecimal index and then four captured words.
Page 1 contains decimal indices 0-15 in four rows. Page 2 contains decimal
indices 16-35 in five rows.

| Index (decimal) | Index (hex) | Captured value |
| --- | --- | --- |
| 0-7 | `00-07` | Exception bank 1 R0-R7 |
| 8-14 | `08-0E` | R8-R14 |
| 15 | `0F` | SGR: interrupted owner's SP |
| 16-23 | `10-17` | Other bank 0 R0-R7 |
| 24 | `18` | SPC: saved next PC |
| 25 | `19` | SSR: interrupted SR |
| 26 | `1A` | PR |
| 27 | `1B` | GBR |
| 28 | `1C` | MACH |
| 29 | `1D` | MACL |
| 30 | `1E` | EXPEVT |
| 31 | `1F` | BRCR |
| 32 | `20` | CCR |
| 33 | `21` | MMUCR |
| 34 | `22` | DBR |
| 35 | `23` | Original DBR |

SSR.RB (`0x20000000`) selects the interrupted R0-R7 bank. If set, use
indices 0-7; if clear, use indices 16-23. This distinction matters because
the exception entry itself runs with bank 1 selected.

## Admission and limits

The diagnostic leaves the owner's SR and VBR unchanged. It installs the
scoped debug entry only after checking its stage context, low symbols and
existing debug-channel state. Reviewed original startup paths clear BL before
their heap work, and the reviewed native interrupt path preserves DBR. Those
source observations support this test; they do not guarantee a console trap.

The watch cannot capture writes to other helper addresses, writes performed
while BL is set, P0-alias writes or hardware DMA. A no-hit result does not prove
the staged helper was untouched.
The no-hit page is retained so this limitation produces explicit evidence.

## Verification and packaging

Build the watch profile with `BUILD=build/vc2-ce-vmu-watch VC2_WATCH=1` using
`Makefile.vc2-ce-vmu`. The linked watch-package audit checks the actual compiled
layout and stack, and the native entry test binds its results to the exact
resident ELF. The packager requires clean, matching source provenance for
all three installed modules and verifies both evidence files against their
artifacts before creating the ZIP.

The ZIP includes its source patch from the stated main commit, checksums,
license and test evidence. It is checked with Python's ZIP integrity test,
full extraction and byte comparison, and `unzip -t`. It contains no retail
game files. Passing these checks establishes the package and authored entry
behavior, not hardware watch acceptance or VMU compatibility.
