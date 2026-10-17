# Virtual VMU native persistence proof

This is a standalone KallistiOS test program, not an in-game VMU emulator. It
exercises K-UI's new command processor, bounded software Maple DMA adapter and
journaled image store on the Dreamcast's real microSD connection. The normal
2.0.0 runtime and resident game readers are not replaced by this source target.

It keeps KOS in control of the real controller. It does not change Maple DMA
registers, interrupts, KOS's device list, or a game's memory. Each virtual-card
request goes through an owned, all-virtual software DMA schedule; no part of
that schedule is submitted to the physical Maple engine. Passing this test
does **not** establish retail-game discovery or save compatibility.

## Build

Use the pinned dependencies and KOS environment described in the repository:

```sh
source .deps/kos/environ.sh
make -f Makefile.vmu package
```

Outputs: `build/vmu-harness/vmu-harness.elf` and the verified
`build/vmu-harness/KUI/tools.kui`. Copy the supplied `tools.kui` into the card's
`KUI` folder, pause startup on the existing compatible boot CD, and choose
**Card tools**. Preserve any older `tools.kui` first if you use it. The normal
`runtime.kui` is not replaced. The ELF can also use your normal development boot
method.

## Console test

1. Start the separately selected VMU proof tool. Choose SCI or SCIF with
   Left/Right to match the microSD connection. The card's existing exFAT
   filesystem is supported; no filesystem conversion or formatting is needed.
2. Press **A** once. It creates a new `p0001`, `p0002`, etc. folder under
   `/KUI/vmu-test`. Existing folders and files are never overwritten by a new
   test. A creates a 128 KiB `card.vmd`, a 1 KiB `card.journal`
   sidecar and a 16-byte `identity.bin` identifying that exact pair.
3. Check that device discovery, memory geometry, four write phases, journaled
   commit, virtual readback and fresh reopen all pass. The program disconnects,
   reconnects and remounts the storage before its final readback. The intermediate
   reopen retains the committed journal to exercise replay; the final successful
   checkpoint syncs the base image and clears the commit marker.
4. Photograph the **PASS / READY** screen. Switch the console off only after
   READY. Start the same tool again, choose the correct connection, and press
   **X**. It opens the highest numbered probe folder, recovers its own journal
   if necessary, and compares the complete 1,024-byte proof save, its VMS package CRC16 and its
   directory/FAT allocation with the expected data.
5. Photograph the second PASS screen. That second result, when preceded by an
   actual power cycle, proves restart persistence on that card and connection.

## Export the card's read-only physical profile

The next preparation step uses **Y** in this standalone tool. The separate
synthetic A1 test has established discovery/read behavior; Y now describes
the allocated `card.vmd` on your particular microSD card. This preparation ZIP
updates **`KUI/tools.kui`** and includes its instructions and reference docs. It supplies
no new `retail-boot.kui` and does not install an SD-backed retail VMU reader.

1. Finish the A persistence test, wait for READY, then power the console off
   and on. Open **Card tools** again through the existing boot CD.
2. Select **SCI**. Press **Y** to export the latest numbered isolated probe.
   Physical VMUs may remain removed; no physical VMU operation is called.
   Y first runs the same latest-probe verification/recovery as X, including
   validation of the allocated VMS, filesystem and clean journal checkpoint.
   Keep power on through the subsequent export until **PROFILE / READY**.
3. Photograph the final screen. It gives the full output path, source probe
   number, physical extent count and image SHA-256 prefix. The output is exactly
   **1,536 bytes**, for example
   `/KUI/vmu-test/p0002/retail-readonly-profile-0001.bin`.
4. Attach that indicated `.bin` file for the next build. Keep its source probe
   folder and microSD card in place. Each additional Y export creates a new
   numbered profile; existing profiles and the source image are never overwritten
   by the export. A different card or relocated source file needs a fresh profile.

After verification/checkpoint finishes, Y reopens `card.vmd` **read only**.
It maps all 256 file sectors using FatFs's authoritative current-sector field
after an actual one-byte data read, adds the selected partition's start sector,
and coalesces contiguous physical allocations into at most 16 extents. The
profile records the card CID/capacity/addressing mode, partition bounds, probe
pair identity, each 512-byte block's CRC32 and the complete image SHA-256.

The tool closes the file, unmounts and disconnects before verification. A fresh
SCI connection, with no mounted filesystem and no write callbacks, checks the
same card identity and reads every recorded physical sector, comparing all
block CRCs and the complete SHA. Only then does it reconnect and create the
unique profile output with `FA_CREATE_NEW`, sync/close it, and reopen it to
verify every exported byte. An identity/map/hash/read failure prevents this
output stage. Y is SCI-only; SCIF selection is refused before export begins.

Y's initial verification can recover/checkpoint its own probe pair just like X;
Y is not a promise that the entire operation performs no storage writes. The
image-mapping and fresh raw-verification stages are read only, and the export
adds only its new profile file after they pass.

