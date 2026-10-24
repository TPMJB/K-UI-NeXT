# Native VMU compatibility: Evolution 2 preparation

Evolution now boots and saves/loads on the console with build `897ee7223b35`,
as reported by the owner. This package retains that startup correction and
adds preparation support for the supplied Evolution 2 revision. Evolution 2
still needs its first console launch/save/reboot/load result.

## Install

Extract this package into a fresh directory, then run:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2
```

This installs the personalized module directly and preserves the prepared card
image and journal. No backup/restore script is required. Keep the usual launch
settings. First try the opening movie and menu, then save, power off, and load.

The virtual card currently occupies **A1**. A game's “slot 1” label is accurate.
Changing it to A2 also needs physical A1 storage/LCD passthrough and matching
transport filters; it is separate from this compatibility change. The stored
card image will not need conversion.

Use `python3 personalize.py --list-games` to see the six packaged address
records. Selecting a record speeds discovery only for its exact program hash.
Another revision or game falls back to discovery and must pass all admission
checks. A record does not establish console compatibility.

## What changed

The supplied Evolution 2 executable ends with a 32 KiB copy of a boot image.
Its timer handlers are linked for the boot address, and its firmware calls
are not live game exit paths. The original game's heap initializer reclaims
that suffix. Previously discovery counted the copied timer handlers, while
memory protection treated the whole file as permanent executable code.

Native preparation now distinguishes the verified source file from the live
program only when the appended boot image and original allocator/caller prove
that reclamation. It preserves full-file loader verification. Every native
patch, exit and lifetime check still applies to the live program. Timer
recognition additionally checks the actual interrupt registration rather than
counting byte-pattern copies.

Evolution 2 also includes the SDK's zero-initialized Maple DMA buffers inside
the loaded file. A second layout version admits those exact initializer-derived
buffers after checking their entire original contents and separation from
code, pointer tables and other owned objects. Partial overlaps or nonzero
buffer contents refuse. The entire live program and SDK buffers stay protected
from allocator releases. Existing outside-image layouts keep their original
rules. These changes use no game-specific addresses.

The discovery cache has a new detector epoch. All six records are regenerated
from their original external inputs and freshly verified; old records miss
and require discovery. Evolution 2's record identifies the live program,
while the evidence also records the complete original file hash and length.

## Checks and limits

The package includes the exact source checkpoint and compiled-code checks.
Original-code models exercise the allocator, initializer handoff, displaced
instructions and repeated SDK submit/completion callbacks with the linked VMU
worker. Mutation tests cover malformed or ambiguous mappings and suffixes.
These models do not execute the whole game, FMV decoder or real hardware bus.

NORMAL retains the cached runtime stack and controller fast path. It adds no
per-poll scan, game trace patches, live counters, automatic diagnostic stop or
exception-vector redirect. The previous legal SH-4 gateway and ordered cache
handoff remain in place.

For another game, install without `--game` to request fresh discovery. If it
refuses preparation, record the whole screen and game/revision. Do not treat
preparation refusal as an in-game crash.

Detailed source identities and software-check results are in
`docs/evidence/native-evolution2-extent-2026-10-23.json`. Earlier SDK/cache and
SH-4 failure analyses remain as historical evidence in the same directory.
No original game executable or save bytes are included.
