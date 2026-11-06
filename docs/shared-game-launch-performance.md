# Shared Games launch work

The owner requires Games-only integration. Startup/splash, menus, branding and
the updater implementation are frozen. Performance fixes belong in the common
operation and must reach every applicable routed module; they are not another
title-specific option. Runtime/SDK compatibility and the availability of a safe
VMU/cache reservation remain separate contracts.

The separately authorized `f7229ee` repair repaints the existing splash artwork
after CD handoff. It does not change the Games cohort or qualify its readers.

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
| Standalone early-SDK nesting bind | Retain the independently proved alternate handler and check both source templates against the live copied stub | Generic SDK correction in progress; original EGG linked old/candidate control passes, console acceptance pending; Background already handled this mode |
| CE receive CRC cost | Restore the original full 256-entry table without moving resident/stack guards | Generic CE and all four CE music owners must fit and pass linked qualification; no accepted replacement yet |

These are implementation targets, not a new hardware acceptance record.
Qualification must bind the exact source, linked modules and GUI admission.
Preserve accepted Smash components, native workers, leases, slots and live
reader behavior. Do not rebuild settled components merely to align labels.

## Standalone early-SDK correction

The delivered `eeed38bd540e` standalone New path exported the early plain
handler as both `normal` and `fpu`, although the shared structural proof could
already recognize a nesting handler. Its late bind consequently refused a
valid nesting-mode installation. The Background 7c route already checked the
alternate in source and live memory; EGG's Background virtual VMU is unaffected.

The common correction retains plain/nesting roles only when the exact handler
and installer are proved, identifies the early stub by its structural layout,
and requires the EXPEVT template, INTEVT template and live common pointer to
agree. The original EGG installer and linked resident instructions reproduce
the old B refusal and candidate bind in an SH model; mixed/unknown pointers,
occupied SCI slots and an already active reader still refuse. This is a source/linked proof, not
console execution. No original Sonic Adventure executable has been restored:
an archive name in the Library index is not its missing file.

The implementation and production tests are
[`native_sdk_sci_family.c`](../src/core/native_sdk_sci_family.c),
[`native_standalone_reader.inc`](../src/loader/native_standalone_reader.inc),
[`test_native_sdk_sci_early_family.c`](../tests/test_native_sdk_sci_early_family.c)
and [`test_retail_async_standalone.c`](../tests/test_retail_async_standalone.c).
No title mapping, VMU arena or new game-time service follows from this bind.

## CE receive-loop baseline remains unresolved

The delivered CE representation in
[`sci_stream.c`](../src/loader/sci_stream.c) folds the 256-entry CRC16 table
into 128 entries and conditionally reconstructs the high-bit contribution for
every byte. Full CRC equivalence protects integrity, but does not establish
the original receive-loop cost. Actual linked generic SCI async code also
uses a temporary index stack slot; the four CE/CDDA loops retain the extra
CRC-bit conditional branch without that spill.

Full-table restoration is under qualification with all existing buffers,
stack guards and payload-copy checks retained. The generic trial fits; current
unchanged-layout trials for all four CE/CDDA residents exceed the guard by
192 bytes. No production replacement is accepted yet. The lossless size work
and all-five linked build/admission checks must finish before claiming the
original CRC cost is restored. Existing gates are
[`test_ce_crc16_factoring.py`](../tests/test_ce_crc16_factoring.py) and the
SCI/SCI-async loop checks in
[`global_loader_rollout.py`](../tools/global_loader_rollout.py).

Armada FMV timings from the folded-table cohort are not a clean baseline for
the original loop. Neither instruction counts nor CRC equivalence establish
elapsed-time impact or explain the FMV problem. Native CDDA, WinCE/CDDA FMV,
simultaneous VMU/prefetch/audio service coverage and hardware streaming tests
remain outstanding.

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
root files cannot be mistaken for the effective selected files. Back up the
two existing selection records outside their active directory when using a
manual ROOT test; do not delete or overwrite them. This uses the selector's
existing no-selection behavior and avoids timing B against the CD loader's
separate cancellation control. Do not modify updater behavior or user data.
Include the exact changed-file inventory and expected reader fingerprints.
No new boot mode, splash or menu behavior belongs in the delivery.

An installed global claim requires complete applicable route coverage. The
ordinary CE module still shares `ce-probe.kui` with a personalized writable-VMU
adapter, so blind replacement would remove an existing service. Preserve that
adapter and keep this deployment gap explicit until the routes are separated
and admitted. Optional SCIF VMU, new SDK support and unproved memory leases do
not become supported merely because the common operation is faster.
