# Quarantined bounded physical-VMU writer prototype

This is a source-only transport experiment. Its public contract is
`include/kui/vmu_write_bounded.h`; no production application, restore path,
manager operation or ordinary build target is authorized to use it. There is
no public release or push associated with this prototype. No physical write
has been performed as part of its development. Physical acceptance remains
pending on a disposable VMU with a deliberate test plan.

The helper attempts one 512-byte block through four BWRITE commands and one
BSYNC command. It never calls the SDK's `vmu_block_write`. It performs no
retry or recovery operation. This does not grant permission to write any
particular block: the accepted raw block range includes the root, FAT,
directory and payload areas, so a valid argument can still destroy a card.

## Protocol evidence

The primary specification is SEGA's *Maple Bus 1.0 Function Type
Specifications FT1: Storage Function*, Rev. 0.80, available from the
[public FT1 resource](https://segaxtreme.net/resources/maple-bus-1-0-function-type-specifications-ft1-storage-function.195/).
The page numbers below are the printed page numbers; they also match the
PDF page numbers of the retrieved 41-page `Ft1_080e.pdf`.

| Primary reference | Consequence for this prototype |
| --- | --- |
| FT1 p20, section 4.1.3, Block_Write | The request identifies the storage function, partition, phase and block. Write phases must be consecutive; an intervening command or an out-of-order phase is an error. |
| FT1 p21, section 4.1.4, Get_Last_Error | The four-write example explicitly expects Device Reply after phases 0, 1, 2 and 3, then Device Reply for a successful phase-4 Get_Last_Error. The same partition and block are used throughout. |
| FT1 p23, section 4.1.6, Device Reply | The successful response is command `0x07` with zero additional data words and no payload. This applies to every write phase and the final phase-4 command. |
| FT1 p28, sections 4.2.3 and 4.2.4 | Transmit Again is `0xfc`; File Error is `0xfb` and carries an error word. Both are terminal failures for this experiment, regardless of the specification's broader retry guidance. |
| FT1 p33, section 5.3, Block_Write Processing Flow | The device can send the phase acknowledgement before transferring the buffered data to the medium. A phase acknowledgement alone is not evidence of persistence. |
| FT1 p34, section 5.4, Get_Last_Error Processing Flow | The final command obtains the result of the preceding operation. It is a required part of this five-command experiment. |

[Dmitry Grinberg's primary VMU investigation](https://dmitry.gr/?proj=25.+VMU+Hacking&r=05.Projects),
section “The SEGA-to-VMU commms protocol revealed,” calls command `0x0d`
Complete Write and specifies phase 4 after the four 128-byte write quarters.
It also observes that a read can substitute for write completion. That is
one reason this prototype must not issue an automatic diagnostic read after
an incomplete or failed write sequence. No specification text, packet dump
or external implementation is embedded here.

For an ordinary 256-block VMU, the attempted sequence is BWRITE `0x0c`,
phases 0 through 3, each carrying exactly 128 payload bytes, followed by
BSYNC `0x0d`, phase 4, without write payload. Partition is zero and the
block is unchanged. Every reply must have response `0x07`, `data_len == 0`,
the exact destination address of the host port, and the exact source address
of the selected VMU. A reply has no function or block echo to validate when
its data length is zero; those fields must not be invented or read from
stale receive bytes.

## Pinned SDK limitations

The reviewed KallistiOS revision is
`fcfa7d869471591ca1c777543261a7bfea7cb726` in `.deps/kos`.
In `kernel/arch/dreamcast/hardware/maple/vmu.c`,
`vmu_block_write_internal` checks the BWRITE response code but not its word
count or reply addresses. It continues through remaining phases after a
phase error and does not inspect the final BSYNC response. The public
`vmu_block_write` wrapper can repeat the entire write sequence four times.
Its timeout path can mark an in-flight device frame vacant. None of these
behaviors is inherited by this experiment.

In the same revision, `maple_dma_irq_hnd` in `maple_irq.c` handles `AGAIN`
before invoking frame callbacks: it marks the frame unsent and leaves it
queued for another DMA submission. A callback and a waiter deadline alone
therefore cannot enforce one transmission per command. The bounded reader's
existing timeout policy is not a sufficient write transport contract.

## Exclusive ownership and IRQ interception

The prototype uses the public ASIC completion-handler API to install a
temporary wrapper for `ASIC_EVT_MAPLE_DMA`. It saves the previous handler
and its data. On DMA completion, the wrapper identifies only its own sent
frame and removes it from normal queue processing before the SDK reaches
its `AGAIN` branch. It captures the owned reply, records completion, and
wakes the waiter. The saved handler still processes other frames and
performs its normal DMA bookkeeping. An owned `AGAIN` is reported as a
failure, never resubmitted. Handler installation, ownership changes and
restoration must be protected against IRQ races.

Both the parent controller's unit-0 frame and the target VMU frame are locked
for the whole five-command group. Taking the controller frame first drains
and excludes the ordinary controller-driven autodetection chain, which can
otherwise use the controller's frame to address a subdevice. Taking the VMU
frame excludes standard VMU polling and APIs using that frame. Successful
completion releases both locks only after the final reply has been checked.
Lock acquisition is bounded; another owner's frame is never reset or stolen.

These locks cover the pinned SDK's standard producers, not arbitrary
application code. The caller must additionally:

- Call from ordinary thread context outside IRQ handlers, with IRQ delivery
  available. A cancellation callback must return promptly without blocking.
- Hold the VMU filesystem mutex for the complete operation and serialize
  filesystem and application-level use of the selected card.
- Exclude custom frames or direct Maple commands to the target address,
  including LCD, clock, reset and storage operations. A separately owned
  frame does not acquire this exclusion automatically.
- Exclude concurrent changes to the Maple DMA handler, and preserve the
  Maple subsystem, the parent controller object and the target device object
  through the operation and any quarantine.
- Keep the prototype single-owner and non-reentrant. Do not unload its code
  or data while a late callback remains possible.

The helper validates the selected device before sending, but a matching
address, capability word or product string is not a hardware identity. They
cannot prove that a user has not exchanged two otherwise identical cards.
Do not hotplug during an experiment.

The 100 ms limits apply separately to the controller-frame lock acquisition,
the target-frame lock acquisition, and each of the five command waits. They
are not a 100 ms deadline for the whole call, and do not bound an arbitrary
caller-supplied callback or scheduler delay.

## Buffer lifetime, cancellation and failure

The 512-byte input is copied into static transport storage before any
command is queued. The owned frame and receive storage also outlive the
call. Late completion never accesses the caller's payload, output
diagnostic, cancellation context or other stack state. The optional
cancellation function is sampled synchronously before commands and is not
retained after return. The output diagnostic is a frozen synchronous copy;
late completion can change only retained internal transport state.

Before anything can have been sent, argument, ownership or cancellation
failure can return the locks and leave the outcome `NOT_SENT`. Once a
command can have reached DMA, any failure or cancellation permanently
latches `tainted` for the rest of the launch and retains both device-frame
locks. This includes a bad address, malformed length, non-ACK response,
timeout, or cancellation between otherwise acknowledged phases. Subsequent
write calls are refused. The caller must stop the experiment and prevent
other commands to that card.

A timeout while the owned frame is `SENT` also retains its frame, buffers
and completion wrapper in quarantine. A late reply is consumed without
retry; it can permit the wrapper to be restored, but never clears taint or
releases the two device locks. A permanently hung DMA remains quarantined.
Do not shut down Maple, unload the prototype, reuse its storage, or resume
card operations merely because the synchronous call returned. Reboot is
the boundary for abandoning this experiment's retained state.

There is no automatic readback, reset, additional sync, phase replay,
whole-block retry or rollback after failure. Such operations can change
device state and cannot be described as harmless cleanup. A completed
phase-4 ACK yields `ACKNOWLEDGED`, not a durability certificate. It does not
establish atomicity, power-loss behavior, absence of collateral writes or
the physical contents of the entire card.

## Verification and acceptance boundary

The isolated test entry points are:

```sh
make -f Makefile.vmu-write-test host-test
source .deps/kos/environ.sh
make -f Makefile.vmu-write-test native-abi
```

Both targets explicitly enable the prototype compile gate. The ordinary
runtime and VMU manager build lists do not reference this writer. Compiling
the source without `KUI_VMU_WRITE_BOUNDED_PROTOTYPE=1` is refused.

The completed host run passes 5,455 fork-isolated cases under ASan and UBSan,
using the exact source with mocked queue, IRQ and scheduler boundaries:
the five-command success sequence for all 256 blocks; exact
payload quarters and location encoding; header, response and length
failures at every phase; suppressed `AGAIN` resubmission; cancellation
before and after possible transmission; lock contention; unsent and sent
timeouts; late completion; permanent taint; held locks; frozen diagnostics;
and forwarding/restoration of the previous completion handler. Tests also
cover a queue failure that nevertheless leaves a sent frame, shared TX/RX
storage, dirty cached receive aliases, a four-byte-only reply allocation,
changed caller payload after queueing, overlapping input/output refusal,
handler theft and a stale self-handler without recursive forwarding.
Failure expectations derive taint, held locks and submission/completion/ACK
masks independently from the injected fault and its phase. They do not use
the helper's returned taint flag to decide whether taint was required. An
unexpected queued state during final cleanup must return failure and an
uncertain outcome, even after all five ACKs were observed.
The native compile against the pinned SDK passes with warnings treated as
errors. The compiler reports 148 bytes of stack for the synchronous entry
point and 24 bytes for the DMA wrapper; the 512-byte payload snapshot is
static. Logs are `build/vmu-write-prototype-host.log` and
`build/vmu-write-prototype-native.log`.

The scheduler mock checks each requested 100 ms wait and exercises completed
responses winning a wait-timeout race; it does not establish elapsed-time
behavior or explicitly deliver completion between queue submission and waiter
registration. Late-completion tests freeze the caller diagnostic and reject
wakes or further commands, but do not poison expired caller memory. Static
callback lifetime and the absence of retained caller pointers also require
source review.

Host tests and native compilation do not constitute physical acceptance.
Before any production integration, a separately authorized disposable-VMU
experiment must establish observed reply behavior, bounded failure behavior,
readback after an entirely successful sequence, and preservation of all
unintended blocks. Interrupted operations remain potentially destructive.
The existing importer, restore/copy paths, format defaults, physical root
handling and public artifacts remain outside this prototype.
