# CHD candidate build and compatibility evidence

CHD decoding and cache preparation run in the KOS shell. A prepared GDI or CUE
is launched through the existing game module. No CHD decoder, allocation or cache
is installed in the detached game resident, and no additional game RAM is
claimed by this work.

This document records the implementation checkpoint reviewed before final
packaging. The package's build record and checksums identify its final source
commit; native audits must be repeated from that clean commit. These results
are host and compiler evidence, not a claim of console compatibility.

## Pinned target build

The recovered toolchain is SH4 GCC 15.2.0, binutils 2.45.1 and newlib
4.6.0.20260123, with KallistiOS
`fcfa7d869471591ca1c777543261a7bfea7cb726`. The compiler and KOS were copied
into an independent dependency directory; the VMU worktree was not modified.

The ordinary native stage and all four resident binaries matched the published
main baseline byte for byte when compiled with the same visible build ID.
The comparison covered `stage.bin`, `resident-scif.bin`, `resident-sci.bin`,
`resident-ide.bin` and `resident-scia.bin`. These are actual linked binary
comparisons, including the accepted native disc and drive-state behavior.

| Module | Linked memory end | Stack audit |
| --- | --- | --- |
| Ordinary SCIF | `0x8c007610` | 1,144 / 1,232 bytes |
| Ordinary SCI | `0x8c0077f4` | 1,184 / 1,232 bytes |
| Ordinary IDE | `0x8c007500` | 1,148 / 1,232 bytes |
| Ordinary SCI background | `0x8c007ba0` | 220 bytes plus 64-byte margin / 304 bytes |
| Separate experimental CSO SCI | `0x8c007708` | 1,216 / 1,232 bytes |

The ordinary instruction audit checked 26,767 instructions. The separate CSO
audit checked 11,696. Both admitted one bootstrap FPSCR setup and rejected
floating-point use in the relay/resident. The CSO resident retained 248 bytes
inside its unchanged code/data limit and 16 bytes inside its conservative
service-stack budget. These narrow CSO margins do not provide space for a CHD
decoder.

The reviewed checkpoint shell ELF flattens to a 2,330,560-byte payload and a
6,278,128-byte memory image including BSS. The existing runtime envelope admits up to 4 MiB
of payload and 8 MiB of memory, leaving 1,863,744 and 2,110,480 bytes
respectively. Final packaging must recompute these values from its actual ELF.

## Shell stack budget

Target compilation used the actual `Makefile.dc` commands with
`-fstack-usage -fcallgraph-info=su`. Source-qualified call graphs retain local
function identities. The review includes the shell worker, CHD adapter/cache,
vendor codecs, FatFs and the generic image parser used after cache preparation.
The worker's KOS-owned stack is now 96 KiB; its game-time counterpart and
detached resident reservation are unchanged.

| Item | Measured target C stack |
| --- | ---: |
| Largest individual emitted frame, generic named-image parser | 22,204 bytes |
| Largest CHD vendor frame, legacy map validation | 18,504 bytes |
| Shell worker's retained frame | 12,636 bytes |
| CHD open chain | 19,028 bytes |
| CHD sector decode chain | 5,884 bytes |
| Preparation through subsequent generic GDI inspection | 45,644 bytes |

The largest conservative combination is 45,644 bytes plus the retained
12,636-byte worker frame. Adding a separate 8,192-byte allowance for external
KOS/newlib, logging, cancellation and storage callback work gives **66,472 /
98,304 bytes**, leaving **31,832 bytes**. This deliberately adds caller frames
even where a tail call could release them.

The graph review includes generic CUE, CDI and ISO parser branches as well as
GDI. Following the CD-companion change, a fresh target compilation retained
the same worst preparation total: nested GDI parsing remains deeper than CUE.

This is a source/compiler budget check, not a complete linked-library stack
proof of KOS or newlib. The external allowance is explicit rather than treating
missing reports as zero. Parent CHDs are refused by the wrapper, so parent-only
recursive library paths are excluded. Self-hunk references use a bounded
iterative walk. Zstandard's short-header retry adds at most one retry frame.
The measured functions have static stack reports.

The target cache workspace is 49,180 heap bytes. It contains the transfer
buffer, descriptor, hash state and cache report, and does not occupy the worker
stack. Decoder maps, hunk buffers and codec workspaces are also heap-owned and
released before game handoff. Heap measurements of specific codec fixtures
must not be presented as a universal maximum for every admitted CHD geometry.

## Preserving the VMU experiment

The existing raw/native interface remains `KUIRUN1` envelope version 1,
`KUIRBT01` relocation header version 1 and a 4,096-byte `KUIRTI03` map version
3. The raw manifest codec and runtime header/file readers are byte-identical
to the accepted VMU branch. The runtime validates each module's own checksum
and typed layout; it does not require the menu and module build IDs to match.

A direct ASan/UBSan probe compiled the actual current frontend admission code
and admitted a real linked mode-4 VMU module carrying the older build identity.
The module used a deterministic public preparation profile. The same probe
refused CE/CSO misrouting, a nonblank map and nonzero reserved header flags.
This checks interface compatibility without incorporating the owner's card
identity into a test or changing the installed personalized module.

The default CHD update therefore installs only `KUI/runtime.kui` plus guides
and notices. It preserves `KUI/tools.kui`, all `KUI/apps/games` modules and VMU
image/journal/identity files. A differently identified runtime alone does not
constitute an ABI mismatch.

The personalized VMU module admits only its exact Toy Commander owner and
supported original raw SCI/Standard route. It will refuse the original CHD
console probe and other titles. An optional ordinary `retail-boot.kui` must
remain outside the automatic `KUI` overlay. To run those tests, preserve the
installed personalized module in an external backup, temporarily install the
optional ordinary module, then restore that exact backup for the VMU proof.
There is no automatic fallback from a refused VMU owner.

## Host validation checkpoint

An isolated checkout of the preceding compressed-reader checkpoint passed
`ASAN_OPTIONS=detect_leaks=0 make -j6 test`: 170 C test invocations across 144
standalone test executables and 473 Python tests. Seven optional oracle tests
initially skipped because independent tools were unavailable. Focused reruns after restoring MAME
chdman, a separately built unmodified upstream libchdr and system liblzo2
passed all six CHD export-oracle tests, all 27 computer-importer tests and both
capture-export oracle tests.

The final CHD candidate's complete host suite, target runtime, package checks
and generated console fixtures still require their own final clean-source
validation. Console testing remains necessary for decoder behavior, cache
creation and game launch on the user's actual media.
