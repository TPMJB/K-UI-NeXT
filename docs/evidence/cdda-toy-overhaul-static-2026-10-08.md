# Toy CDDA overhaul audit

Review date: 2026-10-08 UTC. This is an audit and consolidated correction of
the independently authored exact-Toy Commander pilot. The console tested
build `0e60738ef36c`; it failed before gameplay. This document supersedes the
earlier lifecycle review's dynamic-delegate refusal. The earlier review is
retained as a record of the implementation that failed.

## What the console established

The five supplied photographs show the same build identifier. The decoded
report is:

| Page | Row 1 | Row 2 | Row 3 | Row 4 |
|---|---|---|---|---|
| 0 | `54595031 00000003 00000120 00000007` | `0000000B 00000004 00000004 00000001` | `00010000 00000000 00000000 00000000` | `00000000 00000000 00000000 8CFD0000` |
| 1 | `8D000000 8CFD77E0 00000324 00000000` | `00005104 70CCEEB2 00000001 00000000` | `00000000 00000001 00000001 00000000` | `00000000 00000000 00000000 00000000` |
| 2 | four zero words | four zero words | four zero words | four zero words |
| 3 | `00000000 00000000 00000000 00000001` | four zero words | four zero words | four zero words |
| 4 | `00000002 00000001 00000000 00000000` | `00000006 00000001 00000101 00000002` | four zero words | four zero words |

The expected 20,740-byte driver was verified, native SDK initialization
returned success, and the cache policy was `101`. No sound allocation, raw
audio read, sample copy or bank START occurred. Our pause adapter rejected
its first native busy retry with detail `6`, before any manual SDK pump.
The old worker encoded that adapter failure as stereo phase fault `B`, even
though no stereo voice existed. This is a demonstrated authored startup
regression, not evidence of a failed sound driver or faulty card.

## Startup callback admission

The original title registers the global error handler `8c04d542` during
startup. Its default delegate is `8c04d53e`, with the active flag clear. The
old adapter rejected that ordinary registration without an error occurring.

The exact global handler takes the dynamic delegate only for errors `-23`
or `-33`. The admitted scalar GD CHECK contract reaches neither:

| GD CHECK result | Native completion/error |
|---|---|
| completed `2` | completion `1` |
| processing `1` | progress `4` |
| missing `0` | retirement `0` |
| failed `-1`, class word `1` | error `-28` |
| illegal handle, class word `5` | error `-32` |
| reentry `4` | deferred `67` |

Error `-33` requires failure class `6`. Error `-23` requires class `2`,
sense `58`, and a particular drive response. The actual reader emits class
`1` for its failures and class `5` for an illegal handle. The admitted native
control pump's other local errors are `-19` and `-28`. The original global
handler returns directly for those errors. This domain proof justifies
removing the registration veto; it does not justify arbitrary callbacks.

The corrected adapter retains exact wrapper/GD table identity checks,
native context bounds, record callback exclusions, file-owner deferral,
borrowed native scratch-stack admission, post-pump ownership validation,
exact caller SR restoration and retry limits. It does not install a callback
shim or mutate the title's error delegate. Native-control faults now have
their own code `D`; detail `6` is reserved and is no longer emitted.

## Command ownership and logical source

The mailbox previously retained only the latest opcode and its parameters.
PLAY followed by PAUSE before a worker visit discarded the new source or
repeat policy. RELEASE could then reject the command or resume the prior
track. The new mailbox retains the accepted PLAY generation, source range
and repeat policy independently of subsequent PAUSE/RELEASE parameters.
Selection changes do not release a physically owned bank. A newer source
never takes its resume cursor from the previous source's voice.

RESET cancels the logical selection, including when RELEASE supersedes it
before worker service. Physical bank ownership still retires through the
ordinary STOP and finite-interval proof. STOP retains its deliberate resume
selection; RESET is a different lifecycle boundary.

Native startup may PAUSE or RELEASE before selecting audio. These controls
are admitted and apply without sound allocation, G2 access or raw reads
while quiescent. Scalar idle application runs before the title's sound-ready
flag gate because it does not need that flag or touch sound hardware.

The snapshot exposes the latest logical selection to DRIVE, REQ_STAT and
GETSCD even when another control supersedes PLAY before worker application.
Source-free RELEASE is drive `1`, audio `15`; its applied STOPPED state is
also explicitly projected. Previously the pending RELEASE produced `3/11`
and the later STOPPED projection retained that false PLAYING drive state.
The immutable map still validates and clamps audio positions; idle controls
preserve the ordinary data-session position.

GD completion continues to mean mailbox acceptance. `applied_generation`
records worker application. The GD path stays scalar, leaf-audited and on
its protected serialized stack; it never services sound or native SDK code.

## Native movie pause and intentional service gaps

The exact native PAUSE endpoint returns `0` after GD submission/acceptance.
The title retries only `-13`; on `0` it can proceed immediately toward a
movie. The old success branch gave the worker no visit, so a movie could
begin while an authored STOP remained unapplied.

