# Toy Commander normal Maple completion boundary

This is a read-only analysis of the owner's exact original 748444-byte
`1GUTH.BIN`, SHA-256
`ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd`,
loaded at `0x8c010000`. It does not establish in-game virtual saving. No retail
executable or SDK implementation is included. The probe emits metadata and
sparse scalar checks, and never generates patches:

```
python3 tools/vmu_toy_completion_probe.py /path/to/1GUTH.BIN
python3 tests/test_vmu_toy_completion_probe.py
```

## Normal completion happens before response consumers

| Owner address | Observed instruction/control-flow role |
| --- | --- |
| `0x8c0b5314` | Common completion wrapper, called from IRQ and synchronous API paths |
| `0x8c0b533e` | Installs IMASK15 before the completion gate |
| `0x8c0bf322` | Gate first requires manager `+0xfc0` to be zero: a normal transfer is pending |
| `0x8c0bf33a` | Reads Maple base `+0x18` (MDST); the next test refuses a busy transfer |
| `0x8c0bf34a` | Takes the SDK TAS lock; failure defers consumption |
| `0x8c0b5354` | Only a nonnegative gate result reaches the consumer |
| `0x8c0b535c` | Loads consumer pointer from literal `0x8c0b540c` into **R3** |
| `0x8c0b535e` | Calls the consumer; return PR is `0x8c0b5362` |
| `0x8c0bf37a` | Common consumer loops over enabled ports and device units |
| `0x8c0b538e` | Copies consumed device state to the SDK public snapshot |
| `0x8c0b5394` | Calls `0x8c0bf364`, which marks ready and releases the SDK lock |
| `0x8c0b5470` | Dispatches registered application callbacks after consumption |
| `0x8c0b54b6` | Restores the inherited interrupt mask |

The complete original file has one aligned pointer value naming the common
consumer: `0x8c0b540c`. The encoded PC-relative load scan finds one reference to
that literal, at `0x8c0b535c`; no encoded direct branch targets the consumer
entry. These scans include possible data and cannot establish all dynamic
control flow. The full-file hash is mandatory before interpreting these facts.

The literal is a possible passive observation route. Replacing one exact
pointer avoids relocating the SDK prologue. A reviewed trampoline would need to
preserve integer registers, SR, PR, GBR, MAC registers, the game stack, and all
floating-point state. Its incoming R3 would contain the substituted worker
address; the original consumer would have received `0x8c0bf37a` in R3. Restore
that original value before tail-jumping to the consumer, without changing PR.
R0 is zero from the successful gate and must remain zero. No live patch is
generated or admitted by this analysis.

## Receive ownership and discovery

The global manager pointer is at `0x8c1223a4`; the original manager object is
`0x8c1212a8`. The SDK accesses that object through **P1**, including its changing
selector and pointer table. An observer must use the same cached alias for
those values. Reading an uncached alias of a dirty P1 selector can observe an
old transfer bank.

Manager `+0xfe8` holds two banks of 24 receive pointers, each bank occupying
96 bytes. Manager `+0x10b4` selects the current construction bank, so the
consumer selects completed bank `1 - selector`. Initialization at
`0x8c0beeaa..0x8c0beeba` makes the receive-buffer bases P2 addresses; the table
construction at `0x8c0bf6fc..0x8c0bf724` produces the corresponding banks.
Read each receive buffer through its actual P2 table value. The consumer needs
no cache invalidation for those uncached response bytes.

`0x8c0bfebe` reads a main controller's response header and updates its subunit
mask. `0x8c0bff84` processes a unit response, calling `0x8c0c00ca` to decode its
header before interpreting discovery or condition data. The common consumer
reaches these at `0x8c0bf3ac` and `0x8c0bf3d2`, before application callbacks.
An eventual emulation layer must splice a controller's subunit mask before this
main-header consumption; injecting it later misses enumeration.

## IRQ ordering and a second DMA producer

The startup routine `0x8c0b4ea2` registers completion handler `0x8c0b4d10` with
priority 1 and acknowledgement handler `0x8c0b4dea` with priority `0x40` for the
same normal interrupt event. Registration routine `0x8c0b399c` sorts the active
handler chain by increasing priority. The completion callback invokes the
common wrapper at `0x8c0b4d34`. The acknowledgement callback writes `0x1000` to
ISTNRM (`0xa05f6900`). This supports consumption before acknowledgement in the
original startup configuration, while live handler ownership still needs proof.

There is another MDST start at **`0x8c0b4db6`**, outside the existing normal
pre-submit hook at `0x8c0bf264`. Routine `0x8c0b4d78` constructs a separate gun
descriptor using `0x80000200 | (port << 16)`, writes MDSTAR, sets flag
`0x8c118398`, and starts DMA. The next interrupt follows the flag branch at
`0x8c0b4d2a`, bypasses normal response consumption, and clears the flag at
`0x8c0b4d60`. Pinned primary KOS `maple_queue.c` independently identifies a
`0x200 | (port << 16)` descriptor as gun mode:

https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/maple/maple_queue.c

The existing observer therefore counts **normal SDK chains**, not every Maple
DMA start. A future mutating implementation must also account for this second
producer. The polling loops at `0x8c0bf450` and `0x8c0bf4a8` belong to shutdown
or reset handling; they are not the normal response-consumer boundary.

## Next safe console experiment

Keep the next step passive. Before each common-consumer observation, require
MDST idle, software trigger mode, expected Maple base, SDK pending state, and
the acquired SDK lock. Correlate the normal MDSTAR with the last admitted
pre-submit chain and bound every receive pointer before reading a header.
Record completions, unexpected entry PR/SR, selector values, response counts,
and the special-DMA flag without changing a response or starting extra DMA.

With a stop before normal start 1200, a clean run should normally observe 1199
completed normal schedules. Treat mismatches as evidence to investigate rather
than silently accepting an alternate path. Static identity, zero malformed
counts, and these expected totals still do not prove save/load timing,
recovery behavior, or general compatibility with another game.
