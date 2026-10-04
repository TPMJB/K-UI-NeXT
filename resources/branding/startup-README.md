# K-UI V1.7 Dáinsleif startup

`startup.png` is the new 640×480 RGB release splash, identical to the matching
`boot-red.png` artwork. The built-in image-generation tool created it for
TPMJB on October 4, 2026 in the established crimson/chrome cyberpunk style.
See [boot-red-README.md](boot-red-README.md) and
`startup-dainsleif-prompt.txt` for the prompt, TV-safe conversion and exact hashes.

The encoder pins the artwork and emits the existing RGB565 pixel array.
Runtime dimensions, memory use and startup timing are unchanged. Updating
`KUI/runtime.kui` supplies the new runtime splash; a compatible working boot CD
remains usable. The package also includes an optional matching-artwork CDI.
Previous artwork remains in Git history.

The startup sound still uses the original three-note composition and timbre
documented in the prior K-UI_DS revision's `utils/build_boot_assets.py`. Its
original stereo channels were identical; the new output preserves one channel
at 44.1 kHz. The quiet decay is shortened to end by 2.62 seconds and the original
long silent tail is removed; total PCM duration is 2.65 seconds. No sampled
third-party sound or DreamShell audio player is copied.

The runtime embeds `startup-chime.ogg`, an Ogg Vorbis encoding of those 116,865
samples, instead of the samples themselves: 16,989 bytes rather than 233,730,
so `runtime.kui` and its resident image shrink by 216,741 bytes. It decodes to
exactly 116,865 frames at 44.1 kHz, 37.5 dB against the synthesized samples.
`tools/build_splash.py` checks its pinned SHA-256 before embedding it;
`python3 tools/build_splash.py --encode-chime` regenerates it with the menu
music's pinned FFmpeg/libvorbis settings (quality 5, bitexact output).
The cue allocates a 384 KiB decoder arena and releases it when the cue ends,
before any menu song is loaded. It decodes only up to the last sample, then
pads with silence.

The splash is part of the SD runtime, so updating it needs no new boot disc.
It is shown only at startup, can be skipped with B, and adds no drawing work
during capture. The chime has a 2.7-second budget including audio and decoder setup; its
player drains audio before preference/card/drive work. The shell separately
limits splash display to three seconds so slow preference loading cannot hold
the splash on screen.
