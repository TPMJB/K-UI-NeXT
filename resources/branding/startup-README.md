# K-UI V1.5 “Dáinsleif” startup

`startup.png` is the 640×480 RGB splash for K-UI V1.5 “Dáinsleif”. It keeps
TPMJB's established visor portrait, chrome wordmark and perspective grid, with
crimson neon and restrained cyan accents. The name includes the acute accent
in Dáinsleif. This artwork replaces the SD runtime splash only; the original
badge beneath Sega's logo on the boot disc is a separate asset.

The built-in image generation tool produced `startup-dainsleif-source.png`
on 2026-09-24 as a single edit of the previous startup artwork. The original source is retained separately at
1448×1086 RGB (4:3); the exact request is recorded in
`startup-dainsleif-prompt.txt`. The previous splash was the approved 640×480
preview from [K-UI_DS resources/boot-preview.png](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/resources/boot-preview.png),
Git blob `33b49462795c7db399c1a5e9af7b9b96b1378140`.

The deployment image is downsampled to 592×444 and centered in a 640×480
frame, keeping lettering inside television-safe margins. No lettering or
illustration was changed after generation. The deterministic conversion is:

```sh
convert resources/branding/startup-dainsleif-source.png \
  -filter Lanczos -resize 592x444 -background '#030913' \
  -gravity center -extent 640x480 -strip -define png:color-type=2 \
  resources/branding/startup.png
```

- Runtime PNG Git blob: `b08c8ee03362b6775403e1bd50f8609f1c522f40`.
- Runtime PNG SHA-256: `16e4bed7e4d8c160de400b66445b512b77232261a35ead0c0cb6c7df7f71a7eb`.
- Source PNG SHA-256: `111ec10a6c2a94c5841a28ae4fb918fb3a9707cc1946ac438d8c8187383d0cdf`.

The host encoder checks the pinned runtime PNG identity and converts it to
RGB565 without importing a DreamShell renderer. Runtime dimensions, memory
usage and startup timing are unchanged.

The startup sound still uses the original three-note composition and timbre
documented in the prior K-UI_DS revision's `utils/build_boot_assets.py`. Its
original stereo channels were identical; the new output preserves one channel
at 44.1 kHz. The quiet decay is shortened to end by 2.62 seconds and the original
long silent tail is removed; total PCM duration is 2.65 seconds. No sampled
third-party sound or DreamShell audio player is copied.

The splash is part of the SD runtime, so updating it needs no new boot disc.
It is shown only at startup, can be skipped with B, and adds no drawing work
during capture. The chime has a 2.7-second budget including audio setup; its
player drains audio before preference/card/drive work. The shell separately
limits splash display to three seconds so slow preference loading cannot hold
the splash on screen.
