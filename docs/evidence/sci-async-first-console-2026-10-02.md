# First SCI autonomous probe console result

Recorded 2026-10-02 UTC (2026-10-01 local). The owner returned a photograph
after installing candidate `a3f02d2b0dc5`. Its visible build prefix matches;
the right edge of the build label is cropped. No JSON was saved.

## Photograph

Source `image-1790910698307.jpg`, 283,029 bytes, SHA-256
`bf0025a48b610433fcc315994f67d11ef7f0819c163d145ec0a4135c048accd6`.
The client reported a missing local image, but its attached copy is readable.

```text
SCI async probe
Card reinitialization failed. Restart before using storage.
Slow: 0/1 reads verified; 0 DMA interrupts
Fast: 0/0 reads verified; 0 DMA interrupts
CPU overlap batches: slow 0 / fast 0
CRC unconfirmed  Data unconfirmed  Buffer guards unconfirmed
Normal read recovery: FAILED - restart required
Report not saved; photograph this result.
No saved path
```

The baseline ordinary reads precede the first trial and must have passed for
this path to be reached. The exact reinitialization message means the low-level
probe reported safe local restoration, followed by failed reacquisition/card
initialization. An incomplete DMA quarantine would take a different branch
and would not attempt reinitialization. These results are consistent with an
early framing, token-completion or GPIO check failure. They do not establish
which check failed, whether autonomous clocking works, or any throughput/CPU
overlap result. Fast trials were never attempted. The storage lock and refusal
to write a report operated as intended; restart before ordinary storage use.

## Confirmed source defect

The probe wrote SCSPTR `0x83` to enable error-only receive interrupts and hold
TxD high, then required `(readback & 0x8b) == 0x83`. This incorrectly treated
bit 0 as readable confirmation of the TxD output latch. Renesas section 15.2.8
(printed p. 672) states that bit 0 reads **RxD regardless of SPB0IO**. A low
card MISO signal may therefore reject a correct GPIO configuration before
DMA starts. Clearing TE before reading would not change that read meaning.
The old host model returned the written output latch and missed this behavior.

The correction checks only control bits `0x8a` against `0x82`, ignores both
sampled pin bits during restoration comparison, and rejects a preexisting GPIO
output owner before taking the lease: reading a pin cannot recover an unknown
output latch. Regression tests model RxD and SCK independently of written
latches. This is a real defect consistent with the photograph, not proof that
it was the sole cause of this run's failure.

## Required failure evidence

The wrapper replaced the original trial error with a generic reinitialization
message. The corrected diagnostic must retain the original phase/status,
whether DMA actually started, command response/token and hardware snapshots,
alongside the separate acquire/init/recovery failure. These details must fit
on the result screen because unsafe recovery deliberately prevents JSON writes.
Normal readers, CRC policy, resident limits and Windows CE eligibility remain
unchanged. No new soak is needed; retest the corrected probe once.

Primary source: [Renesas SH7750 Hardware Manual Rev. 7.02](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware),
section 15.2.8, printed pp. 671–675 (including the separate RxD read and TxD
write signal paths). The failed [candidate and original validation](sci-async-probe-2026-10-02.md)
remain part of the record; host validation did not establish console success.

## Corrected candidate and delivery

**38693a0de68e61a0c01769308277444d9fbcb6e0** implements the correction and
failure evidence. [Diagnostic run 196](https://github.com/TPMJB/K-UI-NeXT/actions/runs/36959829276)
passed full host/filesystem and Dreamcast jobs. Strict ASan/UBSan regressions
model RxD/SCK independently of written output latches; both low and high RxD
complete all 80 trials, and the old assertion demonstrably rejects low RxD.
Preexisting GPIO output ownership is rejected without writes. Earlier DMA
quarantine, foreign-owner and integrity cases still pass. Eight runtime wrapper
modes include failed reinitialization and reacquisition while retaining the
original probe cause. Both failure layouts fit 640×480; JSON formatting passes.

The failed screen now shows phase, DMA starts, SSR/SPTR, response/token and
separate recovery detail. If DMA started, it additionally shows remaining count,
CHCR and SCI ERI/RXI counts. Unsafe storage still cannot save a report. No normal
reader, CRC policy or CE eligibility change is included.

Native normal/benchmark instruction audits remain 18,999/19,862 instructions.
SCI/SCIF/IDE resident payloads remain 11,156/10,608/9,832 bytes, memory ends
`0x8c00baec`/`0x8c00b8c8`/`0x8c00b5b8`, and conservative stacks remain
1,172/1,080/1,000 bytes within 1,232. No unresolved symbols.

The 4,561,088-byte source artifact `11207742375` has SHA-256
`b617c71fde5dd97072a2e480af11183e47bf582789b855bc59f5a4c2751d0c8b`.
All 96 manifest file hashes, ZIP integrity, both build IDs and package CRCs
were verified before producing the update:

| Install file | File size | Package CRC32 | SHA-256 |
| --- | ---: | --- | --- |
| `KUI/runtime.kui` | 1,615,616 B | `33ebd3f7` | `375f05cf67d1b9ad46cbcedcf9ebbe6744d18ce47433d8163e98792724e3e3ef` |
| `KUI/apps/games/retail-boot.kui` | 56,712 B | `96138e8e` | `8f486b54caac35499c37e5f4bfb6ef45e1fe78e6e57b14adbb413c3b886913dd` |

Both identify **38693a0de68e**. Runtime payload/memory are
1,615,552/5,273,424 bytes. Delivered
`K-UI-SCI-Async-Probe-38693a0de68e.zip`: 778,714 bytes, SHA-256
`865b51cb3c6c5489b30dd7dea84624d157c63e25e8e35f1aa6d7fc9ce23c090a`.
It contains both files, instructions, hashes and the source build record.

Install both files, reboot with SCI selected, and run
**Diagnostics → Storage tests → SCI async probe → A** once. Return the JSON
and photograph, or just a photograph if it requires restart. Keep the existing
CD/card format. No repeat soak or game timing is needed. Corrected console
validation remains pending.
