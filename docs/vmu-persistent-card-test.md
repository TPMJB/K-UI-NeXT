# Toy Commander persistent SD-save proof

This isolated loader exposes the existing owned SCI image as virtual A1 and
commits acknowledged 512-byte VMU blocks through its real physical journal.
It is an exact-Toy Commander hardware test, separate from the published 2.0.0
loader. The test image already contains the unrelated `KUIPROOF` fixture.

## Install and test

1. Preserve the usual `KUI/apps/games/retail-boot.kui` outside `KUI`. Install
   only this ZIP's **`KUI/apps/games/retail-boot.kui`** in that location.
2. Keep the same SCI card and `/KUI/vmu-test/p0001/{card.vmd,card.journal,
   identity.bin}` together in their current locations. The ZIP is bound to
   the P2 preparation profile supplied for this pair. Do not move, replace,
   defragment or edit these files between launches.
3. Remove physical VMUs and other plug-in accessories from connected
   controllers. Keep controller 1 connected. Launch the supported original,
   uncompressed raw-sector **Toy Commander** image using **SCI**, **Standard**
   reader, **background loading Off**, **CD music/CDDA Off**, **RAM patches
   Off**. Other titles and launch modes are refused by this experiment.
4. Confirm the new build ID and **PERSISTENT SD SAVES** title. Create a clearly
   named Toy Commander save/profile, such as `SDTEST1`. Wait for the game's
   save operation to finish and photograph the result. Do not format the card
   or delete the unrelated `KUIPROOF` fixture during this first test.
5. Power the console completely off. Boot again and launch with the same
   settings. Load `SDTEST1` and check its name/progress. Photograph the result
   and the diagnostic pages. A same-launch load alone does not establish
   restart persistence.
6. Keep the image, journal and identity together after testing. The ordinary
   loader can be restored for other games; it does not expose this virtual
   card. Do not run the earlier synthetic Card tools A/X test or its old
   read-only-profile exporter on this physically bound journal.

An error or diagnostic stop is terminal for that launch. Photograph every
page, preserve the pair, and restart; do not retry through another storage
mapping or recreate the journal. A failed acknowledgement can follow a
durable commit, so only fresh admitted recovery may resolve the pair.

## Persistent storage contract

Each launch strictly decodes the embedded 4096-byte P2 preparation record,
checks all current game/audio extents against the VMU files' whole allocated
clusters, and verifies actual card identity and live filesystem ownership.
These checks complete before any journal recovery write. Recovery uses the
real capacity, image identity and physical image/journal maps. It is followed
by VMU filesystem validation, a clean checkpoint and fresh image SHA/CRCs.
Preparation-time content hashes are provenance; successful saves legitimately
change the image and are admitted using fresh hashes on the next launch.

A sealed compact handoff is copied into the protected worker. Its first
storage transaction requires the checkpointed marker to remain exactly zero;
it cannot replay a new or unexpected journal after handoff. The completion
hook performs no filesystem scan or allocation. It retains no pointer to the
resident's mutable card descriptor or to a temporary borrowed descriptor.

Four 128-byte write phases stage one block. The block-sync command commits
using one scoped lease of the game's existing SCI bus across the complete
redo transaction, barriers and exact base readback. Single-sector writes use
PIO in the high worker; no resident TX-DMA writer is added. Current block CRCs
and the final acknowledgement are published only after successful lease
release/restoration. Storage contention, uncertain I/O, changed identity or
CRC failures stop the proof without a save acknowledgement. No uncertain
transaction is retried inside the game.

Every A1 request is removed before hardware Maple DMA. The original SDK source
and bookkeeping remain intact. Completion validates the captured source,
physical chain, idle engine, inherited ownership/mask and controller response,
then restores the original MDSTAR before issuing virtual commands or replies.
The 64 KiB protected worker boundary and 4 KiB private stack are unchanged.

The existing five-page report layout remains ABI 6. In mode 4 the final counters
describe SD write phases/commits, reads, last block and faults; the RAM-cache
capacity fields are zero. The experiment retains the five-minute card-traffic
window and ten-minute overall bound from the working RAM proof. Photograph
the report if either limit stops the run.

The journal protects one 512-byte VMU block. It does not make an entire
multi-block game save atomic; wait for the game's save operation to finish
before switching off. WinCE, background/async readers, CDDA, other titles,
hot-swapping, card formatting and generic virtual-card UI remain separate work.
