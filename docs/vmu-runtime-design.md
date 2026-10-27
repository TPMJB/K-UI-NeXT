<!-- SPDX-License-Identifier: GPL-3.0-only -->
# In-game virtual VMU: runtime ownership and first retail profile

The protocol, persistence and DMA-list components need two additional proofs
before they can serve a retail game: interception before any virtual command
reaches a physical device, and memory which the game has actually relinquished.
The standalone KOS diagnostic supplies both by owning its executable. It is the
first hardware target. Passing that diagnostic does not admit retail games.

## Native resident budget

`retail_resident_low.ld` reserves `0x8c004000..0x8c007800` for the standard
reader, followed by its guarded stack to `0x8c007d00`. The background reader
extends code/data to `0x8c007ba0` and has a smaller stack. The archived Oct 6
linked audit found 12 bytes spare in standard SCI and no bytes spare in the
background SCI reader. These historical numbers must be recomputed after code
changes; neither establishes room for the VMU service. A 128 KiB card image
does not fit in either reservation.

IP/bootstrap, arbitrary top-of-RAM addresses, OCRAM, AICA RAM and VRAM are not
available allocations. A successful boot or a region containing zeroes cannot
establish ownership. Leave normal 2.0.0 readers intact while investigating a
separate VMU profile.

## Existing main-RAM lease provides a concrete first route

The authored Toy Commander pilot already obtains main RAM from the game's
actual allocator. Relevant source:

- `src/loader/toy_pilot_boot.S`: wraps the exact heap-init slot, calls the
  original initializer with original caller state, and refuses repeat
  initialization before it can destroy a live lease.
- `src/loader/toy_pilot_stage.c:kui_toy_pilot_stage_install`: verifies exact
  executable identity, startup witnesses and live heap metadata; calls
  `0x8c0b3ede(0x30000)`; requires returned `0x8cfd0000` and matching allocation
  records before copying its worker.
- `src/loader/toy_pilot_resident.c`: excludes the owned worker from GD-read
  destinations and shares the SCI bus through its existing acquire/release
  interface.

The 192 KiB main lease has room in principle for a 128 KiB VMU and a service
whose measured code/data/private-stack envelope fits the remaining 64 KiB.
That is a budget candidate, not an already measured VMU module. The VMU
profile would use the lease independently with CD music disabled. It must not
also start the CDDA worker in the same lease.

`toy_pilot_lease.c` is a different allocator for the native SOUND heap. Do not
use that file to claim main RAM for a VMU.

Windows CE's admitted CDDA modules instead lower an exact kernel ROMHDR
RAMend before boot and reserve a typed pool. This is useful architectural
precedent, but its fixed addresses and guards do not admit a native SDK game.

## Maple hook requirements

Primary source is KallistiOS at the revision pinned in `dependencies.json`,
`fcfa7d869471591ca1c777543261a7bfea7cb726`:

- `kernel/arch/dreamcast/include/dc/maple.h`: Maple register addresses,
  software/VBlank triggers and protocol constants.
- `kernel/arch/dreamcast/hardware/maple/maple_queue.c:maple_queue_flush`:
  outgoing descriptor construction, receive address and last-descriptor bit.
- `kernel/arch/dreamcast/hardware/maple/maple_irq.c:maple_dma_irq_hnd`:
  responses are consumed by callbacks after the DMA completion interrupt.
- `kernel/arch/dreamcast/hardware/maple/maple_utils.c`: DMA address/start and
  physical controller/subunit addressing.

A transparent adapter must filter requests before launch, execute only the
allowed physical frames, synthesize virtual replies, splice controller
presence bits and finish before the game's first response consumer. A game
may consume responses by polling rather than by IRQ. Hooking only the IRQ
cannot cover that path. An all-virtual transfer still needs the game's
expected completion event; submitting nothing and hoping an IRQ occurs is
incorrect.

An alternative worth proving on the exact first title is a synchronous
pre-start bridge: keep the game's exclusion active, perform the filtered
physical DMA, wait boundedly for idle, synthesize virtual replies, and resume
past the original start store with its real completion event pending. This
requires proof of software trigger mode, preservation of post-start SDK
bookkeeping, correct stale-IRQ treatment and all relevant start paths. It
must use a benign owned controller probe if a real completion is needed for
an otherwise all-virtual transfer. No virtual write reaches a physical VMU.

