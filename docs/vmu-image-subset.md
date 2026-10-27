# Offline VMU fileset image constructor

This host tool creates a fresh 131072-byte, standard 200-block card image from an
explicit set of entries in an existing image. It uses
`kui_vmu_image_subset()` for complete source admission and output construction.
The Python wrapper handles exact selection, a private immutable source snapshot,
exclusive output creation, readback, and a JSON proof report.

It preserves every retained allocated 512-byte block, the original 32-byte
directory records at their original slots, FAT links and block numbers, and the
entire source root. Protection flags, opaque package headers, mini-game prefixes,
timestamps and final-block padding stay unchanged. Unselected entries and unused
data are zeroed, and removed user FAT allocations become free.

## Build once

From the repository root, run:

```sh
make -f Makefile.vmu-subset-tool
```

Running the wrapper needs Python 3 and the prebuilt host adapter. It does not
compile implicitly. A packaged adapter for the current host can be supplied with
`--host-tool /path/to/vmu-image-subset`; its filename does not affect the output.
The standalone build has no connection to a console, physical VMU or SD card.

## Inspect and select

Validate a source and print its directory inventory without creating an image:

```sh
python3 tools/vmu-image-subset.py /path/to/source.vmd --list
```

The JSON inventory includes exact `name_hex` fields, entry metadata, block chains,
and a SHA-256 of the source snapshot. Slot 0 is block 253, byte 0; slot 16 is
block 252, byte 0; slot 207 is block 241, byte 480.

Choose every related file explicitly. For example, a hypothetical fileset using
directory slots 0, 1 and 2 is created with:

```sh
python3 tools/vmu-image-subset.py /path/to/source.vmd /path/to/fresh-subset.image \
  --slot 0 --slot 1 --slot 2
```

Alternatively, select exact names. `--name` accepts 1–12 printable ASCII bytes
and pads the field with NUL bytes to exactly 12 bytes:

```sh
python3 tools/vmu-image-subset.py /path/to/source.vmd /path/to/fresh-subset.image \
  --name SETUP.CFG --name CHAPTER.001 --name CHAPTER.002
```

For space-padded or non-ASCII raw names, use the exact 24-digit `name_hex` value
from the inventory with repeated `--name-hex` arguments. Selector modes cannot be
mixed. Names are matched byte for byte: no case folding, trimming, wildcards,
prefix matching or inferred game groups. Include system files and mini-game
entries when they belong to the fileset you want. Filesystem-valid unknown
packages are retained without parsing or repairing their payloads.

## Output and limits

The destination must be a fresh path in an existing directory. Existing files,
hard links and symlinks are rejected; source and output cannot be the same path.
The source is opened read-only and never modified. The wrapper snapshots it once
so inspection and construction use the same bytes. Keep the original source
stable while that initial read occurs.

Source admission or selection errors create no destination. A successful output
is flushed, synced and read back before closing; stdout contains JSON with source and
output hashes, selected directory metadata, allocated-payload hashes, and the new
used/free block counts. The JSON can be retained as an offline review record.

This produces only a raw offline image. It creates no pair identity, journal,
live SCI ownership record or profile. Later use requires a fresh identity,
verified zero journal and independently admitted ownership/profile. Existing
pair paths, hashes and profiles must not be edited to imitate a binding. This
tool has no install, SD, physical-card or loader integration.

## Verification

```sh
make -f Makefile.vmu-subset-test host-test
make -f Makefile.vmu-subset-tool host-test
```

Both targets use ASan/UBSan. Fixtures are synthetic and contain no owner or retail
save payloads. Tool tests exercise exact selectors, fresh-path handling,
unchanged input, malformed unselected files, source lengths, missing host tools,
and failed output cleanup.
