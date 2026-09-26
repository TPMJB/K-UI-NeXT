# stb_vorbis source record

`stb_vorbis.c` is based on upstream version 1.22 from
<https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_vorbis.c>.

Upstream SHA-256: `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`

Local SHA-256: `7162e378e5dc6b18bf26fe4e6e383c48f4a1b6a1ab0a015853ad6cb6e321b300`

Local changes are limited to `get_seek_page_info`: both `getn` calls now return
failure before inspecting a short header or lacing table, and the high granule
byte is cast to unsigned `uint32` before its 24-bit shift. The guards address
a real unchecked-read path diagnosed by GCC 15.2; no warning was suppressed.
`tests/test_music_ogg_seek.c` exercises every truncation of both tables, valid
page positioning, and a high-bit granule value under ASan/UBSan.

K-UI selects upstream's MIT license, alternative A, retained in the source and
`LICENSES/stb_vorbis.txt`. No DreamShell code or decoder is used.

The wrapper `src/apps/music_ogg.c` disables stdio and push decoding, limits the
codec to two channels, and supplies a fixed 384 KiB allocation arena. Ogg page
checksums, serial/sequence continuity and complete end-of-stream are checked
before opening the decoder. Chained/multiplexed streams are rejected. Accepted
sample rates are 8–44.1 kHz; clips must contain at least 1024 frames.

Test tones in `tests/fixtures/music_vorbis.h` are original synthesis, generated
by `tests/make_music_vorbis.py`. FFmpeg/libvorbis is a host fixture/demo encoder,
not a linked runtime dependency. Host tests do not need it installed.

# stb_image source record

`stb_image.h` is based on upstream version 2.30 from
<https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h>,
the same stb commit as `stb_vorbis.c`.

Upstream SHA-256: `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`

Local SHA-256: `ea9ff2654bbec309b0cf6561375e69364360cbc8252b14f8c9c04ef7cd62eb9a`

The one local change is in `stbi__getn`: a zero-length read returns success
before copying. An empty PNG chunk previously passed a null buffer to
`memcpy`, which UBSan reports as undefined behavior; `tests/test_cover_image.c`
decodes such a file under ASan/UBSan with recovery disabled.

K-UI selects upstream's MIT license, alternative A, retained in the source and
`LICENSES/stb_image.txt`. No DreamShell code or decoder is used.

The wrapper `src/apps/cover_image.c` builds only the PNG and JPEG readers,
from memory, without stdio, HDR or SIMD. It decodes only box art the owner
places in `KUI/covers`, at most 3 MB and 1.2 megapixels, checked from the
header before decoding. Disc artwork (`0GDTEX.PVR`) never passes through
stb_image: `src/core/pvr_texture.c` is K-UI's own decoder.
