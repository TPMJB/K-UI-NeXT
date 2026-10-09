# Test 14 — SCI PIO and DMA read comparison

Use **K-UI-CDDA-Read-Comparison.zip**. It contains two independent tests:
**14-pio first, then 14-dma**. Run both even if the first stops. You can
photograph both runs and send the results together. Do not repeat tests 00–13
for the same unchanged image and card.

The previous diagnostic, build `72802334ebfc`, reported an image-read failure
with storage **TIMEOUT** before the explicit outer stop; the outer stop's
SCI health check also returned TIMEOUT. That is not evidence of a second
CMD12 timeout. All four inspected stack guards were intact. This narrows the
next investigation to the read transport, but does not identify the failing wait or prove a DMA, clock or
card defect. The [hardware record](evidence/cdda-retail-diagnostic-hardware-2026-10-07.md)
preserves the request and error values.

| Run | File in this ZIP | SCI block payload path |
|---|---|---|
| 1: 14-pio | `observation/14-pio.kui` | Existing programmed byte loop; block DMA disabled |
| 2: 14-dma | `observation/14-dma.kui` | Existing DMA-eligible path; normal alignment/channel checks and PIO fallback retained |

The PIO gate applies to SCI block payloads in both the high launch stage and
the low game reader. The DMA-eligible variant retains that path in both.

Both keep the same SD command/token framing, CRC checks, bounded stream
handling, complete-image admission and CPU/TMU/accepted-PLAY observations.
Neither samples live AICA/G2 registers nor enables integrated CDDA music.
PIO may run more slowly; a difference between the two runs helps distinguish
the read paths and their timing, without establishing a root cause by itself.
Their behavior is unverified until these new console runs are supplied.

## Prepare once

With the console powered off:

1. Keep the existing working-reader backup at
   `/KUI/apps/games/retail-boot-before-observe.kui`. If you have restored the
   normal reader and have no backup, copy it there first. Do not overwrite
   that backup with any diagnostic build.
2. Keep your normal working runtime at `/KUI/runtime.kui`. If test 13 is
   still installed there, restore the working
   `/KUI/runtime-before-cdda.kui`, preserving the backup.
3. Use the same complete original Toy Commander image and card that passed
   test 13. Keep the game folder and its tracks where they are.

The ZIP installs no runtime, application, card-path configuration, descriptor
or game tracks automatically. Each test is a separate manual replacement of
the game reader. The earlier diagnostic ZIP and backups remain separate.

## Run both

For **14-pio**:

1. Copy `observation/14-pio.kui` from this ZIP to
   `/KUI/apps/games/retail-boot.kui`.
2. Safely eject, cold boot SCI, and launch Toy Commander from the normal
   Games menu with the standard SCI reader.
3. Let the introductory screens play without skipping them. Note whether
   the game gets past the earlier stop, reaches the title/menu and reaches
   gameplay. Note whether the short repeating sound occurs.
4. If it stops, photograph the failure screen and every following diagnostic
   page. If it reaches gameplay, play briefly, then hold **A+B+X+Y+Start**
   together and photograph the observation pages. Label these photographs
   **14-pio**.

Power off. On your computer, replace only
`/KUI/apps/games/retail-boot.kui` with this ZIP's
`observation/14-dma.kui`. Safely eject and repeat the same steps for
**14-dma**, labeling those photographs **14-dma**. Use a fresh cold boot for
each variant; neither run depends on the other run's result.

Send both sets together with the intro/title/gameplay outcomes and any short
sound loop. The two builds use the same source build identity; the distinct
PIO/DMA label identifies the variant. There is no numerical PASS target.

## Report pages

Both variants preserve the four CPU/TMU/PLAY observation pages, including
the final **AUDIO NOT SAMPLED** page. A read failure adds one **14 STOP**
screen before those pages: **five photographs on failure, four after a
normal controller return**. Photograph every page, even if some counters are
zero. Photograph an earlier staging refusal too.

Each timed page remains for 1,200 video frames, approximately 20 seconds at
60 Hz or 24 seconds at 50 Hz. Pages do not repeat. A stopped reader holds its
final page until power-off; a normal controller return reboots after the
final observation page.

Both variants use the same terminal read trace as the previous diagnostic:

| Legend | Values, in order |
|---|---|
| `FN ARG FLAG G BAD` | BIOS function, argument, latched hook fault, first current guard word, mismatch mask for all four guard words |
| `CMD LBA N BPS` | Active command, request start LBA, request sector count, bytes per sector |
| `IO LBA N DONE STEP` | Most recent read callback's start LBA and sector count, credited bytes, current sector budget |
| `IMG PRE STOP SD` | Image result, storage result before cleanup, cleanup result, final storage result |
| `DST BLK ERR CARD` | Destination address, successful physical-block count, GD error, last attempted physical card LBA |

An intact guard has first word `4B554947` and mismatch mask zero. These
details describe the failed read when the reason is **IMAGE READ FAILED**;
for another reason, read fields may describe the most recent read instead.

Capturing the exact first failed SCI register wait exceeded the unchanged
low-memory or conservative stack limits, so that extra telemetry is omitted
from both builds. This pair compares the payload read methods; it does not
claim to locate the precise failing wait. A PIO success with a DMA-eligible
failure would implicate a difference in those paths or their timing, without
proving that a DMA transfer actually ran or identifying its root cause.

`FFFFFFFF` in an image or storage result field means that operation was not
attempted for the captured read. A failed read may partially touch its
destination; the credited byte count covers only completed chunks. Storage
TIMEOUT is a software result, not a calibrated elapsed-time measurement.
The diagnostic does not retry silently, relax CRC validation or reinterpret
damaged sectors as successful data.

## Restore

Power off and copy `/KUI/apps/games/retail-boot-before-observe.kui` back over
`/KUI/apps/games/retail-boot.kui`, preserving the backup. Leave the normal
working runtime installed. Safely eject before rebooting.

The ZIP includes the exact published source snapshot, licenses, checksums,
build configuration and linked ELF/map/stack/instruction evidence for each
variant. Each native image must pass its own unchanged low-memory and stack
limits before packaging; these build checks do not prove console compatibility.
