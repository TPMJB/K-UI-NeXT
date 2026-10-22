# Native VMU startup measurements

The hardware test of `d503f1a` saved, rebooted and loaded successfully with the
prepared `p0005` pair. Startup took approximately five minutes and gameplay and
FMVs were slow. Those are separate execution phases: the high-stage relay
deliberately disables both caches while checking the executable and installing
the resident; the game later calls the installed worker.

## Scan work

`profile_vmu_native_startup.c` reads a supplied executable into host memory and
times the actual fresh driver/layout, allocator, initializer-call, exit-family
and patch-interior checks. It does not patch or write the input. Build and run
from the repository root:

```sh
mkdir -p build/native-startup-profile
cc -std=c11 -O2 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -Iinclude \
  tools/profile_vmu_native_startup.c src/core/vmu_native_layout.c \
  src/core/vmu_native_discovery.c src/core/vmu_native_heap.c \
  src/core/vmu_native_exit.c src/core/vmu_native_patch.c src/core/hash.c \
  -Wl,--gc-sections -o build/native-startup-profile/profile-native
build/native-startup-profile/profile-native /path/to/1ST_READ.BIN 40
```

On the 3,097,497-byte Power Stone 2 startup input, one host run reported:

| Check | Mean host elapsed milliseconds |
| --- | ---: |
| Fresh driver and buffer layout | 21.155 |
| Allocator family | 3.464 |
| Initializer call | 1.579 |
| Exit family and its three patch windows | 8.234 |
| Additional old six-window pass | 5.714 |
| Combined exit and all six windows | 8.495 |
| Previous complete sequence | 40.145 |
| Combined complete sequence | 34.692 |

These host timings identify relative work; they do not estimate Dreamcast
latency. Hardware startup code runs without the host's memory/cache behavior.

The performance build additionally changes the temporary relay's initial CCR
from `0x808` to `0x908` after the existing full-RAM publication through P2.
Only the instruction cache is enabled; the data cache remains disabled.
After eight settling instructions the relay calls high-stage C through P1.
Its P2 return path still invalidates both caches and restores the owner's
saved enable/mode bits before resuming the game. Other build modes retain
the original relay setting. This change is separate from the combined-scan
host timings above; its console effect still needs measurement.

The persistent stage now uses `kui_vmu_native_exit_discover_guarded`: the same
complete exit-family discovery checks its three entries together with the
caller's submit, initializer and release windows. This removes one full image
pass. The independent public exit-discovery API still checks every exit window.
The patch guard normalizes physical window bounds once, rejects targets outside
their overall envelope, and still checks every encoded direct branch and every
aligned main-RAM pointer, including aliases, boundaries and targets in gaps.

## Persistent preparation I/O

`profile_vmu_persist_startup.c` counts callbacks in the existing synthetic
filesystem/session fixtures. It opens no device or uploaded save:

```sh
cc -std=c11 -O2 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -Iinclude \
  tools/profile_vmu_persist_startup.c src/loader/vmu_persist_stage.c \
  src/core/vmu_durable.c src/core/vmu_persist_session.c \
  src/core/vmu_persist_profile.c src/core/vmu_fs_admission.c \
  src/core/vmu_store.c src/core/vmu_image.c src/core/hash.c \
  src/core/data.c src/core/retail_image.c -Wl,--gc-sections \
  -o build/native-startup-profile/profile-persist
build/native-startup-profile/profile-persist
```

| Fixture | Sector reads | CID checks | Writes | Syncs | Filesystem admission reads |
| --- | ---: | ---: | ---: | ---: | ---: |
| Clean journal | 1,096 | 13 | 1 | 2 | 41 |
| Pending journal recovery | 1,097 | 16 | 2 | 4 | 41 |
| Imported opaque root and sibling pair | 1,098 | 13 | 1 | 2 | 43 |

The clean preparation performs 568 reads during recovery/session preparation,
one during the explicit checkpoint, 526 during the fresh snapshot and one for
the final marker. Preparation validates and hashes the entire current image
before and after the durable checkpoint. The second pass detects damage or
failure during the checkpoint and remains required. CID is checked at session
boundaries and write/recovery boundaries, not for every sector read.

The startup adapter currently issues one-sector CMD17 reads. Its DMA callback is
disabled only around the synchronous PIO write and is restored immediately
afterward. No startup cache, changed ownership proof, skipped recovery or
weakened checkpoint was introduced by the scan optimization.

Actual cards can have much larger directory trees or fragmented FAT chains than
these fixtures. Their bounded filesystem-ownership walk can require more reads;
the fixture counts do not claim a bound on the user's card.
