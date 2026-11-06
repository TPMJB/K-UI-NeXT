# Shared Games launch work

The owner requires Games-only integration. Startup/splash, menus, branding and
the updater implementation are frozen. Performance fixes belong in the common
operation and must reach every applicable routed module; they are not another
title-specific option. Runtime/SDK compatibility and the availability of a safe
VMU/cache reservation remain separate contracts.

The reported 9d GUI result is about four seconds to bootstrap in Power Stone 2,
down from almost twenty. The subsequent 7fc bootstrap candidate produced no
noticeable reported improvement. Its changed GUI ID is not proof of the final
reader. Neither host work counts nor a new filename establish console speed.

## Current shared gaps

| Operation | Shared correction | Coverage boundary |
| --- | --- | --- |
| WinCE/CDDA kernel identity and later executable CRC | Compute SHA and exact-byte CRC in one complete read; consume a launch-local proof only for the same image, extent, length and unchanged storage epoch | All four supported CE music profiles; mismatches retain the full CRC read |
| Native discovery after a cache miss | Run immutable image discovery in a guarded operand-cache scope, then publish private output/stack and restore the original profile | Standard and compact Background image stages; SDK/live-state checks and patching stay outside |
| Duplicate native role validation | Reuse the role proof just established by fresh image discovery or complete replay in this same invocation | The supported early SDK family; current coherence windows/slots and later live checks remain |
| Cold-launch photo holds | Default to zero across native, CE and physical-disc loading; retain explicit diagnostic opt-in | Rebuild and qualify the actual selected cold stages; preserved binaries do not acquire source changes |
| Native mapping hint lookup | Direct advisory lookup by canonical image/extent and runtime requirements before the existing bounded fallback | No source hash or filesystem authority is inferred from a hint; detached full image verification remains |
| Final reader identity | Capture the final verified source module after fallback and before personalization | Records source path/build/payload size/CRC; preparation is not proof of successful execution |

These are implementation targets, not a new hardware acceptance record.
Qualification must bind the exact source, linked modules and GUI admission.
Preserve accepted Smash components, native workers, leases, slots and live
reader behavior. Do not rebuild settled components merely to align labels.

## Remaining volume-dependent admission cost

FAT32 admission checks incoming links across the complete allocation table and
its mirrors. At the supported maximum, two copies total 32 MiB of metadata.
Sixteen-sector batching reduces commands but cannot eliminate those bytes.
exFAT instead checks the namespace and live FAT chains. The owner's supplied
November 3 diagnostic identifies exFAT with 128 KiB clusters, so the FAT32
whole-table bound does not explain that observed layout. Neither filesystem
operation counts nor the older log establish the current bootstrap bottleneck.

A cached GUI profile checks current file mappings, not all incoming owners.
It cannot replace the stage's complete ownership proof. Even a complete GUI
scan is followed by allocation-capable profile/activity/report writes before
handoff. A future one-shot RAM allocation handoff must be sealed after the last
such writer, bind the exact card/profile/game map/module, and expire on faults,
unknown writes, cancellation or reset. The accepted collection epoch does not
by itself authorize unrelated cold launches. Recovery, current image hashes,
checkpoint/readback and final device health remain required.

## Delivery requirements

Use a manual-copy Games test when the owner has excluded updater testing.
Document how the existing selected bank is bypassed for that test so copied
root files cannot be mistaken for the effective selected files. Do not modify
updater behavior, erase saved selections or overwrite user data to achieve it.
Include the exact changed-file inventory and expected reader fingerprints.
Retain the existing root-bypass control for this purpose rather than adding a
new boot mode. No new splash or menu behavior belongs in the delivery.

An installed global claim requires complete applicable route coverage. The
ordinary CE module still shares `ce-probe.kui` with a personalized writable-VMU
adapter, so blind replacement would remove an existing service. Preserve that
adapter and keep this deployment gap explicit until the routes are separated
and admitted. Optional SCIF VMU, new SDK support and unproved memory leases do
not become supported merely because the common operation is faster.
