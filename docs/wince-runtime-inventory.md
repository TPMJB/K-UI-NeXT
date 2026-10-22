<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Batch Windows CE runtime inventory

Use extracted owner-supplied startup kernels to compare runtime components
before reviewing another CDDA adapter. From the source checkout:

```sh
python3 tools/wince_runtime_inventory.py "/samples/title-a/0WINCEOS.BIN" \
  "/samples/title-b/0WINCEOS.BIN" > runtime-inventory.json
```

The JSON inventories ROM modules, section locations/compression flags,
stored SHA256s, embedded files and initialization copy records. It groups
whole stored module payloads, individual sections, and embedded files
separately. Physical and nominal placement are reported but do not split
otherwise identical stored section payloads. Entry points, section layout,
flags and directory metadata remain part of whole-payload identity.

These are conservative byte-identity groups. A relocation, compressed
encoding difference or changed literal can split related implementations.
Matching stored bytes do not establish live API addresses, allocator
ownership, sound behavior or playback compatibility. The output always
states `playback_admitted: false`; it does not count unique games or global
CDDA titles. No runtime profile is added by running the tool.

The parser accepts up to 128 single-load kernels, each at most 16 MiB, with
bounded ROM tables in Dreamcast main RAM. Normal images need the ECEC ROM
marker. The supplied markerless Worms Armageddon CE2 image has an explicit
full-SHA-bound inventory header location; other markerless images stop
instead of guessing. Compressed sections are hashed as stored and never
decoded or read as executable RVAs. An invalid input stops the batch with
an error and no partial JSON on stdout. Inputs are never modified.

The original `armada_ce_rom_audit.py` entry point still requires Armada's
exact size and complete SHA before evaluating its reviewed contracts.
`ROMInventory` is the separate metadata-only parser used by this tool.

Validation on 2026-10-15: all four supplied Armada/Bust-A-Move 4/Worms
Armageddon/World Party kernels inventory successfully. Across them there
are 26 identical section groups shared between kernels, but no identical
whole module payload groups. Armada and Bust-A-Move 4 also share embedded
ARMCOM firmware; World Party's firmware is a different revision. This is
evidence for grouping below the complete-kernel/module level, rather than
assuming that matching one component admits a game.

Synthetic parser/CLI checks run with:

```sh
python3 tests/test_wince_runtime_inventory.py -v
```
