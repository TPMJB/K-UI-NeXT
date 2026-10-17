# Experimental virtual VMU: Maple schedule boundary

The packet backend and schedule adapter are implemented. They do not yet install
a retail virtual-card service. A separate exact-Toy observation module installs
a read-only pre-start counter hook; it does not emulate a card. A software-chain
test passing demonstrates packet routing;
it does not demonstrate that a commercial game can discover, save to, and reload
a virtual card. The native harness must label that distinction.

## The implemented boundary

`include/kui/vmu_maple_dma.h` and `src/loader/vmu_maple_dma.c` implement an
allocation-free, bounded adapter around caller-owned Maple DMA schedules.
They do not touch MMIO, install exceptions, change IRQ state, or cast untrusted
physical addresses into pointers. The caller explicitly supplies owned response
regions and performs any cache maintenance or hardware transfer.

1. `kui_vmu_maple_dma_prepare()` validates the complete source chain, response
   addresses, packet lengths, port/source addresses, output capacity, and buffer
   separation. Every response needs its own nonoverlapping 1024-byte owned slot.
2. It copies physical-device descriptors unchanged into a separate chain and
   removes descriptors addressed to one configured virtual subunit. It repairs
   the LAST bit, including when the original last descriptor was virtual.
3. After real DMA completes, `kui_vmu_maple_dma_complete()` passes virtual packets
   to the emulation backend. Successful physical main-peripheral responses gain
   the virtual subunit bit in their source address, preserving physical subunit
   bits and all controller payload bytes.
4. The caller exposes completion only after virtual operations and controller
   enumeration updates finish. It performs software completion for an empty
   filtered chain; it must not submit an empty hardware chain.

There are at most 32 descriptors. Only normal unicast transfers are admitted.
Gun/reset/NOP descriptor patterns, multicast addresses, overlapping response
slots, bad/unknown addresses, and malformed chains are declined. An invalid plan
does not invoke storage or change response buffers. A failing completion is
terminal: repeating an already attempted save commit would be unsafe.

The response-region API describes host memory ownership, not a retail memory
validator. A future retail profile must independently prove its DMA-list bounds,
response allocations, resident exclusion, and cached/uncached alias handling.
On SH4, use uncached P2 aliases for DMA-facing owned memory. A caller must keep
the source schedule immutable until completion and exclude every other Maple
submitter. The adapter is single-caller and is not an interrupt-safe scheduler.

## Exact descriptor and enumeration facts

The source of truth is the project's pinned KallistiOS revision
`fcfa7d869471591ca1c777543261a7bfea7cb726`, inspected locally from the source
archive shipped with 2.0.0. No DreamShell loader implementation was consulted.

| Offset | CPU/DMA representation |
| --- | --- |
| 0 | LE32 control: payload word count in bits 7:0, port in bits 17:16, LAST in bit 31 |
| 4 | LE32 physical response address |
| 8 | Header bytes `{command, destination, source, payload_words}` |
| 12 | Unchanged payload bytes, four per word |

A normal descriptor is `12 + 4 * payload_words` bytes. The Maple packet header
is included in the transfer, but excluded from the control length. The descriptor
list address is 32-byte aligned. Response allocations are 1024 bytes because a
physical peripheral determines its response packet length. The hardware timeout
marker is one word `0xffffffff`.

A1 through A5 have addresses `0x01`, `0x02`, `0x04`, `0x08`, `0x10`; the main
controller is `0x20`. The high two address bits select physical port A through D.
When the main peripheral responds, its source-address subunit bits announce
attached devices. Returning virtual DEVINFO alone is insufficient: KOS's
enumerator obtains that mask from the real controller's DEVINFO response before
it probes each subunit. The adapter adds only the configured bit to successful
responses from the configured physical parent. A missing controller is preserved
as missing; the adapter does not invent controller input.

The configured virtual slot must be selected deliberately. Routing commands for
A2 to a virtual card hides a physical device in A2. The initial harness should
use an empty slot, retain the physical controller, and avoid physical VMU writes.
The adapter filters all commands for that slot, including LCD and clock commands;
it does not combine virtual memory with a physical screen in the same slot.
Physical traffic runs before virtual completion, so admission must also establish
that cross-device interleaving in the original list is not required.

Relevant primary implementation references:

