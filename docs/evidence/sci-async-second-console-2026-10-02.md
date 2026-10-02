# SCI autonomous probe: first verified transfers

The owner supplied `sci-async-probe.json` for executable **38693a0de68e**.
The original report is preserved as
[`sci-async-console-38693a0de68e.json`](sci-async-console-38693a0de68e.json),
SHA-256 `33dd2417f672361d16b96f3ab22740b6fd4c87a72b8720cce0a892bfe2f11226`.
The test stopped on the second full-speed command; this is partial success,
not an accepted asynchronous reader or a sustained throughput benchmark.

## Observed result

| Measurement | Slow, 390,625 Hz | Fast, 12.5 MHz |
| --- | ---: | ---: |
| Reads passed / attempted | 16 / 16 | 1 / 2 |
| DMA starts / completion interrupts | 16 / 16 | 1 / 1 |
| CPU overlap batches / iterations | 3,273 / 52,368 | 176 / 2,816 |
| Receive time, cumulative microseconds | 170,453 | 338 |
| Trailing overruns | 0 | 1 |
| Premature errors / timeouts | 0 / 0 | 0 / 0 |

Each passed trial verified all 512 data bytes against the ordinary-read
baseline, checked the two received CRC bytes and validated buffer guards.
The aggregate `crc_ok`, `guards_ok` and `baseline_ok` flags were reset at the
start of the failed next trial; their false values do not negate the 17
previous successful reads. Complete-run integrity/completion flags remain
false correctly because all planned trials did not pass.

The failed fast trial reached command framing, with response `0xff`, before
starting another DMA. Its phase snapshot SSR was `0x86`, with no receive error
bits set. The earlier completed transfer ended with SSR `0xe6` (RDRF and ORER),
zero DMA count and CHCR `0x4912` (TE set, DE/IE cleared). This is consistent
with extra receive clocks after the requested payload and CRC completed.
The report does not prove why the next command got no response, nor that the
card explicitly rejected it: `0xff` can also reflect a latched bus failure.

Handlers and registers restored; no DMA quarantine or foreign DMA was
reported. Card reinitialization and a normal CMD17 read succeeded, matched
the baseline and allowed the JSON to be saved. Maximum recorded IRQ-masked
and handler intervals were 27 and 5 microseconds respectively. Independent
timer/scheduler IRQ progress was not instrumented.

## What this establishes

The Dreamcast completed autonomous SCI DMA reads with useful CPU work during
the payload, including one verified full-speed transfer. The first experiment's
GPIO rejection no longer blocks these trials. At 12.5 Mbit/s, 514 wire bytes
take 328.96 microseconds; the reported 338 microseconds covers the receive
interval and completion handling, not all command setup, checking and cleanup.
It must not be reported as filesystem or game throughput.

The next question is reliable transition from autonomous reception back to
ordinary command framing. The accepted synchronous bus deliberately latches
receive errors; the original probe's one-shot status clear did not establish
that the receiver had settled before using that bus again. A bounded handoff
experiment must preserve the completion snapshot, confirm stable receive
cleanup and separate a bus failure from an SD command response. It must not
clear a preexisting bus fault or inspect an incompletely stopped DMA buffer.

The [Renesas SH7750 hardware manual Rev. 7.02](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware),
section 15.3.4, figure 15.21, requires confirming ORER is cleared before
reception resumes. This motivates stronger cleanup checks; it does not prove
that ORER was the sole cause of this console failure.

## Implemented follow-up candidate

The probe now checks the stopped receiver before returning to the normal bus.
It makes at most eight status-cleanup attempts, then refuses an unclean/non-idle
state. After deselecting the card, it restores synchronous full-duplex mode
using the current BRR, preserving the normal driver's cached speed. Register
readbacks are checked before another command. This reinitialization is an
experiment; successful host checks do not establish the exact console cause.
Framing checks now distinguish the normal driver's latched bus failure from
an SD response. No preexisting fault is cleared to make a trial continue.

JSON and failure photos include handoff checks/retries/failures, SSR/SCR/SPTR
and bus-fault count alongside the original phase, command/token, DMA and
recovery evidence. Complete owned DMA remains required before buffer access;
incomplete-DMA quarantine and foreign-channel protection remain unchanged.

Host regressions model the production bus's sticky-fault contract. They cover
a reasserted receive flag, permanently stuck flags, a missing idle indication,
faults at deselection and at the second fast command, plus the existing CRC,
guard, early/late interrupt and quarantine cases. The reasserted flag is a
fault-injected hypothesis, not a reproduction established by this JSON.
ASan/UBSan targeted checks pass locally with LeakSanitizer disabled because the
local runner cannot inspect `/proc`; full CI retains the standard configuration.
The report formatter fits worst-case integer values in its fixed buffers and
the eight-line failure screen has been rendered and visually checked.

Normal game reads and Windows CE eligibility are unchanged. The next console
test remains one run of Diagnostics → Storage tests → SCI async probe, with
the JSON and a full result photograph. No repeat soak is needed for this
isolated experiment.
