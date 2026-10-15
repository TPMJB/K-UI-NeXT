<!-- SPDX-License-Identifier: GPL-3.0-only -->
# WinCE CD music: cheaper RAM copies

The owner reports that build 3e5fce65537a mostly removed the Worms gameplay
lag without affecting music, with a little perceptible lag remaining.
That is the console baseline for this small follow-up.

The player copied each checked audio byte into staging and then into the
native ring using volatile byte accesses. Aligned RAM copies now use
four-byte integer accesses, with exact byte tails and an unaligned fallback.
Worms uses this helper for its fully-audio 64-byte chunks; the final padded
chunk retains its byte path. The same 64-byte interrupt masks, consumer
frontiers and per-page witnesses still enclose each Worms chunk. No copy
crosses a ticket, page or checked-source boundary.

This reduces the loop iterations and load/store instructions for aligned
audio copies. It does not make the physical SD link faster. The 32-pump
playing budget, 16-sector source requests, priority, sleep intervals,
buffers, CRC, urgent PIO and ordinary CE loader behavior are unchanged.
There are no changes to the shared disc reader or sound driver.

The real-player tests cover source/destination alignment combinations,
zero and odd lengths, unchanged canaries and forward-overlapping staging
carry. Existing PCM integrity, modeled supply, cancellation and native
ownership regressions remain, including Worms moving-frontier, wrap and
padding cases. Console stack, instruction and layout audits must pass for
each packaged profile. These establish correctness and generated integer
word access, not a measured frame-rate improvement on hardware.

Install all five matching KUI files from the ZIP with the console off.
Use the same Worms image and match, Background loading On and CD music On,
and compare responsiveness and music continuity for about two minutes.
There is no need to repeat the already-established music-Off control or
wait on the title screen. Any further pacing gain is expected to be modest.
