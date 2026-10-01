# K-UI crimson Dáinsleif startup

`startup.png` is the 640×480 RGB SD runtime splash. At the owner's request on
2026-10-01, it now uses the exact approved red boot-menu artwork: the new visor
portrait, crimson light, chrome K-UI lettering and Dáinsleif title. Software
remains 1.5.1; 2.0 follows SCI testing/tuning. The badge beneath Sega's logo is
a separate asset.

The runtime and `boot-red.png` are byte-for-byte identical. Artwork provenance,
the original generation prompt and TV-safe conversion are recorded in
[boot-red-README.md](boot-red-README.md). The 576×432 composition is centered in
a 640×480 frame with a `#090102` border. No new image generation or lettering
change was needed for this runtime update.

- Runtime and boot PNG Git blob: `f2b9132613a527f90759fdb0fd70d47a6eb07563`.
- PNG SHA-256: `f1cfdd82dba67518af18fe208a1334f18dc0bee2041590f9ade2af0f2c3549e1`.
- Original source PNG SHA-256: `d4b67cc589009c776bff4488d0206dc71abc3efce566b0248a624abeed1314fe`.

The host encoder checks the pinned PNG identity and converts it to RGB565.
Each executable links only its own fixed 640×480 array: runtime dimensions,
memory usage, sound and startup timing are unchanged. Updating `KUI/runtime.kui`
is sufficient for this splash change; the confirmed `6af5e11` boot CD remains
compatible and does not need rebuilding or reburning.

The preceding September 24 artwork and its generation prompt remain documented
in Git history (`startup.png` blob `b08c8ee03362b6775403e1bd50f8609f1c522f40`).

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
