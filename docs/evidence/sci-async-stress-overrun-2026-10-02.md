# SCI sustained read overrun and redraw-isolation candidate

Recorded 2026-10-02. The owner tested runtime **c0c285482dac**. Quick passed;
the 60-second test failed both after Quick and when run first after reboot.
The photograph establishes a mid-payload overrun after hundreds of successful
reads. It does not establish that the new timer observer caused it.

## Successful Quick report

The exact supplied [schema-2 Quick JSON](sci-async-console-c0c285482dac-quick.json)
is 3,853 bytes, SHA-256
`082ccba812f3f94fdce29a5548ea9713498b91703c7f9adb87bce67a58f5f178`.
It identifies mode `quick`, not the failed sustained run.

- 16/16 slow and 64/64 fast reads, with all 80 completion IRQs.
- CRC, baseline data and guards pass. CPU work overlaps DMA.
- 64 fast trailing overruns, all 64 conditional SCI-only resets successful;
  STBCR `02 -> 03 -> 02`. No premature errors, timeouts or bus faults.
- Normal checked storage recovery succeeds without reinitialization.
- Total Quick duration 234,603 us. Mean fast receive observation 345.4375 us;
  mean full fast trial 797.234375 us. These are diagnostic timing fields,
  not filesystem throughput or exclusive CPU blocking time.

## Failed stress photograph

The owner supplied `image-1790945690566.jpg`. Transcription of the relevant
lines (the build prefix visible is `c0c2854`):

```text
SCI receive error. Storage locked until restart.
Reads559 IRQ559 Reset559/559 F0 S001
Handoff checks 559 retries 0 failures 0 faults 0
Handoff SSR84 SCR30 SPTR05 STB02/03/02
DMA left 363 CHCR00004910 ERI1 RXI0
Normal read recovery: FAILED - restart required
Report not saved; photograph this result.
No saved path
DMA: DMA560 SSRA4 SPTR86 R100 TKFE
```

559 reads passed publication, CRC/baseline/guards and handoff, with matching
completion IRQs and successful resets. On attempt 560, CMD17 response was
`00` and its data token was `fe`. SCI ERI fired with ORER set (`SSR=a4`),
while 363 of the 514 DMA bytes remained: only 151 bytes had moved. The
captured channel control is **after** freeze cleared DE/IE; `4910` does not
preserve the original enabled state and has no TE completion bit.

This is an incomplete receive, not the previously fixed post-read framing
fault. The persistent DMA buffer/channel quarantine is retained; a partial
transfer is not treated as drained merely because software cleared DE.
The [SH7750 hardware manual, revision 7.02](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware),
SCI sections 15.3.4 and 15.4, describes ORER when received data is not serviced
in time. Its bus arbitration rules in 13.3.11 do not make a context switch
alone evidence that DMA stopped; actual competing transactions need isolating.
No recovery read or report write was attempted after unsafe cleanup. The
old screen's word `FAILED` conflates a blocked recovery with an attempted
read failure; the next candidate distinguishes them.

In this 16-sector traversal, attempt 560 is zero-based trial 559: even pass
34, index 15. That sector had already completed 34 async reads, in addition
to baseline checks. This result does not identify an unreadable end sector.
The photograph lacks elapsed time and the interrupted PC; neither should
be invented from the successful-read count.

## Why isolate framebuffer writes next

The runtime redraws a busy screen twice per second. `draw_shell()` calls
`vid_clear()` for a 640x480 RGB565 framebuffer (614,400 bytes), followed by
software VRAM rendering, viewport work and a frame flip. The pinned KOS
clear uses SH-4 store queues. Quick finished in less than the 500-ms redraw
interval, whereas the longer stress run crosses periodic redraws. Applying
Quick's mean trial time to 559 reads suggests a similar timescale, but that
estimate is **not a measured failure timestamp or proof of contention**.

Pinned KOS [sq.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/sq.c)
shows `sq_set32()` submitting store-queue transfers and unlocking without an
explicit drain. The [public sq.h contract](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sq.h)
provides `sq_wait()` to wait for both queues. Calling that API does not copy
third-party driver implementation into K-UI.

The next X test keeps the same fast SCI clock, sector cohort, CRC checks,
timer observer, CPU work and cancellation. It pauses shell framebuffer
writes during the sustained operation. A UI-thread handshake first finishes
the final notice frame and drains store queues; the worker must receive that
acknowledgment before starting async DMA. A finite timeout or cancellation
before acknowledgment prevents the test from starting. Timer interrupts,
scheduling and controller polling continue. All exit paths release the
display pause and request a fresh result frame.

The candidate also preserves a first failed pre-stop DMA register snapshot,
so an error can report the original enabled/control/count state rather than
only the values altered by cleanup. Successful normal completion avoids the
full failure-capture path. Ownership, CRC and incomplete-DMA quarantine
remain required.

If this run passes, it supports a narrower operating condition with shell
redraws paused. It does not establish that game rendering can safely overlap
SCI reception. If it fails, the new fault record will help distinguish global
DMA state, a receive overrun and other interruption paths. Game integration
and CE work still require their separately documented contracts.

Implementation review, full build/package verification and the next console
result will be recorded with delivery. No corrected hardware result is claimed.


## Verified candidate delivery

Source **9e8fd837270588b8559c6d2b66d19ef14b1aed3f** passed independent source
review and both jobs of [Diagnostic build 201](https://github.com/TPMJB/K-UI-NeXT/actions/runs/37012523280).
The [completed-log CI audit](sci-async-quiet-ci-9e8fd8372705.json) records all
12 wrapper cases, lifecycle/heartbeat, quiet-handshake and first-fault checks.
Native instruction counts, resident layouts, stack bounds and stage sizes
remain identical to c0c285482dac; no unresolved symbols or resident FPU use.

SD source artifact **11229130918**, SHA-256
`c39758ac183957e83e32c169ac84233c7aca3760e6813c84f71fda4632a889bc`,
passed ZIP integrity and all 96 manifest hashes. Both delivered packages
identify build `9e8fd8372705` and pass their runtime header/payload CRC checks.
The native game payload is byte-identical to 0e9a2f814231 after normalizing
exactly four build labels.

Minimal delivery: `K-UI-SCI-Async-Quiet-9e8fd8372705.zip`, **789,218 bytes**,
SHA-256 `40c5378d980ea6812f929c804b90a86692f439b3fadf4e8b5a640a9c107bddb2`.
It contains `KUI/runtime.kui`, `KUI/apps/games/retail-boot.kui`, instructions,
checksums and the build record. Keep the existing boot CD and replace both
files. Reboot; run A Quick, then X 60s if Quick passes. The still screen is
intentional; B requests safe cancellation. Return the saved JSON and full
result photograph. If storage locks, photograph the screen and restart.
Console outcome is pending; no sustained-pass or game/CE compatibility claim
is made from the host/CI results.
