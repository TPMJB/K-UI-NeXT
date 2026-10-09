# Toy Commander finite handoff and terminal reporting review

The latest user-reported result for the bounded-bus candidate was roughly
one second of recognizable CD audio followed by silence, then a freeze when
ABXY+Start was attempted. This establishes that some audio was audible; it
supplies neither the latched worker fault nor the stalled program counter.
The changes below correct verified code paths. Their relationship to that
particular console failure remains to be checked on hardware.

## Missed finite active interval

Each stereo bank contains at most 16,384 frames, about 371 ms at 44.1 kHz.
The ordinary game sound updater controls how often the pilot runs. The pure
ownership model already allowed retirement of a pending bank when service
missed its entire active interval. The worker adapter instead faulted with
`PHASE` whenever it had not sampled both active flags, even after confirming
consumption and waiting a full post-consumption finite interval.

The adapter now permits that same conservative retirement only with all
of the following evidence:

- The submitted paired START packet was consumed.
- Both port templates still match the leased bank, PCM16 format, sample
  count and pitch; hardware looping is disabled.
- A full nominal finite duration plus the existing 20 ms guard has elapsed
  since consumption was observed. A timer reset or gap over ten seconds
  remains a clock failure.
- Both port-active flags are zero.
- The current mailbox and ownership generations match while retirement
  commits under the exact saved/restored SR mask.

It does not shorten the finite duration, ignore a live port, relax template
checks or write a pending/playing bank. Retirement advances source progress
and applied generation. `started_observed` still counts only actual paired
active observations; `retired_frames` is safe programmed-frame retirement,
not proof that all those frames were audible. A service gap can still cause
silence between banks. Audio quality and FMV performance remain unresolved.

## Return before teardown and report on worker fault

Private inspection independently confirmed an unbounded global sound-stop
retry in the title's reset callback, before the prior BIOS report route.
The exact-title installer now publishes the validated low terminal hook as
that callback's source before startup registers it. The original reset
decision remains intact. See the separate
[reset contract review](cdda-toy-reset-static-2026-10-08.md) for admission,
registration order, ABI and cache-publication evidence.

Worker config ABI 3 appends the generated low terminal entry, increasing
config size from 48 to 52 bytes. Export size remains 68 bytes and telemetry
remains version 2, 256 bytes. Worker initialization rejects odd entries and
entries outside `[0x8c004000, 0x8c007800)`, excluding the guarded report stack.

Every worker-service exit reaches a common fault-dispatch boundary after
bounded helper scopes restore SR. A latched fault transfers once to the low
terminal hook. The hook masks BL/IMASK, selects its separate low stack and
copies scalar telemetry before resetting the mailbox. It paints the first
page before any display pause and performs no SDK teardown, queue operation,
card read or sound-heap free. GD request and snapshot entries remain pure
RAM leaf functions and never invoke this report callback.

This is not an interrupt-independent watchdog. An original updater that
never returns can still prevent worker fault dispatch and the game reset
decision. No such hardware instruction is claimed identified by these
static fixes.

## Other hypotheses reviewed

Global all-port stop calls in the admitted title belong to initialization
and shutdown; the ordinary updater's stop uses a single-port mask. A
routine all-port revoke after one second was not established. The G2 DMA
completion handler clears channel enable; no verified permanently enabled
normal channel was found. The conservative enabled-or-started DMA deferral
is retained. Queue, bus and clock deadlines remain unchanged.

## Verification scope

Host regressions exercise the actual worker with authored mapped register,
queue and sample-copy fixtures; no game executable or driver is embedded.
They cover normal finite playback, a missed active interval and consecutive
bank progress, inactive-before-duration refusal, active-after-deadline
failure, unconsumed START, changed template, superseding generation and
exact SR restoration. Existing idle, bus, tracked lease, GD progress,
ownership, driver-load and boot/reset admission suites pass with strict
warnings, ASan and UBSan. The same playback fixture compiled against the
previous worker fails at the missed-active retirement assertion, confirming
that it distinguishes the corrected defect. The repeatable command is:

```sh
ASAN_OPTIONS=detect_leaks=0 make -f Makefile.toy_pilot test-playback
```

The separate SH build and packager audit executable fit, guarded stacks,
integer-only instructions, pure GD leaf entries, actual embedded binaries,
generated low symbol bindings and source/package identity. The low resident remains at `0x8c0077e4`, below its guarded stack; the
worker ends at `0x8cfd6ee0`. Conservative authored stack sums are 1,200 of
1,232 bytes on the GD path and 2,664 of 8,096 bytes for the worker, excluding
original SDK, IRQ and ROM frames. Historical reader builds and original
inputs remain outside this update. The release
record contains final compiler/test/package results; this document does not
claim a successful hardware run of the new build.
