# Physical VMU import preparation test

This is a separate Card tools experiment. It reads a physical VMU and creates
a new virtual card on SCI storage. It preserves the physical source and the
working ABC card in /KUI/vmu-test/p0001. It does not change the retail loader.

This first test requires the existing working p0001 folder and its
retail-persistent-profile-0001.bin. The tool admits that current allocation
before creating any new files, so a damaged or stale source volume is refused
before it can allocate over an existing card. It does not replay p0001's journal.

## Root geometry retry

The official white VMU, tested alone in A1 after a cold boot, reached capture
status -5/image -4. Its blocks remained stable through the full digest and
validation reads, then the root geometry check refused it. This is a different
failure from the black card's duplicate-read mismatch.

The earlier validator required the blank formatter's defaults in root bytes
0x40-0x45 and 0x52-0x57 even though the SDK marks those ranges as unknown metadata.
This revision accepts varying values in those opaque ranges and copies them
unchanged. It still requires the 16-byte magic, supported custom-color flag,
FAT 254/1, directory 253/13, 200 user blocks and icon value at most 123, along
with the existing allocation and file-chain checks. Blank formatting is
unchanged. The hardware result does not yet prove which root field caused
the earlier refusal.

If geometry still fails, the screen now shows CLONE phase/read count, the
full root magic and custom flag, FAT/directory/user/icon values, and both
opaque six-byte ranges. The report freezes the root header already read by
validation; it sends no extra Maple operation and performs no SD I/O. A missing
READ reason on this path means no callback read refusal was recorded.

Retry the white VMU alone in A1 after a cold boot. Press X for preview first;
if it refuses, photograph the full root report. The separate import procedure
below applies after a successful preview. No existing virtual-card files need
repair or replacement for this retry.

## Device-guard diagnostic retry

Build 1c161ccbd43c reported capture status -3/image 0, READ block 0/pass 0/
reason 1 on A1. That means the tool refused its callback/device precondition
before sending a Maple block read. It does not establish which precondition
failed or indicate a corrupt VMU filesystem. The earlier busy-frame change
does not explain this result.

This revision preserves the device checks and freezes the first failed check
before drawing the result screen. GUARD bits identify callback context, output
buffer, block range, missing/replaced/invalid device, functions or product name.
CTX, OUT, DEVICE, VALID/FUNC and optional NAME show the corresponding values.
A device-guard refusal keeps this identity report separate from Maple results.

## Duplicate-read diagnostic retry

Build 8f59b7 still reported READ block 0/pass 0/reason 5 after the candidate
cache-coherency correction. Both bounded transactions and device checks passed,
but memcmp returned -32 and the independent byte loop found two differences,
starting at byte 12 with 40/60. The final read-buffer CRC32 values were
f07cc314/49af43bb. This confirms that the comparisons observed different bytes;
it does not establish where the bytes changed or identify a corrupt save.
The earlier cached-alias invalidation remains in place, but the cache
hypothesis has not been confirmed as the physical cause.

The retry freezes the memcmp result, an independent volatile byte comparison,
the first differing index and byte pair, total difference count, both full-block
CRC32 values, and both bounded reply headers before drawing the failure screen.
Each PASS now also reports RX CRC32 before/after copying the validated raw
512-byte reply payload, followed by COPY CRC32 of the helper's output before
the Maple frame is unlocked. The main CRC32 row samples the two caller buffers
after both reads. Together these observations help distinguish a changed raw reply,
a changed copy, and a buffer changed after its helper completed. They do not
change admission or prove the cause of a change between samples.
COMPARE anomaly means the comparisons disagree; it still refuses the capture.
For identical bytes the first-index sentinel is 512 and the displayed byte pair
is 00/00. Both independent physical reads remain mandatory. A refusal copies
no source block to the caller and performs no SD I/O during preview.

Install only KUI/tools.kui, open Card tools and select the same physical VMU
and slot (A1 for the reported capture failure), then press X once. If it fails,
photograph the whole screen and report
whether the card is an official Sega VMU or another model. No SD import is
needed to reproduce this capture failure. The complete 14-row report fits on
one screen, including Capture, READ, CLONE and FAIL. A capture refusal creates no SD files.
No repair or replacement of existing virtual-card files is needed for this
diagnostic preview. Hardware confirmation is still required.

## Import test

1. Keep the working Toy Commander loader installed. Back up your usual
   KUI/tools.kui outside KUI, then install only this ZIP's KUI/tools.kui.
   Safely eject the SD card.
2. With the console off, connect the physical VMU you want to copy to a
   controller. Start K-UI and open Card tools. Select the source slot with
   Left/Right, then press X to read and preview it.
3. Check the source slot, save count and used/free blocks. Press A to create
   the separate virtual card. Keep power on until READY. Photograph the
   complete result, especially the destination path and profile filename.
4. After READY, power off and send the newly exported
   retail-persistent-profile-NNNN.bin from the displayed pNNNN folder.
   Keep card.vmd, card.journal and identity.bin together in that folder.
   The next retail test package will be bound to that new card.
5. The existing ABC test still uses p0001. Remove physical VMUs before using
   that retail experiment. Do not replace p0001 files with the new clone.

One import is allowed per tool launch. Repeat a fresh launch for another
physical VMU; each successful copy gets a separate unused pNNNN folder.
If any check fails, photograph the screen and preserve the new folder.
Do not delete or repair it during the test. A partial folder is skipped by
later attempts, so it cannot be silently reused.

## Additional test with the existing Toy loader

After the import result has reached READY, you can also use the already-working
Toy Commander build with p0001. Remove physical VMUs first. Keep SCI/Standard,
background loading Off, CDDA Off and RAM patches Off.

Create a disposable three-letter profile such as TST, make some recognizable
game progress, and save. Wait for the game to finish saving, fully power off,
then relaunch and actually load that progress. If successful, update the save
once and repeat the power-off/load check. Finally delete only TST, power off,
and confirm that it stays deleted while ABC remains listed.

Photograph the loaded progress and any error/diagnostic pages. These checks
exercise saved payloads, updates and deletion beyond the existing ABC-name
persistence result. The new imported card is not selected by that old loader;
it needs the separate profile-bound package after you send its new .bin.

## What success means

The physical source is read only. The isolated reader validates the exact
Maple response length before accessing its payload or copying 512 bytes.
Its complete 128 KiB contents, including
save metadata, flags, system blocks and padding, are copied without conversion.
The tool validates standard 200-user-block geometry and checks source contents
again before destination creation. The new image is verified after write,
the journal starts empty, and identity is published only after clone success.
Profile export checks the actual card identity, live filesystem allocation,
whole-cluster ownership and current image hashes, then checks final SCI release.

READY proves preparation and verified copying. It does not prove that an
imported save loads in its original game. That is the next separate console
test. Copy protection, other games, multi-disc sharing and card selection
need game-level validation. Source VMUs and existing virtual cards stay intact.
