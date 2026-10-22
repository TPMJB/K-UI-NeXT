<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Automatic native resolution cache integration

`src/apps/games_native_cache.inc` is called after the final launch-module
selection in `games_retail.c`, while FatFs is still mounted and before map
encoding or shutdown. Only a module containing one valid typed cache seed
opts in. Ordinary modules without that container retain their existing path;
this does not select a virtual-card module or expand VMU compatibility.

The integration requires native original GDI, SCI/Standard, default options,
no scrambling, no forced encoding, no CD music and no RAM patches. It reads
the exact canonical boot bytes, excluding logical-sector tail padding. The
SHA256 filename is `0:/KUI/VMU/native-cache/v1/<sha>.nvr`. Each 1024-byte record
contains complete immutable driver/layout/allocator/call/exit discovery and
the detector epoch. Unknown, corrupt, truncated, old-epoch or other-image
records cause a fresh complete discovery. Unsupported discovery leaves the
module unseeded so the original stage can follow its usual path.

Cache files are trusted local configuration, like launch settings. Their
integrity digest is not authentication against a hostile card owner. A valid
record is copied into the module's typed container with its separately
copied digest before handoff. The stage independently hashes the executable
that it actually loaded and checks the complete record's epoch/digest. Live
ownership, allocation, layout binding, lifetime and patch-installation checks
remain required. A stale cache is not a live memory or storage capability.

Fresh records are written to a new temporary file, synced, closed and read
back before publication by rename. An invalid old cache may be removed just
before the rename; a power loss in that gap leaves a cache miss. No partially
written record is published under the canonical name. Cache persistence
failure is nonblocking: the complete fresh proof can still seed this launch
in RAM. Cancellation suppresses module seeding; a completed cached record may
remain available for the next launch. Low memory and optional I/O failures
fall back to the original stage path. Files in the reserved `/KUI/VMU` source
subtree are never written as cache files, including case/short-alias spellings
below that ancestor.

`make test-games-native-cache STARTUP=/path/to/1ST_READ.BIN` compiles the actual
frontend include with the actual detector/cache/seed codecs and bounded fake
image/filesystem I/O. It checks normal hits, option/module gates, low memory,
false and duplicate containers, full actual-image discovery, stale record
replacement, publication faults and cancellation at file-operation boundaries.
The real game executable is supplied locally and is not distributed.

The recovered checkout lacks the full runtime's FatFs/KOS build dependencies.
This source integration and its focused host tests do not constitute a
rebuilt or console-tested Games frontend. The separately packaged preseeded
module exercises the same stage replay contract without requiring a new UI
runtime; it remains a distinct test deliverable.
