# VMU persistent-profile preparation

This is a separate Card tools preparation program. **Retail-game SD saving is
still disabled.** The working RAM-save test remains a separate module.

The tool reads the existing owned VMU image, journal and identity. It exports
their real physical allocations and checks filesystem ownership so the next
implementation can journal writes to the correct sectors. The image and
journal contents are preserved. The only new filesystem file is the exported
profile.

## Install and run

1. Preserve the usual `KUI/tools.kui` outside `KUI`, then copy this ZIP's
   `KUI/tools.kui` into that location. Keep the main shell and current retail
   loader in place.
2. Keep the same SCI card and existing `/KUI/vmu-test/p0001` folder. Do not move,
   replace or defragment `card.vmd`, `card.journal` or `identity.bin`.
3. Power-cycle and open Card tools. Confirm the new preparation build ID and
   SCI storage. Press **Y** to inspect the latest existing probe and export
   its persistent profile. A/X persistence write tests are disabled here.
4. Keep power on while the filesystem ownership scan runs. Large FATs and
   directories can take longer than the earlier read-only profile export.
5. At **PROFILE / READY**, photograph the screen and attach the exact
   `retail-persistent-profile-0001.bin` path it reports, normally under
   `/KUI/vmu-test/p0001/`. Repeated exports use a new number.
6. Power off and restore the usual `tools.kui` when finished.

A refusal means no persistent profile was approved. Preserve the image and
journal together and photograph the full message. In particular, a nonzero
journal marker must be recovered using its original physical binding; this
preparation program never attempts recovery through the old synthetic test
mapping.

The exFAT boot diagnostics show the exact captured fields and rejection mask
on separate short rows when a scan fails. `PartitionOffset=0` is accepted as
unspecified; physical placement still comes from the independently checked MBR.
A different nonzero offset is refused. Backup volume flags are stale, while
the current main-boot dirty/media-failure and unsupported-layout checks still
refuse. The tool does not clear flags or repair the filesystem.

These rules follow the [Microsoft exFAT specification, sections 3.1.4 and
3.1.13](https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification).

## What the profile proves

The canonical 4,096-byte version-2 profile is separate from the earlier
1,536-byte read-only profile. It records the card identity/capacity/addressing,
partition, canonical pair path and identity, FAT32/exFAT geometry, exact file
lengths, directory-entry locators, useful sectors and full allocated clusters
including slack. It also records the current VMU block CRCs and image/journal
hashes. The initial journal marker must be exactly zero.

A bounded read-only raw verifier checks the live filesystem rather than
accepting old physical addresses. FAT32 retains FAT-copy and incoming-link
checks. For exFAT, the verifier walks every live FAT-chained regular file,
checks each allocation bit and exact chain length, and rejects any intersection
with the owned VMU allocations. Contiguous files use their directory-declared
ranges; their unused FAT words do not authorize links. Root, directory and
system allocations remain checked independently. This follows the exFAT
allocation rules without treating stale FAT contents as live crosslinks.
Unsupported layouts, cycles, read failures and scan-limit exhaustion are
refusals; regular-file chain walks have a separate CPU-step limit as well as
the existing media-read limit. Source files are opened read-only; the output profile uses
create-new semantics, sync and exact readback. After output creation, a fresh
raw connection checks the same card, ownership and source snapshots again.

This profile is preparation evidence, **not permission for a retail loader to
write**. A future persistent launch must repeat current allocation checks
before journal recovery or writes. Legitimate saving changes image contents,
so the exported image hash is a preparation baseline; future launches need
fresh hashes after admitted recovery and VMU validation. Per-block journaling
does not make a multi-block game save atomic.