On native `0`, the adapter now services the worker until the accepted PAUSE
generation is applied, or exits if a newer command supersedes PAUSE. It does
not add a native SDK pump on the successful path or alter the native return
value. A nominal one-second deadline is checked after each bounded worker
visit and may overrun by that visit. A separate 1,048,576-visit
cap supplies a stopped-clock escape; the older 65,536 native-busy retry cap
is deliberately not reused for a full finite-bank retirement interval.
Idle startup PAUSE takes one visit and performs no sound work.

Direct GD PAUSE commands retain an intentional-pause fence independently of
a later RELEASE. A long movie can suppress main service beyond the ordinary
ten-second clock limit. At that intentional boundary, the worker retains
all bank ownership and restarts a full finite observation interval and STOP
deadline. A possible clock reset is never treated as elapsed playback.
Consumed START/STOP packets and both inactive ports remain required before
bank reuse. An ordinary playing service gap or timer reset still faults.

A PAUSE captures its resume position once. Repeated PAUSE and RELEASE while
that PAUSE remains outstanding preserve it, rather than replacing it with
the old bank's terminal cursor after a movie. A new START clears that control
history. The success-side drain establishes actual paused ownership before
the native helper returns; the raw GD fence covers other accepted controls.

## Sound setup and efficiency

The old state machine waited for each of nine setup packets on separate
worker visits. This imposed avoidable setup latency at finite-bank exchange.
The revised setup publishes at most eight Close/Open/Pan/Volume packets in
one bounded burst. Its aggregate check is 1,563 TMU ticks between individual
bounded publications; it can exceed that check by at most one publication.
Queue BUSY retains the last successful step and acknowledgement.

The sound processor consumes its queue in order. Consumption of the final
configuration packet acknowledges the earlier configuration packets. A
later worker visit must observe that consumption and validate both finite
templates before publishing START. A partial setup can be superseded, and
generation checks prevent an obsolete START. This does not infer that
published packets were applied synchronously.

Three demonstrated hot-path reductions accompany those changes:

- The raw buffer holds at most two 2,352-byte sectors. Its size is reduced
  from 8,192 to 4,704 bytes, saving 3,488 bytes of BSS. Its alignment and all
  source accesses are retained.
- First START acknowledgement performed the same ten-word, two-port template
  validation twice. Keeping the common validation removes twenty redundant
  sound reads for that acknowledgement.
- An aligned sample-plane word previously used four byte loads plus shifts
  and ORs. A constant-size builtin copy is alias-safe for the `int16_t`
  sample plane and compiles to an aligned SH word load and word store, with
  no memcpy call. FIFO drains and maximum 32-byte bursts remain unchanged.

The unused SEEK constant, unreachable action branch and write-only mailbox
parameter copy are removed; request parameters remain in the report. The
large stack guard remains: its bottom-up scan is an exact watermark, and
the proposed cached or reversed alternatives did not preserve that evidence
or demonstrate useful savings.

The final raw-read scope now rechecks the resident card owner and pending
data-read opcode under its existing mask. An intervening data request is
deferred before invoking the raw reader; its priority refusal is no longer
misreported as a fatal card error. This is a permitted-interleaving defect,
not a cause established by the supplied console photographs.

## Verification scope

`make -f Makefile.toy_pilot test` runs thirteen suites with strict warnings,
AddressSanitizer and UndefinedBehaviorSanitizer. The new startup regression
uses the real GD core and scalar adapter with an independently authored
native status/callback model. The old startup guard fails it. The new flow
suite connects the production worker request/service, GD adapter, GD core
and pause adapter in one fixture. Queue publication and ARM application are
separate events; the old idle RELEASE projection fails its regression.

Coverage includes idle controls and status responses before/after service,
accepted versus applied generations, new PLAY selection across PAUSE and
RELEASE, repeat policy, RESET cancellation, finite EOF, two-bank ownership,
delayed START application, partial queue BUSY, setup supersession, slow
publication, setup clock reset, long movie gaps, saved pause cursor, actual
native-success pause retirement and data-priority interleaving. Focused bus,
lease, mapping, callback ownership, stack and deadline regressions remain.

The package checks the final SH ELF images, unresolved symbols, memory fit,
export ABI, instruction restrictions, pure GD leaf calls, protected base/map
callback origins, borrowed native stack helper, embedded binary identity,
compiler stack sums and complete source snapshot. No validator is weakened
to admit the revised code. Exact final measurements are in `build.json`.

The working 1.8.5 runtime and ordinary reader source are unchanged. No game
binary, ARM binary, private instruction listing, or SWAT implementation is
included in source or delivery. The retained provenance review describes the
earlier separate comparison; this is not a claim of a formal clean room.

These tests execute host models and the authored code, not original native
SH/ARM instructions or physical Dreamcast hardware. Compiled checks do not
prove music was audible, stereo, gap-free, or that the original movie paths
will complete. The new console build has not yet been tested on hardware.
