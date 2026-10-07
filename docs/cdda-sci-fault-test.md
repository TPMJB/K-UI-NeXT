# Test 14 — SCI first-failure and paced DMA comparison

Use **K-UI-CDDA-SCI-Fault-Tests.zip**. Run **14-sci-fault first, then
14-sci-paced**, with a separate cold boot for each. Run both regardless of
the first result and send both sets of photographs together. The unchanged
Toy Commander image and card do not need tests 00–13 repeated.

The [previous comparison](evidence/cdda-retail-read-comparison-hardware-2026-10-07.md)
got past the earlier stops with PIO, though the user reported substantial
lag. Its DMA-eligible counterpart stopped with an image-read failure and
storage TIMEOUT before an accepted CDDA PLAY request. Its inspected stack
guards were intact. This implicates a difference between the read paths or
their timing; the previous counters do not identify the first failing
register wait or prove that the failing block used DMA.

| Run | File in this ZIP | Native report title | SCI read feed |
|---|---|---|---|
| 1 | `observation/14-sci-fault.kui` | `PROFILE14 SCI FAULT /` | Existing DMA-eligible feed, with first-failure capture |
| 2 | `observation/14-sci-paced.kui` | `PROFILE14 SCI PACED /` | Wait for receive DMAC progress before clocking the next byte, with the same capture |

## Install and run both

With the console powered off:

1. Keep the working-reader backup at
   `/KUI/apps/games/retail-boot-before-observe.kui`. If the normal reader is
   currently installed and no backup exists, copy it there first. Preserve
   that backup; do not replace it with a diagnostic.
2. Keep the normal working 1.8.5 runtime at `/KUI/runtime.kui`. Use the same
   complete original Toy Commander folder and all 15 tracks that passed
   test 13. Leave the game folder and card-path configuration unchanged.
3. Copy `observation/14-sci-fault.kui` from this ZIP over
   `/KUI/apps/games/retail-boot.kui`. Safely eject, cold boot SCI, and launch
   Toy Commander with the standard SCI reader from the normal Games menu.
4. Let the introductory screens run without skipping. Note whether the
   earlier stop recurs, whether the title/menu or gameplay is reached,
   whether it is laggy and whether the short repeating sound occurs.
5. If it stops, photograph every report page. If gameplay is reached, play
   briefly and hold **A+B+X+Y+Start** together, then photograph every report
   page. Label this set **14-sci-fault**.

Power off. Replace only `/KUI/apps/games/retail-boot.kui` with
`observation/14-sci-paced.kui`, safely eject, and repeat the same steps after
a fresh cold boot. Label the photographs **14-sci-paced**. Send both sets
with the observed outcomes; neither run depends on the other's result.

The ZIP does not install a runtime, game image, descriptor or configuration
automatically. The two builds share a source build identity; their distinct
report titles identify which reader ran. There is no numerical PASS target.

## Report interpretation

Photograph **two pages after a reader stop**: the **14 STOP** request trace,
then the **PROFILE14 SCI FAULT / SCI PACED** register/counter page. A normal
controller return produces **one** register/counter page. Photograph an
earlier staging refusal too.

The request-trace page stays for 1,200 video frames, approximately 20 seconds
at 60 Hz or 24 seconds at 50 Hz, before showing the register page. A stopped
reader holds the final page until power-off. A normal controller return
shows its single page for the same interval, then reboots. Pages do not
repeat.

The compact reports replace CPU/TMU/AICA observation pages so the additional
SCI trace fits the existing low-memory and private-stack reservations. The
baseline keeps the previous DMA feed but removes per-call inventory and adds
capture/counters, which can change timing; it may not reproduce the previous
crash. Neither new reader has a hardware pass result at packaging time.

The terminal read trace retains these legends:

