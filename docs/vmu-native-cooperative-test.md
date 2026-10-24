<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Cooperative native reader candidate

This experiment targets the native Background reader. It keeps the full
20/25-block foreground catch-up that preserved movie supply, and the
eight-block true SCI interrupt cap. It adds a separate service opportunity
after a successfully validated physical controller completion. Each such
visit delivers at most two SD blocks, with the existing one-block PIO limit.
It never waits for an in-flight DMA to complete. Card command/token/recovery
waits retain their existing finite limits, so this is not a hard millisecond
deadline.

The producer can also restart an idle SD run. The production-reader host test
reproduces an active request with no receiver in flight: further ordinary SCI
interrupt calls make no progress. The cooperative server completes that same
request, with exact output bytes, without another game GD entry. This proves
a software liveness gap and its tested recovery; it does not establish that
this was the cause of the owner's visible console freeze.

VMU packet handling, virtual A2, physical A1 passthrough and the save journal
are unchanged. The existing proved controller hook supplies a recurring
service point; the added work is game-data loading. The original handler's
VBR/SCI context is restored before the SDK continues. No timer is borrowed,
no IRQ priority is raised, and no new game-RAM allocation is made.

## Install

Run from this folder, using the same prepared profile as the previous test:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2 --profile-runtime
```

Use SCI, Background loading On, Normal speed, Auto encoding, CD music Off
and RAM patches Off. Safely eject and cold boot. First check the intro's
pacing/audio, then the same gameplay area as 63304.mp4.

During gameplay, hold both triggers fully and B for about a second, release,
and play through the problematic area for roughly 20 seconds. The program
then intentionally stops and cycles the report. Record at least 25 seconds
starting at `HYBRID READER - COMPLETE REQUEST TIMING`.

## Read the new report

All values are hexadecimal. Valid clock samples use 781250 ticks/second.

* READ MAX times an accepted read's start through the final checked block
  reaching the game destination. It spans all foreground, IRQ and cooperative
  visits. The following row gives that longest completed read's LBA/sectors.
* AGE MAX is the longest sampled age while the GD request was still pending.
  It can include time after data became ready but before the game next polled.
* PUMPS/BLOCKS show whether the additional producer actually did work. BUSY
  and FAULTS show refused visits. IRQ BLOCKS excludes cooperative blocks;
  CALL BLOCKS remains game GD-entry work.
* The older CALL/IRQ per-visit timing fields have been replaced in this
  profile. Compare the new request durations rather than interpreting them
  as the old 8.22-ms reader-visit measurement.
* The VMU COMPLETE scope now includes cooperative reader work, as the screen
  states. It is not a standalone measurement of virtual-card overhead.

The capture includes completion of a request already underway when the
gesture was accepted. It writes no diagnostic log to SD. Clock validity and
sampling overhead retain their previous limits. No console performance or
universal game compatibility is claimed by the host tests.

Without `--profile-runtime`, the same cooperative reader runs normally and
does not intentionally stop for a report.

## Other ordinary native games

For broad virtual-A2 discovery testing, use the separate `Native-A2` folder
with Background Off. Its guide explains cold discovery without `--game`.
The optional catalog selections are startup accelerators, not a title
whitelist. Keep CE and CDDA outside this native test.
