# CE blocking-work diagnostics — 2026-10-04

## Purpose and boundaries

Use the restored CRC-table baseline (`e9ff2353c3cd`, restored by `d6a20a2`)
to distinguish incomplete DMA, repeated stream starts and polled work.
The rejected `bd2b7fb7424b` batch receiver stays reverted. No changes to
peripheral writes, transfer order, CRC checks, repair/retry policy, interrupt
priorities, queue capacity or boot/live-overlay paths are intended.

These are counters, not elapsed times. CE owns TMU0; the existing meter's
single-wrap assumption cannot measure arbitrary IRQ-masked spans. CE's tick
also cannot establish elapsed wall time while its interrupt is blocked.
No timer is reconfigured and no in-game display is added.

Local GCC 15 SH proxy instruction, memory-layout and stack audits pass.
CE async payload is 15,844 bytes (+224), with 5,084-byte linked BSS and
804 bytes remaining below the unchanged `0x8c00d800` limit. Worst stack is
416 plus the 64-byte margin within 2,000 bytes; the IRQ path is 216 bytes.
At a fixed build ID, all native residents/stage and the synchronous CE
resident are byte-identical to the restored baseline. The pinned CI build
and console run remain separate gates; the proxy does not establish speed.

Focused ASan/UBSan checks pass: native/CE stream models, 119,928 native
async checks and 97,625 CE async checks. Added cases cover incomplete bins,
late flag interpretation, token terminators and CMD12 attempts, separate
polled maxima and PIO requested-byte accounting (including rejected calls).
The shared model asserts that the new channel-2 read occurs with receive
stopped. Leak detection alone is disabled for the local container runs.

## Reading the photograph

All values are hexadecimal. Existing DMA/POLLED/STARTS/KEPT/OVERRUNS and
CRC/TOKENERR/FOREIGN/REPAIRED/AHEAD retain their definitions. REPAIRED counts
attempts, not CRC-confirmed repair successes. Successful intentional SCI
overrun stops are not the incomplete-reception counters below.

| Label | Meaning |
| --- | --- |
| LEFT0 | Owned incomplete receptions with zero DMA bytes remaining; completion flag or trailing-byte state was incomplete |
| LEFT128 | Incomplete receptions with 1–128 DMA bytes remaining |
| LEFT384 | Incomplete receptions with 129–384 DMA bytes remaining |
| LEFT513 | Incomplete receptions with 385 or more DMA bytes remaining |
| CH2 LATE | Among those incomplete receptions, channel 2 was enabled and not complete when read after the existing stop/handoff |
| DMAORBAD | Among those incomplete receptions, DMAOR's low three bits differed from enabled/no-error state when read after handoff |
| TOKBYTES | Total bytes examined by data-token searches, including the token or error byte |
| TOKMAX | Largest single token search, in bytes |
| STOPS | CMD12 stop attempts |
| IRQ POL | Maximum completed polled blocks in one delivery visit from interrupt context |
| CALL POL | Maximum completed polled blocks in one delivery visit from call context |
| PIOCALLS | Token-matching `read_part` requests |
| PIOBYTES | Bytes requested by those calls, including calls which later fail |

The incomplete bins include repairable and unrecovered receptions, so their
sum need not equal OVERRUNS. Controller snapshots are late correlations,
not the state at the precise failure instant and not proof of contention.
The polled maxima are neither durations nor totals for an entire GD call;
one call can visit delivery more than once. Polled counts exclude failed
partial receptions, CRC work, token waits and command framing, so they do
not provide a complete blocking budget. Existing finite loop limits remain.

## Console procedure

1. Cold boot the diagnostic build and run the same ARMADA intro with the
   same launch settings. Note whether boot and playback match the restored
   baseline. Do not use a storage soak as a substitute for this game test.
2. Return with the existing menu-return combo and photograph the entire
   screen, including the build ID. Record which game and approximately how
   long it ran. Repeated title-screen resets change the measured workload.
3. Cold boot and repeat with Worms Armageddon. Record whether it boots and
   whether the short intro's audio lead stays constant or grows.

Use remaining-count distributions and stop/token counts to choose whether
to investigate DMA interruption or command/framing work first. A large
CALL POL or IRQ POL motivates a bounded-work scheduling experiment, but
does not by itself tell us which work can safely be deferred: synchronous
PIO requests still need valid bytes before returning.

## Menu comparison (read-only, separate follow-up)

Compared the independent implementation with `TPMJB/K-UI_DS` at
`2a5309298dde8fb100da1e2e4e10517695c9780f`:
[music](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/launch_app/modules/music.c),
[input](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/src/main.c),
[video](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/src/video.c).
The older launcher uses preloaded
PCM WAV and a copy-only audio callback; it releases its state mutex before
sound-driver polling. Its input loop sleeps 10 ms and rendering runs on a
separate thread using Tsunami/PVR. Current `src/apps/music.c` decodes Ogg
inside `snd_stream_poll` under `audio_lock`; `publish_music()` in the main
loop acquires that same lock before input is handled. Input, software
drawing, vblank wait and a 33 ms sleep also share the current main loop.

Both already cache assets. The first targeted menu follow-up is a published
status snapshot that does not wait on decoding. An active-track decoded PCM
cache and render/input scheduling are subsequent options, subject to memory
budgets and separate console measurements. These source differences do not
establish how much of the owner's reported half-second lag each contributes.
No code is copied from the old project and no menu changes accompany this
diagnostic build.
