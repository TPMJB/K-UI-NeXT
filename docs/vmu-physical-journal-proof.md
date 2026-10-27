# VMU physical journal proof

This separate Card tools program tests the existing owned `KUIPROOF` fixture
through its real image and journal sectors. **Retail-game saving is still
disabled.** It does not install a Maple hook or write a physical VMU.

## Install and test

1. Preserve the usual `KUI/tools.kui` outside `KUI`, then install this ZIP's
   **`KUI/tools.kui`**. Keep the main shell and retail loader in place.
2. Keep the same SCI card, the existing `/KUI/vmu-test/p0001` image/journal/
   identity files, and `retail-persistent-profile-0001.bin` in that folder.
   Do not move, defragment, replace or edit them. The profile must be the
   4,096-byte persistent profile exported by preparation build482122bf7a8a
   or a later compatible preparation build.
3. Power-cycle and open Card tools. Confirm the new build ID and the title
   **Virtual VMU - physical journal proof**. Press **A** once.
4. Keep power on until **PASS / READY**. Photograph the screen. A successful
   run changes the test save's short description to generation01 (or the next
   generation), with a matching package checksum, in a single512-byte block.
5. Switch the console off, then boot again and reopen this same tool. Press
   **X**. At **PASS / READY**, photograph the screen. The stored generation
   must match the first screen. This second run rechecks live allocation,
   recovers any matching physical redo record, and verifies the saved bytes.

The tool performs one operation per launch. A failure is terminal for that
launch: preserve all three source files and the profile, photograph the full
message, and restart before further testing. If A never completed, preserve
the journal with the image; an error can occur after a transaction committed.

## What changes

Only the existing owned image's block199 and its two useful journal sectors
receive controlled proof writes. Bytes14/15 of the existing test save's short
description become two uppercase hexadecimal generation digits; its CRC16
at70/71 changes with them. Both fields fit in the same512-byte block. The other
VMU blocks, filesystem metadata, identity file, profile and allocated slack are
preserved. The program refuses a different or edited save fixture.

A valid redo record remains in the real journal after A and X. **Do not run
the earlier native harness's synthetic A/X test or read-only-profile export
on this pair.** Those older commands use a different journal binding. Keep
this tool and the files together for the next implementation step. The older
RAM-save loader's fixed image hash may also refuse the changed test image;
that is expected after this proof.

## Admission and durability

The program opens the P2 file read-only, closes and unmounts, then establishes
a fresh SCI connection. A current CID/capacity probe and full raw semantic
filesystem scan must match the profile's identity and ownership before any
recovery write. The prepared image hash is provenance; legitimate subsequent
saves are checked with newly collected hashes rather than rejected for
differing from that baseline.

The store uses actual card capacity, image identity, and the real physical
image/journal maps. It never rebinds a legacy synthetic journal. Private I/O
callbacks authorize only the exact256 image and2 journal sectors; whole
allocation clusters remain protected by the separate ownership proof.

Recovery precedes VMU filesystem validation. Each controlled commit uses the
existing redo store's marker invalidation, payload, valid marker and base
write with ordering/durability barriers. A success additionally requires
exact base readback, package CRC16, fresh full-image validation/hash, unchanged
CRCs for the other255 blocks, current card identity/health, and a successful
final SCI release/restoration check. No format, allocation, implicit
checkpoint or recovery through the old258-sector mapping is available.

This proves a single512-byte transaction and restart recovery on the owned
fixture. It does not make a multi-block game save atomic or enable a retail
writer. The later game bridge must separately hold the resident's shared SCI
lease for the full transaction and acknowledge only after successful release.
