# Toy Commander driver cursor and finite playback evidence

Date: 2026-10-07. Read-only inspection; no retail audio patch is enabled by
this report. Only independently authored observations and scalar contracts are
recorded. Retail bytes, disassembly and decoder output remain private outside
the repository. The ordinary 1.8.5 reader is outside this candidate's scope.

## Exact inputs and address conventions

The supplied `AUDIO64.DRV` has size 20740, CRC32 `70cceeb2`, SHA256
`477ede3766c27fa58e4c14d5218c583b7806a29c328a6e0d4965293375b704e5`.
It is paired with the previously inspected 748444-byte boot executable,
CRC32 `cdc493b3`, SHA256
`ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd`.
ARM addresses below are offsets from the driver load base zero. Host sound RAM
base is `0xa0800000`; SH addresses assume boot load base `0x8c010000`.
Critical ARM memory operations, bit masks and conditional branches were checked
against the supplied instruction words as well as the private decoder output.

The game calls `amInit` at `0x8c06aa86` with driver main-RAM pointer in `r4`
and actual file byte length in `r5`. Its one observed call uses the literal at
`0x8c04a244`, calls at `0x8c04a1a2`, and returns to `0x8c04a1a6`.
This is a candidate identity-check boundary before the game installs the driver.
`amInit` rounds its internal install length to 32 bytes, so the exact 20740-byte
identity must be checked before installation, rather than hashing 20768 bytes.
The installed image contains writable headers, tables and code-local state;
a later whole-image CRC cannot be assumed to remain the original file CRC.

## Published position: units and updates

The ARM main loop at `0x016c` consumes pending commands through `0x39a0`,
then calls position/event service `0x4aa8`. This is independent of an SH game
frame callback. It is not a fixed-rate cursor interrupt.

| Value | ARM offset / formula | Interpretation established by inspection |
| --- | --- | --- |
| Playing byte | `0x1468 + port` | `0xff` enables polling; zero is retired bookkeeping |
| Current position | `0x14f0 + port * 4` | Published hardware sample position |
| Previous position | `0x15f0 + port * 4` | Previous published current value |
| Event target | `0x16f0 + port * 4` | One armed crossing target |
| Port 62 / 63 playing | `0x14a6` / `0x14a7` | Owned-port status candidate |
| Port 62 / 63 current | `0x15e8` / `0x15ec` | Sample positions for the proposed stereo pair |
| Port 62 / 63 previous | `0x16e8` / `0x16ec` | Previous sampled positions |

Service scans all 64 playing bytes and calls `0x4be4` only for `0xff` entries.
That routine selects the channel in hardware register `0x0080280c`, preserving
bits selected by mask `0x0000c0ff` and inserting `port << 8`. After its delay
loop it reads `0x00802814`. It copies the old current word to the previous
array; if the hardware word differs, it stores that word to the current array.
There is no byte conversion or adapter-side accumulation in this publication.

Yamaha's primary [AICA sound-block manual, Ver. 1.00](https://manuals.plus/m/d260f79355dbff7664bdab58c9b8c9bc4b04fa272fdfc7c933ea28a47a167836.pdf),
hosted on a mirror, identifies the selected monitor's CA value as relative sample
position from SA, with one sample per least significant unit (PDF page 15).
For PCM16, one cursor unit corresponds to two mono bytes; equal stereo voices
therefore advance one four-byte stereo frame per unit.

The SH wrapper `acDigiGetCurrentReadPosition` at `0x8c068420` takes port in
`r4` and output pointer in `r5`. It checks the installed flag and port range,
reads the sound header's cursor-table offset at `+0xe8`, then reads the indexed
word through the existing G2 read-long helper `0x8c0840d6`. Its success value
one means the read succeeded, not that playback has started or completed.

Single Play `0x4428` and MultiPlay `0x4570` set selected playing bytes to
`0xff`, current to zero, and previous to `0xffffffff`. Open does not perform
this playback reset. The published positions remain stale when not polled.
Looped positions are source-relative and can wrap; they are not a monotonic
total played-frame counter. Driver service latency, exact start phase and the
terminal value after finite end are not established by static inspection.

## Finite end and retirement

