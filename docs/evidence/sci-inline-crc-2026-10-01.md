# SCI CRC inlining and the next test sequence

## Comparison build and motivation

Use **ce7006087f20** as the console comparison build: the owner reports an
18-second Kasumi-to-first-fight load, working FMVs and remaining slowdown in
the first seven seconds of combat. Its return counters confirm enlarged
budgets on 79.19% of reading steps and 3.23455 game sectors per step. The
[console record](sci-pacing-period-console-2026-10-01.md) retains the exact
photo, timing limits and latest 525/260 geometry.

The latest retained cost was 1084 scaled counter ticks per game sector.
At the same moving-buffer allowance, 1048 or less would admit a fourth sector.
That 3.32% difference motivates reducing real work per sector; it is not a
fight-specific profile or a prediction that this change removes the slowdown.

## Candidate implemented

The existing, original algebraic CRC16 helper in `sci_sd_bus.c` is now forced
inline. Its formula is unchanged. Native assembly inspection found that the
size-optimized build previously called this helper for each byte and spilled
the source word and byte-loop counter around each call. The candidate removes
**512 CRC call/return pairs and 2,048 associated stack accesses per 512-byte
DMA read**. The 128 word-reversal calls and their outer-loop stack accesses
remain. Assembly evidence establishes removed work, not measured speed.

The shared change applies to runtime and detached game-reader SCI code,
including runtime TX CRC work and polled paths. RX checks still occur after
DMA has fully stopped. There is no new table, buffer, clock rate, framing
sequence, DMA overlap, interrupt contract or pacing allowance. Every block's
CRC remains checked before successful data return.

Inlining alone exceeded the resident limit by 12 bytes. Removing the
`FRAMES SEEN` and `PACE LINE` return-display calls recovers enough code space.
The underlying frame/line state and timing calculations remain intact.
Guard, read/sector, enlarged/spin-step, period, vblank, cost and stillness
fields remain visible. No memory or stack guard was enlarged.

## Validation

Existing focused host checks pass with ASan/UBSan (LeakSanitizer disabled
because of the container environment):

- SCI DMA/polled RX/TX patterns, CRC comparison, finite fault paths and
  ownership/register restoration.
- SCI write protocol and runtime-to-resident storage handoff.
- Normal and fast SD-reader protocol suites, 220,632 assertions each.
- Retail SD ownership/bit edges and independent CRC agreement.

Native normal and benchmark layout, stack and linked-instruction audits pass:

| Image | Payload bytes | Resident memory end | Conservative stack bytes |
| --- | ---: | --- | ---: |
| SCI | 11,156 | `0x8c00baec` | 1,172 / 1,232 |
| SCIF | 10,608 | `0x8c00b8c8` | 1,080 / 1,232 |
| IDE | 9,832 | `0x8c00b5b8` | 1,000 / 1,232 |

SCI retains 20 bytes below the fixed resident ceiling and 60 bytes below the
conservative stack bound. Normal entry/stage payloads are 56,648/48,456 bytes;
benchmark entry/stage payloads are 59,264/51,072 bytes. Instruction audits
inspect 18,999 normal and 19,862 benchmark instructions; no unresolved
symbols or resident FPU use. Official CI/package verification is required
before delivering the candidate. No console gain has yet been established.

## Next console test

1. Keep the current boot CD, card format and images. Replace the matching
   runtime and game-loader files together.
2. Run **Diagnostics → Storage → Quick** once on SCI and send its result.
   This time runtime transport code changed: compare the RX check phase
   against approximately 68.43 us/block and reads against 1,064.98 KiB/s
   from the last soak. Quick-versus-soak is indicative, not a controlled
   same-preset percentage comparison; the earlier Quick read was 1,068.19
   KiB/s with RX checking 68.57 us/block.
3. Repeat DOA2 Kasumi-to-first-fight timing, opening combat and an FMV.
   Send the return screen and note any game resets. Compare with 18 seconds
   and the remaining seven-second slowdown, not the older two-sector build.
4. If Quick is error-free and DOA2 has no regression, test other images the
   launcher identifies as **Native GD**. For each, record build, exact title,
   furthest point reached, load behavior, FMV/audio, gameplay and VMU
   save/load. Photograph the final diagnostic if it stops. A successful
   DOA2 test alone does not establish another title's compatibility.

## Further work, in measured stages

| Candidate | Why it is worth testing | Required evidence / boundary |
| --- | --- | --- |
| Framing and deadline overhead | Token/busy paths invoke the runtime clock repeatedly; SCI PIO preparation rewrites SCR per byte. | Add coarse phase totals/token counts, then bound any grouped deadline sampling. Preserve finite byte budgets, initial/final checks and CMD12 cleanup. Establish PIO mode explicitly when ownership is acquired. |
| Runtime-only CRC table or selective optimization flags | Runtime uses `-Os`; it can afford a 512-byte CRC table that the resident cannot. | Measure after the inline result; independently verify all CRC behavior and keep the resident table-free. This does not automatically improve game reads. |
| Cached game-output copies | Guest output currently uses uncached P2 stores; aligned word copying already exists. | Profile first. A specialized cached bulk-copy path needs publication before return, partial-line/error-path checks and game DMA/alias coherency review. Do not globally change guest mapping. |
| CRC/reversal overlap during DMA | Could hide part of the remaining check phase. | Verify completed-line ownership from primary hardware documentation and bound dummy-byte feeding. Current transfer is already about 330.8 us against a 327.68 us payload floor, so added polling can hurt. Keep this a separate experiment. |

The last runtime soak averaged about 469.49 us per physical block:
330.80 transfer, 68.43 check, 1.95 setup and 68.31 outside those measured
phases. That remainder includes multiple costs and cannot be assigned entirely
to framing. Even eliminating the entire old check phase, with everything else
fixed, gives only about 1,246.7 KiB/s. Reaching the externally reported
filesystem figures will require understanding other costs and matching units
and workloads; it is not a reason to disable CRC.

After the native compatibility pass, proceed to the separate
[ARMADA Windows CE boot milestones](windows-ce-loader-audit-2026-10-01.md).
The first probe verifies prefix/body placement without entering CE; the next
traces bootstrap handoff and actual GD requests. Faster storage alone does
not provide the missing boot layout or establish kernel/resident coexistence.
