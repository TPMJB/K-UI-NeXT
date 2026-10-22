# Native virtual VMU save/load test

This follows the successful Power Stone 2 read-only test. It enables saving to
an existing prepared virtual card on the same SCI SD card.

Extract this ZIP into its own folder. With the SD card mounted, run:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast
```

This selects an existing preparation profile, binds this loader to your card,
and installs only `KUI/apps/games/retail-boot.kui`. It has no backup/restore
flow. It does not create or copy a virtual card, or change its image, journal,
identity or preparation profiles.

If several prepared cards are available, the command lists the choices. Select
the intended existing card explicitly, for example:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0001/retail-persistent-profile-0001.bin
```

Use the actual filename reported by the first command. A profile previously
used successfully for Armada is suitable if the image, journal and identity
remain together in that same folder. Existing saves may have changed the card
since the profile was exported; this is expected. The console repeats live
ownership checks and recovery before allowing save writes.

Safely eject the card. Remove physical VMUs and other controller accessories;
keep controller 1 connected. Launch **original Power Stone 2 GDI** with
**SCI / Standard**, default/Auto encoding, CD music off, background loading off
and RAM patches off. Keep the prepared pair files in their current locations.

Create a test save, wait for the game to finish saving, then power off fully.
Boot again with the same settings and load the save. Report whether the name
and progress survived the reboot; photograph any diagnostic screen. If the
game requests formatting or lacks enough free blocks, stop and report that
screen instead of deleting existing saves to make space.

This is a native save/load experiment for the reviewed Power Stone 2 SDK
family. Broader native games, CDDA and SCIF backing still need their own tests.
