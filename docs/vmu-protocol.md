# Experimental virtual VMU protocol

`src/core/vmu_emulation.c` is an independently written, allocation-free Maple
memory-card endpoint. It is a protocol component, not a complete retail-game
hook. Hardware DMA scheduling, controller enumeration, resident-memory safety,
and backing-store durability are separate requirements. Host tests do not
establish retail-game compatibility.

## Endpoint contract

Initialize one caller-owned `kui_vmu_emulation_t` with a single subunit address
and read/commit callbacks. A1 is `0x01`; B2 is `0x42`. The backend handles only
requests from that port's host to that precise subunit. It never changes a real
controller, another card, or another port. The caller must serialize access and
provide nonoverlapping request, response, and state buffers.

The interface uses **Dreamcast CPU/DMA memory order**, matching pinned KOS's
`maple_response_t` and VMU command construction. It does not accept an array of
big-endian numeric word values from a physical-wire protocol diagram.

| Field | DMA bytes |
| --- | --- |
| Frame header | command, destination, source, payload word count |
| Memory-card function | `00 00 00 02` (`0x02000000` as a little-endian CPU integer) |
| Block location | partition, phase, block high byte, block low byte |
| Stored bytes | unchanged; no 32-bit byte swapping |
| File-error flags | `00 00 00 flags` |

For example, read block 42 from A1 is
`0b 01 00 02 00 00 00 02 00 00 00 2a`. Its successful response has 130 payload
words: function, echoed location, and 512 stored bytes, for 524 bytes including
the header. The serial Maple XOR check byte is handled below this API by the
transport/hardware; it is absent from DMA buffers. The advertised storage
function does not require a separate block-data CRC16. An extra CRC field is an
invalid packet length, rather than silently ignored data.

## Implemented commands

| Command | Response and behavior |
| --- | --- |
| 1 / 2, device / extended information | Storage only; 28 / 48 payload words. Honest K-UI identity, no LCD/clock/processor functions. |
| 3, reset | Discard unfinished RAM transaction and acknowledge. Committed storage remains unchanged. |
| 4, kill | Acknowledge, then remain silent until hardware/API reset. Silence is represented by the exact four-byte DMA timeout marker `ff ff ff ff`. |
| 10, media information | Standard 128 KiB, 256 physical blocks, 200 user blocks, root255, FAT254x1, directory253 descending x13. Standard VMU compatibility parameters200/31; no executable engine. |
| 11, read | One 512-byte access, phase0, partition0, block0..255. Read committed storage, never incomplete staging bytes. |
| 12, write | Four 128-byte accesses, phases0..3. Acknowledge phase receipt into the single staged block. |
| 13, get last error / sync | Phase4 commits a complete write; phase1 can acknowledge a successful one-phase read. |

Other commands return unknown-command. Unsupported function codes return
bad-function. Invalid lengths, partitions, blocks, and phase sequences return
the corresponding documented file-error flag. A backing-store busy result
requests retransmission (`0xfc`); other storage failures return file-error.

The fixed media information intentionally targets standard 200-block images;
241-block/custom layouts are outside this milestone. The data endpoint can
address all 256 physical blocks because filesystem structures must also be
read and written.

## Transaction and durability rules

Phase0 starts a new staged block. Phases1..3 must be consecutive and match its
block. Identical duplicate phases are acknowledged; conflicting duplicates
return phase-error. A different/new phase0 restarts an incomplete transaction.
An identical duplicate phase0 preserves later phases. Rejected malformed
packets do not fill missing phases or call storage. This backend tolerates
unrelated probes/reads without destroying the staged transaction; it does not
try to reproduce every physical flash timing limitation.

Four successful phase responses mean only that all bytes were received.
**Successful phase4 sync means the commit callback returned durable success.**
There is no successful final ACK after a failed or busy commit. The full block
remains available for retry. Once commit has been attempted, changed bytes are
rejected with retry until successful commit or explicit reset; identical phase
retransmissions remain safe. An immediately repeated successful sync is
acknowledged without reprogramming storage.

The commit callback must be idempotent and may return success only when all
512 bytes survive backing-store reopen/power loss according to its durability
contract. Neither callback may retain its buffer pointer after returning. A
store that returns errors must enforce its own recovery policy; the protocol
cannot repair an uncertain SD write. Reset can discard an uncommitted RAM
transaction but never initiates a storage write.

Output capacity is checked before callbacks or transaction changes. The
maximum response is 524 bytes. Callback and state memory are bounded; the
backend stores one block rather than an entire 128 KiB card.

## Independent protocol references

- [Pinned KOS Maple declarations](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/maple.h):
  CPU function masks, command numbers, DMA response and identity layout.
- [Pinned KOS VMU host implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/maple/vmu.c):
  four-phase writes, byte packing, 512-byte reads, phase4 sync.
- [Marcus Comstedt's Maple documentation](https://mc.pp.se/dc/maplebus.html):
  addressing, transfer framing, command/error definitions, hardware timeout.
- [Marcus Comstedt's VMU flash documentation](https://mc.pp.se/dc/vms/flashmem.html):
  standard filesystem geometry and unchanged stored-byte order.
- [Public FT1 Storage Function hardware specification, v0.80](https://github.com/Kochise/dreamcast-docs/blob/master/CONTROLR/DOCS/Ft1_080e.doc):
  function definition, media structure, get-last-error sequencing, kill behavior.
  The hardware document is referenced, not bundled with K-UI.
- [Linux hardware driver media decoding](https://github.com/torvalds/linux/blob/b8910630c967ffee582289451ddb5f9f19c26872/drivers/mtd/maps/vmu-flash.c)
  and [protocol error declarations](https://github.com/torvalds/linux/blob/b8910630c967ffee582289451ddb5f9f19c26872/include/linux/maple.h):
  independent host-side checks on mixed media-field byte order and error flags.

No DreamShell implementation was read or used for this component. The code
does not import an emulator's packet-dispatch algorithm.

## Validation

`tests/test_vmu_emulation.c` uses literal DMA byte fixtures. It checks all256
physical blocks, identity/media responses, every valid subunit on four ports,
incorrect endpoints/function endian order, short/excess packets, every short
output capacity for major commands, out-of-order/conflicting phases, immutable
pending writes, busy/error commit retries, reset/kill, and repeated final sync.
Exact allocations for malformed input lengths make out-of-bounds reads visible
to ASan. The suite passes with AddressSanitizer and UndefinedBehaviorSanitizer.
LeakSanitizer is disabled in the managed host because its process inspection is
restricted; production protocol code allocates no memory.

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
  src/core/vmu_emulation.c tests/test_vmu_emulation.c \
  -o build/test-vmu-emulation
ASAN_OPTIONS=detect_leaks=0 ./build/test-vmu-emulation
```
