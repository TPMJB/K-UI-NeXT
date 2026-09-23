# Disc insertion, Resume and the original startup experience

Keep the existing boot CD. Copy the updated KUI/runtime.kui; keep KUI/apps/music
for menu music. The original startup splash and sound are embedded in the SD
runtime. B skips the cue. This round does not alter the three frozen reader
files; new app hardware behavior still needs console acceptance.

## Controls

- Home Y restores the old volume cycle: Off,15,30,50,75,100%,Off. It applies immediately and tries to save the
  system music choice to SD; Settings reports an unsaved choice if saving fails. The top-right header shows volume/paused status
  and the selected song. Settings X still advances to the next original loop.
  Other apps retain their own Y actions (Verify, backup, save log).
- Ripper X remains full Resume. Advanced > Quick resume (sizes only) asks for
  confirmation before using the existing metadata-only CRC resume path.
  It cannot detect same-size corruption of already saved bytes. Older SHA jobs
  still reread their prefix. Saved settings and bench.cfg are not rewritten by
  Quick Resume. See RESUME-AND-RETRIES.md.
- GD Play asks for confirmation, drains app audio/I/O, then uses normal KOS
  shutdown and the console BIOS reboot path. Stock BIOS region and autostart
  rules apply; BIOS may require selecting Play. This is not a region-free
  software loader and cannot return to K-UI without booting K-UI again.
- Music Player starts at /Music. A opens a folder or plays a WAV; B stops
  playback or goes to the parent; Start returns Home; Left/Right changes page.
  PCM16 WAV, mono/stereo,8–44.1kHz, is supported. Full songs stream from SD;
  the menu's2MiB cache limit does not apply. MP3/FLAC/Ogg and audio-CD controls
  are not implemented. Playback owns SD until it stops, then menu music can
  resume. Create /Music on your PC; browsing never creates or writes files.

## Focused console checks

1. Boot with no buttons, then boot while skipping with B. Confirm the exact
   original splash/cue, followed by normal Home and working controls.
2. From the boot CD, open the lid, insert a GD-ROM and close it. Confirm the
   new title. Repeat with a second GD-ROM and with an empty drive. The
   detector now accepts nonnegative BIOS status returns and initializes once
   when a newly inserted disc's type is unknown/stale. Save Diagnostics if
   it still fails: transition-only BIOS status records now identify the path.
3. Try Home Y through the volume levels and Off, then Settings X next song.
   Check the top-right title/status and that Y Verify still works in Ripper.
4. On an existing stopped CRC-only job, compare the behavior of full Resume
   and explicitly chosen Quick Resume. Do not create another full dump merely
   for this test. Reboot first if the previous run said RESET REQUIRED.
   A later saved-file verification/PC check is still needed to detect old
   same-size card corruption after Quick Resume.
5. Put a normal PCM16 WAV longer than2MiB under /Music, browse to it, play
   through the end, then play again and stop with B. Try a stereo file too.
   Check for sound gaps and that another SD operation works afterward.
6. With a suitable retail game inserted, open GD Play, cancel once, then
   confirm. Expect stock BIOS behavior. Save other test logs before leaving.

If the screen says Checking saved prefix, ~600KiB/s is consistent with the
already measured saved-prefix reread. If it says Capturing, use the diagnostics
to distinguish resumed acquisition from SD checking; a prior DMA timeout can
leave that boot on the slower PIO path. Ordinary damaged-sector retries exist; extensive old
targeted salvage is still a separate port. Region/BIOS maintenance and size
comparisons are covered in INDEPENDENT-APP-PARITY.md.
