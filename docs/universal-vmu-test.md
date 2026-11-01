# Shared VMU compatibility test — @BUILD@

This combines the physical-disc baseline `bc3086754616` with Claude's early SDK work and a smaller controller-poll path for the early Maple driver. The same worker source serves image and disc launches.

## Install

Copy the ZIP's **KUI folder** onto the microSD root and merge/overwrite the existing KUI folder. Boot K-UI normally. Its build should show `@BUILD@`.

There is no installer to run. The five replaced files are the GUI, compact Background modules in A2/A1, standalone New reader, and physical-disc VMU module. Card images, saves and settings remain in their existing locations. The separate physical-disc launcher without VMU remains the existing baseline.

## First test: House of the Dead 2 image

Use **Background, New (X), Virtual VMU On, slot A2**, with physical A2 empty. Play through the first boss in Original mode and compare the lag to the previous test. If stable, save, power off, and load once.

The early driver can now use the smaller controller-only path before physical DMA. Its full card/storage path is retained. This build has passed automated tests but has not been run on the console, so it does not establish that the lag or boss freeze is fixed.

If it still freezes, hold **both triggers fully plus A+B+X+Y** for about three seconds. Capture the full `DETAIL 0000004F` page if it appears, and note where the freeze happened. The gesture is preserved across early controller fast completions.

## Physical-disc regression

Launch the **Power Stone 2 physical disc** with **Virtual VMU On over SCI** and physical A2 empty. Check the accepted save/reload behavior survives the shared-core integration.

Power Stone 1's buffer layout is still unproved; its prior unsupported-layout refusal remains an expected result. HOTD2's reviewed image-module arena has not been extended to physical-disc launches.

## Other compatibility boundaries

Silver's old SCI/Standard adapter already worked. This build does not claim new shared Silver support. Sonic Adventure, Time Stalkers and Elemental Gimmick Gear still need older allocator/exit work before shared virtual VMU admission. Berserk retains Claude's A1 selection.

`details/REVIEW.md` records the corrected history and next compatibility work. `details/BUILD.json` identifies the exact source commit and installed bytes. Attached evidence records checks that ran and any limits; it does not certify console behavior.
