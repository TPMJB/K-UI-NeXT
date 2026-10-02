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
