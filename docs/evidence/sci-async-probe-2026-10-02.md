# Isolated SCI autonomous-read diagnostic

Implemented 2026-10-02 UTC. Console validation is pending. This is an original
runtime diagnostic, not an asynchronous game reader or Windows CE unlock.

The latest [CRC result](sci-inline-crc-result-2026-10-01.md) is effectively
unchanged: run 18 write/read 1,201.16/1,068.10 KiB/s, zero errors, and the owner
reports largely unchanged gameplay. The next question is whether the CPU can
work while SCI receives a complete, verified sector.

## Experiment and controls

Select the SCI card at boot, then **Diagnostics → Storage tests → SCI async
probe → A**. The runtime pauses music and uses the sole filesystem worker.
It unmounts the volume and makes two ordinary CRC-checked reads of LBA 0,
requiring identical data before changing the hardware configuration.

The probe performs 16 CMD17 trials at 390,625 Hz and 64 at 12.5 MHz. After the
response/token, TxD is held high and receive-only SCI clocks a 514-byte channel
1 DMA transfer: 512 payload bytes and the two CRC bytes. A dedicated aligned
544-byte allocation has separate guards. The CPU performs deterministic
checksum work while reception is active; a work batch counts as overlap only
when the remaining byte count decreases and remains nonzero around that batch.
Each completed trial verifies CRC, baseline bytes, padding and guards.

Scoped SCI error/receive and DMTE1 handlers stop the source and record state.
CRC, comparison and logging run outside the ISR. The probe preserves callback
userdata, leaves shared DMAOR and DMAC priority untouched, rejects a busy
channel, and detects loss of ownership. The normal SCI driver and all resident
game-reading code are unchanged. SCSPTR is independently addressed at
`0xffe0001c`; the accepted driver's existing `0xffe00018` zero-write is not
changed by this experiment.

Successful completion requires documented TE plus zero remaining count.
Renesas section 14.3.6 says clearing DE stops accepting requests but permits
accepted transfers to finish; it does not provide a drain acknowledgement for
an incomplete abort. Consequently an incomplete DMA abort quarantines the
persistent receive buffer/channel, prevents further storage access and requires
a restart. It does not inspect the buffer, reuse the channel or save a report
on the assumption that a fixed delay drains outstanding RAM writes.

After a safely completed experiment, the wrapper restores hardware/handlers
and verifies another ordinary checked read before saving a report. Safe
protocol interruptions require reinitialization first. Any unverified cleanup
or failed recovery blocks storage until restart and leaves music paused. The
sector tests are read-only; only the final report writes to the card.

## Results and interpretation

The screen reports slow/fast trials, completion IRQs, CPU overlap, integrity,
restore/recovery and report status. Successful recovery permits a checked
temporary-file write and rename to
`/KUI/tests/sci-async-NNNN/sci-async-probe.json`. This is separate from Storage
soak results/history. If restart is required, photograph the screen instead.

The report separates integrity, completion-IRQ delivery, CPU overlap and
overall pass. A pass requires all 16/64 trials, exactly one DMA completion per
trial, verified data/CRC/guards, measured CPU overlap and successful ordinary
read recovery. It records software timing brackets for masked sections and
the probe's handlers. Receive duration includes worker observation and cleanup;
it is not wire time or a measurement of the game's maximum blocked span.

**Independent timer/scheduler IRQ progress is not instrumented**
(`timer_irq_instrumented: false`). This first implementation deliberately
establishes less than the full future acceptance criteria in the
[asynchronous/CE design](sci-async-and-ce-irq-2026-10-02.md). CPU work and the
probe's own completion IRQ are useful evidence, but do not establish safe game
resumption, general interrupt responsiveness, CMD18 streaming or original
Holly GD-DMA event delivery. Its repeated CMD17 rate is not comparable to the
multi-block Storage soak. No new soak or DOA2 timing is needed for this probe.

## Validation

Focused host tests use progressive DMA, independent bitwise CRC7/CRC16
oracles and fault injection. They cover both clock stages, SDSC addressing,
integrity/guards, missing overlap, timeout, premature/trailing receive errors,
callback restoration, stale ISR lifetime, foreign DMA preservation, failed
rollback and poisoned retry rejection. Runtime tests cover baseline failures,
safe recovery/reinitialization, unsafe cleanup and blocked later I/O. Shell
tests cover entry/action/result behavior; 640×480 menu/idle/result renders fit.
Strict compiler warnings, ASan/UBSan and existing SCI/retail-storage tests pass.
Target compilation and packaged build IDs are required before delivery;
electrical clock behavior and on-console recovery remain unvalidated.

The diagnostic object and wrapper exist only in the SD runtime link. The
bootstrap and resident retain their existing build graph and memory/stack
guards. The existing `6af5e11` boot CD and card format remain compatible.

## Primary hardware basis

See the pinned KOS, SH7091 register-map and SD references in the design record.
The [Renesas SH7750 Hardware Manual Rev. 7.02](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware)
sections 14.3.6 (printed pp. 599–600), 15.2.7–15.2.8, 15.3.4 and 15.4 supply
the transfer-stop, receive-only, GPIO and DMA behavior. No SWAT/DreamShell
driver code is incorporated.
