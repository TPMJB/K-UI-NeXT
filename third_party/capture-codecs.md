# Capture export codecs

These codecs are linked into the normal K-UI capture worker. They are not part
of the resident game loader.

* `miniz/`: miniz 3.1.2 amalgamation from libchdr commit
  `607694ca0812edfc9cc2030c64634fc2393668de`, path `deps/miniz-3.1.2/`.
  Upstream: https://github.com/richgel999/miniz . Its public-domain and MIT
  notices are retained verbatim in `miniz.c`. Only raw DEFLATE/inflate is used;
  ZIP, filesystem, time and zlib compatibility APIs are disabled.
* `minilzo/`: Markus F. X. J. Oberhumer's miniLZO 2.10 distribution, from the
  unmodified archive mirror https://github.com/Upwinded/minilzo at commit
  `0cb716665fb1026555a3fb8cc0b2b30655b766a6`.
  Author's site: http://www.oberhumer.com/opensource/lzo/ . GPL-2.0-or-later;
  `COPYING`, `README.LZO` and source notices are retained. This is the standalone
  codec, not DreamShell application or loader code. LZO1X-1 compressed blocks
  produce the DreamShell ZISO dialect; safe decompression checks readback.

No external codec installation or network access is needed to build these
vendored, pinned sources. Independently authored container writers are in
`src/core/capture_export.c` and `src/core/capture_chd.c`.
miniLZO's documented `LZO_CFG_NO_UNALIGNED=1` option is enabled on every target
so byte-stream accesses are portable and also pass alignment sanitizers.
