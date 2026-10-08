# Toy Commander external driver command evidence

Date: 2026-10-07. This is a read-only static inspection of the exact supplied
`AUDIO64.DRV`: 20740 bytes, CRC32 `70cceeb2`, SHA256
`477ede3766c27fa58e4c14d5218c583b7806a29c328a6e0d4965293375b704e5`.
Only authored observations and scalar contracts are recorded here. The driver,
decoded instructions and analysis scripts remain private outside the repository.
No runtime code or ordinary reader behavior was changed.

Driver addresses below are ARM sound-RAM offsets. The game installs this driver
at ARM address zero, corresponding to host sound base `0xa0800000`.
The critical branch targets, loop-bit operations and PCM length conversion were
also checked directly against the uploaded instruction words, independently of
the private decoder's formatted output.

## Command consumption and application

The main loop at `0x016c` checks the command-pending byte at `0xb400`. When set,
it calls consumer `0x39a0`, then always reaches service `0x4aa8`. The command
ring occupies `[0xb200,0xb400)`: 32 consecutive 16-byte packets.

Consumer `0x39a0` clears the pending word, starts from its saved consumer pointer,
wraps at `0xb400`, and stops at an empty opcode or after 32 slots. For each
nonempty slot it calls dispatch `0x3a08` before clearing that slot's opcode and
marker bytes and advancing by 16. Thus a host enqueue result is not an applied
playback result. Clearing a particular packet follows its dispatch, but there
is no newly established adapter-specific acknowledgement or generation token.

The saved ARM consumer pointer is at `0x399c`. Its legal aligned positions are
`0xb200` through `0xb400`, inclusive; the end position is wrapped at the next
scan. The pending word at `0xb400` is cleared before dispatch begins, so a zero
pending byte does not prove that earlier commands have finished.

Independent host inspection places the queue base pointer at `0x8c112b08` and
the producer halfword at `0x8c112b0c`. Successful enqueue advances the producer
modulo 32; the selected packet is `queue_base + 16 * producer`. The host writes
packet words in descending offset order, publishing the opcode word last.
Capturing that slot immediately after enqueue and observing both opcode and
marker cleared can acknowledge its dispatch only while producer reuse is
excluded. A later game enqueue can reuse the slot; neither an arbitrary empty
slot nor a stale zero playing byte establishes a new Play acknowledgement.
Admission needs producer serialization, an explicit finite poll deadline, and
the expected owned-port state. Timeout must fail closed rather than hang or
reuse a bank whose application is unknown.

Dispatch identifies the following packet contracts, matching the game's host
wrappers. These are interfaces to the installed game driver, not instructions
to copy or replace its implementation.

| Opcode | ARM handler | Relevant fields and effect |
| --- | --- | --- |
| `0x90` Open | `0x4338` | Port byte at +2, sound address at +4, byte length at +8, format byte at +12; prepares a 72-byte port template |
| `0x91` Close | `0x43b8` | Port byte at +2; restores its entire template to defaults |
| `0x92` RequestEvent | `0x43e8` | Port byte at +2, target cursor word at +4; arms a cursor-crossing notification |
| `0x93` Play | `0x4428` | Port byte at +2, loop control word at +8; copies template to channel and starts it |
| `0x94` Stop | `0x46a4` | Port byte at +2; clears channel key-on and loop bits in its hardware write |
| `0x96` Volume | `0x4720` | Port byte at +2, volume byte at +3; sets direct-output level |
| `0x97` Pan | `0x4768` | Port byte at +2, pan byte at +3; maps the host pan scale into direct-output pan |
| `0x9c` MultiPlay | `0x4570` | Loop byte at +2, upper mask at +4, lower mask at +8; prepares selected channels and performs one final key latch |
| `0x9d` MultiStop | `0x463c` | Upper mask at +4, lower mask at +8; stops selected channels |

Open converts format zero's byte length to samples by division by two, leaves
format one's count unchanged, and doubles larger format counts. It sets the
start sample to zero and the end sample to that converted count minus one.
Hardware format zero is signed 16-bit PCM; format one is signed 8-bit PCM.
The adapter must use format zero and a nonzero, even byte length whose converted
end fits the supported 16-bit end register.

