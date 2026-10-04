# K-UI V1.7 crimson Dáinsleif artwork

`boot-red.png` and `startup.png` are identical 640×480 RGB deployment assets
for the graphical bootstrap and SD runtime. TPMJB requested fresh release
artwork on October 4, 2026. The built-in image-generation tool created a new
visor portrait in the established crimson/black/chrome style, with K-UI,
V1.7, Dáinsleif and by TPMJB lettering. The complete production prompt and
conversion record are in `boot-red-prompt.txt`. These project assets are
provided under the repository's GPL-3.0-only terms.

The generated 1448×1086 image was resized to 576×432 with Lanczos and centered
in a 640×480 `#090102` frame for TV-safe margins. No lettering was edited after
generation. Git blob: `c0170e39c990ccc91548c27e892dce62862b67c8`.
SHA-256: `78bc1f77a4579018fd17d0ce238f7e6f0732b0162f2ee895c54d4c1675488f77`.

`tools/build_splash.py` validates those exact bytes before encoding RGB565.
Each executable embeds only its own existing 640×480 pixel array; no runtime
image decoding or additional full-screen buffer is introduced. The separate
badge beneath the Sega logo and the startup chime retain their existing assets.
`release-v1.7-banner.jpg` is the matching wide promotional variant, generated
with the new splash as its identity reference and resized to 1280×720.
Previous artwork and records remain in Git history.
