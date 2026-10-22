# Reusable SCI runtime reader and sustained diagnostic

Recorded 2026-10-02. This implements the next hardware-validation gate after
the [80-read console pass](sci-async-repeat-read-pass-2026-10-02.md). Console
validation of this candidate is pending. Native game reads and CE launch are
unchanged; the [native integration plan](sci-native-async-integration-plan-2026-10-02.md)
records the required cursor, interrupt and memory work.

## Reader contract

The runtime now exposes a serialized `open/begin/poll/finish/cancel/close`
reader. Both the existing quick probe and the sustained test use it. A poll
performs at most eight framing bytes or one DMA-state observation; it never
waits for the in-flight payload to finish. The CPU can return to other work
between calls while receive-only DMA captures the 512 data and two CRC bytes.

Publication still requires completed owned DMA, intact buffer guards, CRC,
an optional expected-sector comparison, and a successful framing handoff.
The diagnostic supplies the expected sector on every read. Without that
optional comparison, the API explicitly reports `baseline_checked=false`.
Generation and handle identity reject stale or copied leases. Each resumed
operation rechecks ownership before touching hardware or receive data.

Cancellation drains an active transfer through bounded polling and discards
its result; closing a pending operation cannot silently abort it. An
incomplete DMA retains its static receive allocation and quarantines the
transport until restart. Unknown SCI module state forbids further SCI MMIO.
The previously console-tested conditional SCI-only reset remains restricted
to a completed, verified payload with trailing overrun.

Review found that ending a slow-only lease could restore BRR without updating
the normal driver's cached speed. A runtime-only helper now reconciles that
cache after verified healthy restoration. It transmits no bytes, changes no
hardware and cannot clear a sticky bus fault. Ordinary verified storage
recovery remains mandatory before report writing.

## The 60-second test

Diagnostics → Storage tests → SCI async probe retains **A: Quick** and adds
**X: 60s stress**. B requests cancellation. The worker pauses existing storage
consumers and unmounts the volume before taking its exclusive lease.

The test obtains each baseline twice through the normal CRC-checked reader.
It selects the first eight sectors and eight more spread through the card's
addressable range, including its last sector. Smaller devices use a bounded
smaller cohort. It then alternates forward and reverse passes over that
cohort at full speed for at least 60 seconds, with a 262,144-read safety cap.
Every read must match its corresponding baseline. The sector tests are
read-only; the final JSON report is the only storage write.

Between bounded polls, the CPU performs independent work and records DMA
count progress. The client allows natural scheduler preemption during bounded
bursts of up to 64 reads and voluntarily yields between bursts. Yielding on
every short DMA poll could otherwise place the receive windows between timer
ticks and weaken the observation. No timer cadence or priority is changed.

An observer temporarily chains the existing KOS TMU0 IRQ handler. It records
real timer interrupts, including those arriving while the reader proves an
owned, incomplete DMA active. It calls the original handler with its original
arguments and does no work after that call. Restoration is verified; a
foreign replacement is preserved and reported. Failure to acquire the hook
does not authorize removing someone else's observer.

The observer uses the pinned KOS handler/scheduler contracts, reviewed in
[timer.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/timer.c),
[irq.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/irq.c),
[thread.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/thread/thread.c)
and [thdswitch.s](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/thdswitch.s).
Voluntary `thd_pass()` does not rearm the primary timer in this revision.
These are interface/semantic references; no DreamShell driver code is used.

## Reporting and interpretation

Schema 2 retains original first-fault, SCI reset and recovery evidence. It
adds the selected LBAs, baseline CRCs and per-LBA counts; elapsed duration;
reader API call maxima; CPU overlap; timer tick counts, active-DMA tick counts
and maximum observed tick gap; and observer acquisition/restoration state.
The quick and sustained runs each save a separate
`/KUI/tests/sci-async-NNNN/sci-async-probe.json` after safe ordinary recovery.

A stress pass requires the complete duration without hitting the iteration
cap, every selected LBA verified, consistent read/completion counters,
CRC/data/guards, CPU overlap, real timer IRQs during DMA, restored observer,
safe engine cleanup and a successful ordinary checked recovery read.
Zero active-DMA timer ticks remains unconfirmed, even if all data matches.
Observer failure is distinct from unsafe SD restoration; it fails the
diagnostic without inventing an SD hardware fault.

API duration is wall time and may include preemption. Receive duration
includes worker observation delay. Tick gaps are observed intervals, not a
proved worst-case interrupt latency. CMD17 stress rate is not the filesystem
CMD18 soak rate and must not be used as a throughput comparison.

## Validation and next gate

Targeted ASan/UBSan tests and independent lifecycle review cover bounded
framing, cancelled/pending close, no early publication, bad CRC/guards,
stale handles, foreign ownership, stopped clocks, timeout quarantine,
SCI reset failures, slow-only cleanup, baseline mismatch, ordinary recovery,
and heartbeat chaining/restoration. UI/report checks include worst-case
counter formatting and the exact pass predicate. Full host/Dreamcast CI and
package checks will be recorded with delivery.

Install both update files, reboot into SCI, run A once, then X if the quick
probe passes. Return both JSON reports and the stress result photograph.
If restart is required, photograph the failure and reboot. A new CD, another
filesystem soak or a DOA2 timing run is not needed for this runtime gate.
Successful console stress will permit the separately reviewed native async
integration candidate; it does not establish game scheduling or CE support.

## Verified delivery

- Source: `c0c285482dac1aea555335e2eacb9c8d5f6b3867` on `codex/storage-transports`.
- [Full CI run 200](https://github.com/TPMJB/K-UI-NeXT/actions/runs/37005779437):
  host and Dreamcast jobs both successful. All 12 runtime wrapper executions,
  reader lifecycle and timer-observer tests pass without sanitizer findings.
- Linked instruction audits: 19,039 normal and 19,908 benchmark instructions;
  no relay/resident FPU use or unresolved symbols. Native layout and stack
  audits are unchanged from the previous successful candidate. SCI resident:
  11,156 bytes, end `0x8c00baec`, stack 1,172/1,232.
- Source SD-update artifact: `11226142051`, 4,571,784 bytes, SHA-256
  `f4f05fc6f51fe3baf0cc831fef1aa9c1c1a62f0570f721efe65338cf731f19e9`.
  ZIP integrity and all 96 source-manifest hashes checked.
- Delivered `K-UI-SCI-Async-Reader-c0c285482dac.zip`: 787,200 bytes, SHA-256
  `2fa0c10ccfb0fdda066f0112d6b4cba6095dd6d11a4c595607d20aee8b97a7dc`.
  Includes only the two update binaries, instructions, hashes and build record.
- `KUI/runtime.kui`: 1,626,308 bytes, SHA-256
  `27ab7a34ec887ccc72287d7a13cce82071e2cfd97e5ac617aa9aaf9e729b5acc`.
- `KUI/apps/games/retail-boot.kui`: 56,832 bytes, SHA-256
  `dade25c0ff67eeeb1871064208c447508d205ce1be9e2803dba4d84a248c2b62`.
  Both packages have verified header/payload CRCs and build `c0c285482dac`.
  The entire retail payload matches 0e9 byte-for-byte after replacing its
  four build-label strings; no game-reader code or layout changed.

The [CI audit record](sci-async-runtime-ci-c0c285482dac.json) preserves the
layout/stack measurements, executed host cases and exact downloaded log
hashes. These checks validate the build, not the new console test result.
