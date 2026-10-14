<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada — CE SCI reader test

This is the first Armada step: adapt the native SCI DMA1 ownership correction
to the Windows CE background reader. It does not install a CE CDDA adapter.
The owner-confirmed MDK2 music/effects loader, native general loader and current
runtime remain installed without replacement.

## Change

The SCI receiver can finish an incomplete block in its interrupt, restore DMA1
and publish a deferred RESUME. CE then returns to the game. Before the next GD
service visit, the game can change that channel's idle registers or start its
own transfer. The old resume path did not acquire the channel again: it could
overwrite a guest transfer or later restore obsolete registers.

The opt-in `KUI_CE_SCI_RESUME_FIX=1` candidate:

1. Checks the physical channel again before deferred resume. An active,
   completion-pending or otherwise unsafe channel uses the existing card
   stop/restart and PIO receive path instead of overwriting DMA1.
2. Captures the current idle DMA registers at every actual acquisition,
   including resumed and repaired receives, and restores that captured tuple.
3. Enables the established deferred cancellation branch in the shared receiver
   source. It fences outstanding writes before restoring the channel and
   sending CMD12, without starting another repair. Host tests exercise that
   routine; the production CE reader does not call it, so linker removal keeps
   it out of this payload. The actual compiled changes are acquisition and
   deferred resume, with CE's existing cancellation behavior retained.

The option defaults to zero and is admitted only for CE background SCI. Its
native counterpart still rejects CE. CE's interrupt handler table, MMU/TLB
handling, SYSINTR delivery, timer, SCI priority, CRC, token slicing, spill queue,
receive-ahead target and game-read completion rules retain their prior code.
No game files are patched and no third-party loader implementation is imported.

The two CPU contexts serialize the check-to-acquisition section: GD calls use
IMASK15, while the physical SCI ISR uses BL and physical resident buffers.
The new receiver code does not dereference CE virtual destinations in that
section. This source/host proof does not establish a console speed improvement.

## Install and test

1. With the console off, back up the card's existing
   `KUI/apps/games/ce-probe.kui`. Merge this package's `KUI` folder onto the card.
   It supplies only that CE payload. No new runtime or CD is needed.
2. Cold boot on SCI microSD using the existing exFAT card. Select **Original
   Armada** and use **A** on its Windows CE launch confirmation. The current
   launcher selects the background SCI reader for CE; the older A-standard,
   X-background instructions do not apply.
3. Check the CE launch/return-screen build against the package README or
   `build.json`. The home-menu runtime stamp stays the same.
4. Let the intro play without skipping, then start a game and play about ten
   minutes. Compare movie motion, speech/effects, loading pauses and gameplay
   with the previous reader. The CE CDDA music path is not implemented yet;
   this first comparison tests the reader without adding that workload.
5. Use the existing **A+B+X+Y+Start** return combo and photograph the full CE
   diagnostic screen, including its build. Note any freeze or audio/video
   interruption and whether it occurred during the movie, loading or gameplay.

The background track/extent map retains its 64-slot limit. Start with the
original raw image for a single controlled comparison; no conversion, format
or rerip is needed for this test.

## Restore

Restore the CE file you backed up and cold boot to recover your exact previous
installation. The package also supplies a rebuilt control under
`restore-ce/KUI/apps/games/ce-probe.kui`: source/build `7899c7d7ad44`, with the
new CE switch omitted and the previous receiver behavior. That control is
reproducible source-based restoration, not a claim that it is byte-identical
to the older CE payload already on your card. It changes no native files.

## Verification

The independent receiver/card fixture reproduces the old defect in both default
controls. The candidate passes 53 ownership cases and 25,866 checks, including
foreign active DMA, updated idle tuples, deferred receives, in-flight writes,
repair and cancellation. The actual CE GD/IRQ driver passes 163,486 checks in
each selector configuration, including CRC, raw/cooked reads, virtual buffers
and its bounded physical spill. Five behavioral mutations and 24 unsafe macro
configurations are rejected. Seven retained native/CE/Toy preprocessors are
exact against the pristine source; retained native ownership tests also pass.

At an equal build stamp, compiled SCIF, IDE and ordinary CE SCI residents are
byte-identical to the pristine control. Only the CE background resident grows
32 bytes. Its state ends at `8c00d6c0`, below the unchanged `8c00d800` stack
boundary. The compiler call-graph bound remains 436 bytes within the existing
2,000 usable stack bytes. Linked instructions, embedded images, relocation
headers and the production launcher's CE admission/rejection cases are checked.
The final package includes the exact identities and validation evidence.

## Next CDDA step

The owner reports fluid Armada gameplay with the DMA ownership candidate, but
no noticeable FMV improvement: movie playback remains similarly choppy. No
new diagnostic capture accompanied that result. Retain the correctness repair;
this observation does not establish a speed improvement or the cause of the
movie slowdown.

The playback core already works with MDK2, but CE owns different memory, sound
interfaces and timing. Its timer cannot be replaced by the MDK2 timing backend.
The historical Armada kernel metadata survives, but the owner's kernel/game
and external sound-driver bytes are not present in the current workspace.

Use the included read-only `tools/kui-armada-startup-bundle.py` on the original
Armada GDI to prepare the small startup/sound-analysis ZIP. It locates the
IP-selected kernel and EXEs throughout the ISO directories, preserving their
paths. DLL/DRV/LIB extension candidates are optional and included within the
32 MiB byte budget, with DLL/DRV candidates before LIB candidates. Any omitted
candidates remain listed with their paths, sizes and reasons in the report.
An extension alone does not prove that a file is runtime code. Up to 2,048
selected files are permitted within the existing scan limit; generated metadata
does not consume that count. A missing external EXE produces a kernel/runtime bundle
with an inventory and a warning, rather than requiring a root-level
`ARMADA.EXE`. Multiple EXEs are all preserved without guessing the game entry.
It does not change the game or card. Upload that ZIP with the reader test result so the next step
can establish the actual CE sound/memory/service contracts rather than reuse
MDK2 addresses. No game executable, kernel, driver or music bytes are included
in this distributed test package.
