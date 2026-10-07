# First isolated CDDA console result

Recorded 2026-10-07 UTC from the owner's photograph of the completed detached
SCI/AICA harness. The screen identifies build `9020d5101c7e`.

## Observed screen

| Field | Photographed value |
| --- | ---: |
| Completed playback stages | 5 |
| Worst half refill | 79,408 us |
| Maximum service gap | 7,445 us |
| Minimum refill margin | 106,145 us |
| Checked card blocks | 20,032 |
| Observed private stack use | 5,184 bytes |
| Failures / stopped audio | 0 |

The photograph shows `CDDA TEST COMPLETE`. These are the exact displayed
values, rather than estimates from a card benchmark or host simulation.

## Meaning and limits

All five automatic stages reached completion: the generated left/right/stereo
fixture, two runs of its non-sector-aligned stereo seek region, the supplied
Toy Commander track 14, and the final-second EOF seek. The nominal half-ring
budget is approximately 185.76 ms. The observed worst refill was 79.408 ms;
the separately reported minimum refill margin was 106.145 ms. No software
failure was reported for this run. The observed stack watermark was within
the harness's guarded 64 KiB private stack.

**The owner has not yet reported subjective audio quality.** A completed
screen does not establish correct left/right output, absence of clicks or
dropouts, correct byte order by listening, or a clean final mute. Those
checks remain open. The watermark is an observed write depth, not a proof
covering every error path or future profile.

This short run did not establish long-duration operation across the 32-bit
12.5 MHz timer's approximately 343.6-second wrap, automatic pause/resume and
deadline recovery, or playback under additional SCI data-read load. It ran a
controlled homebrew program with exclusive main RAM, sound RAM, sound channels
and timer ownership. No retail game was running. It therefore supplies no
evidence of Toy Commander sound-driver coexistence, retail RAM ownership,
CDDA BIOS-command hooks, or audio/game card arbitration.

## Next evidence

Follow [the next console checklist](../cdda-next-test.md): first confirm the
baseline by listening, then run automatic controls/recovery, a seamless
15-minute tone soak, and a separate 15-minute serialized SCI data-read stress
profile. Photograph each final screen and record the audible result as well
as the counters. Preserve the released 1.8.5 runtime and readers throughout.

The original [installation instructions](../cdda-harness-test.md) and
[memory audit](cdda-harness-memory-2026-10-07.md) document the initial harness.
This record updates the hardware evidence only; it does not change those
source/build measurements or claim that CDDA is enabled in Games.
