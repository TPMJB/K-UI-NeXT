# Toy Commander SD-backed read-only A1 proof

This experimental module reads the prepared128 KiB VMU image from the owner's
SCI microSD card. It exposes that image as A1, the first controller's first VMU
slot. It is a read-only test: no save, delete, format, block-write, sync or LCD
operation is admitted. The existing KUIPROOF test save is not a Toy Commander
save and is not expected to appear as a loadable game slot.

The ZIP installs only `KUI/apps/games/retail-boot.kui`. Keep the main2.0.0 shell,
boot CD and Card tools in place. It is tailored to the exact card/profile used
for the build. The included `observer-artifact-audit.json` identifies mode2 and
records the embedded profile's SHA-256 and image map.

## Install and test

1. Preserve your original2.0.0 `KUI/apps/games/retail-boot.kui` outside `KUI`.
2. Keep the same SCI card and `/KUI/vmu-test/p0001` source folder. Do not move,
   replace or defragment its `card.vmd`; a changed allocation needs a new Y
   profile. Copy the supplied module to `KUI/apps/games/retail-boot.kui`.
3. Remove both physical VMUs from controller1. Leave the controller connected.
4. Power-cycle and launch the supported original, uncompressed **raw-sector
   Toy Commander** image through **SCI**, **Standard** reader, **background
   loading Off**, **CD music/CDDA Off** and **RAM patches Off**. This isolated
   module refuses other titles and launch routes.
5. Allow the game to enumerate A1 and open its LOAD/card screen if it reaches
   that point. Do not choose Save, Delete or Format. A card with no recognized
   Toy Commander saves is expected.
6. Photograph the game card screen and all four diagnostic pages. The bounded
   proof stops deliberately after its read window. If a failure stops earlier,
   photograph that complete screen. Power off when finished.
7. Restore the backed-up standard `retail-boot.kui` for ordinary play.

## Read contract

Before game handoff, the high stage decodes the exact1536-byte profile, rejects
any overlap with all mapped game tracks, and checks the initialized card's CID,
capacity and addressing mode. It reads every mapped VMU sector and compares
the per-block CRCs and complete image SHA-256. An invalid profile, stale data,
different card or failed read prevents handoff. The worker copies the decoded
profile into its private owned allocation; no stage pointer is retained.

At the existing SDK completion hook, the worker borrows a typed read lease
from the resident game reader. That lease uses the existing P1 GD lock and the
same initialized SCI bus. It permits no active game SD stream, no card
initialization, no filesystem calls and no writes. A temporary protocol
descriptor is valid only during the lease. The worker checks card identity,
reads one mapped512-byte block into an aligned private buffer, stops its SD
stream and verifies the block CRC before publishing any Maple response.

Contention before I/O stops this proof with diagnostics. It does not fabricate
an unproved Maple retry. Any card, CRC, transfer, stop or restoration failure
latches storage unavailable and stops before the game's original SDK consumer
can observe a partial virtual reply. Parent-controller advertisement remains
the last publication after the whole completed batch is validated.

ABI5 uses64-byte worker exports,88-byte initialization config and a644-byte
snapshot. SD counters record successful block reads, contention, faults, last
result/LBA/CRC and identity checks. Native packaging proves the actual embedded
profile/module/ELF identities, both integer trampolines, private P2 ownership,
instruction restrictions and stack bounds. The worker's4 KiB stack accounts
for the resident lease and SCI callbacks it calls. The ordinary low GD stack
budget is unchanged.

Host and native checks do not establish console timing or SD arbitration under
retail execution. That is the purpose of this hardware test. Durable in-game
saving remains a separate implementation and validation step.
