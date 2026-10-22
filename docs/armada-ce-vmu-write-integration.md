# Armada CE VMU write integration

The cached full readonly build `af18576dba18` reached the existing loader's
playback baseline by the user's report. Its 1,200 DMA fences restored without
recorded faults; one virtual A2 INFO reply was published. This is a discovery
and performance result, not proof of a complete writable card. Physical-disc
playback parity remains separate.

## Implemented components

`armada_ce_vmu_write` is a standalone A2 protocol adapter over the existing
generic engine. It admits exact 140-byte WRITE packets and stages four 128-byte
phases into the engine's existing 512-byte buffer. Phase ACKs promise receipt.
The final ACK requires the explicit provider's durable OK. Duplicate final
requests do not commit again; BUSY/error retries preserve attempted bytes.
The CE phase 0/1 read completion path remains separate. RESET/KILL/LCD and
unproved write-finalization shapes remain excluded. The generic engine and
existing readonly adapter are unchanged.

`KUI_ARMADA_CE_MAPLE_WRITE_STAGING=1` enables a separate transport experiment:
exact 34-word capture, complete final payload comparison, pre-backend ownership
admission and the existing post-backend admission, bounded response publication.
Canonical FILEERR (8 bytes) is clipped to its unchanged header only for CMD12 with
proven capacity 4; other replies retain their full capacity checks. Private
backend DEFERRED (2)/produced (0) is admitted only for READ or phase 4 SYNC, then
returns PUBLISH_PENDING with zero guest stores and no completed-reply counters.
The caller must withhold native completion, leave the masked publication
phase, run the provider with interrupts enabled and repeat the whole ownership
proof before publication. The exact runtime retains MDEN=0 during that work;
it does not release the original completion's hardware ownership early.
The separate `KUI_ARMADA_CE_VMU_WRITE_RUNTIME=1` path now connects this policy
to the native continuation. Raw AGAIN remains a visible game response; it is
not a safe transparent commit wait. The 140-byte private request occupies reply bytes 384..523 while
the backend receives only the disjoint 384-byte response prefix. The default
flag is 0, preserving the accepted readonly input layout and runtime. Staging alone does not connect a writable backend or SD provider; the runtime
flag additionally requires the expanded layout and an explicit prepared P2
profile. Its additional admission
can increment the final-proof counter twice for one write request; the old
initial/final equality rule does not describe this experiment.

`vmu_deferred_io` separates Maple callbacks from worker I/O. Its single job
copies all 512 commit bytes and retains no guest/protocol pointer. The fixed
provider and final-location self identity bind its lifetime; a nonwrapping
ticket identifies each operation. Short platform guards protect transitions:
IDLE → PENDING → RUNNING → DONE → IDLE, or permanent FAULT. The worker must
verify its actual owner/thread/IMASK 0 context. It releases the guard before
calling the provider. Only a matching successful retry consumes completion;
the protocol then handles duplicate final ACKs without another job.

Provider BUSY means no acquisition or I/O. Any acquired/uncertain write,
readback, identity, restoration or release failure must return ERROR and
poison service. An SD provider's OK must include real journal/base barriers,
exact readback, identity/health and successful bus release. These properties
belong to the provider, not the mailbox's success flag. The opt-in runtime now supplies the dedicated CE SCI provider and ordinary-
worker continuation described below; physical console acceptance is pending. A post-I/O guard-entry failure leaves the
job RUNNING with no ACK and no second provider call. There is no cancel/reset API. The owner's separate irreversible poison
operation cannot free an executing provider or admit a new job; a late
completion is rejected after release. An abandoned read otherwise blocks
other jobs until the exact read is consumed.

## Validation

Run the protocol/publisher checks and deferred job/session checks with:

```sh
make -f Makefile.armada-maple-test write-protocol-tests write-staging-tests
make -f Makefile.vmu-deferred-test host-tests
make -f Makefile.vmu-deferred-persist-test host-test
make -f Makefile.armada-maple-test a2-tests
```

