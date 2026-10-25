<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution 2 bounded read-ahead candidate

This candidate builds on the hardware-tested SDK SCI route in1eba1784a83e.
SCI remains at priority1. A192KiB physical-card cache fills after a demand
read completes, using the time before the game asks for the next chunk.
The cache preserves the existing raw/cooked sector decoder and request
completion rules. Misses and backward seeks use the existing demand reader.

The isolated profile reserves 384 KiB instead of 192 KiB through the same
verified game allocator. The 128 KiB VMU worker, map and reader region keep
their addresses within that allocation. New cache code and its 4 KiB private
stack occupy separately admitted space; cache data lives in the upper 192 KiB.
No additional game timer, runtime SD log or future game buffer is retained.

Speculation stays inside the current validated track extent and never crosses
an unproved physical-card boundary. A block becomes valid only after the
existing SCI CRC check and a complete 512-byte copy. Speculative failures do
not complete or fail an unrelated game request. VMU storage loans quiesce
speculative DMA; checked cached disc bytes survive the loan.

## Install and capture every run

From the extracted folder:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2 --profile-runtime
```

Use SCI, Background On, Normal speed, Auto/default encoding, CD music Off and
RAM patches Off. Safely eject, then cold boot. Expect **SDK SCI ROUTE READY /
LEVEL 1**, followed by **BOUNDED READ AHEAD READY / 192 KIB CACHE**. If launch
stops at **PHASE 0B**, photograph the detail code.

Test the same new-game opening and movement area. Hold both triggers fully
plus B for about one second, release, and keep moving until the intentional
diagnostic stop after roughly 20 seconds. Record 90 seconds of the cycling
pages, including the new **CRC VALIDATED PHYSICAL READ AHEAD** page. Capture
is included in the command on every run; no separate switch is needed.

Compare the remaining in-engine hitches and confirm that FMVs stay fluid.
The first uncached read and an unpredicted seek can still take the normal
reader's time. Subsequent sequential requests are the main target.

## Read-ahead page

All values are hexadecimal. Capacity 384 is physical 512-byte blocks, not
Dreamcast sectors. **HITS** counts cached blocks fed to the original cursor;
**HIT REQUESTS** counts requests receiving at least one cached block, including
partial hits. **PRODUCED** counts CRC-checked speculative blocks. **HEARTBEAT**
and **IRQ** show the service opportunities; **MAX AUX VISIT TICKS** is elapsed
visit time at 781250 ticks/second and includes demand fallback work. Counters
reset at the capture gesture while valid cache data remains available.

The ordinary request pages still report whole-read times. A single cold
read may dominate the maximum even if later requests become faster, so keep
the per-request pages in the recording. These counters do not directly
measure rendered frames or the duration of a visible hitch.

This first cache candidate retains exact Evolution 2 executable admission.
It does not establish broad native-game VMU compatibility. Existing CE/CDDA
profiles keep their independently compiled paths. Console boot and performance
of this new candidate require this hardware test.
