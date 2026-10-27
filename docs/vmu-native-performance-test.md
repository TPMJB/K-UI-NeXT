# Native VMU performance test

Power Stone 2 save/load survived a full reboot on the previous build, but
startup, FMV and gameplay were much too slow. This build reduces repeated
controller-poll validation, runs immutable worker code through the instruction
cache, enables instruction caching during startup scans, and combines duplicate
startup patch scans. Mutable state, DMA buffers
and the private stack remain uncached. Existing storage recovery and save
commit/readback checks remain enabled.

Extract this ZIP into its own folder. With the SD card mounted, run:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin
```

This uses the same existing prepared card as the successful save/load test.
It installs only `KUI/apps/games/retail-boot.kui`, binding its prepared profile
to this loader. It does not recreate or copy the VMU image, journal or identity.
There is no backup/restore flow.

Safely eject the SD card. Remove physical VMUs and other controller
accessories; keep controller 1 connected. Launch the same **original Power
Stone 2 GDI**, **SCI / Standard**, default/Auto encoding, CD music off,
background loading off and RAM patches off.

Compare these three things with the previous build and your normal baseline:

1. Time from launch to gameplay, including time spent on the scan messages.
2. FMV playback speed and audio.
3. Gameplay speed and responsiveness.

Then load the existing test save. Create or update a test save, wait for saving
to finish, power off fully, reboot with the same settings and load it again.
If the game requests formatting, stop and report the screen. Photograph any
diagnostic screen and include whether the slowdown begins before or after
the VMU is detected.

Host tests prove fewer repeated checks and the cached-code relocation/stack
contract. They do not establish console performance. This build still uses
the reviewed native SDK family; CDDA, SCIF backing and broader routing remain
separate work.
