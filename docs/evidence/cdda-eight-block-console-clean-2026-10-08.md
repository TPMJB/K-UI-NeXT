<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Clean CDDA console run, 2026-10-08

All eight uploaded report photos identify `7b55156aafa2`, API 8, 448 bytes.
The user heard uninterrupted CDDA throughout this natural-intro test and
reported persistent visual stalls. This establishes a successful audio result
for this run; it does not establish extended gameplay or other-title support.

| Report observation | Result |
|---|---:|
| Hardware STARTs / loops | 1 / 1 |
| Internal recovery counts, all eight reasons | 0 |
| Handoff gaps / raw errors / queue errors | 0 / 0 / 0 |
| Completed / retired logical blocks | 332 / 324 |
| Raw callbacks / raw bytes | 2,313 / 5,440,176 |
| Raw callback mean / maximum | 4.4420 / 9.8752 ms |
| Worker visit maximum / service gap maximum | 21.6474 / 81.2966 ms |
| GD read steps / logical sectors | 2,670 / 5,261 |
| GD dispatch read-time total | 22.0280 s |
| GD completed step mean / maximum | 8.2502 / 10.4474 ms |
| Maximum sectors per GD step / invalid timing samples | 2 / 0 |
| GD calls / requests / rejected requests / last error | 9,522 / 593 / 18 / 0 |

Times use the admitted nominal 1.28 microseconds per TMU0 tick. The GD timer
measures completed service dispatches with credited sectors, excluding hook
entry/exit and trailing clock-profile checks. Its 22.028 seconds are cumulative
read time, not the clip's length or movie duration. Rejected requests remain
unclassified; they are not automatically card failures or a video-stall count.
The zero legacy active-bank-write field is not an independent hardware
overwrite detector.

The supplied recording is about 38.415 seconds. Early images remain similar
around 5.4–8.9 seconds (mouth close-up) and 8.9–13.2 seconds (boy shot).
Bear/toy action visibly changes afterward. No known-good visual recording was
available to establish intended motion within those early shots. Audio
continuity comes from the user's listening report, not from RMS measurements.

At one completed GD EXEC per 60 Hz frame, a two-sector limit admits at most
240 KiB/s of 2048-byte payload; at 30 Hz it admits 120 KiB/s. The measured
callback-only payload rate is about 477.7 KiB/s. This is evidence of a possible
service-rate ceiling; it does not measure the game's actual EXEC frequency or
attribute every visual hold to that ceiling.

The next isolated candidate raises the per-EXEC limit to three sectors.
It retains synchronous reads and the existing IRQ mask, so a larger callback
can still delay the game. No hard elapsed-time limit follows from a sector cap.
Audio, source mapping, card CRC and callback cleanup are unchanged.

The exact sixteen-word pages, media identities and decoded metrics are in
[cdda-eight-block-console-clean-2026-10-08.json](cdda-eight-block-console-clean-2026-10-08.json).
The successful source is pinned as `cdda-console-clean-7b55156-20261008`.
