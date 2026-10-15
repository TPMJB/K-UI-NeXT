# K-UI V2.0 Tyrfing startup

`startup.png` and `boot-red.png` are matching 640×480 RGB deployment assets
for the SD runtime and graphical CD bootstrap. The historical `boot-red`
filename remains the bootstrap's asset name; its current artwork is orange.
The source is `startup-tyrfing-master.png`, created with the built-in image
generation tool for TPMJB's October 15, 2026 Tyrfing request. The exact prompt
is retained in [startup-tyrfing-prompt.txt](startup-tyrfing-prompt.txt).

The visor portrait and chrome K-UI/V2.0 lettering retain the established
retro futuristic composition. An orange, rune-lit Tyrfing blade replaces the
crimson release motif. The artwork carries the target V2.0 identity; the live
console version and build label identify release candidates separately.

The 1448×1086 RGB master is resized with Pillow Lanczos to 576×432 and centered
at (32,24) on a 640×480 `#090102` canvas. This supplies television-safe margins
without cropping the generated lettering. No lettering or painted detail is
edited after generation. The lower area stays dark for live boot controls.
The deployment files have identical bytes:

- Git blob: `943c815fcff5884857c2f772a6329649e6725e6f`.
- SHA-256: `2c5bce50bcd53dd119a9284360b4be45645a96ea3e79f1a9d55329e273fed386`.

`tools/build_splash.py` validates those exact bytes before encoding each
executable's existing 640×480 RGB565 array. Dimensions, memory allocation,
startup timing and rendering behavior are unchanged. Previous Dáinsleif
artwork remains in Git history and historical release banners.

`release-v2.0-banner.jpg` presents the same generated master at 960×720,
centered on a black 1280×720 canvas, JPEG quality 92 with no chroma subsampling.
It is a release presentation asset; it adds no runtime image or decoder.

Updating `KUI/runtime.kui` supplies the runtime splash and header. The
matching-artwork CDI updates images stored on the boot disc; an existing
compatible working bootstrap still loads this runtime. The separate small
header and disc badge have their own vector provenance records.

The original three-note startup composition and shortened 2.65-second mono
encoding remain unchanged. `startup-chime.ogg` is still pinned by SHA-256 in
`tools/build_splash.py`; no third-party sound or DreamShell audio code is added.
The existing decoder arena is released before menu music loads. The splash
is skippable and adds no drawing work during capture.