| Legend | Values, in order |
|---|---|
| `FN ARG FLAG G BAD` | BIOS function, argument, latched hook fault, first current guard word, mismatch mask for all four guard words |
| `CMD LBA N BPS` | Active command, request start LBA, request sector count, bytes per sector |
| `IO LBA N DONE STEP` | Most recent read callback's start LBA and sector count, credited bytes, current sector budget |
| `IMG PRE STOP SD` | Image result, storage result before cleanup, cleanup result, final storage result |
| `DST BLK ERR CARD` | Destination address, successful physical-block count, GD error, last attempted physical card LBA |

An intact guard has first word `4B554947` and mismatch mask zero. These
fields describe the failed read for **IMAGE READ FAILED**; for another
terminal reason, they can describe the most recent read. `FFFFFFFF` in an
image or storage result means that operation was not attempted for the
captured read. A failed chunk may partially touch its destination; credited
bytes cover completed chunks only. A repeated outer TIMEOUT can be a latched
SCI health result rather than a second observed SD command timeout.

The second page prints:

| Legend | Values, in order |
|---|---|
| `PHASE REASON EXPECT POLLS` | Failing branch phase, reason, expected flag/count, software loop samples |
| `SSR SCR SMR BRR` | SCI status sample from the failing branch, then SCI control, mode and baud register samples |
| `CHCR1 TCR1 DMAOR CHCR2 TCR2` | Channel 1 control and remaining count, DMA operation, channel 2 control and remaining count |
| `DMA START OK FALLBACK` | Started DMA payload blocks, completed DMA payload blocks, guarded PIO fallback blocks |
| `CALL PLAY20 PLAY21 BAD` | Observed GD calls, accepted PLAY20 requests, accepted PLAY21 requests, latched hook fault |

The phase values are **1**: programmed flag wait, **2**: DMA transmit-clock
feed, **3**: DMA completion wait, **4**: post-completion validation, and
**5**: paced receive-count wait. Reasons are **1**: timeout, **2**: SCI error
bits, **3**: DMA operation state, and **4**: unexpected remaining count.
Expected means an SCI/DMA flag mask for phases 1–3, zero remaining bytes for
phase 4, or the expected receive count for phase 5.

**Phase zero means no SCI failure was captured for the current lease.**
Ignore the phase/reason/expected/poll/register fields then; earlier fields can
remain stale after a successful reacquire clears the phase. Counters are
lifetime totals for the resident's linked bus instance and exclude the
separate high launch stage. A completed DMA block is a bus completion, not a
claim that the later SD CRC comparison or whole image request passed.

The first failure snapshot is captured before the driver's cleanup writes.
Cleanup cannot overwrite it. Register reads are successive rather than
atomic; an active channel can progress between reads. Counters record started
DMA reads and guarded PIO fallbacks in aggregate; a nonzero START alone does
not identify the failing block's transfer method. Poll counts are software
loop counts, not calibrated elapsed time, and none of these samples establish
permanent ownership of a channel.

This batch tests one receive timing hypothesis. Both retain aligned-buffer
and idle-channel borrowing guards, PIO fallback, SD command/token framing,
bounded stream handling and CRC validation. The paced variant waits for the
previous receive DMA byte before it clocks the next byte. It does not turn
an already started DMA failure into a PIO retry.

The readers do not retry silently, relax CRC validation or accept incomplete
audio backing. The complete original 451-byte Toy Commander GDI identity,
15 backed tracks, 64 manifest slots, standard SCI reader and clear Windows CE
flag remain the admission requirements.

Neither reader enables integrated CDDA music or takes ownership of the
game's sound channels or timers. A short sound loop after a reader stop does
not establish a CDDA playback failure.

## Restore

Power off and copy `/KUI/apps/games/retail-boot-before-observe.kui` back over
`/KUI/apps/games/retail-boot.kui`, preserving the backup. Keep the working
runtime installed and safely eject before rebooting.

This ZIP includes the published source snapshot, licenses, checksums, build
configuration and actual linked ELF/map/stack/instruction evidence for both
readers. Each final reader must pass the unchanged low-memory and conservative
stack checks before packaging. Those checks establish build fit, not console
compatibility.
