<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native SCI interrupt rearm candidate

7121b2983749 is owner-rejected: slower FMVs and longer gameplay stutters.
Its capture supplied zero blocks from SCI interrupts, 2,264 blocks from GD
calls, and a longest completed read of 271.3216 ms. VMU completion bodies
maxed at 108.8 microseconds; storage calls and commits were zero.

This is a new candidate addressing a reproduced reader bug. The native
hook returned early whenever its VBR was still installed. It did not check
whether the SDK had independently changed the SCI priority field in IPRB.
If that field was cleared, later game calls retained our vectors but left
SCI interrupts disabled. CE's reader already checks the field while hooked.

The native hook now checks both independently. If our vectors are installed
and the field has changed, it remembers the SDK's updated owner field,
sets only SCI to the reader's existing lowest level 1, and leaves every
other IPRB field alone. Release restores the remembered field. An unchanged
field causes no register write. No timer is added and the SCI level never
exceeds the existing reader level 1.

The new actual-card test fails against the previous native hook and passes
with this fix. It checks a cleared field and a changed nonzero field, actual
IRQ delivery, exact owner restoration, CRC/fault/contention behavior, and
byte-correct cooked/raw output. A separate model executes the linked
132-byte hook in 4,096 cases across every SCI field and both VBR states;
eight unsafe mutations are rejected. This proves the code path, not that
the owner's SDK has actually cleared IPRB during the reported hitch.

Every CHECK and EXEC again has its full 20/25-block catch-up allowance.
There is no sixteen-poll gate or controller-completion reader pump. The
eight-block true IRQ ceiling and one-block PIO allowance remain. The scoped
pure extent cache pass and its verification checks remain from 7121.

## Install

Use the existing prepared pair from this folder:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2
```

Use SCI, Background On, Normal speed, Auto/default encoding, CD music Off,
and RAM patches Off. Safely eject and cold boot. Compare intro audio/video
pace and the same movement area. Normal mode has no intentional stop.

## Optional capture

Add `--profile-runtime` to the same installation command. Hold both triggers
fully plus B for about one second, release, and continue through the issue
for roughly 20 seconds until the intentional stop. Record 25 seconds of
the cycling pages. The reader page fits all sixteen available body lines.

SCI LEVEL REARMS counts same-VBR priority repairs during the capture.
IRQ ENTRIES counts real SCI handler entries, including entries that produce
no block. REHOOKS and RELEASE100/400/600 show interrupt/exception forwarding
and return activity since the capture began. IRQ BLOCKS remains actual
interrupt-delivered data. These counters use existing reserved words.

Nonzero rearms show the newly fixed path occurred; they do not by themselves
prove every hitch was caused by it. Zero IRQ entries after rearms would call
for checking interrupt masking and routing next. Zero rearms would weaken
this specific explanation. READ MAX remains whole-request elapsed time,
not one GD call or a frame-time measurement. Runs at different LBAs are not
controlled performance comparisons.

Virtual A2, physical A1, save ownership and the existing memory/stack bounds
remain. Console performance and universal non-CE/CDDA compatibility are
unverified. Previous candidates stay separately available.