Open ORs the sound-address high bits and format into the existing template;
it does not clear previous address/format bits first. Fresh excluded ports or a
successful Close before a changed Open are required. Stop changes the hardware
control but does not reset the stored template. A stop alone is not sufficient
to admit a new address/format after a different prior Open.

## Ports 62 and 63, synchronized request, finite end

MultiPlay visits selected ports from 63 down to zero. Upper mask `0xc0000000`
and lower mask zero select exactly ports 63 and 62. While copying each selected
template, it suppresses hardware key-execute bit `0x8000`. After all selected
channels are configured, the original control word is written once to the last
selected channel, port 62. The templates' default control is `0xc000`, supplying
key-on and key-execute. This is a shared hardware key latch, stronger than two
unrelated queued mono starts. Actual audible phase still requires hardware
validation with equal format, pitch, length and start state.

The pan handler maps host values below 16 to their negated low nibble, with
host zero forced to hardware `0x0f`; values 16–31 pass through. The Yamaha
manual's mixer table on PDF page 28 (zero-based page 27) makes hardware `0x0f`
right only and `0x1f` left only. Thus a pair with port 62 carrying left samples
uses host pan 31 on port 62 and host pan zero on port 63. Hardware pan values
cannot be passed directly as though they were the host API scale.

| State | Port 62 ARM offset | Port 63 ARM offset |
| --- | --- | --- |
| 72-byte template | `0x2ca8` | `0x2cf0` |
| Hardware channel base | `0x801f00` | `0x801f80` |
| Driver playing byte | `0x14a6` | `0x14a7` |
| Published cursor word | `0x15e8` | `0x15ec` |
| Previous cursor word | `0x16e8` | `0x16ec` |

MultiPlay reduces its loop byte to bit zero, placing it in hardware loop-control
bit nine (`0x0200`). Loop argument zero therefore selects finite playback;
`0xff` selects forward looping. Single Play performs the equivalent reduction.
Stop and MultiStop clear `0x4200` (key-on plus loop control) in their channel
control writes. Their masks must remain restricted to the owned ports.

