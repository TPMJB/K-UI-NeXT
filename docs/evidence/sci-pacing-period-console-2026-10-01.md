# Scanline-period correction: DOA2 console result

## Owner observation

Build **ce7006087f20**, SCI microSD native DOA2 launch:

- Kasumi selection to the first fight: **18 seconds**, versus the preceding
  reported 25 seconds and earlier 29 seconds. These are owner timings, not
  repeated automated trials: 28% shorter than 25 seconds, 37.93% shorter
  than 29 seconds.
- **FMVs now play fine**, according to the owner.
- The remaining reported issue is the **first seven seconds of a fight**,
  continuing the fight-start slowdown previously lasting about ten seconds.
  No precise frame-rate trace or severity measurement accompanies the report.

This is the current DOA2 comparison build. It is not broad game compatibility,
long-play or VMU save/load acceptance. Windows CE remains a separate task.

## Return photograph

`61637.jpg` displays build `CE7006087F20`, `NATIVE GD IMAGE` and
`GAME MENU RETURN`. All numeric display values are hexadecimal.

| Displayed field | Hexadecimal | Decimal |
| --- | --- | ---: |
| GUARD FAULT | `00000000` | 0 |
| READ STEPS | `00001A6A` | 6,762 |
| SECTORS READ | `00005570` | 21,872 |
| FRAMES SEEN | `00002957` | 10,583 |
| PACED STEPS | `000014EB` | 5,355 |
| SPIN STEPS | `00000000` | 0 |
| PACE PERIOD | `0000020D` | 525 |
| PACE VBI | `00000104` | 260 |
| PACE COST16 | `0000043C` | 1,084 |
| PACE STILL | `00000001` | 1 |
| PACE LINE | `00000001` | 1 |

Enlarged budgets activated on **79.19255%** of successful reading steps;
the session delivered **3.234546 game sectors per step**, versus 1.989950
in the previous photo. These are cumulative session counters and do not
isolate the first-fight interval. Normal game resets preserve them, whereas
launching a new resident from K-UI starts a new session. An enlarged budget
can deliver fewer sectors at a request tail. Zero guard fault is reported;
this screen does not count physical DMA use or establish all possible errors.

The captured period/vblank pair **525/260** is precisely the legitimate
geometry that the old inferred-period code mishandled. Together with the
nonzero enlarged-step count and improved reported loading/FMV behavior,
this supports the [period correction](sci-pacing-period-fix-2026-10-01.md).
The retained geometry and cost are latest state, possibly after title-screen
activity; they are not a saved trace of the first seven seconds of combat.

## What the latest cost does and does not show

The latest estimated cost is `1084 / 16 = 67.75` counter ticks per game
sector. With stillness 1, the moving-buffer policy has a half-period allowance
of `floor(525/2) * 16 = 4192` in scaled ticks:

- Three sectors fit: `3 * 1084 = 3252`.
- Four do not: `4 * 1084 = 4336`.
- A cost estimate at or below **1048**, 3.32% lower, would permit four within
  the existing allowance at this sample. It is not a promised speed gain,
  a fight-specific threshold observation or proof of exact refresh frequency.

The next useful target is lower CPU/protocol cost per sector with CRC retained.
The post-DMA CRC assembly still contains 512 per-byte call/return pairs and
2,048 associated stack accesses per 512-byte block. An isolated experiment
forcing only the existing SCI CRC helper inline removes those calls/spills,
but ends at `0x8c00bb0c`, 12 bytes beyond the unchanged resident limit.
That experiment was stopped at the linker guard and is not a delivered build.
It needs deliberate code-space recovery and complete native audits before
console comparison; no speed gain is claimed from assembly alone. Main-repo
executable source remains at the tested build. Runtime throughput
was not measured in this report; the period correction did not change the
physical transport. No further Storage soak is needed to validate this result.

## SWAT comparison reported by the owner

The owner relays SWAT's direct reply: about **1450** for raw sectors with
CRC, and about **1350** through a filesystem, reported in chat as `kbps`.
Retain those as externally reported figures, not independently reproduced
measurements. The exact byte/bit and decimal/binary units, workload, card,
filesystem and test implementation have not been supplied. Do not normalize
them into KiB/s or claim an exact percentage gap. CRC being disabled is not
an explanation consistent with the reported raw result. Continue the original
implementation policy: behavioral ideas and primary hardware documentation,
without copying SWAT/DreamShell drivers.

## Provenance and shipped validation

Photo: 242,191 bytes; SHA-256
`39f3a7a3c19370761b93ff394b64b91bb54e744fd893bc01416fc6a092d4988d`.
Inspected supplied local copy:
`/workspace/scratch/e58804339be4/upload/01-61637.jpg`.

Source commit: `ce7006087f20eedbb06220b7b0d55d466ba5ee31`.
[CI run 36947691757](https://github.com/TPMJB/K-UI-NeXT/actions/runs/36947691757)
passed host and Dreamcast jobs. Official native layout/stack/instruction
audits match the preflight figures in the design record. All 96 SD artifact
manifest files, both package CRCs and matching build IDs were verified.
Shipped focused update ZIP SHA-256:
`94aa3566d18a797e82f149623a8e223fcfc22dfd6d43ab3619f8eb8414626170`.
