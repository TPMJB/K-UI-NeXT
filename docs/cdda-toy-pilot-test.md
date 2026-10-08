# Toy Commander finite stereo CDDA pilot

This build removes every new pilot call into the game's unbounded sound
helpers. It uses bounded stable reads, sample copies, packet publication and
allocation-canary writes. A genuinely stopped, paused or finished pilot does
no sound-bus access or sound allocation. This directly addresses a verified
way the pilot could hang with interrupts masked; the latest recording does
not supply the stalled program counter, so the observed freeze is not yet
proven resolved on hardware. See the [deep review](evidence/cdda-toy-bounded-bus-static-2026-10-08.md).

It retains the earlier correction to GD command progress. The hardware run
after that correction still froze; it was insufficient on its own.

The first GD `EXEC` now acknowledges a command accepted by the audio mailbox.
It still reports a missing snapshot, an existing worker fault or a cancelled
generation as failure. Completion means queued acceptance, not that music has
started or a pause/stop has reached the sound hardware. `applied_generation`
continues to report actual worker application. Sound work remains in the
normal game sound hook, on the worker's private stack.

The build also retains sound-driver verification at the successful file-load
boundary inside the game's initialization, before SDK installation. Build
`42bd06d32d16` checked the allocated buffer before it had been filled, so the
game could boot while CDDA remained disabled. The exact 20,740-byte driver
SHA-256 remains required; the SDK's alignment padding is excluded.

It also retains the raw-GDI launch correction: manifests may omit the optional
source CRC, but the exact loaded executable CRC and SHA-256 remain required
before installing hooks.

This is one new, manually installed Toy Commander pilot. It uses the game's
own ARM sound driver and tracked sound-heap records. It keeps the working 1.8.5
runtime and ordinary reader source unchanged. Use the same complete original
Toy Commander image and card that passed preflight 13; do not repeat tests
00–14 for this unchanged image.

The pilot tests whether disc music can play alongside the game's effects and
movie sound. It does not establish compatibility with another game or fix
the reported intro-video freeze and skip. Its two stereo banks use finite
playback, with hardware looping disabled. Each bank holds 16,384 stereo
frames, approximately 371 milliseconds at 44.1 kHz. Silence between banks is
allowed in this first experiment. A stalled service path cannot turn a
completed finite bank into a continuously replaying music loop.

Live GD audio-status, subcode and drive-position responses remain a limitation
of this first pilot. It does not refresh `GETSCD`, `REQ_STAT` or `DRIVE` while
music plays; the baseline subcode audio status remains 15, unavailable. An
initial applied request position and the pilot's own cursor telemetry do not
establish correct responses to the game's later position/status queries.

## Install one file

With the Dreamcast powered off:

1. Preserve your working ordinary reader backup. The earlier instructions
   used `/KUI/apps/games/retail-boot-before-observe.kui`; use that same known
   working backup if you retained it. If the ordinary reader is currently
   installed and there is no backup, copy it to that path first. Do not
   replace a working backup with a diagnostic reader.
2. Keep the working 1.8.5 runtime at `/KUI/runtime.kui`. Leave the game folder,
   its GDI, all 15 track files and the existing card-path configuration
   unchanged. This ZIP contains no game files and installs no configuration.
3. Copy only `pilot/15-toy-finite-stereo.kui` from this ZIP over
   `/KUI/apps/games/retail-boot.kui`. Extracting the ZIP by itself installs
   nothing on the card.
4. Safely eject, cold boot with **SCI**, and launch the same Toy Commander
   image through the normal Games menu with the standard SCI reader.

The launcher checks the known original boot/image identity before installing
the title-specific hooks. A refusal is a result to report; it is not an
instruction to alter the GDI or game files.

## Listen and play

Let the intro run without manually skipping it. Note whether its video and
sound stay smooth, whether a freeze eventually skips the video, and whether
the earlier repeating short sound returns. These observations matter even
if the game reaches its menu.

At the menu, listen for disc music. Start a short gameplay section that would
normally use disc music and trigger several sound effects. Listen for music
and effects together, missing effects, repeated fragments, silence, clicks,
or an unexpected change in volume. Let the music continue for at least a
minute so several finite-bank handoffs occur. Where practical, let a track
reach its end and note whether it stops or follows the game's next music
request. Do not treat periodic handoff gaps as a completed audio-quality
result.

Record the outcome in ordinary terms: intro behavior, menu music, gameplay
music, effects, and the character of any gaps or repeated sound. Counter
values alone do not establish that the output was audible or stereo.

Then hold **A+B+X+Y+Start** together and photograph all report pages, including
the build ID. If the reader stops earlier, photograph its failure screen and
every report page that follows. Keep the two sets distinct if you perform
another cold boot. There is no numerical PASS target before this pilot has
run on hardware.

## Read the four pages

The caption is **PILOT PAGE**. The displayed page numbers
are **0, 1, 2 and 3**. Each page has four `WORDS` rows with four hexadecimal
values per row. Pages repeat until power-off. A page remains for 900 video
frames: approximately 15 seconds at 60 Hz or 18 seconds at 50 Hz. A stopped
video scan can shorten this bounded display wait; this is not a wall-clock
timer calibration.

The exact word order is:

| Page | Row | Values from left to right |
|---|---|---|
| 0 | 1 | `magic`, `version`, `bytes`, `state` |
| 0 | 2 | `fault`, `generation`, `applied_generation`, `driver_generation` |
| 0 | 3 | `command`, `parameters[0]`, `parameters[1]`, `parameters[2]` |
| 0 | 4 | `position_fad`, `track`, `end_fad`, `main_begin` |
| 1 | 1 | `main_end`, `worker_end`, `stack_used`, `stack_fault` |
| 1 | 2 | `driver_bytes`, `driver_crc`, `driver_verified`, `sound_address` |
| 1 | 3 | `sound_bytes`, `sound_generation`, `service_calls`, `service_skips` |
| 1 | 4 | `service_gap_max`, `step_ticks_max`, `raw_calls`, `raw_bytes` |
| 2 | 1 | `raw_errors`, `copy_calls`, `copy_ticks_max`, `bank_fills` |
| 2 | 2 | `bank_starts`, `bank_ends`, `handoff_gaps`, `gap_ticks_max` |
| 2 | 3 | `start_waits`, `stop_waits`, `queue_errors`, `stale_actions` |
| 2 | 4 | `active_left`, `active_right`, `cursor_left`, `cursor_right` |
| 3 | 1 | `started_observed`, `finite_ends`, `shutdowns`, `sdk_init_result` |
| 3 | 2 | `retired_frames`, `filled_frames`, `queue_producer`, `queue_consumer` |
| 3 | 3 | `dma_busy`, `dma_suspended`, `hardware_loops`, `active_bank_writes` |
| 3 | 4 | `bus_last_result`, `bus_deferrals`, `updater_entries`, `updater_returns` |

The telemetry header is magic `54595031`, version `00000002`, bytes
`00000100`. An all-zero report means that valid worker telemetry was not
available; it does not mean that audio passed. The expected driver identity
is 20,740 bytes (`00005104`), CRC32 `70CCEEB2`; `driver_verified` records whether
the successfully loaded file matched before SDK installation. It is cleared
when the pilot shuts down, so a zero on the return report alone is not proof
of failed verification. The retained CRC and fill/start counts distinguish
that case. It is not a
readback hash of the installed ARM image. `main_begin` is `8CFD0000`, `main_end` is
`8D000000`, and `worker_end` must remain at or below `8CFE0000`.

`state` is hexadecimal **0**: off, **1**: stopped, **2**: prefill,
**3**: waiting for start, **4**: playing, **5**: paused, **6**: end of track,
or **7**: fault. `fault` is **0**: none, **1**: configuration,
**2**: driver identity, **3**: heap allocation, **4**: port ownership,
**5**: command queue, **6**: card read, **7**: track/range,
**8**: clock, **9**: stack, **A**: stale generation, **B**: stereo phase, or **C**: bounded bus failure.

The generation fields distinguish a requested command from the generation
the worker actually applied. The service, read, copy, fill/start/end and gap
counts describe the pilot's work; they are not sound-quality scores. Fields
ending in `ticks_max`, plus `service_gap_max` and `step_ticks_max`, use the
game's existing TMU0 clock units. This pilot does not reprogram that timer.
The worker stack includes a checked guard and watermark; the packaged
compiler stack sum covers authored code and an assembly allowance, rather
than the game SDK's internals.

`hardware_loops` and `active_bank_writes` describe operations this pilot must
avoid. `dma_busy` and `dma_suspended` are retained legacy fields and remain zero.
The worker never suspends or programs DMA; `bus_deferrals` counts refusals
including enabled/active G2 DMA and occupied queue slots. Photograph the actual values even when an error
has already occurred. Queue consumption, bank starts and observed active
channels do not establish audible stereo output by themselves.
`retired_frames` counts programmed source frames whose bank was safely
retired after the finite interval and completion evidence. It is not a count
of frames verified audible at the speakers.

A launch/installation refusal remains on its failure screen. An ordinary
reader failure can remain on its request-rejection screen instead of reaching
these four controller-return pages. Photograph whichever screen is visible;
there is no need to wait indefinitely for another page.

## What this build reserves

The candidate uses the game's first main-heap allocation: a 192 KiB lease at
`[0x8cfd0000,0x8d000000)`. Only its lower 64 KiB,
`[0x8cfd0000,0x8cfe0000)`, holds the worker; the upper part is padding around
the game's known top-of-RAM scratch references. The sound buffer is a
128 KiB fixed allocation registered in the game's tracked sound-heap tail.
Its canary is written and drained before publishing allocation metadata. The observed game
voice picker and updater are restricted to ports 0–61; the pilot uses ports
62 and 63. These are exact-title contracts, not a general reservation scheme
for retail games.

The pilot reads the game's existing timer without reprogramming it. It does
not upload another ARM driver, reset the ARM, retune timers, or replace the
game's effect mixer. It retains SCI framing and CRC checks. A read or sound
failure must be visible in the reports, and finite sound-bank playback must
remain bounded even if the game stops calling the worker.

Clock admission checks the known console profile without changing registers:
the low 12 bits of FRQCR are `0E0A`, TMU0's reload is `FFFFFFFF`,
`TCR0 & 0x0027` is `0x0002`, and TMU0 is running. The timing conversion assumes this
console's 50 MHz peripheral clock divided by 64, nominally 1.28 microseconds
per tick. Register agreement is not an independent oscillator calibration.
An incompatible profile produces a clock fault rather than retuning a timer.

Each new pilot G2 transaction has a shared 10,000-poll cap and 1,563-TMU0-tick
cap (nominally 2 ms in the admitted clock profile). It restores exact caller
SR on every return. Enabled or running G2 DMA causes deferral, with no DMA
register writes. Repeated unresolved deferrals fault after nominally one
second of active service. A packet whose command header was published is
never republished after a final-drain failure. Canary failure publishes no
heap record. Inactive-bank copies never authorize reuse of a playing bank.

`bus_last_result` is **0**: success, **1**: deferred busy, **2**: deadline or
poll-cap timeout, **3**: invalid argument, **4**: invalid installed state,
**5**: command already published when the final drain failed. `bus_deferrals`
counts busy observations. The updater entry/return counters surround the
original game updater; they are breadcrumbs, not a watchdog. A difference
can localize an unfinished original call only if a snapshot can still be
obtained. The report captures these fields before issuing a reset.

The game and its unchanged SDK still contain waits without deadlines.
Finite-bank playback bounds the pilot's music buffers. This change does
not establish that every original game path can recover, or fix slow FMVs.

## Restore

Power off. Copy the preserved working reader backup over
`/KUI/apps/games/retail-boot.kui`, keeping the backup. Keep the working runtime
installed, safely eject, and reboot. This is an experimental pilot rather
than an ordinary-use reader.

`build.json` identifies the published source commit and exact tree.
`SHA256SUMS` covers the package contents. The ZIP includes the complete
corresponding source snapshot, license notices, linked ELF/map files and
compiler stack evidence. Build checks establish linked fit and identity;
the listening and video observations establish what happened on this console.