Open `0x4338` converts format-zero byte length to samples by shifting right
once, sets LSA to zero, and sets LEA to the converted count minus one.
Play and MultiPlay reduce the loop argument to bit zero and place it in control
bit `0x0200`. Thus argument zero clears the loop-control bit.

The hardware manual defines format zero as signed PCM16, LSA/LEA in sample
units, and loop-control zero as ending at LEA (PDF pages 9–10). Its loop-off
example includes the sample at LEA (PDF page 20). A 32768-byte mono allocation
therefore describes 16384 samples, LSA zero and LEA `0x3fff`. Two such planes
use 65536 bytes and have nominal duration 16384 / 44100, about 371.5 ms.
Hardware finite end does not need an SH callback to stop stale repetition.

The driver's own retirement mechanism is weaker than an exact completion
counter: when `0x4be4` reads the same CA as the preceding current value, it
checks hardware loop bit `0x0200`. If clear, it writes control masked by
`0x000087ff` and clears the playing byte. If the loop bit is set, it leaves the
playing byte active. It does not read hardware LP or append an exact terminal
sample count. A zero playing byte can also precede any new play, and a hardware
key-off may finish before or differently from natural LEA completion.

Stop `0x46a4` and MultiStop `0x463c` clear `0x4200` in their hardware control
write, using the stored template, but do not themselves clear the playing byte,
current or previous arrays. Close `0x43b8` only resets the 72-byte template;
it neither stops hardware nor clears position bookkeeping. Therefore enqueue
success or a consumed Close is not permission to overwrite an active bank.
A bank-reuse state must distinguish a known applied play from initial inactive
state and retain the sound lease until no owned voice can read that bank.
For the first finite pilot, combine known consumed Start and expected applied
finite state with a conservative elapsed interval covering the full programmed
block at validated pitch, then same-generation driver retirement. Retirement
alone does not prove that all 16384 samples played: the first unchanged CA can
also be zero. An applied owned stop requires its own retirement/quiescence gate.

Hardware LP is a clear-on-read monitor flag. This driver does not use it in the
inspected position service. The candidate should not introduce direct monitor
selection/LP reads into retail execution: ARM already shares that selection
register, and extra reads could consume evidence or race its monitor scan.

## Cursor events and command application

RequestEvent `0x43e8` stores its target in `0x16f0[port]` and sets one bit in
`0x14e8` for ports 0–31 or `0x14ec` for ports 32–63. Service compares the
previous/current pair with that target using signed comparisons and wrap
branches. Its normal increasing case fires when current is at or beyond the
target and previous is at or below it. Wrap cases can also fire.

On a crossing, service writes the port byte into the 64-byte event ring
`[0x14a8,0x14e8)`, advances the write pointer published at header offset
`0xe4`, clears the requested bit, and writes `0x20` to `0x008028b8` to request
the existing host interrupt. There is no sequence number, block length,
next-buffer address or automatic second play in this event path. It is a
one-shot crossing notification, not a finite block chain or deadline watchdog.
The first finite pilot can leave this ring and interrupt handler untouched.

The host queue producer in `_acSetHostCommand` at `0x8c083d34` uses state base
`0x8c112afc`, queue-base pointer at `+12` (`0x8c112b08`) and a 16-bit producer
index at `+16` (`0x8c112b0c`). It increments/wraps the index through 32 slots,
forms `queueBase + index * 16`, reads the first word until stable, and requires
its low 16 bits to be zero. It publishes the packet then notifies the ARM.
An occupied producer slot returns failure instead of waiting for consumption.
Successful enqueue can be associated with the immediately captured producer
slot, subject to ordinary-context serialization and unrelated queue reuse.

The ARM clears a packet's opcode and marker after dispatch. A bounded status
step can observe that slot's low bytes becoming zero, then validate the owned
port's applied template/finite control and expected generation. The global
pending flag is cleared before dispatch and is not a queue-empty acknowledgement.
A cleared slot alone has no adapter generation and must not substitute for
applied ownership/state checks after the game reuses the queue.

## Existing sound-copy interfaces and lifecycle gates

The game's established main-to-sound payload helper is `0x8c049e24`:
`void copy(destination r4, source r5, byteLength r6)`. Destination must be
four-byte aligned and byteLength positive/divisible by four. Source alignment
selects byte, halfword or longword input branches. It waits for
`0xa05f688c & 0x11` to clear before each group of at most eight 32-bit writes.
It does not suspend DMA or change the interrupt mask; it follows the game's
normal-context payload convention. Zero length is invalid because the loop
decrements an initial zero word count. Its hardware waits have no static limit.

