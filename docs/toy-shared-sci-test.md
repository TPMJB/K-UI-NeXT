# Toy Commander asynchronous CDDA SCI experiment — unreleased

This opt-in experiment for the original Toy Commander image is retained as
source for review. It is not a hardware candidate or the installed runtime in
`K-UI-Integrated-Checkpoint.zip`. That checkpoint restores the unchanged
clean-audio `7b55156aafa2` game runtime and the `6c4a9915ba33` launcher; it does
not introduce a video fix.

The integrated Toy build defaults match the shipped clean-audio policy:
`GD_FIXED_STEP=2`, `SHARED_SCI=0` and `ASYNC_CDDA=0`. The opt-in shared
experiments below require their separate three-sector bootstrap setting.

This experiment follows the `dc455cbfa50b` foreground top-up test, which recovered
the first shared transport's visual regression but did not improve the intro
over GD3. That run still spent about 4.42 ms inside each synchronous CDDA raw
callback. The async revision receives both game data and raw audio through the
shared SCI transport so the game can run while one raw sector is pending.
The synchronous reader handles installation and bootstrap before the worker
is installed.

The eight-block CDDA ring, original PCM, native sound driver, sound allocation,
STOP acknowledgement, cursor evidence and protected memory bounds are retained.
Host checks cover the ownership and delivery behavior. Scheduling review found
that the single staged raw sector cannot sustain the existing worker cadence.
The experiment is therefore withheld from console testing; functional host
success is not a sustained audio-throughput pass.

## Scheduling failure

A completed 2,352-byte sector contains 588 stereo frames. Each safe worker visit
can accept at most one such sector. After accepting it, the next LBA arms a new
transfer; the nonblocking poll cannot complete all of its physical card blocks
immediately. That pending poll advances no PCM, ending the worker's quantum
loop. Four nominal quanta do not create four ready sectors.

CDDA requires 75 sectors per second, or 44,100 stereo frames per second. Even
with ideal transport completion, two safe visits per 80 ms SDK update can
accept at most 25 sectors per second. Closely clustered visits can do worse.
Two clustered visits per 60 Hz update can accept at most 60 sectors per second
when the second visit precedes completion of the newly armed transfer. That
leaves a 15-sector-per-second deficit despite a faster update rate.

The 32,768-frame ring holds about 743 ms of audio when full. It can postpone
starvation but cannot repair a sustained delivery deficit. The finite-file
worker test uses independently spaced 8.333 ms visits, approximately 120 visits
per second; it verifies PCM order, pending-output protection, cancellation and
physical EOF under that schedule. It does not establish that the unchanged
clustered console schedule can sustain playback. No Dreamcast playback result
is claimed for this revision.

## Retained fallback

The following applies only to the integrated checkpoint with the retained
working runtime, not to an async candidate. With the Dreamcast powered off:

1. Preserve `K-UI-CDDA-Eight-Block-Test.zip` and the clean-audio
   `7b55156aafa2` game runtime. Preserve your ordinary 1.8.5 reader backup.
2. Copy the ZIP's `KUI` folder onto the SD card, replacing only
   `/KUI/runtime.kui` and `/KUI/apps/games/retail-boot.kui`.
   The launcher is the unchanged `6c4a9915ba33` install fix.
3. Keep the existing card-path configuration, original GDI and all 15 original
   track files. The ZIP contains no game or driver files.
4. Safely eject, cold boot using **SCI**, then launch Toy Commander with **A**.

The installed game remains build `7b55156aafa2`; the merged source checkpoint
has its own identity recorded separately in `build.json`.
If launch or playback regresses, restore the preserved clean-audio
`7b55156aafa2` game runtime at `/KUI/apps/games/retail-boot.kui` and cold boot
again. The unchanged `6c4a9915ba33` launcher can remain installed.

## Evidence interpretation for a future redesign

These instructions describe how to review transport diagnostics if a revised
scheduler is separately admitted for hardware testing. No async installation is
provided by this checkpoint. Let the intro run without skipping. Note whether visual holds shorten and
whether music stays continuous. At the first menu, hold **A+B+X+Y+Start** and
photograph the build ID and pages **0 through 7**. A short recording of the
intro is useful for comparing motion and sound together.

Pages 0 through 4 retain the existing CDDA report. Page 5 replaces the old
three-sector timing page with transport counters. Pages 6 and 7 retain the
CDDA recovery and reserve fields. Linked addresses and stack telemetry belong
to this build; interpret them against the included `build.json`.

Page 5 shows these hexadecimal counters in row order:

| Row | Column 1 | Column 2 | Column 3 | Column 4 |
| --- | --- | --- | --- | --- |
| 1 | Transport pump/service calls | SCI IRQ calls | Physical blocks consumed in calls | Physical blocks consumed in IRQs |
| 2 | Audio card claims | Audio claims deferred | Audio card releases | Data stream resumes |
| 3 | Transport errors | Retries | Token waits yielded | Blocks received by PIO fallback |
| 4 | Longest pump/service, ticks | Longest IRQ, ticks | Active data token | Card owner |

