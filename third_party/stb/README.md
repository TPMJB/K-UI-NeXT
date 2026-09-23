# stb_vorbis source record

`stb_vorbis.c` is unmodified upstream version 1.22 from
<https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_vorbis.c>.

SHA-256: `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`

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
