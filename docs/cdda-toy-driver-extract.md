# Toy Commander sound-driver extraction

The uploaded `1GUTH.BIN` refers to the external `AUDIO64.DRV` sound driver.
This helper exports that single file from the original complete Toy Commander
GDI. It does not install or alter anything on the Dreamcast or its card.

Keep `extract_cdda_toy_driver.py` and `extract_cdda_toy_boot.py` together in the
same directory. Leave the original GDI beside its unchanged track files.

On Linux, run this from the directory containing the helpers, adjusting only
the GDI path. Save the output in your computer home folder, where hard links
are supported; exporting onto an exFAT card is refused safely.

```sh
python3 extract_cdda_toy_driver.py "/run/media/apollonius/Dreamcast/Games/TOY_COMMANDER/TOY_COMMANDER.gdi" "$HOME/ToyCommander-AUDIO64.DRV"
```

The command prints the file's ISO9660 path, logical LBA, exact byte count and
SHA256. Upload only `ToyCommander-AUDIO64.DRV`. The exported bytes are the
original driver payload; raw-sector headers, error-correction bytes and final
sector padding are removed. Existing output files are never overwritten.

The helper is pinned to the original 451-byte GDI SHA256
`96e3a54b9aa528c7172121ba3c6cfabf391aacb5a84f292e89843ff2d8853803`.
Before searching directories, it verifies the 32768-byte IP SHA256
`dcb2b68f92456fac4261ab8e386875f8f337e40e6e26bd11368f873f55fbc5a6`
and the 748444-byte executable SHA256
`ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd`
plus CRC32 `cdc493b3`. Every read validates the raw Mode 1 header and logical
address. Only the verified data-track geometries are mapped; audio tracks and
disc gaps remain unavailable.

The helper searches at most 16 ISO9660 volume descriptors for the primary
descriptor; a terminator before the primary descriptor is refused. The directory
walk checks both byte orders, record boundaries, padding, volume geometry and
mapped absolute extents. It accepts a case-insensitive driver name
with or without a decimal file-version suffix. Duplicate driver matches,
directory aliases/cycles, extended attributes, interleaved records and
multi-extent records are refused. Limits are 128 directories, 4096 records,
2 MiB of directory-sector reads and a 1 MiB driver payload.

The printed driver SHA256 identifies the extracted bytes; it is not a comparison
against a previously measured driver hash. The extractor and its generated-data
tests contain no retail executable or sound-driver bytes.
