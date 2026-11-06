# Sega Smash Pack Genesis save scope

The reviewed original `MDE_US.BIN` has two separate VMU save paths. The
Genesis **Save Game** option writes emulated cartridge SRAM. Controller
settings use `SMASH_01.CFG`, independently of cartridge SRAM. Keeping both
paths available matters throughout the shared Genesis engine.

All ten original Genesis selections already use one native engine, one shared virtual
VMU A1 backing-card assignment (the current loader recreates its service per selection) and their original ROM/folder/options. The PSII/Shining Force
direct-child diagnostic enum is not the production collection catalog. SRAM
eligibility is a cartridge property, not a missing game-specific loader.

The source owner, scalar addresses and hashed source windows are recorded in
[`evidence/smash-genesis-save-scope.json`](evidence/smash-genesis-save-scope.json).
No retail instructions or ROM payload are included in that evidence.

## What the original engine supports

| Path | Original behavior | Scope |
| --- | --- | --- |
| Genesis Save Game | Enabled only when cartridge SRAM size and dirty state are nonzero | Per cartridge save files named from `SP1_`, four title bytes and a slot number |
| Controller settings | Loads/saves a 1,024-byte payload in `SMASH_01.CFG`; original UI requires four VMU blocks | Shared configuration; independent of the cartridge save gate |

The original SRAM parser at `0x8c018066` checks the 512-byte Genesis header
for `RA` at `0x1b0`, `0x20` at `0x1b3` and a type of `F0`, `F8` or `E0`
at `0x1b2`. Its narrow size calculation uses only `header[0x1ba]`:
`(value + 1) * 256`, divided by two for `F0` and `F8`. Other descriptors
produce zero. This describes the exact engine's admission logic, rather than
a general Genesis SRAM parser.

The only cartridge header in the supplied source bundles is
`SHFORCE/SHFORCE.SGA`. It declares `RA F820`, with an original engine SRAM
size of 8,192 bytes. The other nine raw header prefixes remain absent from the local source bundle.
The owner has now returned the automatic858 report from all ten actual ROMs:
Phantasy StarII also has8192 bytes; the other eight report0. See
`evidence/smash-genesis-save-capabilities-858ca7ee3db1.json`. These values answer
SRAM eligibility without another header-extraction errand; they do not establish
persistent high scores or confirm save/load results for other mechanisms.

No additional high-score-specific VMU path was identified in the reviewed
engine save and option code. That does **not** establish whether an individual
game stores scores in its SRAM, or rule out every other code path. A cartridge
header establishes SRAM eligibility; actual score writes and dirty/save/load
behavior must be checked before calling a game's high scores persistent.

## Automatic report

After each successful collection preparation, the GUI writes
`KUI/apps/games/smash-genesis-saves.txt`. It reports all ten original titles,
ROM paths and the exact original engine's SRAM byte count, or an explicit
unavailable reason. It inspects only512 bytes per cartridge using one2048-byte
source sector; two cached directory sectors keep the normal authored fixture
at21 total source reads. A hard64-sector limit includes all directory and ROM
reads. The source reader is closed before the report write, and report failure
does not turn a prepared launch into failure.

The classifier `kui_smash_genesis_sram_bytes` accepts exactly512 bytes and uses
the reviewed parser above. It does not patch headers or invent SRAM. Positive
SRAM eligibility alone does not prove that high scores are written, marked
dirty, saved and reloaded. The original Sega manual positively documents the
two-step progress-save process for Phantasy Star II and Shining Force; no
per-title persistent-high-score claim is established for the other eight.
Controller settings remain a separate shared save path for all ten.

## Optional developer header extraction

Run the new helper beside the owner's original raw2352 GDI:

```sh
python3 kui-smash-genesis-save-headers.py /path/to/disc.gdi --output SmashPack-Genesis-save-headers.zip
```

The output contains the original GDI descriptor, exactly the first 512 bytes
of each of the ten `.SGA` files and `save-headers.json`. Its ROM-prefix payload
totals 5,120 bytes. Each hash covers only that prefix. It collects no full ROM
or audio payload and does not change the source image. Existing output paths
are refused.

The helper checks the reviewed IP identity, raw Mode1 data framing, ISO
directory metadata, exact catalog file sizes and complete declared data-track
extents. Directory reads and traversal are bounded. It validates ROM extent
geometry from metadata without reading ROM tails.

## Validation

```sh
python3 tests/test_smash_genesis_save_headers.py
python3 tests/check_smash_genesis_save_source.py /path/to/original/MDE_US.BIN
make -f Makefile.smash-genesis-save-header-tests
make -f Makefile.smash-genesis-save-report-tests
```

The first command runs 13 synthetic GDI tests covering exact prefix output,
source preservation, malformed source rejection and atomic output publication.
The second requires the exact original executable and executes its unmodified
SRAM parser against 1,024 authored headers in the existing bounded SH model.
All admitted type/size combinations, every rejected type and the fixed
signature checks match the helper's classification; writes stay within the
two parser cells, with dirty state unchanged. This verifies the scalar parser
contract, not game execution, filesystem behavior or hardware timing.

The C classifier test exhausts all65536 type/size pairs, malformed signatures,
length refusals and input immutability. The report test executes the actual
GUI report, original ten-row catalog, ISO parser and path normalizer against
authored sectors. It checks the21-sector normal path, zero versus unavailable
results, malformed directories, cancellation, budget exhaustion and report
open/write/sync/source-close/report-close failures under ASan/UBSan.
