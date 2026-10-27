<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution Maple delay control @BUILD@

The owner reports that cec542aefa8a passed the previous black-screen transition
with New demand reads active and both Maple C callbacks bypassed. That result
implicates the removed callback path; it does not yet distinguish execution
time from its other effects.

This control keeps both real Maple C callbacks off and adds only bounded,
read-only delays at their existing callsites: 59 TMU0 ticks before submit and
76 before completion. With the admitted existing game clock these correspond
to about 75.5 and 97.3 microseconds. They are reference budgets from the earlier
PROFILE capture, which included instrumentation, not measured NORMAL timings.
The delays never configure a timer, alter Maple packets, publish VMU replies
or start storage work. Invalid or frozen clocks produce a terminal diagnostic
instead of an unbounded wait.

New demand reads, SCI delivery, the cache arena, the 384 KiB bottom reservation,
assembly saves/masking/stack/restore and heap/reclaim/coherence guards remain.
Virtual VMU and prefetch remain off. This is a causal test, not a release fix.

## Test once

Extract into a fresh folder and run there:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

The installer accepts the passing cec542 isolation session and earlier
995/3f51/5fa97 sessions, keeping their original loader backups. No card
preparation, formatting, save reset or backup deletion is needed.

Safely eject and cold boot the same Evolution image with **SCI / Background
reader New (X) / Auto encoding / CD music Off / RAM patches Off**. Keep the
physical setup unchanged, with physical A2 empty. Check the startup banner
identifies the Maple delay control. A missing-memory-card warning is expected.

Report whether it passes the same black-screen transition and plays the
opening FMV. If an automatic error page appears, photograph that page.
No capture gesture or repeated Standard test is requested.

A failure would support the hypothesis that callback execution time obstructs
the game's scanline-zero window. A pass would keep the removed C side effects
and cache/bus interactions among the open leads. Neither result proves a
particular defective instruction or exact hardware timing.

## Return to the passing build

From this same extracted folder:

```sh
python3 passed-bypass/install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

This returns directly to the exact cec542aefa8a modules that passed both cold
boots. Its included installer accepts this delay test's recovery receipt and
keeps the initial backups. Use the main install command above to return to
the delay test; no manual backup deletion or intermediate restore is needed.

## Restore

```sh
python3 install.py /run/media/apollonius/Dreamcast --variant restore
```

This restores both exact original pre-isolation loaders from the retained
initial backup. The GUI, prepared profile, card, journal, identity, launch
settings and game images stay in place. The full source and build evidence
are included; console acceptance of this delay control remains pending.
