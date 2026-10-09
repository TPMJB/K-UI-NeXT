# Toy CDDA lifecycle and FMV pause review

Review date: 2026-10-08 UTC. This evidence concerns the exact Toy Commander
pilot, not the ordinary reader. The reported Handoff-Fix run produced a short
piece of music and stopped; skipping the intro froze the game and prevented
the reset decision. Static review can identify defects that explain these
symptoms, but cannot identify the console's final instruction or certify the
new build's playback.

The reviewed original inputs retain the identities recorded in the earlier
[driver review](cdda-toy-driver-command-static-2026-10-07.md). Game binaries,
driver binaries, private disassembly, and the separate DreamShell checkout
are excluded from source and delivery. Addresses below are admission
contracts and observations, rather than copied executable instructions.

## Cache mode was changed again by native startup

Toy's startup reaches its cache setup before the heap hook used to install
the worker. The native setup reads the policy word at `8c0c5bc4`, whose
original value is `00000105`. Its uncached helper applies that value and
performs cache invalidation while preserving its mode bits. Consequently,
the initial reader handoff mode does not determine the later worker mode.

Movie paths at `8c04c238` and `8c06da62` discard operand-cache tags. With
P1 copy-back selected, dirty worker, stack, or GD state could be discarded
without being written to RAM. The logical risk applies even if the GD
request and sound-bank code are otherwise correct.

The independently written correction admits only the known original policy
and substitutes `00000101` before native startup consumes it. Both caches
remain enabled; P1 uses write-through. The installer then verifies
`CCR & 00000105 == 00000101` before admitting the worker. Installing only
the policy word at the later heap hook would be too late, so that approach
is not used. Other reviewed cache writes preserve this mode or disable
the caches during teardown.

