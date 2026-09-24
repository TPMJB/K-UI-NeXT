# Image-backed GD BIOS interface: bounded proof

This implements a small independently authored request service for the next
Games probe. It is not a complete retail loader. The accepted original fixture
probe used K-UI's private ABI; this probe instead calls the actual Dreamcast
GD vector with an original executable and reads the selected image afterward.
Host tests establish the portable contract. The owner's DOA2 console result
now accepts the tested native hook, selected-image mapping and register/stack
handoff: build `c4cfd4585ec5`, 11 checks passed, 93 physical SD blocks after
launcher shutdown. [Hardware evidence](evidence/games-selected-image-hardware-2026-09-24.json).
Retail execution, cache/interrupt behavior and arbitrary game requests remain
outside that acceptance.

## Primary sources

The interface was checked against the repository's pinned KallistiOS revision
`fcfa7d869471591ca1c777543261a7bfea7cb726`, rather than DreamShell's loader:

- [syscalls.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/syscalls.c):
  indirect vector, r4-r7 convention, GD superfunction and function indices.
- [syscalls.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/syscalls.h):
  command numbers, parameter structures, status fields, TOC layout and datatype.
- [cdrom.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/cdrom.h):
  TOC field extraction, FAD terminology and buffer alignment.
- [cdrom.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/cdrom.c):
  submit/execute/check usage, datatype initialization, physical-address DMA
  buffers and the real driver's dependency on hardware DMA completion events.

The new service and tests are K-UI code. No DreamShell loader implementation,
retail executable, firmware implementation or firmware dump is included.

## Calling convention and supported surface

The pointer stored at `0x8c0000bc` selects the shared MISC/GD function. A GD
call supplies arguments in `r4` and `r5`, GD superfunction **0 in r6**, and
the function number in `r7`; the signed result returns in `r0`. The native
hook must preserve the caller's ABI and switch to resident-owned stack space.
The portable service receives those four registers as integers. It does not
access guessed host pointers or depend on KOS state.

| Function | Inputs | Implemented behavior |
| --- | --- | --- |
| 0, request | r4 command; r5 parameter address | Queue one supported request; positive signed token, or 0 on rejection/busy |
| 1, check | r4 token; r5 status address | Write four words; return -1 failed, 0 unknown, 1 processing or 2 completed; no SD access |
| 2, execute | unused | Execute the queued operation; return 0 |
| 3, initialize | unused | Discard any request and restore Mode 1/2048; return 0 |
| 4, drive status | r4 output address | Write `{busy=0 or paused=1, GD-ROM=0x80}`; return 0 |
| 5-7 | DMA streaming callback/transfer/check | Unsupported, return -1 |
| 8, abort | r4 token | Cancel a queued operation before I/O; return 0; other tokens/states return -1 |
| 9, reset | unused | Same bounded reset as function 3 |
| 10, datatype | r4 four-word parameter address | **rw=0 sets; rw=1 gets** the current mode; return 0 or -1 |
| 11-13 | PIO streaming callback/transfer/check | Unsupported, return -1 |
| 14-15 and other indices | undocumented/unimplemented | Return -1 |

Non-GD superfunctions are rejected by the portable service. It does not
implement MISC initialization or allow a guest to replace the handler. Other
firmware vectors, including physical VMU-related functionality, are outside
this service.

| Command | Parameters, 32-bit words | Supported result |
| --- | --- | --- |
| 16, PIO read | FAD, count, destination, test=0 | Read 1-64 sectors, destination aligned to two bytes |
| 17, DMA read request | FAD, count, destination, test=0 | Same bytes via CPU copy; destination aligned to 32 bytes |
| 19, GETTOC2 | area 0/1, destination | Write 408 bytes for selected low/high density track map |
| 24, initialize command | none | Restore Mode 1/2048 when executed |
| 29, NOP | none | Complete when executed |
| 33, stop | none | Complete without physical drive access |

Unsupported commands return request token 0 before backend I/O. This includes
CDDA playback, subcode/session requests and streaming commands. Firmware error
handling, license checks and all SDK-specific behavior are not claimed.

