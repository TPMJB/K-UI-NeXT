# First independent DOA2 gameplay, 2026-09-24

## Owner report and reproducible baseline

After testing build `7fd48f11be02`, the owner reported that DOA2 loaded into
actual gameplay. Loading seemed slower than DreamShell; gameplay lag was
present but not unplayable. FMVs were estimated at about 0.5 frames/second.
These are qualitative owner observations, not instrumented throughput or
frame-rate measurements. Physical VMU save/load and repeated loading
transitions have not been separately confirmed. G4 is therefore partial,
not broad game compatibility or completed Games-app acceptance.

- Game: owner-provided DOA2 `T3601N`, `V1.100`, region `U`, existing raw GDI.
- Source: `50f8c45c582f9e11233ed931c772a94dc272577e`.
- Packaged merge: `7fd48f11be023161225783c9f27a44eb58b12016`.
- CI run: `36027138206`; SD-update artifact: `10820196454`.
- SD ZIP SHA-256: `ad6635a4f680ada3970b8a980294816f65b389057987ad3dea36a39bdfca8932`.
- Runtime CRC32: `44ab8f47`; retail-loader CRC32: `679a2612`.
- Same boot CD, SD adapter and game dump; no new capture required.

Keep this package as the first working gameplay baseline for performance
comparisons. Recording this feedback changes documentation only, without a
new console build or repeated accepted probe.

## Concrete performance differences in the current source

| Area | Independent retail path | Inspected DreamShell/KOS reference |
| --- | --- | --- |
| SD transactions | `retail_image.c` asks for one physical 512-byte block at a time; `sd_reader.c` issues CMD17 for each | DreamShell `dev/sd/sd.c` and pinned KOS `sd.c` use CMD18 plus CMD12 for multi-block reads |
| Payload receive | `retail_sd.c` calls the generic full-duplex byte routine, doing transmit-bit calculations and slow-mode branches for each bit even for `0xff,false` | DreamShell `dev/sd/spi.c` and pinned KOS `scif-spi.c` have dedicated receive loops, fixed low/high pin values and unrolled/batched reads |
| Data CRC16 | Eight polynomial steps per received byte | DreamShell `sd_crc16` uses an algebraic byte update; CRC verification need not be removed to reduce this cost |
| Scheduling | Resident hook masks interrupts while each synchronous chunk of up to eight game sectors is read and copied | Our design does not yet provide asynchronous game-side completion or interrupt-driven DMA |

DreamShell paths refer to the existing user tree
`firmware/isoldr/loader/dev/sd/{sd.c,spi.c}`; KOS is the already pinned
`fcfa7d869471591ca1c777543261a7bfea7cb726` SCIF/SD source. These findings
identify avoidable work, not a measured breakdown of the FMV slowdown.
Game interrupts held off during lengthy reads may compound the bandwidth
cost. Increasing the chunk size alone can extend those pauses. Making chunks
smaller alone can add polling and card-command overhead; neither is a proven
performance fix without comparison.

## Next performance work

Start with a dedicated normal-speed receive path and faster equivalent CRC16,
preserving pin timing order, CRC rejection and finite work limits. Then add
bounded contiguous SD bursts across the raw-sector unpacker, with CMD12 cleanup
on every completion/error/cancel path and no reads beyond mapped extents.
Review interrupt hold time alongside throughput; faster copying alone is not
an asynchronous reader. Keep the known-working package available for rollback.

The resident currently ends at `0x8c00ba8c`, leaving only 116 bytes before its
`0x8c00bb00` guard. Its conservative stack bound is 924 of 1,232 usable bytes.
A larger bulk implementation needs space recovered or a proven revised layout,
not an unchecked buffer added to the game's startup stack region. A speedup
and smooth FMVs cannot be promised from code inspection alone.