The primary hardware reference is Yamaha's
[AICA (FQ8005) Sound-block User's Manual, Ver. 1.00](https://manuals.plus/m/d260f79355dbff7664bdab58c9b8c9bc4b04fa272fdfc7c933ea28a47a167836.pdf),
PDF pages 9–10 (zero-based pages 8–9). It defines loop-control zero as playback
ending at the programmed end address; start/end addresses use sample counts,
and PCM format zero is signed 16-bit data. It also prohibits end address
`0xffff`. This is a hardware finite-end contract, independent of an SH callback
or the ARM cursor-service poll.

A proposed first pilot with 32768 bytes per mono channel therefore programs
start zero, end `0x3fff`, and finite mode, using 65536 sound bytes for the pair.
At 44100 Hz its nominal block duration is about 371.5 ms. Exact audible last
sample/interpolation timing is not established by this static analysis.
After a stall, an exhausted finite block can become silence; it does not need
to replay a stale circular buffer. The adapter must not silently convert it to
loop mode to hide gaps or enqueue an uncontrolled chain of restarts.

## Initialization, service and limits

The reset vector reaches initialization at `0x0100`; the normal stack starts
at `0xb000`. Initialization `0x058c` creates all 64 templates, initializes shared
hardware/state, and the completed startup publishes first free sound memory
`0x30040` at shared header offset `0xec` and internal word `0xb408`. That is
driver metadata for the game sound heap; it does not authorize an adapter to
use an unallocated address there.

Timer-A/FIQ setup is performed at `0x02d0`, using the existing AICA interrupt
registers. The FIQ vector reaches `0x01a4`; its timer path reloads Timer A and
increments shared/internal counters. The normal ARM loop, rather than that
FIQ handler, consumes host packets and polls port positions. The adapter must
preserve this driver, its interrupt configuration, DSP state, mixer and ARM
execution. It must not call driver startup, reset ARM, install another driver,
or take over the AICA timer to obtain service.

Service `0x4aa8` scans the 64 playing bytes and samples active channels using
`0x4be4`. MultiPlay marks both owned bytes `0xff` and initializes their current
cursor to zero and previous cursor to `0xffffffff`. Cursor service later reads
hardware position and retires finite channels whose sampled cursor has stopped
changing. Hardware finite end precedes and does not depend on this bookkeeping.
The cursor table is a sampled publication, not a continuously updated played
counter or an acknowledged state transition. Retirement compares the previous
sampled current position directly with the new hardware position; the service
does not read the hardware's clear-on-read loop-end flag. A retired byte alone
therefore does not prove that every sample through the programmed end played.

RequestEvent stores one per-port target and enables a crossing check in the
normal ARM service. Notifications use a 64-byte event ring and trigger the
existing host interrupt. This does not establish queued block chaining, a
deadline stop, or an SH stall watchdog. The first finite pilot need not alter
the event ring or interrupt handler.

## Loaded-driver admission identity

The installed image cannot retain its file CRC: its header, templates, cursor
arrays and inline work words change during initialization and service. These
half-open sound offsets identify immutable instructions/constants needed by
the proposed finite path. CRC32 values are measured from the supplied file;
none of these ranges contains an identified driver write target.

| Sound offsets `[start,end)` | Bytes | CRC32 |
| --- | ---: | --- |
| `0x0000..0x0020` | 32 | `81ec56e2` |
| `0x0100..0x0188` | 136 | `4edae6a3` |
| Default port template `0x1af0..0x1b38` | 72 | `09dfe1c1` |
| `0x39a0..0x39fc` | 92 | `760de545` |
| `0x3a08..0x3c84` | 636 | `353d913f` |
| `0x4338..0x456c` | 564 | `c36eea6f` |
| `0x4570..0x46d0` | 352 | `876af872` |
| Volume/pan `0x4720..0x47c8` | 168 | `44871c65` |
| Active/event service `0x4aa8..0x4bdc` | 308 | `786f7c6c` |
| Cursor sampling/retirement `0x4be4..0x4cb4` | 208 | `1bd0f36a` |
| `0x4d3c..0x4d64` | 40 | `b6ec0894` |
| Stable name/version header `0x0020..0x00c0` | 160 | `f89d79c5` |

The header version at `0x88` is `1.80d.2`. Fixed header pointer words are
`0xc0=0xbf00`, `0xc4=0xb600`, `0xc8=0xb400`, `0xcc=0xc000`,
`0xd0=0xb200`, `0xd4=0xc800`, `0xd8=0xbe00`, `0xdc=0x100`,
`0xe0=0x1468`, and `0xe8=0x14f0`. The event producer at `0xe4` is mutable
within `[0x14a8,0x14e8)`; the initialized free-memory word at `0xec` is
`0x30040`. The channel hardware-base word at `0x1464` becomes `0x800000`.
Validate loaded state through read-only sound RAM, including legal mutable
pointer ranges, without writing the shared hardware monitor selector. These
range CRCs check the selected implementation; they are not a hash of a pristine
full loaded image or permission to reset it.

Playing bytes `0x14a6` and `0x14a7` can be read without a hardware monitor write.
They are initially zero, become `0xff` during Play application, and later return
to zero through ARM service. A finite pilot must distinguish never applied,
observed active, and retired within its own sound generation. It must not
overwrite a pending or active bank merely because a sampled byte is zero.
Safe reuse also needs a conservative finite-duration deadline measured from
acknowledged application, with the intended pitch and unchanged generation.
This avoids treating early unchanged-cursor retirement as complete playback;
the exact service timing and sample completion remain hardware checks.

Remaining admission gates are actual main/sound heap leases and port exclusion,
fresh template state, command failure/application handling, sound-generation
and global-stop/reset boundaries, host ABI/cache/interrupt preservation, and
measured stereo start/end behavior on hardware. Continuous gap-free playback
requires a later bounded scheduling mechanism. Finite mode resolves the stale
loop failure mode; it does not by itself prove continuous CDDA service.