The owner test always checks the recorded metadata. To reproduce all 19 source
windows and 79 decoded scalars against privately extracted startup files, run:

```sh
python3 tests/test_armada_ce_maple_write_owner.py --startup-dir /path/to/Armada-startup
python3 tools/armada_ce_maple_write_owner_audit.py --startup-dir /path/to/Armada-startup --check docs/evidence/armada-ce-maple-write-owner.json
```

The simulated-device integration executes the real persistence session and
redo store through the new CE endpoint and mailbox: four phases, AGAIN while
pending, worker commit, final ACK, duplicate ACK, readback, CE read completion,
fresh-session admission/recovery and reopen. It injects failures at each of
four journal/base writes and four barriers, plus a failure after data persisted
but before the provider reports release success. No failed commit is ACKed.
This establishes host behavior; it does not establish physical SCI durability,
CE worker scheduling, driver retry handling or game save compatibility.

The standalone SH protocol state is 600 bytes, retaining the existing 512-byte
stage; the new mailbox adds another bounded private 512-byte job. Both require
explicitly reserved space. Default production helper equivalence and enabled
transport object/stack checks are recorded separately with the checkpoint.

## Exact owner findings from the supplied startup archive

The supplied kernel, MAPLE.DLL and MAPLEDEV.DLL match every prior identity and
RX ownership contract. The new metadata-only audit is
`tools/armada_ce_maple_write_owner_audit.py`; its checked output is
`evidence/armada-ce-maple-write-owner.json`. No owner binary/disassembly is
included in the source checkpoint.

| Owner behavior | Result |
| --- | --- |
| WRITE packets | Four140-byte packets, phases 0, 1, 2, 3; four-byte caller RX |
| CMD13 after writes | Phase 4; eight-byte caller RX; ACK 7 completes |
| Finalizer AGAIN | Error 0x868500b0; no reviewed whole-block retry |
| FILEERR / invalid WRITE ACK | Error 0x86850010; at most 3 whole-block attempts |
| READ | 524-byte caller RX; one-phase read finalization uses phase0 |
| 16 ms wait timeout | Separate token-validation IOCTL, then wait again; original IRP remains pending |
| Ordinary completion worker | Same worker owns normal unit removal; no enclosing critical-section lease |
| Root shutdown | Can terminate the worker after its bounded wait; not covered by ordinary pending lifetime |

A canonical private FILEERR (8 bytes) header can be published within CMD12's four-byte
requested slice: the exact completion copies min(logical reply bytes, caller
capacity), and WRITE reads only the header. It keeps the declared one-word
error payload; it does not fake a zero-word error or expand into bucket slack.
CMD13 can carry the complete 8-byte error normally.

Holding the existing GD-active guard is not transparent SCI backpressure.
The exact WSEGACD read caller turns REQUEST's zero token into 0xc0000185 instead
of retrying. An audio worker can also enter its transport independently of
that guard. SCI borrowing therefore requires a loan-aware GD wait/defer path,
explicit exclusion of audio transport work, a successful CMD18 stop and bus
release/restoration checks. An idle snapshot alone does not exclude a later
read while provider I/O runs unmasked.

## Connected opt-in runtime

`armada_ce_vmu_runtime` binds the real CE endpoint, immutable single-job queue
and dedicated SD provider once, after a complete source-owner admission. All
protocol callbacks remain masked and perform no device I/O. A private pending
result withholds the original consumer inside PRE and retains its admitted
MDEN=0 window. It does not retain an interrupt mask during storage work.
The pinned shim runs one storage operation on a separate 4 KiB stack with
actual kernel mode, BL clear and IMASK 0. It then masks again, verifies the
same held hardware window, and repeats the complete capture, exact pending
input/slices and final proof
before publishing or entering the original response consumer.

