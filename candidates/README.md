# Two new native SCI Background candidates

| Candidate | Build | Foreground catch-up | Reader interrupt ceiling |
| --- | --- | --- | --- |
| A | a3e01cd78335 | 20/25 blocks | 20/25 blocks |
| B | 2204b6c31786 | 20/25 blocks | 8 blocks |

Both keep full DMA waits, receive-ahead, CRC/recovery and the one-block PIO
fallback. The one-block CHECK cap and 1,024-poll slice from the rejected build
are removed. A limits whole-request DMA drains. B additionally shortens IRQ
bursts to target gameplay interruptions. Console improvement is unverified.

## Install one candidate

Extract the ZIP. From its root, run the command for A or B:

```sh
python3 A/personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2
python3 B/personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2
```

Adjust the mount/profile only if yours differs. Install one candidate for each
console run. Safely eject and cold boot. Use SCI, Background On, Normal speed,
Auto/default encoding, CD music Off and RAM patches Off. Leave physical A2
empty; physical A1 may remain populated. The virtual save card stays A2.
The installer preserves the prepared card, profile and journal.

Try A, then B with the same image and settings. For each, check the intro video
and audio, the same gameplay section that stalled, then save/power-off/reload.
Report A/B or the displayed build alongside the result. This is a native-game
comparison, not a Windows CE or combined CDDA/VMU build.

## Verification

Both match the earlier reader's service-round counts in all 32 delayed-DMA
schedules, using finite call/IRQ opportunities. In one model case the rejected
build needed 261 rounds versus 13 for the earlier reader and both new candidates.
Rounds are not measured console frames. Each candidate passed its data/ownership
regressions, the linked memory/stack checks and 15 extracted-installer cases.
TEST-EVIDENCE.json and each probe-report.json contain the results.

The embedded profiles are canonical synthetic templates. No owner card,
prepared profile, journal, saves, game executable or track is included.
