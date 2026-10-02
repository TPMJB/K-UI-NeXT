# SCI async probe: repeat-read framing fault

The owner tested build `0d400a471601` and supplied
[`sci-async-probe(1).json`](sci-async-console-0d400a471601.json), reporting
"Framing bus fault" on the screen. The exact report is preserved alongside
this interpretation. SHA-256:
`196aed47fc40fa5505e13e6901903ac80d4e9706b8528589ddc8802f6877f6fc`.
No new throughput or game result was supplied.

## Console result

| Observation | Slow, 390,625 Hz | Fast, 12,500,000 Hz |
| --- | ---: | ---: |
| Attempted / passed | 16 / 16 | 2 / 1 |
| DMA starts / completion IRQs | 16 / 16 | 1 / 1 |
| CPU overlap batches | 3,274 | 175 |
| Handoff checks / retries / failures | 16 / 0 / 0 | 1 / 0 / 0 |
| Handoff SSR / SCR / SPTR | `86 / 30 / 05` | `86 / 30 / 05` |
| Framing bus faults | 0 | 1 |
| Trailing overruns | 0 | 1 |

All 17 passed reads checked CRC, baseline data and guards before being counted.
The final aggregate CRC/data/guard booleans were reset for the second fast
attempt, which failed before DMA; they do not revoke the completed checks.
Fast reception was 341 microseconds for 514 bytes, including completion
handling. This is not a filesystem throughput measurement. Maximum observed
masked and handler intervals were 28 and 6 microseconds; independent timer
IRQ progress remains uninstrumented.

The second fast trial failed in **ready** phase, before its CMD17 bytes or a
second fast DMA. Normal storage reinitialized and a checked CMD17 read matched
the baseline. Handlers/registers restored, no foreign DMA, no quarantine.

## What changed in our understanding

The bounded cleanup and handoff readbacks all passed, but repeated fast reads
still failed. The new bus-health checks establish a latched normal SCI driver
fault; the earlier `0xff` should not be interpreted as the card rejecting a
command. They do not establish why the bus failed.

The `ready` phase includes deselecting, sending one byte with CS high, and
polling ready after selecting the card. The report does not distinguish those
operations. Also, `wait_flag()` stops SCI with SCR=0 before returning failure.
The probe's final SSR=`0x86` therefore samples the stopped peripheral, not
necessarily the status that caused the wait to fail. A clean post-stop SSR
does not rule out a timeout or a preceding receive error.

There is no demonstrated cached-speed mismatch: first fast command preparation
sets the normal bus to fast, and handoff verifies BRR=0. No evidence justifies
clearing a sticky bus fault and continuing or loosening CRC/ownership checks.
The normal handoff already clears TE and RE before re-enabling both, matching
the Renesas synchronous-mode requirement. Trailing overrun remains correlated
with the full-speed transition, not a proven root cause.

## Next diagnostic change

Capture the normal driver's **first failed wait before SCR is cleared**:
the awaited flag, final sampled SSR, SCR, SMR, BRR, SCMR, SPTR, CS register and
number of polls. Preserve that snapshot through release and later failures;
clear it only when a fresh SCI acquisition succeeds. The probe copies it into
its result before card recovery can reset the driver. Capture exact framing
operation and zero-based byte index alongside it.

The snapshot is runtime-only, excluded from the freestanding resident build.
It adds no successful-byte MMIO reads and does not change waits, deadlines,
reset policy or error acceptance. The full JSON retains handoff and DMA
evidence; failure photos prioritize the original failed wait and framing step.
For `bus_wait_flag`, `0x04` means transmit-end, `0x80` transmit-register-empty,
and `0x40` receive-register-full. SSR error bits distinguish an error exit
from exhausting the fixed wait bound. `bus_fault_valid=0` means no captured
normal-driver wait is available and must not be interpreted as SSR=0 evidence.

Host fault injection checks separate failures in initial deselect, CS-high
dummy clocking and selected ready polling, plus command/handoff failures.
Driver tests check pre-stop state, first-fault retention and fresh-acquisition
reset. These validate diagnostic capture; they do not reproduce the actual
console trigger or establish a fix for repeated reads.

Next console action is one SCI async probe run and its saved JSON/photo. No
new soak, gameplay timing or boot CD is needed for this diagnostic. Windows CE
launch and asynchronous game I/O remain separate milestones.

Primary hardware reference: [Renesas SH7750 hardware manual Rev. 7.02](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware),
sections 15.3.4 and 15.5. The unchanged receive/transfer policy is deliberately
retained until first-fault evidence identifies the next experiment.

## Local validation

Strict builds and ASan/UBSan pass for the production SCI bus, async probe and
runtime wrapper. Local LeakSanitizer is disabled because this runner cannot
inspect `/proc`; CI retains its standard configuration. Independent review
passed. Resident preprocessing and a compiled host resident object are byte
identical before and after telemetry changes. The actual JSON formatter stress
check fits 1,290/1,536 bytes per stage and 3,916/4,608 overall with maximum
integer values and 40-character names. The failure screen was rendered and
visually inspected. Console cause and repeat-read recovery remain unproven.
