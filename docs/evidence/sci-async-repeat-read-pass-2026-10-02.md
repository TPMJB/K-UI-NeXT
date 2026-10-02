# SCI autonomous repeated reads: first full console pass

The owner supplied `sci-async-probe(3).json` for **0e9a2f814231** and reported
"Seems to have worked that time." The exact report is preserved as
[`sci-async-console-0e9a2f814231.json`](sci-async-console-0e9a2f814231.json).
SHA-256: `9877fde090eee097d7b42e714f23d5ee59c7459aafb41ef9e96f91f2b341d91c`.

This meets every acceptance condition of this isolated diagnostic. It is the
first full console pass of repeated autonomous payload reception, following
the [failed receiver handoff and module-reset experiment](sci-async-module-reset-2026-10-02.md).

## Observed result

| Measure | Slow, 390,625 Hz | Fast, 12,500,000 Hz |
| --- | ---: | ---: |
| Attempts / verified reads | 16 / 16 | 64 / 64 |
| DMA starts / completion interrupts | 16 / 16 | 64 / 64 |
| CPU work batches / iterations during reception | 3,276 / 52,416 | 11,813 / 189,008 |
| Trailing overruns | 0 | 64 |
| SCI module-reset attempts / successes | 0 / 0 | 64 / 64 |
| Handoff checks / retries / failures | 16 / 0 / 0 | 64 / 0 / 0 |
| Bus faults / timeouts / premature errors | 0 / 0 / 0 | 0 / 0 / 0 |
| Total receive time | 172,106 us | 21,732 us |
| Maximum single receive time | 13,039 us | 341 us |

All 80 passed reads include CRC, baseline data and buffer-guard checks. All
aggregate integrity/completion/overlap flags are true. Both handlers and
registers restored; no foreign DMA, DMA quarantine or unsafe restoration.

All fast overruns were classified after confirmed completion of the 514-byte
payload-plus-CRC receive. Each took the new bounded reset path successfully.
The final reset state is OK with STBCR `02 -> 03 -> 02`: the SCI bit was
asserted and cleared while the other bits were preserved. Slow controls used
the existing handoff and required no reset. There were no SCI error IRQs or
unexpected RX IRQs.

**Normal read recovery succeeded without card reinitialization.** The recovery
CMD17 returned R1=0, retained healthy bus status and matched the baseline with
CRC checking. The original failure required reinitialization before ordinary
reads recovered. This distinction establishes usable handoff back to the
normal driver within this run.

The recorded maximum IRQ-masked interval is 29 us and maximum handler time is
5 us. Mean fast receive time is 339.5625 us for 514 bytes, compared with 340 us
for the single successful fast read in c25c. The improvement is repeatability
and handoff, not a demonstrated increase in serial transfer speed.

## What this establishes, and its limits

The SCI reset experiment resolves the previously repeatable framing failure
in this console run. It supports the receiver-state explanation without
identifying the exact internal silicon state or proving a hardware erratum.
The CPU executes independent work during autonomous DMA reception; completion
interrupts, checked data and return to ordinary storage all work together.

This is one short run against LBA 0: 80 single-block CMD17 transfers, not a
filesystem workload, game read stream, long soak or multi-card compatibility
test. The fast stage's 46,143 us includes command preparation, checks and
handoff; its receive-only time must not be presented as filesystem throughput.
`timer_irq_instrumented=false`: the work counters and DMTE interrupts do not
prove independent scheduler or timer interrupt responsiveness.

No new DOA2 or Windows CE result was supplied. The production game reader
still has its prior transfer implementation, as independently verified by the
delivered retail payload comparison. Windows CE's placement, MMU, BIOS service
and interrupt contracts remain separate work described in the existing CE
review. A successful physical SCI transfer alone does not satisfy them.

## Next engineering gate

Use this build and report as the known successful autonomous-transfer baseline.
The next implementation gate is an isolated read-only runtime stress client
using a bounded start/poll/finish operation, multiple prevalidated existing
sectors and an independent timer/scheduler heartbeat during reception. Measure
end-to-end latency separately from payload time; verify every block and normal
recovery. Preserve CRC, ownership, guard checks and quarantine semantics.

After that, adapt the game reader's request/completion path so it can return
control to the game while DMA is in flight. The current SCI native resident
has only 20 bytes below its code/data limit and 60 bytes of conservative stack
margin, so integration requires an explicit layout plan and linked audits.
Do not transplant the runtime probe wholesale or remove those limits.

The uploaded report and numerical consistency checks were independently
reviewed. This evidence update changes documentation only; it requires no
replacement binaries or repeat full CI.
