<!-- SPDX-License-Identifier: GPL-3.0-only -->
# WinCE CD music: bounded worker turns

Build 8a22ec45229a played Worms music without observed underruns in both
submitted runs, but the owner reported gameplay lag. After replacing the old
Armada observer in the ordinary CE slot, the owner confirmed smooth gameplay
with CD music Off and lag with it On. Earlier DMA-based music builds had rare
audio cuts without this reported lag. This narrows the regression to the
optional music path and makes the newly forced polled-read work the leading
cause; it does not independently measure each component's CPU cost.

## Targeted change

Previously the priority-2 source worker could perform two complete 16-sector
jobs in one turn: about 75 KB before sleeping. At 12.5 Mbps that is roughly
48 ms of wire time alone. Interrupts were restored between 512-byte blocks,
but runnable normal-priority gameplay still had to wait for the worker to
sleep. CRC checks, byte callbacks and copies add processing work.

During active native playback, the worker now shares a 32-pump physical-work
budget across its entire step, including both source visits. This permits
roughly 16 KiB of reception before sleeping; setup, polling and token work
also consume that budget. The worker publishes checked partial-job PCM and
drains it into the native ring before yielding. An exhausted budget cannot
be bypassed by submitting or preparatorily pumping another physical job.

The complete 16-sector source ticket and current transport ownership remain
valid across steps. Eager prefill before native playback starts remains.
Urgent worker PIO, IRQ/DATA DMA, priority 2, existing native sleeps, buffers,
CRC checks and Worms copy exclusion are retained. The shared SCI reader has
no new code. This changes the length of worker turns, not the raw job size.

The budget avoids the clearly insufficient 8 KiB-per-50-ms case: playback
needs 8,820 bytes in 50 ms. Sixteen KiB represents about 92.9 ms of audio.
Actual reception plus all other processing and scheduling still must fit
the resulting allowance; hardware timing is not proven by a host model.
Long source jobs can remain pending during DATA interference.

Production-player host regressions model 50 ms gameplay turns, 4 ms of
native/copy overhead and either 0.5 or 1.0 ms per source pump. They check
continued supply without recovery, the shared turn limit and publication
before sleep. Those per-pump costs are explicit test assumptions, not
measurements of the console's physical read path.

## Install and check

With the console off, merge `KUI` from the ZIP onto the SCI card and cold
boot. It contains a matching runtime, the three separate music modules and
the ordinary `ce-probe.kui` audio-off loader with the existing CE DMA resume
fix. Including the ordinary loader prevents an old Armada-only test file
from blocking the Off comparison again; its behavior is not retuned here.

Use the same Worms image and match with **Background loading On** and
**CD music On**. Check that gameplay is closer to the smooth Off run while
music remains continuous for about two minutes. No menu wait is required.
If a cut occurs, A+B+X+Y+Start and a 20-second recording retain all four
diagnostic pages. Existing Off-versus-On evidence need not be repeated.

This is a console candidate. Host regressions can establish the work bound,
partial publication and modeled supply, but cannot certify console pacing
or the complete absence of audio cuts.