- [Pinned KOS descriptor writer](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/maple/maple_queue.c)
- [Pinned KOS completion and subunit enumeration](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/maple/maple_irq.c)
- [Pinned KOS register and response declarations](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/maple.h)
- [Pinned KOS address encoding and DMA submission](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/maple/maple_utils.c)
- [Marcus Comstedt's hardware description](https://mc.pp.se/dc/maplebus.html)

## Why a generic MDST watchpoint is insufficient

The relevant Maple registers are MDSTAR (`0xa05f6c04`, descriptor-list pointer),
MDTSEL (`0xa05f6c10`, software/VBlank trigger), MDEN (`0xa05f6c14`, enable), and
MDST (`0xa05f6c18`, software start and busy state). Physical completion is normal
ASIC interrupt bit 12; an IRQ consumer is only one possible completion consumer.
A game may poll MDST directly.

The Renesas SH7750 hardware manual, sections 20.3.6 and 20.3.7, specifies that
an address-only operand break follows the matching instruction. Including a data
condition can delay the exception by additional instructions. Consequently a
watch on the MDST store cannot guarantee interception before hardware owns the
list. Changing a live DMA list is not an acceptable first implementation.

An address-only MDSTAR watch is a possible research mechanism only after proving
that the bus is idle, the game uses software triggering, and no earlier arming
allows a VBlank-triggered transfer. It still requires exception ownership and a
completion boundary. It is not implemented or advertised as working.

Primary processor reference:
[Renesas SH7750/SH7750S/SH7750R hardware manual, Rev. 7.02](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware).

## Practical route to a retail proof

First validate an owned mixed schedule on a Dreamcast: a real controller request
must reach Maple hardware, its response must survive, a virtual subunit must be
advertised, and the virtual packet must reach the durable backing store. Exclude
KOS's periodic Maple scheduler for the transfer and restore it on every exit.
This proves transport and storage plumbing without guessed game patches.

For the first retail title, admit a single exact executable and its known memory
lease. A literal reference to a Maple register is merely a candidate; it does not
prove an SDK submission function, all callsites, or response-buffer bounds.
Validate the entire pre-submit path and every completion consumer before enabling
patches. The ordinary 2.0.0 resident and Games launch path remain untouched.

Two implementation options are worth distinguishing:

- An asynchronous wrapper filters before MDST and splices replies before the
  game's IRQ/callback **and** before every polling consumer. It must preserve
  pending interrupts, physical-device traffic, and CPU/cache state.
- A synchronous pre-submit wrapper can finish the filtered physical transfer,
  virtual storage work, and source-mask splice before resuming the game. It must
  skip the original MDST store, preserve the real completion event, and measure
  latency. An all-virtual schedule would need a benign owned DEVINFO transfer
  into an owned scratch reply to generate a physical DMA completion event;
  forwarding a virtual write to a real card is never a substitute.

Both require serialization with SCI game reads and a resident allocation that
the game cannot reclaim. Neither follows merely from passing host packet tests.
The exact Toy Commander allocation already used for the separate CDDA experiment
is a candidate for a VMU-only experiment with CDDA disabled, subject to its own
admission and memory-size audit.

The optional `Makefile.vmu-toy` package advances the retail evidence step with a
read-only hook before Toy's exact software-start store. It replays the six
overwritten instructions, counts bounded descriptor headers, and reports live
Maple trigger/enable/busy state plus inherited CPU interrupt masks. It stops for
four photo pages after 1200 attempted starts. It injects no VMU command; the
game's real physical-card operations continue. See
[the observer test instructions](vmu-maple-observer-test.md). Console results are
still required before treating this candidate as a validated retail hook.

## Host validation

Run:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -g -Iinclude \
  src/loader/vmu_maple_dma.c tests/test_vmu_maple_dma.c \
  -o /tmp/test_vmu_maple_dma
ASAN_OPTIONS=detect_leaks=0 /tmp/test_vmu_maple_dma
```

Leak detection is disabled only because the execution host restricts the process
inspection LeakSanitizer requires; address and undefined-behavior checks remain
enabled. Tests cover mixed physical/virtual schedules, parent mask preservation,
timeouts and errors, all-virtual completion, duplicate-completion rejection,
malformed/truncated/unowned/overlapping buffers, maximum packet size and frame
count, plus 12,000 bounded input mutations. They do not replace console results.
