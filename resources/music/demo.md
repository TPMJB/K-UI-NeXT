# Harbor Lights — one-minute playback sample

The SD update includes `Music/harbor-lights.wav`. Copy the `Music` directory to
the root of the SD card, open **Music**, and choose **harbor-lights.wav**.

Harbor Lights is now also the sixth song in the menu rotation, shipped as
`KUI/apps/music/harbor-lights.ogg` (197,444 bytes, Ogg Vorbis quality 5) and
recorded in the menu music manifest. The `Music` copies remain for testing the
custom-song path.

This original instrumental uses warm synth chords, a small arpeggio and quiet
electronic percussion. It was synthesized for TPMJB/K-UI-NeXT without external
recordings, samples, sound fonts or musical material. The generator is
`tools/generate_music_demo.py`, licensed GPL-3.0-only. As with K-UI's original
menu music, the generated composition and recording may be used, modified and
redistributed without additional restrictions to the extent rights exist in
that generated material.

| Property | Value |
| --- | --- |
| Duration | 60 seconds |
| Format | Mono PCM16 WAV, 22,050 Hz |
| PCM bytes | 2,646,000 |
| WAV bytes | 2,646,044 (about 2.52 MiB) |
| Frames | 1,323,000 |

It exceeds the earlier 2-MiB music-file limit while fitting the new 6-MiB cache.
Check that music continues beyond 48 seconds, when it passes the old limit,
and stays audible while moving between Music, Home and the ripper. Listen for
interruptions when opening menus. This is an app/audio check; no new full-disc
benchmark or boot-disc burn is needed. Playback during disc capture still needs
console confirmation.

`Music/harbor-lights.json` records the format, length and generated SHA-256.
Regenerate the exact PCM WAV with Python's standard library:

```sh
python3 tools/generate_music_demo.py --directory build/music-demo
```

The `--ogg` flag uses a locally installed `ffmpeg`/libvorbis to produce an Ogg
Vorbis copy. The Music app now accepts both formats. Ogg keeps the compressed
file in RAM and decodes small output blocks during playback; it does not expand
the whole song into a PCM cache or read SD while playing.

For the next hardware check, compare `harbor-lights.wav` and
`harbor-lights.ogg` at the same volume, then switch away and back using the Home
or Ripper triggers. The selected custom song participates in that cycle while
cached. Verify looping and menu navigation before using Ogg during a rip; codec
CPU cost and audio stability on the console remain unmeasured.
