# GD Play: reuse verified VMU preflight

This update restores the earlier physical-disc startup optimization. With
Virtual VMU enabled, K-UI proves the Maple driver, buffer layout, allocator and
guarded exit windows before handoff. The bootstrap can reuse that complete
proof after checking the actual running program, instead of repeating the
structural discovery scans at **Resolving disc Maple driver and allocator**.

The update also enables the image launcher's alignment-safe fast decoders for
native preflight and fallback scans, and its equivalent faster CRC routine for
disc preparation and the bootstraps. The card picker uses checked metadata
rather than reading and hashing the complete executable merely to identify
a product. Actual boot
still reads and verifies the entire executable. Staging and optical hash
buffers use separate stack frames so they do not reserve duplicate buffers.

The bootstrap should show **USING VERIFIED DISC VMU PREFLIGHT** on a successful
replay. A missing, damaged or mismatched proof uses full discovery. Fresh
program hashes and live allocator, placement, card and installation checks
remain required. The proof is rebuilt for each launch; no cache file or game
image on SD is needed. Virtual VMU Off bypasses this work.

## Install and test

With the console powered off, back up the four existing files below and merge
the ZIP's `KUI` folder into the SD installation, replacing all four files:

- `KUI/runtime.kui`
- `KUI/apps/disc/native-disc.kui`
- `KUI/apps/disc/native-disc-vmu.kui`
- `KUI/apps/disc/native-disc-vmu-a1.kui`

The runtime and modules form one matching update. To revert, restore the four
backed-up files together. Existing cards, saves, preferences and other files
are kept. The package retains the larger native executable support, improved
VMU labels/details and **GD Play → START: Cards** management.

Launch Skies of Arcadia with the same Virtual VMU setting and slot used in the
slow run. Check the new verified-preflight message and the time spent after
bootstrap. Verify the existing save loads, then check a smaller native disc.
Virtual VMU Off should retain its physical-card launch behavior. Save
Diagnostics and note the selected mode/slot if a launch refuses.

This change restores the proof-replay path from the earlier tested
implementation while retaining this thread's later changes. Host tests and
linked-code audits do not establish the new combined build's console timing.
Native/SCI VMU admission remains in force; this does not add WinCE or CD direct
boot. The 12 MiB executable ceiling remains subject to actual contiguous RAM.

`source/HEAD.tar.gz` contains the complete committed source, and
`source/delta.patch` contains the change from the manifest's local base.
`build/` contains matching ELF files, maps and clean-source compile records;
`evidence/` contains the supplied test/build logs. `manifest.json` covers every
other ZIP member with sizes and SHA-256 hashes. No game or personal VMU image
is included.
