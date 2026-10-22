# Silver: imported saves, read-only test

This test presents the unchanged full white-VMU import in
`/KUI/vmu-test/p0002/card.vmd` as virtual A1. It is tailored to the exact US
Silver executable and the same SCI card used for the import. Saving, deletion
and formatting are disabled. The four free blocks do not prevent a load test.

The three Silver files are already in this full backup. No subset image or new
card is installed by this ZIP. The ABC card (`p0001`), full import (`p0002`),
their journals and physical VMUs are not written by this test.

## Install and test

1. Back up the installed 2.0.0 `KUI/apps/games/retail-boot.kui` outside the
   `KUI` folder. Keep earlier test loaders separately from your ordinary loader.
2. Keep the same SCI microSD and the complete `p0002` folder in place. Do not
   move, replace or defragment its files. This loader uses the exact image map
   from the supplied `retail-persistent-profile-0002.bin`; it verifies the map's
   image bytes before launching Silver.
3. With the console off, remove all physical VMUs and controller accessories.
   Use one ordinary controller in port A, and keep it connected until power-off.
4. Copy this ZIP's `KUI/apps/games/retail-boot.kui` over the installed copy.
5. Launch the original, uncompressed raw-sector Silver GDI using **SCI**,
   **Standard** reader, **background loading Off**, **CDDA emulation Off** and
   **RAM patches Off**. Allow the executable and card checks to finish.
6. Select **Load Game**. Photograph the entries shown. Select one existing
   Silver save and record whether it reaches your saved gameplay position.
   Do not start a new game or choose Save, Delete or Format. If Silver attempts
   a card write automatically, this test deliberately stops before submitting
   that write; photograph the report.
7. Photograph all four diagnostic pages, numbered 0 through 3. The run ends
   within 36,000 attempted SDK DMA starts, or 18,000 additional starts after
   the first card media/read request. At 60 starts per second that post-read
   window is about five minutes; the actual schedule rate can vary. An earlier
   storage or ownership refusal is useful evidence; photograph its full screen.
8. Power off. Restore the backed-up 2.0.0 `KUI/apps/games/retail-boot.kui` before normal play or other games.

## Read contract

The private read-only profile preserves the imported image's CID, capacity,
addressing mode, partition, identity, image extents, full SHA-256 and all 256
block CRCs. Projection from the preparation profile does not change the live
pair or enable journal recovery. The high stage rejects game-track overlap and
verifies the actual SCI card plus every mapped image sector before handoff.
Moved, changed or unreadable data is refused.

The game-time worker copies the profile into its owned allocation. A block
read borrows the existing resident reader's serialized SCI lease, verifies card
identity, reads one mapped sector, stops and releases the stream, and checks
the sector CRC and bus health before publishing a reply. Contention or I/O
failure stops the test. No filesystem is mounted during retail execution.

Every reply also requires the exact Silver caller, inherited mask 6 or 15,
held ready/TAS contract, idle software-triggered Maple DMA, inactive timer
callbacks, both banks, all 48 slot pointers and unchanged request metadata.
The worker runs at interrupt mask 15. Only A1 replies and its parent's presence
bit may change. Unsupported requests stop before their DMA is submitted.

This test checks imported-save discovery and loading. It establishes neither
save updates nor restart persistence for Silver. The earlier empty-card result
and Toy Commander persistence evidence are separate observations.
