# K-UI V2.0 Tyrfing pixel-art startup — RC14

The maintainer selected variation 2, the character-free pixel-art Tyrfing
scene, on October 15, 2026. `startup-tyrfing-pixel-master.png` is the unmodified
1672×941 RGB image returned by the built-in image generation tool. Its exact
production prompt and reference are recorded in
[startup-tyrfing-pixel-prompt.txt](startup-tyrfing-pixel-prompt.txt).
The previous portrait master and prompt remain as historical sources.

Deployment conversion only: Pillow nearest-neighbor resize of the complete
master to 576×324, pasted at (32,24) on a 640×480 RGB `#090102` canvas. This
preserves all lettering, 32-pixel side margins and a clear lower region for
the existing bootstrap countdown at y=360..456. No cropping, repainted pixels
or lettering edits are applied. `startup.png` and `boot-red.png` are identical.
The historical boot-red filename remains the bootstrap asset contract.

- Deployment Git blob: `a851c7eee7f32cc8340f72de93e9c0030fce1b97`.
- Deployment SHA-256: `45f70493ee914e05f65cd804f17e3f8db7cc7ad927e2b003ca64001f9a9b4d09`.
- Master SHA-256: `9dd274432cf82cb35087823c2819ed9d8932c6c940a4ce0443e81cbef1d6c74b`.

`tools/build_splash.py` pins those deployment bytes before encoding the same
640×480 RGB565 arrays. Runtime buffers, timing, sound, input and loaders do not
change. The orange vector header and disc sword badge remain from RC11.

`release-v2.0-banner.jpg` uses the complete selected master resized with
nearest-neighbor to 1280×720, JPEG quality 95 without chroma subsampling.
The runtime update supplies the new splash; the optional matching CDI supplies
the boot-disc background. The existing compatible boot CD remains usable.
To preview the bootstrap from that CD, use its Card tools entry with the
matching `KUI/tools.kui` from this candidate's optional bootstrap package.

The existing three-note 2.65-second mono startup chime is unchanged. Original
project artwork and derived assets are offered under GPL-3.0-only.
