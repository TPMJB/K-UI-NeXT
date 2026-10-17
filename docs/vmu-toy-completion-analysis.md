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
96 bytes. Manager `+0x10b4` selects the construction bank. The consumer selects
completed bank `1 - selector`. These are indices into a pointer table, not the
distance between response buffers.

The game's call at `0x8c04e936` enters wrapper `0x8c0b3288` with raw RX storage
`0x8c0e007c` in R5 and TX storage `0x8c0ec09c` in R6. The wrapper calls
`0x8c0b4fac` at `0x8c0b32d4`; that routine calls the uniform SDK initializer
`0x8c0bed28` with RX in R6 and TX in R4. The initializer rounds each base up to
32-byte alignment and converts it to P2 at `0x8c0bed7e..0x8c0bed92`. Its call
to `0x8c0bf6d0` constructs all 48 receive pointers:

```
RX[bank][port * 6 + unit] = 0xac0e0080 + bank * 0x6000
                          + (port * 6 + unit) * 0x400
bank = 0..1, port = 0..3, unit = 0..5
```

Here `unit` is the slot ordinal. The owner address table at `0x8c0c63a8`
maps ordinals 0 through 5 to port-local address bits
`0x20, 0x01, 0x02, 0x04, 0x08, 0x10` respectively. A raw one-hot subunit
address is not itself the ordinal; reject combinations outside that mapping.

The constructor writes bank zero at `0x8c0bf70a`, bank one at
`0x8c0bf71e`, and advances each device allocation by literal `0x400` from
`0x8c0bf7b4`. Literal `0x6000` at `0x8c0bf7b2` separates the two banks.
Each device owns **1024 bytes per bank**, including A1. A1 uses `0xac0e0480`
and `0xac0e6480`. Both the 116-byte DEVINFO response and the 524-byte block
read response fit. The complete RX arena is `[0xac0e0080, 0xac0ec080)`;
TX begins at `0xac0ec0a0`, leaving a 32-byte gap. This conclusion comes from
the initialized allocation layout, not from a general physical RAM bound.

The owner also contains a custom initializer at `0x8c0bee44` that clears the
tables and installs supplied per-unit pointers. The full original file has no
aligned pointer or encoded direct branch naming its public wrapper
`0x8c0b3360`. That scan cannot exclude arbitrary dynamic calls. All static
`0xfe8`-based table accesses were inspected: only the two initialization paths
write the tables. Normal and alternate builders read them at
`0x8c0bfa68` and `0x8c0bfcae`; response consumers only read them. No dynamic
per-function RX resizing was found. A live admission must reject a custom or
changed table instead of assuming the uniform layout.

| Live P1 address | Field and fixed-layout requirement |
| --- | --- |
| `0x8c1223a4` | Manager pointer equals `0x8c1212a8` before any manager dereference |
| `0x8c122268` | Manager `+0xfc0`, ready state: 1 at normal pre-submit; 0 at completion |
| `0x8c12226c` | Manager `+0xfc4`, TX base equals `0xac0ec0a0` |
| `0x8c122270` | Manager `+0xfc8`, RX base equals `0xac0e0080` |
| `0x8c122290` / `0x8c1222f0` | RX pointer tables: all 24 entries in each bank match the formula |
| `0x8c122350` / `0x8c122354` | Manager `+0x10a8/+0x10ac`, TX banks equal `0xac0ec0a0/0xac0ef0a0` |
| `0x8c122358` | Manager `+0x10b0`, pending TX pointer matches the pre-submit bank |
| `0x8c12235c` | Manager `+0x10b4`, RX construction selector |
| `0x8c122360` | Manager `+0x10b8`, TX construction selector |
| `0x8c12236c` | Manager `+0x10c4`, endian mode must be 0 for the observed header encoding |

At normal pre-submit site `0x8c0bf264`, both selectors must equal the admitted
bank, 0 or 1. The pending MDSTAR must name that bank's TX base, and each normal
descriptor RX must equal its decoded `port * 6 + unit` slot in the same bank.
After MDST starts at `0x8c0bf26a`, the TX selector toggles at
`0x8c0bf27a..0x8c0bf27c`; the RX construction selector toggles at
`0x8c0bf29e..0x8c0bf2a0`. Therefore, at the common completion boundary **both
selectors must equal the admitted bank XOR 1**. Neither remains at the old
bank until consumption. Validate the full fixed table at both boundaries,
along with source-bank bounds, nonoverlapping response slots, exact pending
MDSTAR, and the existing hardware/SDK context checks.