SDK `acG2Read` at `0x8c083fae` is a generic supplied-address word copy:
`void copy(destination r4, source r5, byteLength r6)`. It checks nonnull inputs,
length at least four/divisible by four, and source alignment. Its destination
must also be word aligned. It saves/raises IMASK, invokes `acDmaSuspend` at
`0x8c094c9e`, performs FIFO waits and at most eight word copies between waits,
invokes `acDmaResume` at `0x8c094cd2`, and restores IMASK. Its valid tail
`0x8c0840b0..0x8c0840c0` reconstructs/restores SR in `r0`; the subsequent
epilogue returns without placing a Boolean success value in `r0`. Do not infer
copy success from that incidental register value. Prevalidate all arguments.
There is no separately observed general `acG2Write` API. The supplied-address
copy direction is numeric, but its only observed literal caller reads the
event ring. A first pilot may prefer the actual game's main-to-sound helper.

The scalar helper at `0x8c0840d6` instead has an explicit success result:
`bool readLong(sourceAddress r4, uint32_t *output r5)`. It checks nonnull
arguments, saves/raises IMASK, suspends DMA, waits for the same FIFO condition,
and reads the source word twice until the two values agree. It stores the
stable word, resumes DMA, restores IMASK, and sets `r0` to one at
`0x8c08419e`. Read byte/halfword publications through their aligned containing
word and extract the required field. Its FIFO/stability loops still have no
static elapsed bound; a bounded outer poll cannot bound a failed G2 wait.

DMA wrappers dispatch through callback pointer `0x8c0b01c8`; suspend/resume use
operations 7/8. A null pointer logs and returns zero while the generic copy
continues. No observed caller installs this pointer through `0x8c094b04`.
This absence is not proof against other indirect/dynamic installation.
`acDmaMemCpy` at `0x8c094b90` dispatches operation 3 and accepts a fourth
callback/control parameter; its asynchronous lifetime/ownership has not been
established. It is not required for the first small finite test.

Incremental aligned chunks (for example 256 bytes) can limit the number of
writes per ordinary service opportunity, but cannot bound a failed hardware
wait. Record elapsed time and avoid loading an entire large audio half inside
one frame call. Serialization with the existing card reader remains a separate
gate. The finite hardware end prevents stale looping even if such work stalls.

Before every candidate sound call, validate the exact executable/driver profile,
successful game sound initialization, actual sound-heap lease, excluded owned
ports and current generation. Start only after game flag `0x8c0a7318` becomes
one: its initializer sends all-port stop after `amInit` returns.
Invalidate work before the game release entry `0x8c049f98`, SDK shutdown
`0x8c06aa0e`, driver reinstall or a fixed-heap clear. The release path's shutdown
literal is `0x8c04a050`, called at `0x8c049fb2`. The all-stop literals are
`0x8c04a04c` in release and `0x8c04a258` in initialization. Any all-port stop or
owned-port interruption ends the candidate playback generation; normal effect
activity on other ports does not authorize modifying their state.

## Bounded next console test

The implementable first test is game-owned, finite stereo PCM using reserved
ports 62/63 and real sound-heap storage. Incrementally fill immutable planes,
stop/close and validate fresh templates before changed addresses, enqueue one
finite MultiPlay with upper mask `0xc0000000` and lower zero, then observe
applied state and cursor progression in normal context. Record callback gap,
copy/read duration, left/right sample deltas, finite retirement and effects/FMV
behavior. Require the conservative elapsed and retirement gate before bank
reuse; do not label a sampled retired flag as exact LEA completion. Do not
refill a currently sounding bank, queue unlimited restarts, or
use loop-on to cover scheduling gaps. Two alternating finite banks are a later
extension only when reuse and application gates are implemented.

Static evidence proves sample units and finite hardware end. It does not prove
an exact audible terminal cursor, atomic paired cursor snapshot, maximum service
gap, gap-free bank switching or continuous CDDA completion. Hardware observation
must establish those narrower timing/phase facts before a continuous mode is
admitted.
