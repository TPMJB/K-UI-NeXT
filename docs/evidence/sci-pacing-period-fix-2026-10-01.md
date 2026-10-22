# Game pacing: use the programmed scanline period

**Console follow-up:** build `ce7006087f20` now shows enlarged steps on
79.19% of reading calls, an owner-reported 18-second Kasumi load and working
FMVs. See the [result and remaining fight-start issue](sci-pacing-period-console-2026-10-01.md).
The original pre-test reasoning below is retained for provenance.

## Trigger and limits

The owner's `8d930310f79d` return photo still shows zero enlarged steps:
5,970 successful read steps and 11,880 game sectors (1.98995 per step).
The button combination first returned DOA2 to its title screen and required
several more attempts before returning to K-UI. Those soft resets preserve
the resident's cumulative diagnostics. They add activity to the totals;
they cannot erase earlier enlarged steps. See the
[photo and reported 25-second load](sci-game-pacing-result-2026-10-01.md).

The photo does not report video-register geometry or actual resident read
cost. It therefore does not prove that the defect below occurred in this
particular launch. The reported smoother combat and shorter loading remain
observations with an unestablished cause, not proof that batching activated.

## Reproduced defect

The old reader inferred counter length from the vblank-in interrupt line,
capping the largest observed scanline at `vbi + vbi/8`. These are independent
register settings. A valid NTSC interlaced configuration has a 525-tick
counter and vblank-in at 260; a PAL interlaced configuration can have a
625-tick counter and the same interrupt line.

For `(period=525, vbi=260)`, the old estimate was capped at 293 ticks.
It rejected valid current lines above 292, halved the moving-screen time
allowance, and could underflow when timing a read across a counter wrap:

```
start=450, samples=524 then 50, one wrap, two game sectors
old cost16 = (293 + 50 - 450) * 16 / 2 = 2147482792 (uint32_t)
true cost16 = (525 + 50 - 450) * 16 / 2 = 1000
```

That inflated cost can suppress batching until it decays, while another
wrap can inflate it again. A temporary host harness reproduced the old
result against the unmodified source. This is an arithmetic regression
case, not a measured console performance result.

## Original implementation

- Read PowerVR `SPG_LOAD` at offset `0xd8`; its bits 25:16 store the vertical
  counter period minus one. Replace inferred maximum scanline state with
  this programmed period.
- Use that period both for elapsed read timing and the existing pacing
  allowance. Sector limits and duration policy are unchanged: two by default,
  at most four for moving buffers within a predicted half-period, up to
  eight for a sufficiently still buffer.
- Reject invalid start/end lines and implausible geometry. A timing epoch
  invalidates measurements spanning a geometry change, including a change
  back to the original mode, or an inconsistent sample. These changes clear
  learned cost, stillness and spin accumulation without inventing counter
  wraps. Early vblank interrupt positions are valid independently of period.
- Replace less useful return-screen fields with latest `PACE PERIOD`,
  `PACE VBI`, `PACE COST16`, `PACE STILL` and `PACE LINE`. Keep cumulative
  read, sector, frame, enlarged-step, spin-step and guard-fault counters.
  The five timing fields are current state, not a snapshot of the fight;
  returning to the title screen can change them. `PACE COST16 / 16` is
  estimated counter ticks per game sector, not microseconds.

No SWAT/DreamShell driver code was copied. Transport, per-block CRC, stream
cleanup, memory/stack reservations and interrupt restoration are unchanged.
The new geometry read is read-only. Windows CE remains a separate loader task.

## Primary cross-checks

- [KallistiOS video implementation](https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/hardware/video.c),
  retrieved blob `7a1907890668beba74b8fd94194018412aac438c`: 524/624
  interlace vertical counts, interrupt line 260, and register programming.
- [Flycast scanline generator](https://github.com/flyinghead/flycast/blob/master/core/hw/pvr/spg.cpp),
  retrieved blob `f2f0cca5051ddd3f829f7b70814da05e5406f649`: counter wraps
  at `SPG_LOAD.vcount + 1`, including interlaced modes.
- [Flycast register definitions](https://github.com/flyinghead/flycast/blob/master/core/hw/pvr/pvr_regs.h),
  retrieved blob `81bc5cd27cdfa7c2f0da8c594a72cf3c6c7593e8`: register
  offsets and field widths. These sources establish the independent
  period/interrupt settings; they do not establish DOA2's particular settings.

## Validation and next console check

246 focused host checks pass under AddressSanitizer/UndefinedBehaviorSanitizer.
Coverage includes real interlace tuples, VGA, explicit wrap arithmetic,
moving/still limits, early vblank positions, invalid samples and geometry
changes. Host models do not establish hardware speed.

Local SH-4 normal and benchmark builds pass linked instruction, layout and
stack audits with unchanged guards. SCI payload is 11,164 bytes, resident
end `0x8c00baec` (20 bytes free), conservative stack 1,184/1,232 bytes.
SCIF/IDE payloads are 10,652/9,876 bytes and stack bounds 1,084/1,004 bytes.
Normal entry/stage payloads are 56,704/48,512 bytes; normal/benchmark audits
inspect 18,962/19,831 linked instructions. There are no unresolved symbols
or resident FPU instructions. Official CI run 36947691757 subsequently passed
both jobs with the same audit figures; the console result records package
verification and provenance.

Repeat the same Kasumi-to-first-fight timing and check the first ten seconds
of combat, speech and an FMV. Then capture the K-UI return screen; if DOA2
first resets to its title screen, release the buttons before trying again.
Note any intervening resets. The decisive first question is whether
`PACED STEPS` becomes nonzero. A new Storage soak is unnecessary for this
resident-only correction. Use `8d930310f79d` as the console comparison build.
