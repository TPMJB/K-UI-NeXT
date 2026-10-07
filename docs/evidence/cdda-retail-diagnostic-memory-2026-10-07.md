# Replacement test 14 read diagnostic audit — 2026-10-07

The original observer stopped during Toy Commander's intro, before any accepted
CDDA PLAY request. Its aggregate image-service error did not expose the
underlying image, transport or cleanup result. The
[replacement](../cdda-observe-fix-test.md) adds terminal read details and omits
live sound-register sampling. Its retail behavior has not yet been tested on
console; omitting the sampler tests a hypothesis rather than establishing the
cause of the original failure.

## Separate native build

Provisional command with the pinned SH compiler:

```sh
make -f Makefile.retail_observe BUILD=build/retail-observe-fix BUILD_ID=000000000000 OBSERVE_AUDIO=0
```

The final package rebuilds with its published source commit's twelve-character
identity and reruns the actual three-ELF audit. Its `build.json` records the
final checksums, exact source tree, compiler, layout, stack and instruction
results. Ordinary native targets and the original test 14 outputs are not
overwritten.

| Provisional linked image | Payload bytes | Memory end |
|---|---:|---|
| Entry | 75,944 | `0x8c0228a8` |
| Temporary high stage | 67,752 | `0x8ce3da28` |
| Native SCI resident | 11,968 | `0x8c007800` |

The resident's code/data/BSS finish exactly at the unchanged `0x8c007800`
boundary. The guard/private-stack reservation remains
`0x8c007800..0x8c007d00`; no limit was widened. The conservative sum of all
46 emitted LTO C frames plus the existing 256-byte assembly allowance is
1,224 bytes against 1,232 usable bytes. This leaves eight bytes under that
conservative bound; it is not a console high-water measurement.

The provisional linked instruction audit accepts 16 entry, 7,544 stage and
4,939 resident instructions. Only the named high-stage startup FPSCR setup is
allowed; no resident or relay FPU instructions are allowed. No undefined symbols
remain. All four legacy stage slots contain exact copies of the admitted SCI
resident. Allocated-section coverage, embedded stage/relay bytes, relocation
header, guard symbols and the blank 4,096-byte card manifest pass their checks.

The replacement retains the existing native SCI/standard-reader and original
complete Toy descriptor admission. It adds no sound, timer or IRQ ownership,
audio ring, hidden retry or changed image-read acceptance rules.

## Terminal trace and host checks

The stopped path captures 21 words at image-cache offsets 64–147. The formatter
uses offsets 0–45. All four current guard words are inspected before rendering;
the first raw word and a four-bit mismatch mask are shown alongside the
pre-existing latched fault. Five legend/value pairs plus stop/reason occupy
12 lines, ending at native row 372, below the renderer's wrap threshold.

The trace preserves the most recent read callback's logical LBA/count,
request geometry, credited byte prefix, step budget, destination, image result,
storage result before cleanup, stop result, final storage result and last
attempted physical card LBA. Image/transport trace sentinels initialize before
the first read and reset on each callback. Acquisition failures leave image
and cleanup unattempted; image-mode refusals retain a successful storage result;
cleanup-only failures preserve a successful image read separately.

The focused test includes the actual native observer include with bounded host
MMIO/display facades. Six acquisition/image/read/stop/guard scenarios verify
every printed word, all four guard bits and snapshot survival even when the
display facade destroys the live source fields. It also exercises 258 live
CPU/TMU calls and accepted PLAY parameters, rejecting any AICA/G2 read through
its MMIO allowlist. Optimized and ASan/UBSan runs pass.

The existing observation core passes 131 sanitized checks, and the GD service
with the observation layout passes 668,180. The latter retains its tests for
partial progress, cancellation versus I/O error and request acceptance. Leak
checking is disabled in this execution environment because LeakSanitizer cannot
inspect its process/task state; AddressSanitizer and UndefinedBehaviorSanitizer
remain enabled.

The reusable focused target is `make test-retail-observe`. On this execution
environment it was run with `ASAN_OPTIONS=detect_leaks=0`.

## Preservation

Independent ordinary-native SCI preprocessing remains byte-exact to the source
before these guarded changes: 41,580 bytes, SHA-256
`de194aa4537c2c990abcf00bd4f521480b06c0c077fc8569e096f7cdcde7629a`.
This is a preprocessing comparison, not a fresh ordinary binary rebuild.

The original `324c330bdb6c` test 14 binary remains unchanged at SHA-256
`110140acc2f8b77b12260e0d1eaa3f911902819bbd3ff49889db8f222de69677`.
The original combined ZIP and corrected test 13 ZIP also retain their previously
recorded checksums. The replacement is distributed under a new filename and
contains no normal runtime, game payloads, descriptor or automatic app overwrite.
