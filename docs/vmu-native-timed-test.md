<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native reader service-window candidate

The owner-tested f518592088f9 hybrid retained slightly improved FMVs but the
same movement pauses. Its capture measured a 2.06921216-second completed
67-sector read while controller polling continued (largest gap 23.46624 ms).
There were 56 cooperative visits but zero delivered blocks. That did not
demonstrate useful cooperative production on the console.

This candidate keeps the full 20/25-block foreground catch-up, eight-block
true SCI interrupt cap, existing one-block PIO allowance, and the same VMU
packet/save protocol. After a proved physical controller completion, it can
now repeat the nonblocking reader visit until the first delivery, a validated
read-only clock deadline, or 64 attempts. The first delivery ends the window;
each low visit remains capped at two blocks, so the complete window is too.

The deadline is 586 ticks (0.75008 ms) only with the existing admitted 781250-Hz
TMU0 profile. The code never configures a timer. An invalid clock gets one
ordinary nonblocking visit; a changed clock aborts repetition. An independent
64-visit ceiling also terminates a frozen counter. Deadline checks occur
between visits. Existing card command/token/recovery limits remain, so this
is not a hard elapsed-time guarantee.

## Install

From this folder, using the existing prepared pair:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2
```

Use SCI, Background On, Normal speed, Auto/default encoding, CD music Off,
and RAM patches Off. Safely eject and cold boot. Compare intro pace/audio
and the same movement area first. The ordinary mode has no intentional stop.

For a capture, add `--profile-runtime` to that command. Hold both triggers
fully plus B for about one second during gameplay, then release. Continue
through the problem for roughly 20 seconds until the intentional stop. Record
at least 25 seconds of all cycling pages.

READ MAX remains the whole accepted read through final checked destination
delivery. AGE MAX samples pending age, which can include data waiting for a
game CHECK. PUMPS now counts individual low-reader visits; WINDOWS counts
controller-completion opportunities. BLOCKS shows useful cooperative output.
EXPIRED counts time/attempt exhaustion without delivery. CLOCK BAD records
unsupported/changed clocks; MAX TICKS is the largest admitted observed window.
VMU COMPLETE includes all the added loader service and its observations.

## Startup after resident verification

The full executable CRC and byte-for-byte resident comparison still run.
After those checks, Evolution 2 must validate its appended 32 KiB bootstrap's
reclaimed lifetime before replaying the native cache. That proof includes
whole-program reference scans, allocator/caller checks, and exit discovery;
the cache replay then independently checks the loaded program's SHA256.

The candidate uses aligned little-endian halfword/word decoding in the
stage's extent/allocator/exit scans, with exact unaligned byte fallbacks.
It also rejects nonmatching bootstrap-signature first bytes before calling
memcmp. All ranges, mutations, lifetime proofs and complete hashes remain.
The game/runtime worker's heap decoders and cache mode are unchanged.

The new messages identify the remaining phases:

* VERIFYING EXECUTABLE LIFETIME EXTENT
* VERIFYING NATIVE CACHE AND IMAGE SHA256
* USING VERIFIED NATIVE MAPPING CACHE, or the existing cold-scan messages
* NATIVE DISCOVERY COMPLETE - ARMING HOOK

No boot-time improvement or gameplay fix has yet been measured on console.
The earlier Native-A2 package remains available for broader Standard-reader
compatibility investigation; this candidate changes no SDK-family admission.
