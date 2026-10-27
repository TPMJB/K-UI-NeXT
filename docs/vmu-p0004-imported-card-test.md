# Imported p0004 card: Toy Commander load and persistence test

The owner imported all 18 saves from the VMU Manager Y/backup-all archive into
`/KUI/vmu-test/p0004`. Card tools reached READY, reporting 196 used blocks and
4 free. The uploaded P2 profile passes independent Python and production C
codecs; its image SHA256 and all 256 block CRCs match the reconstructed image.
Its fresh journal and identity digests match, and its allocation spans are
bounded and separate. Live SD ownership is still rechecked at every launch.

Owner result, 2026-10-19: Toy Commander lists the old imported save and newly
created saves, and the new saves survive a full reboot. This confirms imported
save usability and restart persistence by owner report. The updated image was
not uploaded for independent byte-level inspection. Preserve the current p0004
image and journal; they now contain progress absent from the reconstruction.

This isolated test uses the previously audited bf91dd1e994c Toy Commander
persistent loader. Only its embedded 4096-byte P2 record and runtime envelope
checksums are personalized for p0004. Executable code, worker, resident and
memory layout are unchanged. The packet includes the derivation audit.

## Install and run

1. Back up the current ordinary `KUI/apps/games/retail-boot.kui` outside KUI,
   then copy this ZIP's `KUI` folder onto the SCI SD card. Keep all existing
   `pNNNN` folders and the working Silver module. No card image, identity or
   journal is included in this installer.
2. Safely eject and power off. Remove physical VMUs and other accessories
   from controllers. Keep one ordinary controller connected in port A.
3. Launch the original uncompressed, raw-sector Toy Commander GDI using SCI,
   Standard reader, background loading Off, CD music/CDDA Off and RAM patches
   Off. This loader admits only the exact supported Toy Commander disc.
4. Confirm startup shows PERSISTENT SD SAVES and `/KUI/vmu-test/p0004`.
   The base code build ID remains bf91dd1e994c; the p0004 path identifies this
   profile binding. Load the imported AAA profile and check its name/progress.
   Photograph the profile selector and report whether AAA loads.
5. The card starts with only 4 free blocks. If the game offers enough space,
   create a separate test profile such as P04. Complete its normal save and
   wait until saving finishes. Keep the imported AAA profile and other saves.
6. Fully power off, restart with the same settings and confirm P04 loads.
   A load in the same session alone does not establish restart persistence.
7. Restore the ordinary `retail-boot.kui` after this test so other games use
   their existing loader. The optional Silver module remains bound to p0003.

This inherited Toy proof has a diagnostic card window of roughly five minutes
and an overall bound of roughly ten minutes. Complete the first test promptly.
If a diagnostic appears, photograph all five pages (0 through 4) and preserve
the p0004 image, journal and identity together. A planned window stop does not
by itself establish a storage failure. This is a compatibility/persistence test,
not a routine-play or all-games release.

## Verification limits

The 18-save archive includes `TOY_COMM.___` and `TOY_COMM.AAA`; both have zero
internal header CRC16 values differing from the calculated values. Their K-UI
backup proofs pass, and every original payload byte is preserved. The imported
AAA load test determines whether that game's original save is usable. No
checksum repair was attempted, and the photograph/profile do not prove game
save validity.

The journal makes a committed 512-byte block recoverable. A game's multi-block
save still requires keeping power on until it finishes. Preparation hashes
are provenance: legitimate saves can change the image, and later launches
validate current contents after journal recovery. Moving or replacing a
prepared pair requires a new ownership profile; keep its files in place.