The block counters now include both game data and raw audio. Audio claim/release
counters describe physical raw-audio card leases, not every worker poll.
Card owner is 0 when idle, 1 for data and 2 for audio. Convert timer ticks at 1.28 microseconds per tick only when the
existing TMU0 profile admission succeeds. Photos alone do not establish
frame pacing; compare the visible intro and audible continuity as well.

The existing CDDA snapshot prefix remains API 8 and 448 bytes. The new shared
transport config is 68 bytes; it is internal to this isolated package.

The existing page 7 raw timing fields have a different meaning in this build:
they measure elapsed time from the worker's first sector request to complete
verified delivery. This includes game execution between visits. They are not
CPU blocking time and must not be compared with the earlier synchronous raw
callback duration as if they measured the same thing. Raw calls count completed
sector deliveries; repeated pending polls do not increment them.

## Transport limits

Accepted data submission, SCI IRQ and worker raw-audio polling retain their
one-step, nonblocking behavior. CHECK/EXEC calls also service raw audio when no
game-data handle is pending. Their foreground allowance is four verified physical
512-byte blocks since the preceding CHECK/EXEC; blocks already consumed by IRQs
or other entries reduce that allowance. The allowance includes either data or
raw-audio progress. Four physical blocks are not four
2,048-byte game sectors. The engine does not read ahead of the live request.

The foreground loop admits another finite stream step only while its elapsed
timer count is below 1,500 ticks (1.92 ms under the admitted TMU profile). An
independent 1,024-step ceiling bounds a stopped timer. These are between-step
admission limits: one stream step, command or recovery can cross the timer
limit, so this is not a hard 1.92 ms deadline. The complete external entry shares
one 256-byte token-search allowance. Immediate partial-block repair is allowed
only on the foreground path; IRQ, submission and worker audio polls defer it.
PIO fallback still receives one finite card block when recovery requires it.

An audio request identifies one 2,352-byte raw sector by LBA, mailbox generation
and stable destination. The transport stages its physical blocks privately;
pending calls expose no partial sector. Only the matching worker call copies a
complete verified sector into the PCM decoder's input. Cancellation invalidates
the request before fencing the receive, so stale generations cannot deliver PCM.

Raw audio claims the shared card only after game data reaches a verified block
boundary. Raw completion releases the physical lease immediately, allowing data
to resume before the worker accepts the ready sector. Data abort preserves an
independent raw request; audio cancellation preserves an independent data
request. Global reset revokes both before cleanup. There is no second SCI owner.

CMD18 idle and CMD12 busy waits each retain their 4,096-byte limit. Cancellation fences the existing
bounded DMA before stopping the stream; reset cannot hand an active receiver
to the synchronous reader. A timeout is a transport error rather than an
unbounded wait. These byte budgets do not guarantee a particular frame time.

Both preceding hardware reports are included. The first shared test recorded
many transport entries and retries; the top-up substantially reduced those
events but restored only the earlier visual performance. Its raw callback
timing motivates this experiment without proving that serialization is the
sole cause.

The worker's refill cadence and 16 ms between-quantum admission remain unchanged.
IRQ/GD service can finish a sector privately, but PCM decoding and publication
still wait for a worker visit. The preceding run's maximum worker gap was about
80 ms. The single-sector design fails the scheduling requirement above; the
transport's ability to finish a private sector cannot itself drain PCM into the
ring or admit another sector before the next safe worker call.

## Review and reproduction

The integration ZIP retains the complete authored source checkpoint, licences
and validation evidence separately from its installed known-good bytes. An
async cross-build, if made for review, has
separate guarded stacks for game-data calls and SCI interrupts, within the
lower 64 KiB worker reservation. The native SDK's upper 128 KiB scratch region
remains outside the worker. The low resident still ends at or below
`0x8c007800`.

The checkpoint packager checks the retained native package against the retained
launcher's actual C layout gate, including malformed packages with valid outer
checksums.
The linked audit checks integer register preservation, the fifth GD argument
across stack switching, independent stack reservations, relocation blobs,
exact title/driver admission and absence of authored FPU use. It also records
the bounded service policy and verifies the linked service entry's caller scope.
The audio audit binds the worker's enabled path to the asynchronous raw API;
the old low synchronous callback remains available to the retained bootstrap
and compatibility paths.

```sh
python3 tools/test_toy_pilot.py
make -f Makefile.toy_pilot BUILD=build/shared-sci-async-cdda SHARED_SCI=1 GD_FIXED_STEP=3 ASYNC_CDDA=1 all
```

These commands reproduce the opt-in experiment for review with the SH-4
toolchain; they are not hardware installation instructions. This is an unpublished local checkpoint; the package records its
exact source identity without claiming a public repository push.
