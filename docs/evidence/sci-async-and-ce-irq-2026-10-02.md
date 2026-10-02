# SCI asynchronous reads and Windows CE IRQ requirements

Recorded 2026-10-02 UTC. This is a
design/evidence update, not an implemented asynchronous reader or CE unlock.
The CRC-inlining candidate **93794e47df59** is unchanged; its CI/package and
console validation are separate from the proposals below.

## New report and confirmed architecture

The owner relayed SWAT's explanation that the remaining DOA2 slowdown comes
from CPU-blocking reads, including IDE PIO, and that CE needs DMA plus the
original IRQ, which his IDE path can provide. This is an attributed technical
report, not a measured K-UI CE trace. It changes the next priority from merely
raising KiB/s to returning CPU time while a read is in flight.

K-UI's source confirms the distinction:

| Layer | Current behavior | Missing capability |
| --- | --- | --- |
| Physical SCI receive | Channel 1 DMA moves bytes into RAM, but `feed_read()` supplies 512 dummy bytes synchronously. Reversal/CRC follows. | Autonomous clocking and a transfer lifetime that can outlive the calling routine. |
| Game GD service | `retail_gd.c:execute()` calls `ops.read()` synchronously; resident entry keeps interrupts masked until return. | Game execution during the transfer, independently completed requests and bounded short service work. |
| Game-visible completion | DMAREAD uses the polling/CPU-copy service. Nonzero callbacks and DMA transfer/check functions are unsupported. | The completion/IRQ/register behavior the actual title requires. |
| K-UI IDE resident | `retail_storage_impl.h` calls `kui_ata_read()`; `ata.c` uses PIO word transfers. | G1 DMA and asynchronous completion; buying the hardware alone does not add them to this implementation. |

The remaining seven-second DOA2 slowdown is consistent with CPU blocking, but
its exact breakdown has not been measured. Faster CRC can shorten that work;
it does not convert it into background I/O. Larger batches also change the
length of each uninterrupted call, so aggregate throughput is insufficient.

## Why the original GD interrupt matters

Pinned KOS `g1ata.c` starts G1 DMA and can return without waiting; it installs
its handler on `ASIC_EVT_GD_DMA`, the same hardware event used for GD-ROM DMA.
Linux's Dreamcast IRQ implementation and Flycast's Holly model agree that
software writes acknowledge/clear an event. There is no ordinary operation to
raise that original event by setting its status bit from SCI software.

This supports IDE as the natural existing-hardware route to that event among
SCIF, SCI and G1 ATA. It does not establish a universal impossibility theorem
for a more invasive SCI-specific CE adaptation. Nor does a BIOS DMA callback
equal the original Holly IRQ: KOS documents that callback for STREAM_EX.
**ARMADA's actual GD calls, register accesses and interrupt expectations remain
untraced.** The separate CE prefix/high-stage overlap still needs fixing.

Guest completion must follow all data publication. Raw2352-to-2048 conversion
and fragmented track extents can require several backend transfers for one
guest read. Intermediate ATA DMA events must not falsely complete that read.
Any design needs explicit completion/acknowledgment ordering, abort/error
handling, cache publication, reentrancy and retained GD-ROM bus ownership.

## First SCI experiment: bounded CMD17 receive-only diagnostic

**Unimplemented hypothesis, not a promised speed gain:** after synchronously
receiving a CMD17 response and data token, use receive-only internal clocking
and channel 1 DMA for **514 bytes** (512 payload plus two CRC bytes) into a
dedicated aligned scratch buffer. Configure MOSI high with transmission off.
Run independent CPU work during reception; stop RE, deselect and restore the
receiver/flags/ownership after confirmed completion, then verify every byte's
integrity and compare with the accepted read path.

**Register-map prerequisite:** K-UI and pinned KOS name `0xffe00018` as the
SCI port register, whereas the generic SH7750 manual places SCMR at offset
`0x18` and SCSPTR at `0x1c`; pinned Flycast also distinguishes those addresses.
Linux v2.6.32 explicitly includes `CONFIG_CPU_SUBTYPE_SH7091` in its branch
defining SCSPTR1 as `0xffe0001c`. This is strong SH7091-specific implementation
corroboration for an isolated GPIO/EIO probe, not a demonstrated fault in
K-UI's existing `0x18` zero-write or normal read path. Independently validate
the probe's TxD pin and EIO behavior, name and snapshot SCMR/SCSPTR separately,
and leave the normal driver's addresses and writes unchanged.

Renesas documents receive-only clocking while RE is set, GPIO TxD control with
TE clear, and automatic RDRF clearing by receive DMA. Precise RE timing is
needed to end the clock at an exact final bit. The diagnostic instead tests
whether trailing idle clocks after a complete **single** block can be safely
discarded. SD's CMD17 sequence returns to the next-command phase after CRC;
high MOSI does not supply a command's zero start bit. This protocol inference
still needs electrical and console validation. Do not extend it to CMD18,
where extra clocks could consume the next token/data.

Renesas says an unread byte can cause overrun, preserves the preceding RDR,
and blocks subsequent reception/transmission. It does **not** explicitly
guarantee that ORER stops SCK, so this proposal must not depend on that.
Overrun before a confirmed 514-byte transfer is a failure. The probe needs
guards, finite deadlines, cache-coherency checks and verified recovery before
another command. Repeated CMD17 may sacrifice throughput to command overhead.

