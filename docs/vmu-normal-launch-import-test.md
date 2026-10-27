# Virtual card: normal Silver launch and black VMU import

This bundle makes two concrete steps toward using virtual cards from K-UI.
Silver can select its persistent card during a normal Games launch, and Card
tools can copy all saves even when unused physical VMU storage changes.
General per-game card selection and the VMU menu's live virtual-card browser
are still pending. This is not an all-games compatibility release.

## Test 1: Silver from Games

1. Back up your current `KUI/runtime.kui`, `KUI/apps/games/retail-boot.kui`
   and `KUI/tools.kui` outside KUI. Preserve every `KUI/vmu-test` folder.
2. Restore your backed-up **ordinary** `KUI/apps/games/retail-boot.kui`.
   The recent Silver test replaced that file. This bundle deliberately does
   not supply another ordinary loader.
3. Copy the contents of `NORMAL-LAUNCH/KUI` to your SD card's `KUI` folder.
   This installs `runtime.kui` and `apps/games/vmu-silver.kui`. Do not copy
   either file into `retail-boot.kui`.
4. Keep your current `/KUI/vmu-test/p0003` image, identity and journal together.
   They contain the save you confirmed survived a reboot. No replacement
   image or journal is included. The supplied Silver module is personalized
   for your existing p0003 ownership profile.
5. Safely eject, power off, remove physical VMUs, and start K-UI. Launch the
   same original Silver GDI through Games with SCI storage, Standard reader,
   background loading Off, Auto boot encoding, CD music Off and RAM patches Off.
6. Startup should select the separate Silver virtual-card module and show
   `/KUI/vmu-test/p0003`. The owner-SHA pause remains. Load your existing save,
   save recognizable progress, wait until saving finishes, fully power off,
   and confirm that new progress loads after restarting.
7. Launch a different game that normally works. It should use its ordinary
   loader. This test does not enable its virtual VMU support.

Installing the separate module opts this exact Silver disc into virtual-card
launches. A present corrupt, mismatched or unsupported module refuses launch
instead of silently switching cards. Removing `vmu-silver.kui` restores the
ordinary Silver route. An unsupported reader, image or option also refuses.
The module is validated against the full disc map and executable CRC before
launch. The persistent backend retains its current storage ownership,
identity, recovery, durable-write and CPU-context guards.

If Silver refuses, photograph the launch error or diagnostic screen. Report
whether startup showed p0003, whether your existing save loaded, and whether
new progress survived full power off. Do not change any p0003 files to retry.

## Test 2: copy the black VMU's saves

1. Install only `BLACK-VMU-IMPORT/KUI/tools.kui` as `KUI/tools.kui` after
   backing up the existing tool. Keep all current virtual-card folders.
2. After a cold boot with the black VMU alone in A1, open Card tools and press
   X for a read-only preview. Check its save count and used/free blocks.
3. If the preview is correct, press A and keep power on until READY. It
   creates a new unused `pNNNN` folder; it never overwrites p0001, p0002,
   p0003 or any other earlier folder. The physical VMU remains read only.
4. Photograph the result and, after power off, send the newly exported
   `retail-persistent-profile-NNNN.bin` from the displayed folder. Keep
   `card.vmd`, `identity.bin` and `card.journal` together there. Leave the
   physical saves intact until the new virtual card has been tested.

The importer now preserves every allocated file and all physical metadata,
including protection flags and mini-games. It reads and verifies those bytes
repeatedly. FAT-free user blocks are never physically read and become zero in
the destination. This is a complete save snapshot, rather than an exact
backup of unused physical bytes. A changing allocated block or metadata,
invalid filesystem, disconnected card or failed SD check still refuses.

Preview creates no SD files. One import is allowed per tool launch; restart
to import another physical card. A failed partial output is preserved and
skipped next time. On refusal, photograph the full screen and preserve the
folders. The existing p0001 profile remains the required read-only allocation
anchor before the importer allocates any new files.

This copies the entire source card into a separate virtual card; it does not
merge several cards or bypass the standard 200-block capacity. The newly
imported card is not automatically assigned to retail games yet.

## Restore

Restore your runtime and Card tools backups and remove the optional
`KUI/apps/games/vmu-silver.kui` to undo the launch/tool changes. Preserve all
virtual-card folders and restore the ordinary loader when using normal games.
