<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Private Toy report launcher bank

This isolated launcher starts from the exact GUI-only updater source
`332c9028b5cb3feb31d5aa0607e097829b510b08`, tree
`cda3b296b60ea879e5e21877d134804030ac2c47`, recovered from its delivery archive.
No updater wire-format change, root-file removal or reader/VMU rebuild is used.

`KUI_TOY_REPORT_TEST` defaults to zero. Its opt-in GUI says **TOY TXT TEST**
in the persistent header and startup log. Only Toy Commander, SCI, Standard /
Background Off, CD music On and saved Emulated VMU Off may launch. A complete
admitted Toy map selects the typed raw/cooked adapter directly, so old exact
Toy files in the retained root cannot take precedence while this bank runs.
The adapter's envelope build must equal this GUI's twelve-character build;
the existing descriptor, payload CRC, complete map and loaded executable /
driver / hook guards remain mandatory. Missing or invalid adapters refuse.

Build only the GUI, from a clean source commit with the pinned dependencies:

```sh
source .deps/kos/environ.sh
make -f Makefile.dc build/kui-runtime.elf KUI_TOY_REPORT_TEST=1 BUILD_ID=0123456789ab
python3 tools/runtime_package.py build build/kui-runtime.elf runtime.kui --build-id 0123456789ab
```

Use the same admitted bank ID for its Toy adapter builds. A sparse package
contains this `runtime.kui` and the two typed Toy paths; every other catalog
path inherits the retained root. Package with `tools/update_package.py build
STAGED_KUI OUTPUT.kup --build-id ID --channel testing --inherit-missing`.
The normal updater's selection/previous-bank restoration keeps root software
and shared saves/preferences intact. The test does not change the VMU setting.

Run `python3 tests/test_toy_report_launcher.py` for default/private selection,
known exact and broader Toy raw/cooked maps, non-Toy refusal, bank identity and
descriptor/CRC mutations, plus real FAT32/exFAT preparation refusal checks.
These host checks do not establish console report export or movie performance.
