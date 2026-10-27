<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE read-only virtual A2 test

The previous gate build cbfe36952c04 passed on the owner console: all 4,151
PRE/POST/held-window/confirmed-restore counters matched, with zero refusals,
faults or unresolved active state. The complete result is recorded in
evidence/armada-ce-maple-console-cbfe36.json.

This next isolated module advertises virtual A2 and serves a generated,
formatted, empty 200-block card. It exercises real CE discovery, metadata,
block reads and read completion through the original driver. Physical A1
remains available. The synthetic card has no SD image or persistent saves.
No write, format or save operation is acknowledged by its readonly backend.
Existing imported cards, game cards and journal files are not opened.

## Install and console test

1. Power off. Back up your current `KUI/apps/games/ce-probe.kui` to the PC.
2. Replace only that file with this package's `KUI/apps/games/ce-probe.kui`.
3. Keep the physical VMU in A1, A2 empty, and the controller connected.
4. Cold boot original Armada with the admitted original raw GDI:
   **SCI SD, Background On, CD music Off, virtual VMU Off**. The experimental
   module supplies its own blank A2; the existing SD virtual-card option stays
   off for this test.
5. Open the load/memory-card selection screen. Check whether A2 appears as an
   empty formatted card. Loading an existing physical A1 save should remain
   available. Use the load/card-selection path; leave save creation and format
   actions alone for this readonly stage. Play for about a minute if it loads.
6. Press **A+B+X+Y+Start**. Photograph all five rotating pages, including BUILD
   and the bottom fault rows: **TRAFFIC, FENCE, A2, MAPPING, DRIVER**.
7. Power off and restore your backed-up module before the next cold boot.

If it stops early, photograph the last screen, power off and restore the
backup. No virtual-card recreation is needed. This archive does not include
an exact rollback copy of the file currently on your SD.

## Expected evidence

The A2 page has magic `31523241` (A2R1). Advertisements should advance;
DEVINFO, media and block-read counts should appear as the original driver
discovers/mounts the card. The exact counts depend on the game's screens.
The FENCE page should retain paired holds/restores and clear historical faults.

| A2 row | Values |
| --- | --- |
| MAGIC VISITS COMPLETE ADVERTISE REPLIES | A2R1, publication visits/completions, parent advertisements, target replies |
| INFO ALLINFO MEDIA READ FINALIZE | Generated A2 responses by command |
| PARENT REQUEST RESPONSE RX CAPACITY FUNC | Original physical parent evidence; A1 bits are preserved |
| TARGET REQUEST RESPONSE RX CAPACITY BYTES | Latest synthetic target reply and proven receive capacity |
| REFUSED BACKEND STORES FLAGS ATTEMPTS | Refusals, protocol calls, confirmed stores, historical flags, latest store attempts |
| FAULT WHY AT VALUE EXPECT DETAIL | First failure, latched to block later A2 publication |

A pre-existing physical A2 response/advertisement is a conflict and prevents
publication. Unsupported commands, changed ownership/mapping, insufficient
capacity and failed stores also stop further A2 publication. The ordinary
worker still returns through POST to restore DMA. Initial controller discovery
is deliberately skipped until its real live unit/function exists.

## Source-specific transport

The passed 92-byte worker wrapper is retained, with its private helper moved
to P2 `acfca000`. The pre-NK reservation is 24 KiB, P1
`8cfca000..8cfd0000`, below the unchanged OEM globals and pools. Existing
TRAFFIC/FENCE logs remain at `acfcfe00`/`acfcfe80`; the new A2 log is at
`acfcff00`. Its protocol state, 536-byte publisher staging and RX-query
workspace are private BSS. There is no 128 KiB card mirror or runtime allocation.

While DMA is held idle with interrupts masked, the adapter proves the live
port-A unit/function, included request, exact RX pointer and requested capacity,
source allocation bucket, repeated metadata PTEs, uncached pool mappings,
descriptor identity and disjoint ranges. A pool-contained address alone is
insufficient. It refuses explicit physical receive buffers. The source allocator
provides 32/64/256/512/1024-byte buckets; a 524-byte block response therefore
requires the correct live request and 1024-byte bucket, rather than borrowing
space after an arbitrary RX header.

The original controller reply is used to advertise A2. Its source bitmap gains
only bit02, preserving A1 and every other original header bit. The original CE
driver then creates A2 and submits its own transfers. The adapter replaces only
an admitted A2 hardware timeout, without appending TX records or altering the
driver's object table. Complete synthetic responses are staged privately; the
live ownership/mapping/request and held-DMA witnesses are repeated after staging.
Publication uses verified aligned P2 word stores, in payload, target-header,
parent-advertisement order. A failed store has no rollback claim; the first fault
blocks subsequent publication.

The protocol reuses K-UI's readonly encoder and pure blank-block generator.
Analysis of the original MAPLEDEV binary shows that its one-phase read sends CMD13 phase0
after CMD11. The CE adapter admits this completion (and the native phase1
shape) only after a successful matching-block read. Mutations, failed reads or
mismatched completion revoke that evidence. The ordinary native protocol is
unchanged. Fixed-image reader callbacks are disabled in the production CE
adapter; metadata and generated reads perform no storage I/O.

## Reproduction and limits

```
make -f Makefile.armada-maple-test a2-tests
python3 tools/armada_ce_maple_rx_slice_owner_audit.py --startup-dir /path/to/startup --check docs/evidence/armada-ce-maple-rx-slice-contracts.json
source .deps/kos/environ.sh
make -f Makefile.armada-vmu-a2 build/armada-ce-vmu-a2/entry.elf
python3 tools/check_armada_ce_vmu_a2_package.py build/armada-ce-vmu-a2
```

Host tests cover exact owner metadata, changed PTEs, buffer capacities and
aliases, every read/store callback failure, all generated blank blocks,
production blank-only reader guards, read completion, mutation refusals,
payload-before-header-before-advertisement ordering and final admission.
The native package audit pins bridge/shim/cache/MMIO/RX-store bodies, checks
integer-only instructions and the complete private stack/callback closure,
and bounds the pre-NK reservation. The package includes authored source changes
and proofs; original owner executables and disassembly are private inputs.

This console result is pending. It cannot establish SD-backed virtual reads,
save writes, journal commits or compatibility with other CE kernels/games.
The passed gate's normal-operation limits still apply: no recovery claim for
driver unload, exceptions or forced termination that bypasses POST. ABXY
terminal return may catch one final open gate; photograph then power off.
