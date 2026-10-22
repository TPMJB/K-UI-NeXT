<!-- SPDX-License-Identifier: GPL-3.0-only -->
# WinCE CD music: underrun diagnostics and 2048 DATA candidate

Follow-up: the [urgent-refill candidate](wince-cdda-refill-fix.md) fixes a
cooked-track control error after admission and changes urgent worker reception.
The description below records the preceding diagnostic-only build.

This candidate adds a frozen report around the last real buffer underrun and
admits cooked 2048-byte DATA alongside original raw 2352-byte AUDIO in the
three exact WinCE CD-music profiles. It does not retune playback, thread
priority, buffer sizes, source batches, recovery or SCI scheduling quotas.
The ordinary CE loader and native/Toy/MDK2 loaders are not replaced.

## Install and test

With the console off, merge `KUI` onto the SCI microSD card, then cold boot.
It contains a matching runtime and updated Armada, BAM4 and Worms CD-music
modules. The prior working binaries remain in the earlier delivery ZIPs.

Use Worms first. Select the original GDI, enable **Background loading** and
**CD music**, and play normally until a cut or about three minutes. Press
**A+B+X+Y+Start** and record **20 seconds**, covering all four pages, then
power-cycle. The new fourth page retains the last underrun even after music
recovers or the game stops it. All zeroes means no underrun context was
captured; the event and fault pages remain available too.
No extra gameplay video or menu wait is needed.

For the 2048 test, select the converted GDI. Its DATA tracks may be 2048 bytes;
its AUDIO tracks must retain the original raw audio, LBAs and sector counts.
Enable Background loading and CD music for this copy too: settings are saved
per selected image. Enter a match and check that music plays. If the launcher
still refuses it, photograph the complete error message and note whether CD
music was On or Off.

## What the frozen page measures

All numbers are hexadecimal; times are milliseconds on CE's existing clock.
The first four rows describe the last UND event, followed by three preceding
history samples retained at intervals of at least 100 ms. The history cadence
can be longer if the worker is delayed. It does not interpolate missing time.

| Row | Fields in order |
| --- | --- |
| 1 | Event time; fresh ring bytes before observation; native cursor advance; staged bytes; active source-read age |
| 2 | Worker entry-to-entry gap; requested sleep; current entry-to-position-observation time; age since checked source publication; age since ring publication |
| 3 | Source status; instantaneous SCI owner; checked source-job bytes; source-job total bytes; retry count |
| 4 | DATA blocks, AUDIO blocks, preemptions, worker budget yields and copy retries since the preceding history sample |
| 5–7 | Previous sample time; cumulative source bytes; fresh ring bytes; staged bytes; packed state flags |

`FFFFFFFF` means unavailable/not observed in the current playback. With no
matching active source job, status and read age use this sentinel. Owner 0
means none, 1 DATA and 2 AUDIO. Packed history flags use bit 0 for an active
read, bit 1 native playback, bit 2 urgent audio, bits 8–15 model state,
bits 16–17 owner and bits 20–23 source status.

These values narrow the boundary: a long entry gap identifies a long worker
interval; entry-to-observation time includes guards/native position-call
cost; source-job counters expose progress and arbitration; staged data with
no ring publication points toward refill exclusion. An instantaneous owner
does not prove who held SCI for the whole interval. Existing clock resolution
limits precision. Missing fresh bytes do not measure the audible gap.
The transport snapshot is taken after releasing the native consumer lease;
its counters may advance after the timestamped cursor observation. No new
clock call is added to measure that interval.

The report uses existing clock observations and authored transport counters.
It adds no native clock calls, hardware polls or disk logging. It freezes only
on a real underrun; later healthy samples do not replace the evidence.

## 2048-byte scope

The general CE loader already reads cooked DATA. The optional CD-music adapter
previously rejected it with an all-raw-track admission rule. This candidate
allows exact DATA control/stride combinations of raw 2352 or cooked 2048,
requiring the executable CRC for cooked DATA. AUDIO remains raw 2352 only.
Exact product, kernel, track starts, sector counts and other runtime guards
remain in force.

The converter retains raw audio and disc addressing. It discards raw DATA
headers/parity, so this change does not reconstruct full raw DATA sectors.
A game requesting those sectors after boot may still require the original
image. Successful cooked-image admission alone is not a console playback
result or universal WinCE compatibility.
