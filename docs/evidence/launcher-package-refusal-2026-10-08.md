# Refused package identity and deployment correction

The supplied `62144.jpg` shows a launch refusal with these consecutive lines:

```
Loading SD runtime e4c521ddd8b5 (102332 bytes)
Retail boot: unsupported retail-boot package layout
```

The loader's message identifies the package it just opened, not necessarily the
running launcher. `kui_games_retail_prepare_reader()` calls `kui_runtime_read()`
for `/KUI/apps/games/retail-boot.kui`; that function emits the same generic text
for every runtime-envelope package. The launch stopped before map preparation
and detached execution.

The previously delivered video test's actual bytes identify `d4d18c11ffeb`,
61,652 payload bytes and 61,716 total bytes. Its source ZIP SHA-256 is
`0de59dcb06ae86d5c2564a762e0b8fc5e5ec7fedb783a63c0dacdaae8b146bd7`.
The photographed file therefore differs from that delivered test. The rejected
file has not been supplied, and its exact provenance is unconfirmed.

The delivered video package and successful CDDA baseline share every relocation
header field except the stage length: 53,460 versus 53,456 bytes. Both lengths
are four-byte aligned, both fit the same allowed stage region, and both select
the exact accepted low-resident tuple. The original release's launcher
`games_retail.c` at `e4c521ddd8b5afe018dfbb82e7a9c5974d9de0b2`, fetched read-only
from the project repository, uses the same layout acceptance gate. No policy
change is needed to accept the shipped candidate.

An independent host run of the extracted production C gate with ASan/UBSan
accepted both actual delivered packages and rejected ten malformed variants.
The committed reusable checker additionally checks a wrong-file case, with
valid envelope checksums for every negative fixture. The final bundle checks
the actual newly built launcher as the rejected game-launch file.

The correction supplies the unchanged video test already named at its exact
destination and improves filename/build/size diagnostics in the launcher. The
layout validation, retail mapper, resident, stage, worker, and SCI code are
unchanged. This report does not claim a console-tested launch or video fix.

Reproduce after building the launcher and collecting the bundle:

```sh
python3 tools/check_retail_launcher_package.py \
  /path/to/KUI/apps/games/retail-boot.kui --reject /path/to/KUI/runtime.kui
```
