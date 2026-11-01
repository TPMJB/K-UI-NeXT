# Shared VMU compatibility test — @BUILD@

This follows build `26ed262974a8`. HOTD2 still worked but felt laggier, and Power Stone 2 physical-disc save/reload still worked with a long pause under the relocation caption. This test targets those two reports. The same worker source serves image and disc launches.

## Install

Copy the ZIP's **KUI folder** onto the microSD root and merge/overwrite the existing KUI folder. Boot K-UI normally. Its build should show `@BUILD@`.

There is no installer to run. The six replaced files are the GUI, compact Background modules in A2/A1, standalone New reader, and physical-disc VMU modules in A2/A1. Card images, saves and settings remain in their existing locations. The separate physical-disc launcher without VMU remains the existing baseline.

## First test: House of the Dead 2 image

Use **Background, New (X), Virtual VMU On, slot A2**, with physical A2 empty. Play through the first boss in Original mode and compare the lag to the previous test. If stable, save, power off, and load once.

The early driver retains the smaller controller-only path. Accessory-information queries now keep their former scheduling policy instead of starting extra read-ahead work, and completion avoids a duplicate command scan. Its full card/storage path is retained. Automated checks do not establish that the console lag or prior boss freeze is fixed.

If it still freezes, hold **both triggers fully plus A+B+X+Y** for about three seconds. Capture the full `DETAIL 0000004F` page if it appears, and note where the freeze happened. The gesture is preserved across early controller fast completions.

## Physical-disc regression

Launch the **Power Stone 2 physical disc** with **Virtual VMU On over SCI** and physical A2 empty. Check the accepted save/reload behavior survives the shared-core integration.

Note which caption stays visible during the long startup pause: **RELOCATING PASSIVE MAPLE WORKER**, **INITIALIZING PASSIVE MAPLE STATE**, **ARMING PASSIVE MAPLE WORKER**, or **PUBLISHING PASSIVE MAPLE HOOKS**. Previously the first caption covered all four phases. Relocation uses aligned word accesses where possible while retaining byte accesses for unaligned buffers. The full pause is not yet explained by console timing evidence.

## Disc virtual-card slot

GD Play Options now offers **A1/A2**, with **A2** the default for new and existing settings. This disc preference is also available in the main VMU menu and persists between boots. The existing shared Virtual VMU On/Off preference is retained. Choosing a slot retains the same selected virtual card and save files; it changes the Maple endpoint, not the card identity. Leave the selected physical slot empty when using virtual VMU.

Use **A1** for the Sword of the Berserk physical-disc test, then check a save and power-cycle reload. Its physical-disc compatibility and save behavior still require a console test. Power Stone 2 in **A2** remains the known-working regression case. Image launches retain their existing per-game Background slot setting.

Power Stone 1's buffer layout is still unproved; its prior unsupported-layout refusal remains an expected result. HOTD2's reviewed image-module arena has not been extended to physical-disc launches.

## Other compatibility boundaries

Silver's old SCI/Standard adapter already worked. This build does not claim new shared Silver support. Sonic Adventure, Time Stalkers and Elemental Gimmick Gear still need older allocator/exit work before shared virtual VMU admission. Berserk retains Claude's A1 selection.

`details/REVIEW.md` records the corrected history and follow-up work. `details/BUILD.json` identifies the exact source commit and installed bytes. `details/source/source.tar.gz` contains that exact committed source. Attached evidence records checks that ran and any limits; it does not certify console behavior.
