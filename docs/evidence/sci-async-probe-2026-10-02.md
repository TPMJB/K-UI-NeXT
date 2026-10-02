# Isolated SCI autonomous-read diagnostic

Implemented 2026-10-02 UTC. Console validation is pending. This is an original
runtime diagnostic, not an asynchronous game reader or Windows CE unlock.

Update: the [first console run failed and exposed a pin-check defect](sci-async-first-console-2026-10-02.md).
The corrected candidate **38693a0de68e** passed full CI and is delivered for
retest. The original candidate record below is retained as historical evidence.

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
Target compilation and packaged build IDs now pass as recorded below;
electrical clock behavior and on-console recovery remain unvalidated.

The diagnostic object and wrapper exist only in the SD runtime link. The
bootstrap and resident retain their existing build graph and memory/stack
guards. The existing `6af5e11` boot CD and card format remain compatible.

## Delivered candidate

Source **a3f02d2b0dc566c6b4e50f4ac0598336f122f396**, Diagnostic build
[run 195](https://github.com/TPMJB/K-UI-NeXT/actions/runs/36957378557), passed
both full host/filesystem and Dreamcast jobs. The new probe and all six runtime
wrapper modes ran in hosted CI. Normal/benchmark instruction audits checked
18,999/19,862 instructions; no unresolved symbols. Resident limits are unchanged:

| Backend | Payload | Memory end | Conservative stack / available |
| --- | ---: | --- | ---: |
| SCI | 11,156 B | `0x8c00baec` | 1,172 / 1,232 B |
| SCIF | 10,608 B | `0x8c00b8c8` | 1,080 / 1,232 B |
| IDE | 9,832 B | `0x8c00b5b8` | 1,000 / 1,232 B |

The 4,559,631-byte SD source artifact `11206572864` has SHA-256
`6845efa3c80eeb4a74f0bf05d74bf2152c717292c580620341c83f2190fca015`.
All 96 manifest file hashes, ZIP integrity, both package CRCs and both build IDs
were checked before preparing the two-file update:

| Install file | File size | Package CRC32 | SHA-256 |
| --- | ---: | --- | --- |
| `KUI/runtime.kui` | 1,614,952 B | `580a700c` | `9053a22b7457f5cd8c637603b0b70f0f0bfcb9ad22852bbeb8fb88c6d936a107` |
| `KUI/apps/games/retail-boot.kui` | 56,712 B | `a1640355` | `acae9467a67931bee419075782b5a8772fa1399a7b5300bc48a3569453cabe84` |

Both identify **a3f02d2b0dc5**. Runtime payload/memory are
1,614,888/5,272,720 bytes. The delivered
`K-UI-SCI-Async-Probe-a3f02d2b0dc5.zip` is 776,761 bytes, SHA-256
`584d3912946a420db894e447a20b5de425075c802f37e7e4f8fbba37327fc495`.
It contains both install files, instructions, hashes and the source build record.
The normal game reader remains unchanged. Run the new probe once; return its
JSON and photo, or only a photo if it requires restart. No repeat soak or DOA2
timing is needed for this diagnostic. The owner's later Claude comparison and
[next throughput experiments](sci-throughput-next-2026-10-02.md) are recorded
separately, including the existing Compare preset and 64 KiB physical-stream cap.

## Primary hardware basis

See the pinned KOS, SH7091 register-map and SD references in the design record.
The [Renesas SH7750 Hardware Manual Rev. 7.02](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware)
sections 14.3.6 (printed pp. 599–600), 15.2.7–15.2.8, 15.3.4 and 15.4 supply
the transfer-stop, receive-only, GPIO and DMA behavior. No SWAT/DreamShell
driver code is incorporated.
