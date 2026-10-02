# SCI async reader: overrun retry, screen-on stress and speed comparison

Recorded 2026-10-02. Continues Astra's runtime reader on `codex/storage-transports`
after the owner's passing 60-second stress (`9e8fd8372705`, 71,641 reads, no
errors, shell redraws paused). Console results for this build are pending.
Native game reads and the CE gate are unchanged.

## What the passing run did and did not show

The quiet stress verified 71,641 single-block reads in 60 s with the timer
interrupt firing 2,278 times during DMA. The earlier failure (`c0c2854`, read
560) was a receive overrun after 151 of 514 bytes while the shell was
redrawing. SCI holds one received byte, so at 12.5 MHz the DMA has about
0.64 us to store each byte; any longer bus stall breaks the block. Games keep
the bus busy all the time, so a game-facing reader must survive overruns.

The run's 597 KiB/s is not a reader speed: each read made about 32 timed polls
with CPU work between them. Only 4 of the 16 sampled sectors held distinct data.

## Changes

**Overrun retry (`src/dreamcast/sci_async_probe.c`).** An ERI with only ORER set
on an incomplete payload is retried when the stopped channel is provably idle:
after SCR=0 and DE=0, the count and address must agree and stay unchanged over
four consecutive checks, each after an uncached read of the receive area, with
at most two late transfers tolerated. The request then reuses the existing
handoff (deselect, MSTP0 reset, synchronous re-init), clocks 1,024 bytes with
CS low to finish the interrupted block, and re-issues the same CMD17, at most
three times per request. A late byte can only land in the static receive area,
which the retry rewrites; CRC and guards still gate publication. Any other
incomplete stop, or an overrun that cannot be proven idle, quarantines as
before. The first overrun's pre-stop record is kept in `first_overrun` even
when its retry succeeds.

**Counters.** Per stage: `payload_overruns`, `overrun_retries`,
`undrained_overruns`, `token_bytes`/`max_token_bytes` (0xff bytes between R1
and the data token, i.e. the card's access time) and `framing_us`/`finish_us`.
`kui_sci_async_set_framing_quantum()` lets a client send the command and wait
for the token in one poll.

**Diagnostics → Storage tests → SCI async probe.**

| Button | Test |
| --- | --- |
| A | Quick, unchanged. |
| X | 60-second stress with the screen paused, unchanged. |
| Y | The same 60-second stress with the screen updating; overruns are counted and retried. Every read must still verify. |
| R | Speed: 1 MiB starting at `/KUI/runtime.kui`'s first cluster (or the data area), read by the ordinary reader in CMD18 runs and then by the async reader one CMD17 per block with no other work. Both passes' CRC32 must match. Reports KiB/s for each, per-block setup/receive/finish time and the card's average and worst token wait. |

The JSON report gains `first_overrun`, `screen_redraws`, a `speed` object and
the new per-stage counters. Mode names: `quick`, `sustained`,
`sustained-screen`, `speed`.

## Validation

Host tests (ASan/UBSan) cover one overrun retried to success, a late byte after
the stop, cancellation during an overrun, the retry limit (failure without
quarantine), a channel still moving after the stop (quarantine), token-wait
counting and the framing quantum. The runtime wrapper test covers the speed
pass, its fallback range and a card smaller than the range. Shell tests cover
the Y and R buttons.

## Delivery

Source `8daab44490f5ec196b816288d7ad9fc2b0013190` passed
[Diagnostic run 202](https://github.com/TPMJB/K-UI-NeXT/actions/runs/37017797243):
host tests (including the new overrun and speed cases) and the Dreamcast build.
The update is the run's `kui-1.5.1-dainsleif-sd-update` artifact; only
`KUI/runtime.kui` and `KUI/apps/games/retail-boot.kui` need replacing.

## What to look for on the console

1. **Y**: how many overruns occur in 60 s with the screen updating, and whether
   every one retried successfully. Zero restarts is the key result.
2. **R**: async KiB/s versus ordinary KiB/s, and the setup time per block. If
   setup (command plus card wait) is small, single-block reads can approach the
   CMD18 reader; if it is large, the async path needs streaming reads first.