Develop this first under a diagnostic that owns its interrupt configuration.
Measure useful CPU work during reception and maximum uninterrupted blocking
span, timer/IRQ responsiveness, transfer/check/setup time, KiB/s, CRC errors
and recovery. CPU work in a polling loop alone does not establish interrupt
responsiveness or that a game can safely resume during reception.
Do not merely unmask interrupts on the existing resident's single private
stack or leave its SCI interrupt sources unowned. A later game reader needs
an explicit begin/progress/finish/abort state machine and service reentrancy
rules before it can return while a transfer remains active.

The smallest proposed integration is a runtime-only Diagnostics action on the
existing sole storage worker. Require SCI, quiesce music/FTP/background I/O,
and unmount the volume for an exclusive experimental lease. Read LBA 0 with
the accepted CMD17 path as a CRC-checked baseline; no card write or scratch
file is needed. Use separate aligned baseline/work buffers and a 544-byte RX
allocation (514 received bytes plus full cache-line padding), with guards
outside those cache lines. A separate begin/result/abort/release API leaves
normal `transfer_block` and game paths untouched.

Keep any completion ISR short: stop reception and publish captured state,
without CRC, filesystem or logging work. Main context verifies CRC, baseline
bytes and guards, restores registers/handlers/priorities, and proves another
ordinary read succeeds before remounting. Report start-return latency, CPU
checksum work completed while DMA remains active, timer/DMAC/SCI event counts
and timestamps, and maximum uninterrupted blocking. UI progress alone cannot
prove concurrency during a roughly 330-us payload. Passing requires correct
bytes/CRC/guards, real CPU overlap, timer/IRQ responsiveness over repeated
trials, bounded completion/abort and successful normal-read recovery. One
roughly 330-us transfer may not span a scheduler tick; do not count that alone
as a failure. A faster KiB/s figure alone is not a pass.

The generic SH7750 CHCR table lists SCI request encodings, but does not prove
the custom Dreamcast's routing supports using channel 3 for SCI. Pinned KOS
documents channel 3 as memory-only. There is no channel-3 shortcut in this plan.

## Sequence and evidence needed

1. Complete the unchanged CRC candidate's Storage Quick/DOA2 comparison.
2. Build and validate the receive-only diagnostic; compare both throughput
   and CPU availability before choosing a game-reader architecture.
3. Broaden native-game testing with title-specific gameplay, FMV/audio and
   VMU checks; native CDDA remains unimplemented.
4. Keep the [CE placement probes](windows-ce-loader-audit-2026-10-01.md)
   separate. After boot placement, treat asynchronous transfer and original
   guest completion/IRQ behavior as explicit prerequisites, alongside memory
   and MMU coexistence. Favor an independently implemented G1 DMA path where
   original IRQ behavior is required; do not claim SCI CE support in advance.

## Primary references (behavior only; no implementation copied)

- [Renesas SH7750/SH7750S/SH7750R Hardware Manual, Rev. 7.02](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware):
  sections 14.2.4 (printed p. 561), 15.2.7 (pp. 668-669), 15.2.8
  (pp. 671-672), 15.3.4 (p. 708), 15.4 (p. 717).
- [SD Association Physical Layer Simplified specification archive](https://www.sdcard.org/downloads/pls/archives/)
  and [official archived download](https://www.sdcard.org/cms/wp-content/themes/sdcard-org/dl.php?f=Part1_Physical_Layer_Simplified_Specification_Ver5.10.pdf):
  indexed/extracted header identifies version 5.00; section 7.2.3/Figure 7-3.
  [Original SDA v2.00 copy at USPTO](https://ptacts.uspto.gov/ptacts/public-informations/petitions/1460367/download-documents?artifactId=L6bWtTGX9gPxpcMwo2VvG4-AJTl40WsfKB3_PbzHM_xF7dMQSVZYpcQ),
  sections 7.2.3 (p. 96) and 7.3.1.1 (p. 101), corroborates framing.
- KOS at `fcfa7d869471591ca1c777543261a7bfea7cb726`:
  [`g1ata.c`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/g1ata.c),
  [`syscalls.h`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/syscalls.h),
  [`sci.c`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/sci.c)
  (port-register label at line 31), and
  [`arch/dmac.h`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/dmac.h).
- Flycast at `04669ebe4164bb589e6a2050aa1559724aa94291`,
  [`sh4_mmr.h`](https://github.com/flyinghead/flycast/blob/04669ebe4164bb589e6a2050aa1559724aa94291/core/hw/sh4/sh4_mmr.h),
  distinguishes SCI SCMR and SCSPTR physical register addresses. Renesas
  Table 15.2 (printed p. 659) supplies the corresponding generic register map.
- [Linux v2.6.32 `sh-sci.h`](https://github.com/torvalds/linux/blob/v2.6.32/drivers/serial/sh-sci.h),
  blob `3e2fcf93b42e6668380e79db15a1b4250840544f`, explicitly includes SH7091
  in the `0xffe0001c` SCSPTR1 definition and uses that register for pin reads.
- [Linux Dreamcast IRQ handling](https://github.com/torvalds/linux/blob/master/arch/sh/boards/mach-dreamcast/irq.c),
  retrieved blob `0eec82fb85e7c6b4cb262fc79ed7fa24d3e4f70b`.
- Flycast [`holly_intc.cpp`](https://github.com/flyinghead/flycast/blob/master/core/hw/holly/holly_intc.cpp),
  blob `a6d59465790676c52417a236083ada283234b409`, and
  [`gdromv3.cpp`](https://github.com/flyinghead/flycast/blob/master/core/hw/gdrom/gdromv3.cpp),
  blob `cada497289564d6d75b6d378c0623b80ef4fa98b`.