## Sector addresses, datatype and TOC

Image maps use zero-based LBA. GD read requests and TOC fields use FAD:
`FAD = LBA + 150`. The service rejects underflow, end overflow, counts above
64, image gaps and unsupported track/type combinations before queuing. The
backend's range check covers every sector; no implicit zero-fill is performed.

Only these datatype settings are accepted:

- Mode 1 user data: `{rw=0, part=0x2000, type=1024, bytes=2048}`.
- Raw data or audio sectors: `{rw=0, part=0x1000, type=0, bytes=2352}`.

Query uses rw=1 and preserves it while replacing the other three words.
The default after initialization/reset is Mode 1/2048. Mode 2/XA and other
sector sizes are rejected. Datatype changes and queries during a pending
request are rejected in this bounded implementation.

TOC is 99 track entries followed by first track, last track and leadout.
Unused entries are `0xffffffff`. Entries pack control in bits 28-31, ADR=1
in bits 24-27 and FAD in bits 0-23. First/last entries put the track number
in bits 16-23. The low area contains tracks beginning below LBA 45000; the
high area contains tracks beginning at or above LBA 45000. The leadout is
derived from the last mapped track's exclusive end plus 150. A GDI does not
preserve every original firmware TOC detail; this is explicitly a TOC derived
from the selected image's validated track files. Missing areas are rejected.

## Memory and lifecycle

Parameter blocks/status/TOC must be aligned to four bytes. Addresses may use
physical `0x0c...`, cached P1 `0x8c...`, or uncached P2 `0xac...` main-RAM
aliases. Every complete range is checked, then normalized to P1 for the
adapter. Other address areas are rejected rather than indiscriminately masked.
The first 64 KiB, resident code/data/stacks and wrapping/out-of-range buffers
cannot be used as guest outputs. The native adapter provides an explicit
guest interval; the host adapter maps the same 32-bit numbers to real host
memory without pointer truncation. Cache coherency outside the cache-off
probe is a later retail-loader responsibility.

Submission copies all request parameters and validates output bounds. Polling
never performs I/O. Execution validates output memory again before calling the
reader. A terminal result remains available until another request or reset;
old tokens then return unknown. Reset/initialize invalidate pending work without
reading it. Tokens are positive signed 32-bit values and wrap to 1.

The check status words are `{err1, err2, transferred_bytes, ATA_state}`.
Successful/pending operations have zero error fields. A failed operation has
err1=1 and a **K-UI-specific diagnostic** in err2: 1 backend I/O, 2 cancellation,
3 invalidated output mapping. Those values are not presented as a reverse
engineering of exact firmware sense codes. Pending ATA state is busy=4;
terminal state is internal=0. Failed reads report zero accepted bytes, even
if a backend partially touched the destination: callers must discard it.

Cancellation is before execution, not interruption of a blocking SD transfer.
A reentrant request is rejected; a reentrant status returns busy=4, while
other reentrant calls fail. No additional requests or callbacks run inside
the reader.

**DMA command acceptance currently proves buffer/request compatibility only.**
It does not start the Dreamcast's GD DMA hardware or raise its completion IRQ.
The source KOS driver shows why interrupt-driven retail clients need more work
before this can be called DMA-compatible game execution. Streaming, cache-on
operation, retail boot state and arbitrary game requests remain later gates.

## Verification

`tests/test_gd_service.c` runs without KOS under AddressSanitizer and
UndefinedBehaviorSanitizer. It checks independently specified TOC values,
FAD/LBA conversion, every byte of sample output, no I/O on submission/polling,
read-after-cancel, token reset/wrap/staleness, raw audio and Mode 1 constraints,
all three main-RAM aliases, firmware/resident/address-boundary rejection,
alignment, zero/oversized/overflowed requests, unmapped output after submission,
backend I/O failure, unsupported commands/functions and callback reentrancy.
The original native client has separately passed the selected DOA2 image test
on hardware. Neither that bounded pass nor portable tests establish retail-game
compatibility.