The original completion owns that window until its consumer/builder returns;
only the paired POST then restores MDEN. Re-enabling earlier allows a VBlank
to replay the old descriptor before CE rebuilds it. A second latched Maple
status can survive the current worker's status query and become visible at
InterruptDone after the builder clears RX. The next completion would then
contain a zero parent header. Retaining MDEN=0 prevents that replay while
keeping CE scheduling and the SCI/GD loan service available. Ordinary unit
removal is owned by this same worker; external forced teardown remains the
separate unverified lifecycle described below. Terminal paths attempt normal
fence restoration and never enter the original consumer with an unresolved
request. The publisher's physical-parent and A2 conflict checks are unchanged.

The continuation binds actual PTHREAD from the audited NK KData word; the
extension's CreateThread HANDLE is distinct. A pre-acquisition BUSY yields by
waiting 1 ms on this worker's own admitted HANDLE through MAPLE's existing
WaitForSingleObject import. That external API runs on the original CE stack,
through a fixed assembly ABI adapter. The adapter allocates CE's mandatory
16-byte caller argument area and preserves PR/GBR separately. The authored
CE export writes all four home slots; a direct GNU C function-pointer call
does not allocate them and overwrites its own return frame. The native test
models those real stores before checking the complete resumed continuation.
Neither private stack is used by that external API. After 1,000 pre-acquisition BUSY results the
queue faults permanently; a freshly admitted retry can publish a real FILEERR.
DONE/RUNNING jobs are not discarded by that deadline. A separate bounded
continuation fuse terminates unresolved ownership or fence restoration without
calling the original consumer.

The reader uses separate loan state rather than holding the recursive GD
entry guard. It accepts and copies real GD requests, returns positive tokens,
and leaves CHECK pending while storage is deferred. Reader/IRQ/audio pumps do
no storage I/O during the loan; PIO_CHECK offers zero bytes. A claim requires
idle service/reader/audio state. CMD18 is stopped unmasked, then the port is
released and its fault latch checked under a short mask. Successful writer
release clears the loan and resumes the queued reader in CE's GD context.
Acquired failures permanently exclude storage; they cannot become BUSY or ACK.

The dedicated writer uses bounded programmed transfers, with no block/DMA
callback. It obtains actual CRC-checked CID and CSD geometry, limits every LBA
to the prepared image/journal, requires an initially clean marker, executes
the existing redo barriers, verifies all 512 committed bytes and final identity,
and requires both writer and reader release success. Only then can the queue
complete and the final ACK become eligible for fresh publication.

The high stage now runs fresh filesystem ownership/recovery/VMU validation and
checkpointing against the explicitly selected P2 profile before NK starts.
It publishes the sealed 1272-byte handoff and independently sealed resident
entrypoints after installation; the cache-off relay checks both. No borrowed
card, path or filesystem pointer survives. See
`armada-ce-vmu-runtime-stage.md` for allocation and build details.

## Remaining acceptance and broader integration

1. Run the console save/load/power-off test in
   `armada-ce-vmu-write-console-test.md`. Physical durability, actual CE timing
   and game compatibility are not established by host/native tests.
2. Validate external shutdown/forced termination during an acquired loan. The
   current failure policy withholds acknowledgement and leaves storage
   excluded; it does not claim a cancellation lease over CE driver teardown.
3. After hardware acceptance, select prepared cards through the normal launch
   flow and extend the same service to other validated owner families and
   storage transports. The current explicit SCI/ASYNC profile is source-specific.

## Universal and physical-disc direction

The packet and storage APIs are independent of media; hook detection and
lifetimes still require validated native SDK/CE driver adapters. An arbitrary
signature match is insufficient. A physical GD-ROM launch additionally needs
retained runtime/bootstrap support and an independently owned SD save provider:
the current BIOS-reboot GD Play path discards resident VMU state. These remain
later work; the connected Armada experiment is not a universal or disc-boot VMU release.
