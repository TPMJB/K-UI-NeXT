<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE virtual A2 automatic checkpoint tests

The owner reported that build b92c5fc49bab froze during the opening FMV and
A+B+X+Y+Start did not enter the diagnostic pages. This does not identify
whether the CPU stopped or the game blocked in a CE VMU operation.

These four isolated modules deliberately end the test at a checkpoint and
show the existing rotating report pages without a game reset request. They
are diagnostic tests, not a completed VMU/save release or a recovery watchdog.
The card is still a generated blank read-only A2, with no SD card image and no
save writes. The physical A1 card remains a physical device.

## Run in order

Back up the CURRENT `KUI/apps/games/ce-probe.kui` from your SD to your PC.
Cold boot each test with the same original Armada raw GDI and settings:
**SCI SD; Background On; CD music Off; virtual VMU Off.** Keep the controller
connected, the physical VMU in A1, and A2 physically empty. The module itself
provides synthetic A2. Do not select save/format operations for this test.

| Folder | Intended stop | AUTO REASON / PHASE |
| --- | --- | --- |
| 01-advert-before | First synthetic A2 advertisement, before CE consumes it | 1 / 1 |
| 02-advert-after | Same advertisement, after original CE handler returns | 1 / 2 |
| 03-info-after | First synthesized A2 DEVINFO, after CE handler returns | 2 / 2 |
| 04-read-after | First synthesized A2 block read, after CE handler returns | 3 / 2 |

1. With power off, copy only the selected folder's `KUI/apps/games/ce-probe.kui`
   over the same SD file. Begin with **01-advert-before**.
2. Launch Armada. A deliberate `AUTO A2 STOP` diagnostic screen is success for
   a checkpoint test; the game is not meant to continue after that screen.
   The first tests may stop before an FMV or before any menu appears.
3. Photograph **TRAFFIC, FENCE, A2, MAPPING, DRIVER**, including build and the
   AUTO REASON/PHASE row. The pages keep rotating until power-off. MAPPING or
   DRIVER can be all-zero in an early checkpoint, before the ordinary PLAY
   observation has run; retain those pages as evidence anyway.
4. If the expected checkpoint was reached, power off and try the next folder.
   If a test freezes instead, stop the sequence and report its folder and
   the last visible screen. ABXY is not needed or expected to recover it.
5. Restore your backed-up module after testing.

Any A2 fault takes priority and stops before another original CE completion:
reason **5**. Reason **6** means 1,200 completion visits occurred without the
selected event; this is a fallback checkpoint, not a timed watchdog. Report
either result before continuing to later tests. A PRE stop deliberately shows
an active DMA fence because CE has not consumed the staged reply; POST stops
should show the corresponding restore. Both are terminal and require power-off.
The archive contains no rollback copy of the file currently on your SD.

## Implementation and validation

The helper's terminal leaf enters the installed resident menu command-1 hook,
which masks interrupts and switches from the helper's stack to the resident's
existing report stack. It validates the live vector against the exact identity
published during resident installation. Preboot relay verification admits only
that one metadata word while retaining zero checks for the other private state.
No exception vector, interrupt handler, timer, game reset poll or SD write is
added. If execution stalls before a selected checkpoint, the corresponding
build can still freeze: this is why the tests bracket the original consumer.

Each metadata-page mapping is now reused only within one uninterrupted query,
then all retained mappings are walked again at the final witness check. The
representative query drops from 660 to 462 read callbacks, while retaining all
360 coherent/cache witness reads. Mapping reuse resets on every public query.
This is a performance improvement; it is not proof that latency caused the
hardware failure.

Host protocol/ownership/publisher/bridge tests and native integer-instruction,
layout, stack and package checks are included in the package evidence. The
normal CE/native/CE audio-observer routes preprocess identically to b92c5fc
when the experimental A2 flags are absent. Hardware results for these four
new modules remain pending.
