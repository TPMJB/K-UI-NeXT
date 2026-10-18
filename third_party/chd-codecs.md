# CHD decoder dependencies

`third_party/libchdr` is independently sourced from the original upstream
repository, <https://github.com/rtissera/libchdr>, pinned to commit
`47d794151b2b64f6aba60304e6480f4f5a2bc24b` (2026-10-11). It is a read-only
CHD container implementation derived from MAME. Upstream C sources and public
headers retain their original notices. Documented security adaptations in
`src/libchdr_chd.c` reject unknown legacy map types and resolve self references
iteratively, with a 4096-hop ceiling, instead of recursively consuming the
worker stack. It also checks the complete 48-bit self target before conversion
to a hunk index. The resolved base hunk retains the upstream's optional self cache. `dependencies.json` records upstream and adapted hashes.
Allocation checks in the Huffman/bitstream constructors and lazy codec
initialization additionally prevent null dereferences and free partial decoder
state on allocation failure, allowing retry and close without retained heap.
LZMA and Zstd initialization preserve an explicit out-of-memory result.
The local `kui_config.h` and `config/libchdr.mk` supply the K-UI build
configuration. The unused optional C++ microflac backend is not built or fetched.

The decoder runs while the KOS shell is alive and uses its heap and callback
file access. It is **not** a detached-game resident. K-UI's cache preparation
path closes it before starting the existing uncompressed game loader.

Enabled configuration: raw sector reconstruction, subcode reconstruction,
per-hunk CRC verification, low-memory checkpointed maps, in-place CD sector
spreading, bundled portable C codecs. The CD codecs `cdlz`, `cdzl`, `cdfl`
and `cdzs` are built together with the general DEFLATE/LZMA/Huffman/FLAC/Zstd
codecs referenced by libchdr. AV/hard-disk CHDs are excluded by the K-UI
optical wrapper's header/metadata policy.

## Licenses retained in the vendor tree

* libchdr: BSD 3-Clause, `libchdr/LICENSE.txt`; individual source notices remain.
* LZMA SDK 26.02: public domain, `libchdr/deps/lzma-26.02/LICENSE`.
* miniz 3.1.2: MIT and public domain/Unlicense notices embedded in its source files.
* dr_flac: public domain or MIT-0, embedded in
  `libchdr/include/dr_libs/dr_flac.h`.
* Zstandard 1.5.7: BSD 3-Clause selected from its dual license;
  `libchdr/deps/zstd-1.5.7/LICENSE`, sourced directly from
  <https://github.com/facebook/zstd/blob/v1.5.7/LICENSE>.

This miniz is separate from the previously vendored capture encoder. The
private forced-include configuration prefixes its externally defined miniz
symbols with `kui_chdr_`, and does not alter capture's source or build flags.

The K-UI adapter is `include/kui/chd_lib.h` / `src/core/chd_lib.c` (GPL-3.0-only).
It bounds optical geometry and metadata before libchdr opens the container,
refuses parents, accepts v4/v5 and zero-pregap optical track metadata, keeps
disc addresses distinct from aligned storage frames, and converts canonical
audio to the little-endian GDI convention only at the extraction boundary.
Unknown track modes, explicit/stored pregaps, postgaps and subchannels are
refused instead of being dropped. Capture's restricted fixed-map reader is
retained separately as an independent fixture/test implementation.

The wrapper additionally prevalidates fixed v4 maps, requires CRCs for direct
and mini entries, and refuses secondary compression/parents, unknown flags,
bad raw lengths and invalid self targets. Self-referenced entries inherit the
verified target's CRC. `tools/chd_notices.py` retains complete embedded notice
blocks and all explicit dependency licenses in distributable packages.
