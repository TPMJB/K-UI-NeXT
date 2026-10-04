# ATA readiness candidate — 2026-10-04

## Scope and evidence boundary

The owner cannot test IDE/CF until the soldering work is complete. They asked
to prepare the driver and a release build while the SCI 512-byte token test
(`6dc75f1ea0b2`) is still pending. Work is isolated on `codex/ata-readiness`;
the SCI test branch and its verified update remain available unchanged.

This candidate improves synchronous ATA PIO. It does not implement G1 DMA,
claim ATA hardware acceptance, or enable IDE for Windows CE. The canonical
published version remains unchanged. Candidate packages have a distinct
artifact prefix, candidate metadata and installation notes. No release tag
or public release is created by this work.

## ATA changes

- The resident reader can retain one READ SECTORS command for at most eight
  contiguous sectors within the caller's validated request/extent span.
  Each callback still delivers exactly one 512-byte block. There is no
  speculative read-ahead buffer or change to image mapping.
- Stop, early termination and a discontinuous next request consume the
  bounded unread tail before restoring the previous device selection. A
  device that cannot become idle is invalidated; no reset or forced device
  switch is used. The caller keeps exclusive G1 ownership across the run.
- Optional whole-sector bus callbacks remove the indirect callback per ATA
  word. The native path still uses volatile 16-bit PIO accesses, with aligned
  and unaligned memory paths. Existing word callbacks remain the fallback.
- The saved device-select byte is read after BSY clears, because register
  reads while busy can return status instead. IDENTIFY command-set validity
  is applied before interpreting the write-cache capability bit.

SCIF, SCI and CE tuning are unchanged. The common image/GD contract and
resident memory/stack limits remain unchanged. The ATA functions and native
ATA bus are the optimization boundary; the stage still selects one resident
matching the manifest's fixed transport. Native/CE reader comparisons at a
fixed build ID check whether that isolation holds in the compiled images.

## Next DMA milestone

The current runtime serializes ATA with the GD-ROM using the G1 semaphore
and separately checks K-UI's BIOS disc-DMA activity. A future asynchronous
DMA diagnostic must preserve that ownership and the existing G1 completion
and error handlers. It must validate cache/alignment/range handling, actual
interrupt delivery, CPU progress during transfer, byte-for-byte data against
PIO, and GD-ROM reads before and after ATA use. Game integration and a CE-safe
IDE resident follow that hardware evidence. No automated host test proves
those physical timings or compatibility properties.

## Validation record

ATA protocol and storage-dispatch ASan/UBSan checks pass, including command
counts 255/256/257, LBA28/48 boundaries, busy-master selection, delayed/error
status, stream continuation/abort/discontinuity, output guards, unaligned
bulk copies and finite waits with a stopped or absent clock. Five candidate
package/archive tests, 24 retail package/layout tests and six workflow
hygiene checks pass. Leak detection is disabled for the container.

All native and CE SH GCC 15 proxy layout, stack and instruction audits pass.
At fixed build ID `6dc75f1ea0b2`, SCIF, native SCI, native asynchronous SCI,
CE SCI and CE asynchronous SCI resident binaries are byte-identical to the
baseline. IDE payload grows 544 bytes to 10,328; memory ends at
`0x8c00b79c`, leaving 868 bytes below the unchanged `0x8c00bb00` limit.
Its conservative stack bound is 1,076 of 1,232 bytes. The stage changes
because it embeds the new IDE reader and includes its own ATA driver.

Pinned native CI and complete host/filesystem checks are separate required
gates. Their completed build and checksum record is retained in the
candidate PR. The console result for SCI512 and all ATA hardware results
remain pending; compiled identity is not hardware validation.
