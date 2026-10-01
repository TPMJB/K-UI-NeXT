# Read-only ext4 boot CD support

The development boot CD can read `/KUI/runtime.kui` from a supported, clean
ext4 volume on SCIF SD, SCI SD or IDE/CF. This is **CD bootstrap support only**.
The current app runtime still uses FatFs for exFAT/FAT32; Games preparation,
File Manager, ripping, music and settings do not acquire ext4 support from the
CD. **Keep the working card's existing filesystem** for normal use and the
first SCI hardware test.

Burn the matching `kui-bootstrap.cdi` when installing this bootstrap capability.
A later runtime that implements ext4 app access can be copied onto an ext4 card
as `/KUI/runtime.kui` and loaded by this CD, provided its filesystem features,
storage transport and version-1 package remain compatible. Ordinary compatible
runtime updates then need only card files. A bootstrap bug fix or an incompatible
format/transport change can still require another CD.

## What the bootstrap does

Startup keeps the SCIF, SCI, IDE/CF search order. For each connected device it
tries the existing FatFs runtime reader, releases that mount, then tries the
read-only ext4 reader if needed. It validates the runtime header, exact file
length and payload CRC before execution and passes the selected transport to
the runtime. Hold B retains the CD recovery path.

The ext4 adapter never forwards writes or synchronization calls. Journaling is
disabled in the bootstrap build; it performs no journal replay, formatting,
metadata repair or filesystem conversion. Dirty/error-state filesystems,
pending recovery or orphan cleanup, unsupported features and invalid metadata
are rejected. Cleanly unmount the volume on the computer before booting it;
perform any required repair there. CD recovery tools themselves still use
FatFs, so this addition does not enable saving logs or running write benchmarks
on ext4.

## Accepted format

- 512-byte device sectors, with at most `UINT32_MAX` sectors through the
  existing 32-bit media interface.
- A whole-device filesystem; a sole primary Linux `0x83` MBR partition; or GPT
  with exactly one Linux-filesystem-type partition. GPT may include other
  non-overlapping partition types. Both GPT headers and partition arrays must
  validate and agree: revision 1.0, 92-byte headers, 128-byte entries and at most
  128 entries. Hybrid/extended MBR layouts, multiple Linux candidates, damaged
  GPT copies, duplicate partition GUIDs and overlaps are rejected.
- Clean ext4 with 1, 2 or 4 KiB filesystem blocks, valid geometry, and supported
  inode sizes. The real-image fixture uses **4 KiB blocks and 256-byte inodes**;
  accepting 1/2 KiB geometry is not a console validation result.

The reproducible fixture explicitly enables this feature profile instead of
depending on changing `mkfs.ext4` defaults:

```text
has_journal,ext_attr,resize_inode,dir_index,filetype,extent,64bit,flex_bg,
sparse_super,large_file,huge_file,dir_nlink,extra_isize,metadata_csum
```

The reader also permits the pinned library's `meta_bg` and `uninit_bg`/group
descriptor checksum features. Unknown flags are rejected, including unknown
read-only-compatible flags. Encryption, inline data, bigalloc, metadata checksum
seeds, MMP, orphan-file, external journals, quotas and large-directory extensions
are outside this profile. A clean internal journal is allowed, but never replayed.
Do not reformat the working card merely to enable the CD feature.

## Implementation and validation boundary

The reader is [ext4_boot.c](../src/core/ext4_boot.c); partition selection is
[boot_volume.c](../src/core/boot_volume.c), separate from the existing FatFs
selector. [dependencies.json](../dependencies.json) pins lwext4 to
`58bcf89a121b72d4fb66334f1693d3b30e4cb9c5`, and
[the bootstrap configuration](../config/lwext4/generated/ext4_config.h) limits it
to one device/mount and disables journal operations. Retained upstream notices
and local adaptations are tracked with the dependency.

[The real-image fixture](../tests/test_ext4_boot.py) covers loading from raw, MBR and GPT volumes,
missing/corrupt runtime, dirty/recovery/unsupported-feature rejection, metadata
checksum corruption, I/O failure and cancellation. The partition unit test
separately covers GPT copies, CRCs, bounds and ambiguity. The adapter checks that
loading never invokes device writes. These are host checks; console ext4 boot,
SCI microSD and IDE/CF remain hardware validation work.

Full ext4 application access and game extent export remain the next filesystem
work described in [storage-filesystems.md](storage-filesystems.md). A filesystem
change alone does not guarantee higher game transfer speeds.