The successful normal start holds the TAS lock from `0x8c0bf1a4` through
`0x8c0bf314`; pre-submit therefore has lock byte `0x80` and ready state 1.
After submission, ready becomes 0 at `0x8c0bf30e`. The completion gate reacquires
the lock at `0x8c0bf34a`, so completion has lock byte `0x80` and ready state 0.
The lock pointer is read through P1 cell `0x8c0c0964`; the admitted pointer
value is `0xac0c0968`, and its byte is read through P2. Require the separate
gun flag at `0x8c118398` to be zero at both normal boundaries.

The P2 consumer route is direct. The main consumer loads its table pointer at
`0x8c0bfefa` and reads the header through that pointer at `0x8c0bfefc`.
The unit consumer loads its table pointer into R11 at `0x8c0bffe4`, passes it
to decoder `0x8c0c00ca`, and that decoder reads it at `0x8c0c00d0`. The payload
consumer `0x8c0c02fe` advances the same pointer by four bytes. No route here
converts the buffer to P1. Read or write through the actual admitted P2 slot;
the SDK consumes uncached response bytes without a cache invalidation.

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

The packet builder at `0x8c0bf980` can insert spacing between consecutive
same-port packets. Its call at `0x8c0bfa16` reaches helper `0x8c0bf93c`, which
loads literal `0x0700` at `0x8c0bf952`, writes one word at `0x8c0bf962`, and
advances four bytes at `0x8c0bf968`. Initialization stores spacing count five
in manager `+0x10cc` at `0x8c0bf85a`. These are padding entries with no RX or
packet header. A bounded observer can recognize exact `0x00000700` separately
from normal packets; this does not justify accepting other control patterns
or a LAST-marked padding entry. Build `83bf9649d565` stopped after the second
schedule's A-controller GETCOND, before recording a subunit request. The
padding form explains a missing parser capability, but the rejected word was
not retained in that build and is not directly established by its photographs.

Keep the next step passive. Before each common-consumer observation, require
MDST idle, software trigger mode, expected Maple base, SDK pending state, and
the acquired SDK lock. Correlate the normal MDSTAR with the last admitted
pre-submit chain and bound every receive pointer before reading a header.
Record completions, unexpected entry PR/SR, selector values, response counts,
and the special-DMA flag without changing a response or starting extra DMA.

The later padding-capable D878 run reached its planned stop with `0x4cc`
normal starts and `0x4cb` matched completions. It observed 28 subunit requests,
including one DEVINFO, one LCD update, one MEMCARD geometry request, and 25
MEMCARD reads. Its block read response header `0x82010008` describes 130
payload words, or 524 total bytes. Its recorded RX range
`0x0c0e0080..0x0c0ea880` agrees with the uniform table's main-device slots
across both banks. An earlier reported 256-byte RX spacing remains an
unreconciled photograph interpretation and is not admitted as another layout.

The smallest subsequent virtual-discovery proof can keep hardware chains
unchanged and replace only a validated absent A1 response at this completion
boundary. Before any write, establish the full 48-slot layout above and all
existing completion gates. Match the pending request's exact port, unit,
command and source address; require its A1 RX header to be the hardware
timeout marker `0xffffffff`. Require the unmodified real controller response
to report A1 absent, and refuse a real A1 reply or later physical presence.
The SDK also recognizes `0x0000ff00` as a missing response; this is not enough
evidence to admit replacing that alternate marker.

The main consumer updates the subunit mask from every valid parent response,
not only DEVINFO. An eventual overlay must maintain the added A1 presence bit
in valid, matched parent replies while preserving the controller's other
bits, before `0x8c0bfebe` runs. The unit discovery consumer at
`0x8c0c01cc` detects that new bit and schedules a real A1 DEVINFO request;
its successful DEVINFO path at `0x8c0c0240..0x8c0c024e` publishes the device.
With endian mode 0, it consumes the replacement payload directly through P2.

A MEMCARD-only formatted empty image can prove discovery, geometry, and block
reads without SD writes or a full 128 KiB RAM mirror. Writes and write-phase
synchronization must stop before submission for this proof. It still needs
implementation review, exact-owner admission, and a console test with the
physical A1 VMU absent. Static identity, a capacity contract, and clean
passive totals do not prove save/load timing, recovery, or another game's
compatibility.
