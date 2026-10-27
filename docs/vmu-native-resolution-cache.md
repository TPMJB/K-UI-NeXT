# Complete native discovery cache

This cache reuses the complete immutable native-game resolution: Maple driver,
buffer layout, allocator, initializer call and terminal exits. It contains no
live manager state, allocated lease, SD ownership state, save data or authority
to install hooks. Every launch still needs live buffer binding, allocator and
lifetime checks, storage admission/recovery and normal installation guards.

The existing 128-byte `KUINVM01` driver-only `.nvm` format is unchanged. Its
reference loader still repeats full discovery. The complete cache has a distinct
`KUINVR01` format and API.

## Proof and trust

`kui_vmu_native_resolution_cache_build()` performs all fresh discovery,
uniqueness and patch-window proofs, including the allocator release instruction
replay. It emits a canonical 1,024-byte record and optionally the complete typed
resolution. Unsupported or ambiguous inputs leave both outputs untouched.

`..._replay()` first validates the record's canonical encoding and detector
epoch, compares its SHA256 to an independently trusted expected digest, and
hashes the current executable. Image SHA256, exact byte count and load address
must all match. Those equalities reuse the complete original immutable proof,
including its uniqueness decisions. No scan of remembered target addresses is
needed. Changing any image byte, target field, header, reserved byte, digest,
load or detector epoch causes a cache miss. The caller then runs full discovery.

The expected digest must have independent provenance. For example, it may be
pinned in a personalized module after a host performed the complete builder, or
provided by the launcher under the project's explicit trust in ordinary local
configuration. A checksum stored next to an untrusted record authenticates
nothing: an editor can change both. Fast replay deliberately does not claim
protection against a malicious user who replaces both the metadata and its
trusted configuration. The loader and modules are already user controlled.

`..._record_digest()` verifies encoding, schema, epoch and local integrity only.
It does not prove that discovery ever happened. `..._encode()` likewise only
serializes schema-valid metadata. Callers must not turn either operation into
an implicit certificate for an untrusted file.

`..._validate_untrusted()` retains the strict reference path: it repeats all
fresh discovery and compares the complete canonical result. It needs no trusted
digest and rejects fabricated metadata, even if its local integrity hash was
recomputed correctly. It intentionally does not provide the speed of replay.

All outputs remain untouched on failure. Build outputs cannot overlap each
other or the image. Replay output cannot overlap the image or record. The
independent digest cannot overlap the record, image or output, including
Dreamcast physical aliases. This prevents accidentally supplying the record's
own trailing checksum as the independent pin.

## Canonical wire record

Every integer is little endian. Fields are enumerated explicitly; no C struct
bytes, compiler padding, native pointers or live capabilities are serialized.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 8 | `KUINVR01` magic |
| 8 | 4 | Wire version 1 |
| 12 | 4 | Record length 1,024 |
| 16 | 4 | Detector epoch 1 |
| 20 | 4 | Complete-proof feature value 1 |
| 24 | 4 | Executable byte count |
| 28 | 4 | Exact executable load address |
| 32 | 32 | Executable SHA256 |
| 64 | 104 | Driver metadata, including its identical image identity |
| 168 | 440 | Initializer and buffer-layout metadata |
| 608 | 72 | Allocator metadata |
| 680 | 20 | Initializer-call metadata |
| 700 | 48 | Terminal-exit metadata |
| 748 | 244 | Required zero reserved bytes |
| 992 | 32 | SHA256 of bytes 0 through 991; this is `record_digest` |

The immutable layouts admitted by this version are explicit48 with 12 source
inputs and uniform48 with one source input. The runtime binder independently
normalizes either to the exact explicit 48-slot view. Exit families 1, 2 and 3
have three terminal entries. Reviewed pair-only family 4 has exactly two, with
the unused third entry and executable thunk zero. The full builder must still
prove the corresponding complete family in the image.

Increment `KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH` whenever detector, witness,
uniqueness or patch-admission semantics change after release. A compatible
struct is insufficient to make a previous proof current. The initial epoch
includes the reviewed uniform-layout and exit-family expansion. Struct-size
assertions also require an explicit codec/schema review when metadata grows.

