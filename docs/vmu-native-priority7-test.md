<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native SCI priority7 experiment

This new candidate follows timeline build `302f73152e4b`. Its video captured
all thirteen pages. The four longest GD calls entered with `SR=0x60000160`
(`IMASK=6`, BL clear), while the SCI field in IPRB was `0x0010` (priority 1).
All reader IRQ entries and IRQ-delivered blocks remained zero. The reader's
VBR was installed by these calls; the trace does not establish a VBR-hook
failure or the interrupt mask during every moment outside GD calls.

The experiment changes one scheduling parameter: native SCI priority 1 to 7.
The same shared constant controls the C hook/rearm/unhook and the assembly
rehook trampoline. Other IPRB fields, the game's original SCI field on
release, caller SR, CHECK/EXEC 20/25 and IRQ 8 budgets remain preserved.
The change is compiled only for this paired native timeline experiment.

At the sampled IMASK 6 contexts, level 1 cannot be accepted and level 7 can
be accepted when BL is clear. This does not establish that every interval
outside a call has IMASK 6 or that changing priority fixes the movement
pause. IRQ entries/blocks and read-wall durations are the discriminators.
All timings include the unchanged diagnostic instrumentation overhead.

## Install and capture every run

From this extracted folder, using the existing prepared pair:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2 --profile-runtime
```

Use SCI, Background On, Normal speed, Auto/default encoding, CD music Off
and RAM patches Off. Safely eject and cold boot. In the same movement area,
hold both triggers fully plus B for about one second, then release. Keep
moving through the pauses until the intentional stop after roughly 20 seconds.
Record 90 seconds of the cycling pages so all thirteen pages are included.

Capture remains part of every diagnostic install. No counter transcription
is needed. Note whether FMV speed, movement pauses or audio change.

## Reading this experiment

* The reader page's SCI IRQ entries and IRQ blocks should establish whether
  background delivery gets execution opportunities. Level 7 snapshots have
  IPRB's SCI field `0x0070`. Other nibbles may reflect the game's own priorities.
* Compare complete-read wall times and the new timeline's inside/outside-GD
  request split against `302f73152e4b`. More IRQ activity alone is not success;
  gameplay, FMVs and audio still need the console comparison.
* Zero clock breaks, unmatched events, overflow and saturation are required
  before interpreting the timeline's durations.

The unchanged timeline format is described in
docs/vmu-native-timeline-test.md in the exact source checkpoint. It retains
32 time bins, eight longest calls and sixteen recent request summaries,
printing the four longest calls and eight recent requests.

The complete linked hook and assembly rehook must pass their instruction
models and fixed code/literal checks, and the original low-stack admission
must still pass before this package can be produced. No additional game
allocation or runtime storage logging is introduced.

Primary hardware reference: Renesas SH7750 Group User's Manual, Rev. 7.02,
sections 2.2.4, 19.3.1 and 19.4.1:
https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware
