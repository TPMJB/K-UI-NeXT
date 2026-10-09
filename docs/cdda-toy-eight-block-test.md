# Toy Commander eight-block CDDA test

This is the first architecture experiment after the working `372b435abd26`
build. It keeps the original stereo PCM16 at 44.1 kHz, the same 128 KiB sound
allocation and the existing SCI reader. It divides the 32,768-frame hardware
loop into eight logical refill blocks of 4,096 frames. The hardware channels
still start together and loop over the full allocation.

A played block can become writable after approximately 92.9 ms. Its ideal
time until reuse is approximately 650.2 ms, compared with 371.5 ms for the
previous half-ring design. This creates an earlier refill opportunity; it
does not add buffered audio or card bandwidth. Actual write permission still
uses conservative cursor evidence and a five-millisecond copy reserve.

The prior build is pinned separately as `cdda-console-baseline-372b435-20261008`
at source checkpoint `56df22966bbb9093374372f8dad7a2e09a05fcb7`. That checkpoint
adds evidence without changing the `372b435abd26` production sources. Keep the
previous `K-UI-CDDA-Toy-Overhaul.zip` as the fallback. This experiment has its
own ZIP and runtime filename.

The unchanged cursor proof requires two changed publications per channel,
both in the same 16,384-frame proof half, within 50 ms and at most 64 frames
apart. Logical block retirement follows the slower channel. The deadline
for refilling a particular block protects against the faster channel and
the age of the justified capture bound. An observation can cross several
logical block boundaries; every crossed block must have the expected source
position and committed stereo data. Modulo block indices alone grant no
ownership.

This build retains the fixed two-sector GD data steps, one-sector raw audio
callbacks, four-quantum refill cap and nominal 16 ms admission budget. It
retains CRC checking, mandatory card-stream closure and bus release after
each callback. The Start/menu and native CHECK mapping corrections remain.
SCI transport prototypes are evaluated separately so this comparison can
measure the block design with the existing transport.

Each refill quantum completes at most one source sector. A sector split
across two logical blocks can finish its retained fragment in the same
quantum; both fragments repeat the ownership checks. The worker also makes
one additional bounded observation at entry and one after refill work.
These increase chances to collect fresh evidence without changing what
evidence grants ownership. Their elapsed time is charged to the existing
service-entry admission clock; there is no additional card read allowance.

The build checks do not establish audible continuity. This candidate has
not run on the console. Persistent insufficient service or stalled game
paths can still require recovery or leave the hardware replaying old PCM.
Video freezes remain an unresolved observation.

All 22 host regression suites pass, as do the linked SH-4 layout and stack
audits. The asynchronous model passes 72 of 72 comparison schedules versus
54 for the fallback, retaining every fallback success. Its expanded sweep
passes 120 of 144 schedules; all 24 remaining 20 Hz schedules still require
conservative recovery. Optimized and ASan/UBSan results agree. Independent
checks cover exact source/stereo PCM, physical write destinations, block
boundaries, deferred transfers and the full-ring STOP fence. These are
synthetic results, not a console playback claim. See
[cadence evidence](evidence/cdda-eight-block-cadence-2026-10-08.md).

## Install one file

With the Dreamcast powered off:

1. Keep the known working ordinary reader and `372b435abd26` CDDA backups.
   Do not replace those backups with this experimental reader.
2. Keep the working 1.8.5 runtime at `/KUI/runtime.kui`, the original GDI,
   all 15 original track files and the existing card-path configuration.
3. Copy only `pilot/15-toy-eight-block-ring.kui` from this ZIP over
   `/KUI/apps/games/retail-boot.kui`.
4. Safely eject, cold boot with **SCI**, and launch the same original raw
   Toy Commander image with **A**, the standard reader.

Extracting the ZIP installs nothing. The converted 2048-byte image and X/Y
background readers are unsupported for this test and are refused before
the pilot is installed. The bundle includes no game content.

## Run the natural intro

Let the intro run without skipping it. Note music interruptions and whether
audio resumes, repeats or remains silent. Note video holds and loading time
separately. At the first menu, enter the report with **A+B+X+Y+Start** held
together. Photograph the build ID and all eight pages, **0 through 7**.
A worker fault enters the report automatically; an original-game hang can
still prevent the return callback. Photograph any refusal screen instead.

The pages repeat until power-off. Each page stays for 900 video frames,
approximately 15 seconds at 60 Hz. There is no numerical PASS target before
the console run. The first comparison should use the same natural intro as
the fallback, so extended gameplay does not add unrelated recovery counts.

## Read the eight pages