## UBC is a diagnostic aid, not a generic interception proof

The official [Renesas SH7750 hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware),
revision 7.02, sections 20.3.7 and 20.3.9, establishes that an address-only
operand breakpoint occurs after the matching instruction executes; adding
data comparison can delay the exception further. A watch on the DMA-start
store cannot prevent that store. Instruction breakpoints support execution
before a chosen instruction, but need an exact callsite. User breaks can be
masked by SR.BL. DBR can decouple the handler destination from VBR, but does
not remove those timing and masking limits. The KOS UBC implementation uses
its own exception machinery; installing it before handing the machine to a
retail game does not preserve that machinery automatically.

Watching MDSTAR before a later software MDST write may help observe a known
driver, but it does not establish coverage of VBlank launch, reset paths or
other writers. An owned KOS UBC experiment can test these mechanics without
claiming a retail hook.

## Offline probe and actual owner evidence

Run the read-only candidate finder on an owner-extracted unscrambled
`1ST_READ.BIN` (the existing extracted filename may differ):

```sh
python3 tools/vmu_retail_probe.py /owner/1ST_READ.BIN --ip /owner/IP.BIN > vmu-probe.json
```

`--ip` is optional and accepts a complete 32 KiB bootstrap. The scanner hashes
the complete executable, finds Maple/Holly MMIO literals and their exact
PC-relative SH4 load references, and follows a bounded explicit instruction
subset. Unknown instructions discard constant knowledge. It never generates
patches or modifies the input. Every report states `retail_admitted: false`;
data may look like code and dynamic pointer paths may be missed.

The recovered owner `ToyCommander-1GUTH.BIN` has 748444 bytes, CRC32
`cdc493b3`, SHA256
`ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd`.
It matches the existing main-heap profile and all six startup witnesses.
This confirms file identity, not a live lease or hook.

Independent SH4 disassembly of that exact input locates these additional
candidate contracts:

| Address | Observation in the unchanged owner image |
| --- | --- |
| `0x8c0c0938` | Startup routine uses manager pointer stored at `0x8c1223a4` |
| `0x8c0c0942` | Stores Maple base `0xa05f6c00` at manager offset `0x10f4` |
| `0x8c0bede6` | Initialization writes zero to Maple trigger select |
| `0x8c0bf020` | Another initialization/reset path writes trigger select |
| `0x8c0bf152` | Candidate DMA submission routine entry |
| `0x8c0bf176` | Reads Maple state through the manager's base pointer |
| `0x8c0bf262` | Stores outgoing DMA descriptor address through base + 4 |
| `0x8c0bf26a` | Starts DMA with `r12 = 1` through base + 24 |
| `0x8c0bf26c` onward | SDK updates queue/selector bookkeeping after start |
| `0x8c0bf450`, `0x8c0bf4a8` | Polling paths read DMA state |

These identify where a diagnostic must observe; they authorize no patch.
The exact original words must be checked together with full file identity,
and live manager contents must match the expected Maple base before any
interception experiment.

## Evidence required for the next retail experiment

The next isolated diagnostic needs the original owner executable and its
IP.BIN, the new module's linked map and stack bound, and a bounded live trace
covering controller discovery plus entering a save menu. It must establish:

1. The actual heap initialization arguments, real allocator return and
   allocation metadata; no GD destination overlaps the lease.
2. The live manager pointer, Maple base, MDSTAR, MDST and trigger selection at
   every relevant submission; the hardware is idle before descriptor edits.
3. The inherited SR/IMASK/BL state at interception and through SDK post-start
   bookkeeping; no early completion consumer sees unfinished replies.
4. The game's response-consumer path and completion status/ACK behavior,
   including polling, real controller frames and absent physical VMU frames.
5. Bounded SCI bus ownership shared with GD reads. A deferred/busy save never
   reports successful durable completion prematurely.

Only after those observations should the exact Toy profile become an
optional runtime. Then test discovery/read first, and finally save, cold power
off, reboot and load using a disposable virtual card. Broader title admission,
background reading, CDDA and Windows CE remain separate proofs.