This is the **VMU image's own physical map**, independent of any game's track
map or the standalone store's synthetic sectors 0–257. The profile is input to
the next per-card build. It does not establish retail SDK compatibility,
game-to-card arbitration, or durable in-game saving.

**X** can replay an already committed journal block into that probe's own image;
it is a verify/recovery action, not a strictly read-only file viewer. No
physical VMU read, format, delete or save operation is called. Physical VMUs can
remain attached. The tool never installs them as virtual targets or routes
traffic to them.

A failed run keeps its files. Preserve the entire probe folder, including the
journal and identity file, for analysis. Do not copy only `card.vmd` as a trusted
backup until recovery has succeeded. To create another independent attempt,
press A; a new folder is used. Do not change the physical storage adapter while
the console is powered on.

## Exact scope of this proof

The shared image formatter admits only an exact all-zero newly created image,
then writes and verifies standard empty 200-user-block VMU filesystem geometry.
The tool first verifies that an unfinished write is discarded on virtual-device
reset. It then builds a deterministic **KUIPROOF** data save from original test
bytes: a standard 128-byte VMS header, one 512-byte icon, 64 application bytes,
a correct package CRC16 and padding to two VMU blocks. No proprietary game save
is included.

The payload is written to user blocks 199 and 198. The FAT chain and directory
entry are then installed, leaving 198 free user blocks. All four changed blocks
use the shared software Maple adapter, four 128-byte writes and durable block
sync. Each transaction verifies that reads still return the previous contents
before sync and the new contents afterward. The shared image validator checks
the complete allocation graph and rejects orphaned or shared chains; the
existing VMS import parser checks the package structure and CRC16. The complete
save, directory and FAT are checked again after a virtual-device reset,
a fresh storage reconnect/remount, and the later X power-cycle check.

This establishes a valid, allocated test save that the existing VMU Manager can
browse and extract. It remains an owned test fixture, not evidence that a
particular retail game can discover or use the virtual device. The four-block
save is not atomic as a group: an interruption between its FAT and directory
commits can leave an incomplete allocation, which the next verification refuses.
Preserve such a failed probe; create a new folder with A rather than trying to
continue it.

The store is the same portable journal engine intended for a future resident
service. This harness connects its logical sectors 0–255 to `card.vmd` and
256–257 to `card.journal` through ordinary FatFs seeks, reads, writes and syncs.
These are **synthetic logical sector numbers**. Y independently exports and
raw-verifies the image's physical allocation; the A/X store continues to use
the synthetic logical numbers. A retail-game implementation will still need allocation validation,
physical extent pinning, filesystem unmount, storage arbitration, verified
runtime memory reservations and a real Maple interception/completion path.
The profile export does not install that runtime service.

The journal preserves one committed 512-byte block against interrupted base
image writes, assuming the storage driver and card honor successful sync
barriers. It cannot make an entire multi-block game save atomic or guarantee
that a defective card honors its acknowledgements. The hardware power-cycle
check supplies evidence about this particular card/connection, not a general
proof of every SD card's power-loss behavior.

## Automated host check

```sh
make -f Makefile.vmu host-test
python3 tests/test_vmu_harness_images.py
make build/test-vmu-image-compat
python3 tests/test_vmu_image_compat.py
```

The host binary includes the actual console sequence and file callbacks. Only
the display/controller and physical SD transport are substituted. ASan/UBSan
cover creation of the allocated CRC-valid VMS, incomplete and committed device
reset, verify in a new process, repeat without overwriting the first card,
identity/sidecar-length refusal, and damaged VMU geometry, orphan chains or save
payload refusal. The profile codec suite runs in `host-test`. The real-filesystem
suite exports twice into unique names, independently decodes the 1,536-byte
wire record, checks its actual raw 512-byte allocations/CRC32/SHA-256, and
checks the source card is unchanged. It covers superfloppies and nonzero
partition offsets on both FAT32 and exFAT, plus SCI-only, changed-CID and fresh
raw-read failure refusal. Refused probes must leave storage byte-for-byte unchanged.
Filesystem checks run on disposable FAT32/exFAT images. Unsupported FAT16 must
be refused without changing a byte. These checks need `dosfstools` and
`exfatprogs` installed.

The compatibility test runs the actual host harness to create a card image,
then feeds that exact `card.vmd` to production `src/apps/vmu.c`. It checks blank
and populated image browsing, extraction of the complete VMS and its directory
metadata, the production VMS CRC parser, production external VMS import and
verified restore into a newly formatted simulated physical VMU. Browsing,
extraction and import must make no physical VMU calls. Only the explicitly
exercised restore uses the existing physical-transport simulation. Existing
backup/capture sentinel files and the original VMD remain unchanged; `fsck`
checks both disposable filesystems afterward. This is software interoperability
coverage; real physical VMU writes and real retail SDK traffic still need
hardware tests.
