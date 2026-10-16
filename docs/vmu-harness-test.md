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
   test. The only new contents are a 128 KiB `card.vmd`, a 1 KiB `card.journal`
   sidecar and a 16-byte `identity.bin` identifying that exact pair.
3. Check that device discovery, memory geometry, four write phases, journaled
   commit, virtual readback and fresh reopen all pass. The program disconnects,
   reconnects and remounts the storage before its final readback. The intermediate
   reopen retains the committed journal to exercise replay; the final successful
   checkpoint syncs the base image and clears the commit marker.
4. Photograph the **PASS / READY** screen. Switch the console off only after
   READY. Start the same tool again, choose the correct connection, and press
   **X**. It opens the highest numbered probe folder, recovers its own journal
   if necessary, and compares all 512 proof bytes with the expected pattern.
5. Photograph the second PASS screen. That second result, when preceded by an
   actual power cycle, proves restart persistence on that card and connection.

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

The fresh image uses standard empty 200-user-block VMU filesystem geometry. The
probe writes raw VMU block 42, which remains marked free in the empty VMU FAT.
The deterministic marker is deliberately not presented as a valid game save.
The command sequence uses the documented four 128-byte writes followed by the
block-sync transaction. A read before sync must still return the old block;
a read after success must return the complete new block.

The store is the same portable journal engine intended for a future resident
service. This harness connects its logical sectors 0–255 to `card.vmd` and
256–257 to `card.journal` through ordinary FatFs seeks, reads, writes and syncs.
These are **synthetic logical sector numbers**, not a pinned physical card
extent map. A retail-game implementation will need allocation validation,
physical extent pinning, filesystem unmount, storage arbitration, verified
runtime memory reservations and a real Maple interception/completion path.
The current test does not satisfy those later requirements.

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
```

The host binary includes the actual console sequence and file callbacks. Only
the display/controller and physical SD transport are substituted. ASan/UBSan
cover creation, verify in a new process, repeat without overwriting the first
card, exact identity/sidecar-length refusal, and filesystem checks on disposable
FAT32/exFAT images. Unsupported FAT16 must be refused without changing a byte.
These checks need `dosfstools` and `exfatprogs` installed.
