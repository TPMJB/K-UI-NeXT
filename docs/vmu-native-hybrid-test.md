<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native SCI DMA burst ceiling test

Build 1920659698ae was rejected on console: the owner reported an intro video
about a minute slower than usual, with audio glitches roughly every half
second. Gameplay was not reached. Its one-block CHECK/IRQ caps and shared
1,024-poll EXEC slice are removed from this candidate.

This experiment starts from the reader in 74958b387827, whose Background FMV
was smooth but whose gameplay still stalled. It retains that reader's full
DMA waits and 20/25-block catch-up. The focused change puts a hard ceiling on
the formerly unlimited drain of ready DMA blocks: 20 blocks for Normal and
25 for Aggressive. The existing one-block PIO fallback remains.

Console improvement is unverified. This is not a confirmed gameplay fix.

## Install

Extract the ZIP and run from its root:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2
```

Use the same profile and exact game image as the previous comparison. Adjust
the mount/profile only if different. This replaces the launch module; the
installer preserves the prepared card image, profile and journal. Safely eject
and cold boot.

Use SCI, Background loading On, Normal speed first, Auto/default encoding,
CD music Off and RAM patches Off. Physical A1 remains available; keep physical
A2 empty. The virtual save card remains A2. The package contains the native
Background module, not a Windows CE or combined CDDA/VMU implementation.

## Short comparison

First repeat the opening video. If its pacing and audio match the smooth
74958b387827 baseline, continue to the same gameplay area that stalled.
Then check A2 save, full power-off, reboot and reload. Record the displayed
build and each result separately.

## What the software checks establish

The finite-service test compares the actual baseline, rejected and current
reader sources under identical caller/interrupt schedules. It includes
CHECK-only, EXEC+CHECK, one available IRQ per round and repeated CHECK calls,
with cooked/raw data and Normal/Aggressive settings. In all 32 delayed-DMA
schedules, this candidate uses exactly the baseline's service-round count.
The rejected candidate takes 261 rounds versus 13 in one example.

Those rounds are synthetic caller opportunities, not measured console frames
or milliseconds. The tests now check relative service capacity as well as
exact bytes and work bounds; they still do not simulate the game's decoder.

With immediate DMA completion, the former 256-block IRQ drain stops at 20 or
25 blocks. The original CRC, recovery, bus and guest DMA ownership rules remain.
Card command/token/recovery waits keep their original timeouts, so the cap is
not a hard elapsed-time deadline. Memory/stack boundaries are unchanged.

Reproduce the comparison from the recovered source:

```sh
python3 tools/compare_native_reader_cadence.py
make build/test-retail-async-native-hybrid
ASAN_OPTIONS=detect_leaks=0 ./build/test-retail-async-native-hybrid
```

