# Asynchronous CDDA scheduling result — 2026-10-09

The asynchronous raw-sector trial is **unreleased**. It passes receipt,
publication and cancellation checks, but fails the sustained delivery
requirement under a plausible unchanged sound-updater schedule. No Dreamcast
binary from this trial was offered for installation.

The enabled path uses one private 2,352-byte staging sector and the existing
worker input sector. IRQ and GD CHECK/EXEC entries can finish reception, but
only the verified main-context sound worker can consume the sector and request
the next LBA. After accepting a completed sector, its next fill quantum starts
another receive and returns PENDING. That ends the worker's refill burst.

## Independently timed scheduling test

`tests/test_toy_pilot_sci_async_audio.c` supplies ideal independent DMA/IRQ
progress between worker polls. It requests the next LBA only after the current
sector is delivered, matching the production worker's lifecycle. All delivered
bytes are checked against the raw-sector oracle, and physical card leases must
balance. The timing is a host model, not a measurement of console playback.

| One-second schedule | Verified sectors delivered | Required sectors |
| --- | ---: | ---: |
| Two polls 99.84 microseconds apart per 60 Hz update | 60 | 75 |
| Two clustered polls per 80 ms update | 13 | 75 |

CDDA requires 44,100 stereo frames per second, or 75 sectors of 588 frames.
At 60 delivered sectors per second, the deficit is 8,820 frames per second.
Even an ideally full 32,768-frame ring can hide that deficit for only about
3.7 seconds. The ring remains eight 4,096-frame blocks; its total duration is
about 743 ms. Existing cursor/protected-write proofs may stop playback earlier.

The preceding console result recorded a maximum worker gap around 80 ms.
That maximum does not establish the average updater rate or predict the exact
audible result. The host test establishes an architectural limitation under
the stated schedules, which is sufficient to reject this version for tonight's
console trial.

## What did pass

The async engine fixture checks complete CRC-verified publication, independent
LBA/generation/output replacement, partial-DMA cancellation, DATA/RAW handoff,
CRC retry and PIO fallback, token budgets, and timer admission limits. Its
final run completed 1,914,062 checks with ASan/UBSan.

The separate fixture using the actual SCI receiver and independently timed
MMIO wire arrivals completed 529,627 checks with ASan/UBSan. It exercises
packed/fragmented sectors spanning five or six physical blocks, cancellation,
replacement and CRC faults without a normal full-payload DMA wait.

The physical PCM worker fixture verifies pending calls leave PCM and completion
counters untouched, exact SR restoration, lifecycle revocation, stereo frame
ordering and cached tails at 4,096-frame block boundaries. These correctness
checks do not prove sustained delivery or hardware video performance.

The complete 30-suite host runner passed with ASan/UBSan before integration
and again on the merged source. The async SH build and linked audit passed;
the worker ends at `0x8cfdd040` within its lower 64 KiB reservation. Conservative
emitted stack margins are 64 bytes for the low hook, 336 for SCI IRQ, 2,220 for
GD and 3,240 for audio. The separate bridge mutation run rejected 46 changes.

## Integration decision

Preserve the trial behind `ASYNC_CDDA=1`, which requires `SHARED_SCI=1` and
`GD_FIXED_STEP=3`. Both SCI experiment flags default to zero; the isolated Toy
target defaults to `GD_FIXED_STEP=2`, matching the delivered `7b55156` profile.
Keep the
hardware-confirmed eight-block synchronous audio path as the default and merge
the fixed launcher separately. The integration checkpoint installs unchanged
`7b55156aafa2` game-runtime bytes and unchanged `6c4a9915ba33` launcher bytes.

A future async attempt needs enough bounded read-ahead capacity or a separately
proved safe worker admission point. Calling the sound worker directly from the
serialized GD path would bypass its existing BL/resident-active admission and
verified main-context assumptions. Those gates were retained in this trial.

The earlier unused `GD_FIXED_STEP=0` default places the resident BSS end eight
bytes beyond its fixed stack boundary with the current toolchain. Its BSS size
is unchanged: larger code shifts the aligned BSS start 32 bytes later. Selecting
the actual delivered two-sector profile restores the admitted layout without
changing any memory or stack boundary.

The rebuilt merged `GD_FIXED_STEP=2`, `SHARED_SCI=0`, `ASYNC_CDDA=0` worker is
byte-identical to the delivered clean worker: SHA-256
`f3b510df3034634e01e3dd911711f94551062534d6cbf4b635900a366f903d3a`.