The bit meaning is documented independently in the
[Linux SH-4 cache header](https://kernel.googlesource.com/pub/scm/linux/kernel/git/lftan/nios2/+/f656d46bbb2d3ecdb71c998ccf657dccea8d19a9/arch/sh/include/cpu-sh4/cpu/cache.h).
The exact Toy startup order and invalidation behavior come from inspection
of the supplied original executable, not DreamShell code.

## The game uses logical drive state to end its music request

Toy's music updater at `8c049b4c` checks drive state. When no deliberate
pause is pending, a PAUSED response causes it to clear its music request
identifiers. The old pilot retained the ordinary reader's PAUSED drive
state while audio was being prefetched or exchanged between finite banks.
That can stop the title's own music lifecycle independently of an audible
bank fault.

The pilot-only scalar GD adapter now projects its logical playback into
the existing protected GD encoder. PLAYING remains drive `3`, audio `0x11`
through prefill, start wait, bank exchange, and accepted play/release
requests. Deliberate pause is `1/0x12`, end of track `1/0x13`, and fault `9/0x14`.
Position is bounded to the admitted immutable track. `DRIVE`, `REQ_STAT`,
and `GETSCD` refresh this state before producing responses. The ordinary
core retains data-request priority and validates output mappings; the
adapter reuses that map for the two-byte subcode status update.

An abort of the outstanding audio handle revokes its mailbox with STOP.
Unrelated, invalid, consumed, or data handles do not revoke audio. INIT
and RESET publish reset. The retained first-EXEC completion acknowledges
mailbox acceptance, rather than waiting for hardware application. That
acceptance correction was already in Handoff-Fix; it is not claimed as
a newly discovered cause of the latest failure.

Host tests run the title's end predicate and the actual adapter with the
ordinary core. They cover pending commands, terminal states, cancellation,
aliases, truncated buffers, protected outputs, and position bounds.
Recovery review also found that a repeated GD `EXEC` after `GETSCD`
completion, but before `CHECK`, could overwrite the completed audio status
with the adapter's unavailable default. The correction patches subcode only
on the call that executes the pending request. Its regression repeats EXEC
while the terminal handle remains owned and verifies both the completed
response and output-mapping count remain stable. This is a distinct protocol
defect, not proof of the instruction reached during the console hang.
Protocol values are independently documented in the
[KallistiOS syscall contract](https://kos-docs.dreamcast.wiki/syscalls_8h_source.html).

## Queue consumption can be hidden by native slot reuse

The admitted ARM driver dispatches a packet before clearing its command
and marker. Native SH-4 queue publishers refuse occupied slots, but may
reuse a cleared slot before the worker observes it. Waiting solely for
the submitted slot's low header bits to become zero can therefore miss a
completed dispatch and eventually report a queue fault.

The worker retains all four submitted packet words and its driver/request
generations. A single masked observation acknowledges a cleared or changed
packet, including native reuse. An unchanged packet does not acknowledge
dispatch. Generation and finite-bank checks remain required; packet reuse
never authorizes a write to an active sound bank.

The playback regression now uses realistic native traffic that reuses
every pilot slot. It retires four banks and 58,800 programmed source
frames to EOF, including equal opcodes with different packet payloads,
partial header clearing, unchanged packets, and stale generations. The
same native-reuse fixture fails against the previous worker at the queue
fault, establishing a meaningful regression rather than a test that only
mirrors the new condition.

## Service now surrounds the original updater

The main updater bridge visits the worker both before and after the
original SDK updater. Its pre-visit preserves the original arguments,
caller stack and exact SR; its post-visit preserves the SDK return value.
Both visits retain the existing private-stack guard and reentrancy gate.
There is no new interrupt refill worker or timer programming.

At a simulated 60 Hz main loop, the first ten frames read 94,080 raw bytes
with two visits versus 47,040 with one. The fixture's maximum handoff gap
falls from 104,500 ticks to 52,416 ticks, and its EOF time falls from
1,786,284 ticks to 1,447,990 ticks. These are host-model results for the
existing finite-bank algorithm, not measured console performance. Hardware
looping remains disabled. The original SDK updater still contains waits
without deadlines, and two opportunities are not a guaranteed deadline.

## FMV pause retry needs SDK retirement and a native stack

The title's pause helper retries native result `-13` without returning to
the normal main loop. The native VBlank path also has a retirement pump;
inspection does not establish that VBlank was absent during the reported
freeze. The correction supplies one additional, guarded retirement
opportunity per returning busy attempt, after a bounded worker visit.
It leaves native ownership and the original pause result intact.

The wrapper checks the exact context size, work-record count, wrapper
table, GD table entries, idle/busy flag, and control-record ownership. It
manually pumps only the first control record, with no record completion callbacks.
An aligned, validated pending file record defers the manual pump and returns
with exact SR restored so the existing VBlank handler can retire it. Unknown
records or callbacks are refused; the adapter never calls a file completion
callback. Deferred attempts retain the same liveness budget.
The original error callback is admitted only when its own dynamic callback
branch is disabled (`*8c0a7590 == 0` or `*8c0a759c != 0`). Unsupported
ownership or callbacks cause a terminal report before the pump is called.

A second independent review found that simply calling the native pump on
the worker stack was incorrect: the native CHECK veneer creates its output
buffer on that stack, while the GD map deliberately rejects the protected
worker reservation. The corrected assembly helper borrows the saved
original game caller stack for the native SDK call, then restores the
worker stack. It validates a 192-byte span below that stack, with 64 bytes
of margin over the reviewed 128-byte native depth, and leaves the protected
map unchanged. The shared bridge remains claimed throughout the call.
This preserves the native scratch-buffer contract without granting guest
access to the worker.

CHECK status translation was reviewed through the actual native veneer;
SDK status values are not assumed equal to GD status values. Native code
continues to perform handle checking and ownership retirement. The adapter
does not clear owners or fabricate completion.

Returning retries have a 65,536-attempt and nominal one-second budget. The
attempt limit is increased to avoid an unnecessarily early refusal during
fast file deferrals, but does not guarantee a VBlank opportunity. A stopped
timer still cannot make returning attempts repeat indefinitely.
Blocked-exception entry, a clock discontinuity, unsupported ownership,
unsafe callbacks, invalid caller stack, or post-pump context corruption
produces a distinct numerical detail. Exact SR is restored before the
terminal fault callback. A budget cannot preempt an unrelated original
game call that itself never returns; this build does not claim all native
game waits are bounded.

## Verification and remaining hardware work

The repeatable command is `make -f Makefile.toy_pilot test`. Eleven suites
run with AddressSanitizer and UndefinedBehaviorSanitizer. In addition to
the new regressions, they retain bus deadlines, tracked sound ownership,
driver admission, raw-GDI admission, deferred file ownership and VBlank
retirement, stopped-clock retry limits, active-bank exclusion, missed-active
finite retirement, generation handling, and exact SR checks.

Packaging audits the actual linked ELF files, export ABI, embedded blobs,
integer instruction restrictions, protected layout, and compiler stack
reports. The scalar GD adapter's reviewed callback surface is counted in
the conservative GD-stack sum. The native pause pump borrows the game
stack; compiler sums for authored worker code do not certify all original
SDK internals. Package metadata records the measured linked sizes and
available margins rather than treating model counters as an audio PASS.

The report expands to five pages. Snapshot version is `3`, size is
288 bytes, and the original 64 word offsets stay unchanged. The final
page adds pause attempts, pumps, actual owner retirement, detail,
maximum retries, admitted cache mode, and service visits per updater.

One combined Lifecycle-Fix build is delivered. The useful hardware checks
are a natural intro and sustained music/effects, followed by one fresh
boot that skips the intro with Start. The ordinary runtime, reader backup,
image and card remain the same; repeating the earlier reader-profile
matrix is unnecessary. See the [installation and report legend](../cdda-toy-pilot-test.md)
and the separate [comparison/provenance review](../cdda-toy-dreamshell-comparison.md).

The new build is not yet tested on a physical Dreamcast. Slow FMVs,
uninterrupted audio, reset behavior, and the exact reported freeze remain
hardware questions. The changes address independently identified defects;
they are not a declaration that retail CDDA is finished.
