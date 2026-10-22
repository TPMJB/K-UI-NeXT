# Pinned FatFs download mirror

These are byte-for-byte copies of ChaN's FatFs R0.16 archive and its two
upstream correction patches. They let clean builds work when elm-chan.org
is unavailable. The existing SHA-256 pins in `dependencies.json` remain
authoritative; `tools/fetch_deps.py` checks every input before caching or
extracting anything. A mismatched cache or bundled file stops the build.
If a bundled file is absent, the same pinned upstream download remains the
fallback.

| File | Original source | SHA-256 |
| --- | --- | --- |
| `ff16.zip` | https://elm-chan.org/fsw/ff/arc/ff16.zip | `99f7dc1f7e095356e4a9e3dbe29959090d8b948afe2bbc5441e52fdf4b85449e` |
| `ff16p1.diff` | https://elm-chan.org/fsw/ff/patch/ff16p1.diff | `7996ddc3135f3d534a8153f7d75d587030e5c8551b003c3b7ee823ffb7fc64bf` |
| `ff16p2.diff` | https://elm-chan.org/fsw/ff/patch/ff16p2.diff | `5cd39f1fc299f0f1dec9fa1ce544dc0bc048b2f6eb3b8161476ed20f9cf1e290` |

The files were copied from the already verified build download cache; no
archive contents, patches or line endings were changed. `LICENSE.txt` is
extracted unmodified from `ff16.zip`. FatFs permits source and binary
redistribution with its source copyright notice, condition and disclaimer
retained. The full original archive and its notices are preserved here.