Each `WORDS` row has four hexadecimal values, in the order below. The worker
snapshot is **API 8**, still 448 bytes: magic `54595031`, version `00000008`,
bytes `000001C0`. Its word order matches API 7, but the bank fields now
describe logical 4,096-frame blocks. An old half-ring interpretation is
incorrect for this build.

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
| 4 | 1 | `pause_entries`, `pause_retries`, `pause_pumps`, `pause_retired` |
| 4 | 2 | `pause_detail`, `pause_max_attempts`, `cache_policy`, `service_visits_per_update` |
| 4 | 3 | `native_owner`, `native_work_token`, `gd_owned_command`, `gd_owned_token` |
| 4 | 4 | `native_gd_state`, `check_token`, `check_destination`, `check_result` |
| 5 | 1 | marker `47444D31`, fixed step `2`, `measured_read_steps`, `measured_sectors` |
| 5 | 2 | `read_ticks_total_modulo32`, `read_ticks_max`, `actual_sectors_max`, `timer_invalid_steps` |
| 5 | 3 | GD diagnostic `calls`, `requests`, `rejected`, `last_error` |
| 5 | 4 | four reserved zero words |
| 6 | 1 | recovery counts: invalid ports, phase beyond publication-age allowance, unobserved half, implausible progress |
| 6 | 2 | recovery counts: initial start proof, missing ready block, copy reserve, stream overflow |
| 6 | 3 | `data_blocked_calls`, `data_blocked_intervals`, `data_blocked_ticks_max`, `data_blocked_ticks_total` |
| 6 | 4 | `recovery_last_reason`, `recovery_last_gd_command`, `recovery_last_cursor_age`, `data_blocked_open` |
| 7 | 1 | `reserve_last_cursor`, `reserve_last_proof_age`, `reserve_last_sample_age`, `reserve_last_remaining` |
| 7 | 2 | `reserve_last_bank`, `reserve_last_bank_filled`, `reserve_last_fill_stream`, `reserve_last_site` |
| 7 | 3 | `raw_read_ticks_last`, `raw_read_ticks_max`, `raw_read_ticks_total`, `raw_read_timing_calls` |
| 7 | 4 | `reserve_last_bank_state`, `reserve_last_probe_age`, `reserve_last_window_ticks`, `reserve_last_window_calls` |

`state` is 0 off, 1 stopped, 2 prefill, 3 start wait, 4 playing, 5 paused,
6 end of track, or 7 fault. `fault` is 0 none, 1 configuration, 2 driver,
3 heap, 4 port, 5 queue, 6 card, 7 range, 8 clock, 9 stack, A generation,
B phase, C bus, or D native control adapter.

The expected driver is 20,740 bytes (`00005104`), CRC32 `70CCEEB2`.
`driver_verified` is cleared on shutdown; its zero on the return report alone
does not establish failed verification. `main_begin` is `8CFD0000`,
`main_end` is `8D000000`, and `worker_end` must be at most `8CFE0000`.
The expected cache-policy value remains `00000101`.

| Field | Meaning in API 8 |
|---|---|
| `bank_fills` | Completed stereo 4,096-frame logical blocks, including prefill |
| `bank_starts`, `hardware_loops` | Full-ring paired hardware START submissions |
| `bank_ends` | Logical block retirements established from the slower channel |
| `retired_frames` | Guarded source progress, capped at the finite requested end |
| `filled_frames` | Committed source PCM frames, excluding zero tail padding |
| `reserve_last_bank` | Denied logical block index, 0–7 |
| `reserve_last_bank_filled` | Committed stereo frames in that block, out of 4,096 |
| `reserve_last_remaining` | Frames from accepted fast progress to that block's next absolute use |
| `reserve_last_window_ticks`, `reserve_last_window_calls` | Time and inclusive visits since observed retirement of that target block; initial START anchors primed slots |
| `active_bank_writes` | Retained legacy field, expected zero; not an independent hardware overwrite detector |

Block state is 0 EMPTY, 1 FILLING, 2 READY, 3 START_WAIT or 4 PLAYING.
The reserve window is specific to the denied target. It is not physical
entry time and does not reset with an ordinary renewed cursor proof.
Capture-bound age, remaining frames and filled frames describe the denied
gate's inputs, which can survive a deferred or superseded STOP. They are
not another successful recovery count.

Recovery counts increment only after a successful internal STOP submission.
Intentional controls and finite EOF are excluded. Reason 3 retains the
unchanged half-ring observation-gap/delta bound; smaller refill blocks do
not justify guessing a missed hardware revolution. Reason 6 now refers to
any crossed logical block with missing data or an incorrect absolute source
position. A change in reason alone does not establish smooth playback.

Times use the existing admitted TMU0 profile, nominally 1.28 microseconds per
tick; totals wrap modulo 32 bits. Raw timings measure the physical callback
only, including failed live reads and excluding retained sector remnants.
Page 5 measures completed fixed-step GD service reads, not movie duration.
Data-block spans include time between worker visits and are not SCI-busy
measurements. These values are diagnostics, not audio-quality scores.

## Ownership and compatibility

All eight logical blocks are primed before START. Both channel planes must
commit before block progress advances. Partial sectors retain their exact
source LBA and generation across block boundaries and deferred transfers.
Every actual plane copy repeats the generation, sound-epoch, logical target
and conservative deadline checks.

An expired or ambiguous deadline requests STOP. Refilling after recovery
waits for consumed commands, the full-ring interval plus the 20 ms guard,
and fresh inactive-port evidence. Controls revoke pending source work;
they do not make live sound memory writable immediately.

The tracked 192 KiB main lease contains the worker in its lower 64 KiB;
the upper 128 KiB remains padding for native scratch. The pilot uses ports
62 and 63, the game's existing ARM driver and a tracked 128 KiB sound-heap
tail allocation. It retains the original effect mixer. These are exact-title
contracts. There is no independent IRQ refill worker or ARM watchdog.

The source snapshot and linked evidence in this ZIP identify the exact
candidate. See `build.json` for the build ID, configuration and audits.
The hardware result must be compared with the pinned fallback before this
candidate is treated as an improvement.

A separate SCI cache prototype and its six-configuration sanitizer log are
included under `experiments/` and `evidence/`. It preserves a trailing
CRC-checked physical block for each audio/data owner. In a synthetic interleave
it reduced total physical block reads from 434 to 403. It is not linked into
this runtime: its state needs a separate proven memory lease before integration.
The reproduction and limits are recorded in
[SCI cache evidence](evidence/cdda-sci-interleave-cache-prototype-2026-10-08.md).
