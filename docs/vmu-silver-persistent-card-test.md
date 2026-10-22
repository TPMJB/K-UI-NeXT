# Silver virtual memory card

This exact-Silver loader exposes **`{{PAIR_PATH}}`** as virtual A1. The owner
has confirmed that a save survived a complete power-off and reboot and was
still available on the next launch. Routine play does not require another
allocation-profile upload or rebuilding this package.
The card was prepared with both imported Silver saves and its options file,
leaving **164 free blocks initially**. Keep its current image and journal,
including writes from the earlier run. This loader enables durable saving to this
separate card. Your original physical VMUs, full white-card backup (`p0002`)
and ABC card (`p0001`) remain separate.

## Install and use

1. Back up the ordinary `KUI/apps/games/retail-boot.kui` outside `KUI`, then
   install only that file from this ZIP. Keep the same SCI microSD and all
   files in `{{PAIR_PATH}}` where the preparation tool found them. Do not
   replace the image, identity or journal with earlier copies.
2. Power off before removing all physical VMUs and controller accessories.
   Use one ordinary controller in port A and keep it connected.
3. Launch the same original, uncompressed raw-sector **Silver GDI** with
   **SCI / Standard**, **background loading Off**, **CDDA Off** and
   **RAM patches Off**. Let the startup checks finish. Confirm the screen
   names **SILVER**, **PERSISTENT SD SAVES** and **`{{PAIR_PATH}}`**.
4. Play Silver and use its normal save option. Choose a new
   unused slot if offered, preserving the two imported slots. Otherwise an
   update on this separate p0003 test card is acceptable; the full imported
   backup remains in p0002. Do not delete or format the card. In Silver,
   talk to the **Chronicler** to open the save menu, choose a slot, enter a
   name and select the green checkmark. He appears at specific save points;
   once the Map is available, the Rebel Camp offers repeatable saving.
   See the original US manual, page 8:
   https://manuals.plus/m/144589692657108c8496cef5bdc3ff401368a67e1063c7601ad689716e21c2ef
5. Wait for the game to finish saving before switching off. Keep all three
   card files together for the next launch. No repeated persistence test is
   required for this update; the owner has already confirmed the cold boot.
6. The five-minute and one-hour diagnostic interruptions have been removed
   from ordinary Silver sessions. All storage, card identity, checksum,
   memory, controller and ownership fault checks remain active. If a real
   diagnostic appears, photograph all **five** pages (0 through 4).
7. Preserve the complete p0003 folder after testing. If anything fails, keep
   its journal with the image; do not clear or recreate either. Restore the
   ordinary loader before playing other games.

## Storage and ownership checks

Every launch validates the embedded 4096-byte P2 profile, actual card identity,
live exFAT/FAT32 allocation ownership and separation from game-track extents
before journal recovery. The stage then validates the card filesystem,
checkpoints recovery and gives the worker fresh image checksums. Preparation
hashes are provenance; successful saves can change the image legitimately.

Silver's own heap, exit path, two Maple banks, 48 receive slots, timer callback
guards and inherited interrupt masks 6/15 remain independently checked.
Virtual A1 requests are removed from hardware DMA. The unchanged original
chain, physical controller responses and idle engine are verified before
restoring Silver's DMA address and handling virtual commands.

Four 128-byte write phases stage a 512-byte VMU block. Block sync commits it
through the existing SCI lease, redo journal, barriers and exact readback.
An acknowledgement is published only after a successful commit and lease
release. A storage or ownership error stops the launch without acknowledging
an uncertain write. The next admitted launch handles any pending journal.

The initial f64204b63131 hardware run stopped at its planned card window:
18,135 attempted starts minus first-card start 135 equals exactly 18,000.
It completed 80 write phases and 20 durable block commits, with 111 block
reads and no reported storage, stack, context, bank or chain faults.
The owner had started a new game and reached a cutscene while seeking a save
point. These counters establish block commits; they do not identify their
save-file purpose or prove progress surviving a cold boot. Keep the existing
p0003 image and journal, and inspect Load Game after reboot before replaying
the opening. Do not restore the original prepared image over this test card.

The journal protects one block, not an entire multi-block game save. Keep
power on until the game finishes saving. Silver's first durable run completed
block writes. On 2026-10-18 the owner reported that the saved game was still
present after a full power-off and reboot. This is hardware-confirmed Silver
persistence by owner report; an updated image was not uploaded for independent
byte-level save inspection. The subsequent long pause at **CHECKING OWNER SHA**
was identified as startup validation, not a card read/write failure. Its cache
and hashing behavior is unchanged while virtual-card usability is prioritized.
The published loader and automatic per-game card selection are unchanged.
