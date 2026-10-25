<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution 2 SDK SCI callback route

This is a new loader architecture following the failed priority-7 experiment
8390fed1414d. That build black-screened after bootstrap 2; the SD adapter LED
remained on. The LED is not a transfer-progress measurement.

The original Evolution 2 SDK saves VBR on interrupt entry and restores it on
return. A GD call can install the old reader's VBR in between, then the SDK
restores its original VBR. The SDK's default SCI callback returns without
acknowledging the SCI source. At priority 7 a pending event can then repeat
through that original route. Actual owner-code models reproduce this path;
the failed console run did not capture the exact stopping instruction.

This candidate leaves VBR under SDK ownership and publishes two SCI callback
words in the SDK's original table. Bootstrap SCI interrupts stay masked while
GD calls supply reads. Only after the first real heap initializer has returned,
the retained region is bound, and the exact SDK source/vector/table/default
witnesses pass does the reader enable SCI at priority 1. This respects the
SDK's mask-6 critical sections. The new RTS callback preserves the SDK's caller
context; its C payload uses the existing private reader stack and read budgets.
Every active hook rechecks VBR and both table slots before enabling SCI.

This first routing test admits the exact normalized Evolution 2 program:
4,024,288 bytes, SHA256
`52e00d2b3e9db00016b1f6d782cf8a2141617f3d00bd628200de757fd2e11605`.
Other games and revisions are not established by this experiment. Broad native
VMU support remains separate compatibility work. CE/CDDA profiles use their
existing build paths.

## Install and capture every run

From this extracted folder, using the existing prepared pair:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2 --profile-runtime
```

Use SCI, Background On, Normal speed, Auto/default encoding, CD music Off and
RAM patches Off. Safely eject and cold boot. The installer should display
**SDK SCI ROUTE READY / LEVEL 1** before returning to the game.

If admission fails, the existing automatic report stops at **PHASE 0A** with
a detail code. Photograph that screen; no gameplay gesture is needed to get
that refusal. The SDK table is not enabled on a partial publication.

If gameplay starts, enter the same movement area, hold both triggers fully
plus B for about one second, release, and keep moving until capture stops
after roughly 20 seconds. Record 90 seconds of the cycling pages. Capture is
included in the install command on every run.

## What to compare

* Whether bootstrap reaches the route-ready line and gameplay starts.
* Whether real SCI IRQ entries and IRQ blocks become nonzero.
* Whether periodic foreground read work, FMV speed, movement and audio improve.

The retained timeline has the same thirteen-page format as build302f73152e4b.
VBR should remain the SDK value at both GD boundaries. The engine's reused
`release.armed` word now records the SCI field this route owns (0 or0x10),
not a pending raw-vector return. No raw VBR release/rehook trampoline is
installed by this profile. Read timings include diagnostic overhead and do not
measure rendering or simulation progress directly.

Phase 0A details:1entry span,2descriptor span,3immutable descriptor,
4retained binding,5quiescence,6other owner,7SDK witnesses/context,
8callback publication,9ready-cookie publication. Any refused setup remains
stopped rather than returning to the owner with a partially admitted route.

Validation covers exact owner fingerprints, source-to-live SDK vector copies,
all callback-table mutations, actual linked SH routing/ABI/ownership paths,
the complete private stack bound, host DMA/card/byte-output integration and
the extracted installer. These checks do not establish console boot or
performance. No extra game allocation or runtime SD logging is introduced.
