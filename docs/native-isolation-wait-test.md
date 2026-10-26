<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution focused wait snapshot @BUILD@

Evolution still fails at the black-screen transition with New's optional
prefetch and virtual VMU disabled. The Standard/VMU control passes with the
same 384 KiB reservation. This build keeps that failing New demand-reader
configuration and adds a short, manually triggered observation of the
original game's saved registers and the reader state. It does not patch the
video wait or change the reader, SCI priority, cache or storage policy.

## Install

From this extracted folder:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

The installer accepts your verified earlier 995 or 3f51 isolation session and
keeps its original loader backups. The GUI, prepared profile, card, journal,
identity and launch settings remain unchanged. No reset or formatting is
needed. Normal on-console card ownership validation/recovery still runs.

Safely eject and cold boot. Select the same Evolution image with **SCI /
Background reader New (X) / Auto encoding / CD music Off / RAM patches Off**.
Keep the physical controller/VMU setup unchanged and physical A2 empty.
Confirm the startup label identifies the focused Evolution wait capture.
The missing-VMU warning remains expected in this reader-only build.

## Capture two pages

1. Let the game reach the black screen.
2. Hold **both triggers + B** for about one second, then release.
3. Photograph the **EVO1 WAIT CPU 1/2** and **EVO1 READER STATE 2/2** pages. They
   alternate in a short cycle; two clear photos are enough.
4. Power off after capturing the pages.

The gesture arms collection after 30 completed controller polls. Eight
validated original IRQ contexts are then collected; collection has a bounded
fallback if the exact frame cannot be recovered. The report includes validity
flags, so a failed unwind will not be presented as the game's registers.
There is no 20-second timing window or long sequence of report pages.
If this build gets past the transition, report that result instead of trying
to force a stall.

The CPU page shows the saved game PC, return address, status and video-wait
operands. The reader page shows the owned reader's state and read-only
hardware observations at the stop. A later hardware value is not treated as
the value the game read earlier. Banked registers are labelled valid only
for the independently verified original-frame bank configuration.

This observer does not enable the virtual card or in-game VMU storage. It
does not read arbitrary pointers supplied by the saved game registers.
Unsupported game images refuse the exact Evolution-only capture.

## Restore

```sh
python3 install.py /run/media/apollonius/Dreamcast --variant restore
```

This restores both original pre-isolation loader files using the retained
backup. Select **New (X)** again for ordinary combined-reader use. The same
archive retains the unchanged Standard/A2 control under `--variant vmu-only`
with **Background Off**; its already reported pass does not need repeating.

The snapshot will distinguish incorrect loop operands from a wait that is
receiving nonzero scanline values, and show whether New still has pending
work. Correct operands alone will not prove an IRQ-timing cause. This is a
diagnostic build; a fix has not been established.
