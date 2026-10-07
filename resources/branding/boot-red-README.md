# K-UI V1.8.5 crimson Dáinsleif artwork

`boot-red.png` and `startup.png` are identical 640×480 RGB deployment assets
for the graphical bootstrap and SD runtime. TPMJB requested the V1.8.5 splash
on October 6, 2026. The built-in image-generation tool edited the existing
release splash, retaining its visor portrait and crimson/black/chrome style,
with K-UI, V1.8.5, Dáinsleif and by TPMJB lettering. The complete production prompt and
conversion record are in `boot-red-prompt.txt`. These project assets are
provided under the repository's GPL-3.0-only terms.

The generated 1448×1086 image includes TV-safe margins and a dark lower area.
It was resized directly to 640×480 RGB with Lanczos. No lettering was edited
after generation. Git blob: `a136e6f426140424febf38aebe69169c430ea6aa`.
SHA-256: `bad69f585b46e56872d855b4ec9602c99f7eb671e6a47478e7a9f8a40c5ff232`.

`tools/build_splash.py` validates those exact bytes before encoding RGB565.
Each executable embeds only its own existing 640×480 pixel array; no runtime
image decoding or additional full-screen buffer is introduced. The separate
badge beneath the Sega logo and the startup chime retain their existing assets.
`release-v1.8.5-banner.jpg` is a 1280×720 JPEG presentation of the same generated
splash, resized to 960×720 and centered with black side margins so its entire
portrait and lettering remain visible. `release-v1.7-banner.jpg` is retained
for historical release references.
Previous artwork and records remain in Git history.
