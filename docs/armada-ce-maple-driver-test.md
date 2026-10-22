<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE Maple driver metadata test

This isolated test adds a **DRIVER** page to the DISCOVERY and MAPPING
observations. It checks a small set of driver metadata and the mappings of
its transmit and receive buffers. It remains read-only. Virtual A2 is
unavailable, and this is not a save test.

## Install, photograph and restore

1. With the console off, back up the current
   `KUI/apps/games/ce-probe.kui` from the SCI SD card to your PC. Keep that
   backup: the test package does not include an exact rollback module.
2. Install the test's `KUI/apps/games/ce-probe.kui`. This is the only installed
   file. Keep physical VMUs and existing virtual-card files unchanged; do not
   recreate or format a card.
3. Cold boot and select **Original Armada** with the same original raw GDI.
   Use **SCI SD, Background loading On, CD music Off, virtual VMU Off**.
4. Stay at the menu for about **30 seconds**, then play for about **one minute**.
   Note whether controls, effects and loading behave normally.
5. Press **A+B+X+Y+Start**. The terminal cycles through **DISCOVERY**,
   **MAPPING** and **DRIVER**, showing each for 300 frames, approximately five
   seconds. Photograph the whole screen on **all three pages**, including
   FAULT rows and the BUILD label. Wait for another cycle if needed.
6. Power off after taking the photographs. Restore your backed-up
   `ce-probe.kui` before the next cold boot to return to the previously
   installed module.

Send all three pages even if a check is refused. Partial rows and their fault
witnesses are useful; zero values alone do not establish success.

## What is being checked

DISCOVERY retains module-list and fixed-register observations. An earlier
photograph showed four stable samples, no refusals and 339 reads, with MAPLE
attachment mask `8`. Its missing ROM-backed MAPLEDEV marker was a diagnostic
result, not a crash or proof that MAPLEDEV was absent. Armada's save path uses
MAPLEDEV APIs; an external loaded copy remains possible.

MAPPING retains the 32-byte Maple code fingerprint at RVA `0x5b0c`, expected
CRC32 `7bd7ff55`, and independently resolved owner and canonical roots at RVA
`0x72b8`. A later mapping photograph reported owner-root value `00030a60` and
canonical-root value zero. The source allocation path makes the owner's value
a process-relative heap pointer: with the separately validated slot `08000000`,
its candidate address is `08030a60`. The new test must validate its mapping
again; those earlier values do not establish a current allocation.

| DRIVER row | Meaning |
| --- | --- |
| STATUS READS MASK RAW EXT VA | Result, read count, completed checks, raw root value and resolved extension address |
| EXT PHYSICAL PAGE SHUTDOWN MMIO SIZE | Extension backing and page witness, shutdown word, raw MMIO pointer and reviewed allocation size |
| TX BUS RAW VA LAST END PHYSICAL | Transmit bus address, original virtual pointer, builder cursors and resolved backing |
| RX BUS HIGH RAW VA BIAS PHYSICAL | Receive physical address words, original virtual pointer, address bias and resolved backing |
| TX PAGE RX PAGE0 PAGE1 SLOT0 OWNER | Buffer page-table witnesses and process context |
| FAULT WHY AT VALUE EXPECT DETAIL | Refusal reason and its evidence |

The extension size `0x198` comes from the reviewed allocation instruction; it
is not an object signature read from memory. The DRIVER page reads ten
selected metadata words. It checks mapping metadata for one transmit page
(`0x1000` bytes) and two receive pages (`0x2000` bytes), including uncached
attributes and agreement with the recorded physical addresses. **It does not
dereference transmit or receive buffer pointers.** The raw MMIO pointer is recorded
without following it.

An accepted active sample also requires a zero shutdown word and disjoint
transmit/receive ranges in both virtual and physical address domains, separated
from the sampled extension metadata. Local
pointers must be strictly above `00010000` and below `02000000` before the
validated owner slot is applied. These are conservative probe restrictions.

## Console result: build `b5bcde4bcf64`

The three owner console photographs reported four stable DISCOVERY samples,
no refusals, a successful MAPPING proof, and a successful DRIVER observation.
The DRIVER result used 1,029 reads, completed mask `1f`, and left all five
FAULT values zero. The full transcription is retained in
[the console evidence](evidence/armada-ce-maple-console-b5bcde.json).

The sampled root normalized from `00030a60` to owner address `08030a60`.
TX VA `00130000` mapped to bus `0cfd3000`; RX VA `00100000` mapped to bus
`0cfd1000`, with both RX pages contiguous. All three buffer PTEs had cache bit
`8` clear. The TX last and end cursors were `00130028` and `00130034`.
TTB slot zero and the owner entry both held `8cf93000`.

These are accepted, repeated metadata and mapping observations. The test did
not read request or response buffer headers, pin the allocation, establish a
DMA ownership window, enable virtual A2, or test save reads and writes.

## Developer scope and evidence

The exact source hashes, reviewed RVA ranges, code hashes, ten field offsets
and pointer rules are recorded in
[the metadata evidence](evidence/armada-ce-maple-state-contracts.json).
That file separates source findings, prior-photo readings and conservative
probe restrictions. It contains no owner executable bytes or disassembly.

Sampling remains limited to accepted GD PLAY/PLAY2 requests, capped at 32
attempts per boot. The probe repeats context, root, metadata and mapping
witnesses. Page changes display retained snapshots without resampling CE.
Mutable cursors or a changed mapping can cause a useful refusal.

DriverEntry publishes the root before initialization completes. The object
has no established self-pointer, magic or size member, and a zero shutdown
word does not establish allocation identity or pin its lifetime. Matching metadata and ranges establish a
sampled relationship; they do not establish ownership of DMA or interrupts.
The test installs no hook, follows no descriptor chain, calls no CE or native
Maple API, and writes no guest memory or hardware. Extension lifetime,
interrupt ownership and VBlank-triggered DMA timing remain unresolved. The
native virtual-A2 backend remains disconnected.