## Developer host tool

Build from the repository root:

```sh
mkdir -p build/native-resolution-cache
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Iinclude \
  tools/vmu_native_resolution_cache_tool.c src/core/vmu_native_resolution_cache.c \
  src/core/vmu_native_discovery.c src/core/vmu_native_layout.c \
  src/core/vmu_native_heap.c src/core/vmu_native_exit.c src/core/vmu_native_patch.c \
  src/core/hash.c src/core/data.c -o build/native-resolution-cache/cache-tool
```

The tool reads only explicitly named files. `build` creates a new output and
refuses to overwrite an existing path. The game executable stays read only.
Successful commands print JSON containing the image identity, record digest and
diagnostic target addresses:

```sh
build/native-resolution-cache/cache-tool build /path/to/1ST_READ.BIN /path/to/game.nvr
build/native-resolution-cache/cache-tool inspect /path/to/game.nvr
build/native-resolution-cache/cache-tool verify /path/to/1ST_READ.BIN /path/to/game.nvr
build/native-resolution-cache/cache-tool replay /path/to/1ST_READ.BIN /path/to/game.nvr TRUSTED_DIGEST_HEX
```

`verify` repeats full discovery. `inspect` only checks local record integrity.
An optional final load-address hex argument is accepted by build, verify and
replay; the default is `8c010000`. A valid pin must come from a trusted complete
build or configuration decision, not merely from inspection of an untrusted
cache. Filesystem persistence, interruption-safe publication, launcher policy
and module-seed integration are separate caller responsibilities.

## Validation

The C suite checks canonical field order, every record-byte corruption, every
byte of a synthetic executable and trusted digest, re-sealed schema mutations,
stale load/size/epoch, malformed pointers, physical aliases, output preservation
and the explicit trust boundary. With real startup inputs it also compares full
build, fast replay and strict rebuild output, attacks every component with
recomputed checksums, mutates reviewed instructions/literals and duplicates a
genuine timer candidate to retain ambiguity refusal. Additional supported image
paths after the first Power Stone 2 path exercise the generic corpus path.

```sh
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
  -fno-pie -no-pie -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
  tests/test_vmu_native_resolution_cache.c src/core/vmu_native_resolution_cache.c \
  src/core/vmu_native_discovery.c src/core/vmu_native_layout.c \
  src/core/vmu_native_heap.c src/core/vmu_native_exit.c src/core/vmu_native_patch.c \
  src/core/hash.c src/core/data.c -o build/native-resolution-cache/test-cache
ASAN_OPTIONS=detect_leaks=0 build/native-resolution-cache/test-cache \
  /path/to/PS2/1ST_READ.BIN /path/to/Skies/1ST_READ.BIN /path/to/SA2/1ST_READ.BIN
KUI_NATIVE_STARTUP=/path/to/PS2/1ST_READ.BIN python3 tests/test_vmu_native_resolution_cache_tool.py
```

The host CLI tests independently verify SHA256 using Python and cover build,
inspect, strict verification, trusted replay, malformed inputs, no overwrite,
source preservation, stale images and re-sealed address changes.

Static cache coverage does not establish runtime readiness. In particular, the
reviewed RECV startup can produce and replay a pair-only metadata record while
its independent live geometry check still rejects RX storage overlapping the
loaded executable. That refusal remains required after a cache hit.

`tools/profile_vmu_native_resolution_cache.c` can be built with the same core
sources to compare a complete build with pinned replay on a supplied executable.
It first establishes a trusted record in the same process, checks replay output
against the full result, and includes a fresh image SHA256 on every replay. It
opens no external cache and writes no files. Host elapsed results identify CPU
work saved, not Dreamcast startup latency.

One 20-repeat host run, including a fresh executable SHA256 on every replay:

| Startup input | Full build, ms | Pinned replay, ms | Ratio |
| --- | ---: | ---: | ---: |
| Power Stone 2 | 34.555 | 10.905 | 3.17 |
| Skies of Arcadia | 58.159 | 19.028 | 3.06 |
| Sonic Adventure 2 | 17.469 | 5.304 | 3.29 |
| RECV, static metadata only | 39.862 | 14.483 | 2.75 |
