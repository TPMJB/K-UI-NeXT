# Checked disc-map memory, source ownership and native-vector audit

Independently audited 2026-10-07 UTC for new profiles 11/12. Profile 11 opens a
complete generated six-track GDI; profile 12 admits only the supplied original
track 14 backing. Both console runs and native stack watermarks remain pending.
The [test checklist](../cdda-disc-test.md) defines their distinct gates. The
[passed 09/10 console results](cdda-batch-hardware-2026-10-07.md) remain separate
immutable checkpoints; neither new profile establishes retail-game execution.

## Build provenance and preservation

Measurements use final provisional `CDDA_BUILD_ID=000000000000` builds in
`build/cdda-disc` and `build/cdda-disc-toy`, including pure-dispatch busy guarding,
the selected-map stack reduction and the extended display helper guard. Published
source IDs and final hashes belong to the package's `build.json`; provisional
measurements are not final artifact identities.

Toolchain remains pinned `sh-elf-gcc (GCC) 15.2.0` / binutils 2.45.1, recovered
and verified as documented in the [service audit](cdda-service-memory-2026-10-07.md).
Compilation is freestanding, uses fixed FP registers, integer division,
`-nostdlib` and section GC. No KOS kernel, target libc, allocator or retired
kernel filesystem state is linked. The new detached `memchr` implementation is
an integer bounded scan. Private FatFs remains read-only exFAT; pinned `ff.c`
SHA-256 is `3c30280ffe2d0fb19a5241d9ed9f20a2c3911c2861652ebf6ad57f74fb369250`.

Root rebuilt both accepted images with their original fixed ID `dcdd845d7628`
after the shared conditional additions and verified byte-for-byte preservation:

| Accepted runtime | Bytes | Verified SHA-256 |
| --- | ---: | --- |
| Profile 09 | 3,085,184 | `69766b77179244bba19e9876587cef713aba4a53be6f2f66776cd28a601b5234` |
| Profile 10 | 3,082,720 | `9a045bb9171ef288d05f617f97b758ed83605d8455c09662fc1ae8776cd89ed0` |

Ordinary SCI/1.8.5 reader bodies, released reservations and bootstrap remain
unchanged. New named stat/data functions and source binding are restricted to
profiles 11/12. Earlier ordinary fixed-ID reader identity evidence remains in
the [batch memory audit](cdda-batch-memory-2026-10-07.md); it is not a fresh
ordinary-reader rebuild claim. No broad profiles 00–10 host suite was repeated.

## Linked allocation

Both images reserve **3,211,264 bytes** (`0x310000`) from `0x8c010000` through
`0x8c320000`, with entry `0x8c010000`. Initialized payloads include the large gap
before the separately linked client: **3,086,400 bytes** for 11 and
**3,083,936 bytes** for 12. Package sizes including the 64-byte header are
3,086,464 / 3,084,000 respectively.

| Allocation, exclusive end | Profile 11 initialized / memory bytes | Profile 12 initialized / memory bytes |
| --- | --- | --- |
| Engine RX | `0x8c010000..0x8c020770`, 67,440 / 67,440 | `0x8c010000..0x8c02060c`, 67,084 / 67,084 |
| Engine state RW | `0x8c020780..0x8c032400`, 20 / 72,832 | `0x8c020620..0x8c02e000`, 20 / 55,776 |
| Engine stack RW | `0x8c200000..0x8c210000`, 0 / 65,536 | Same |
| Worker stack RW | `0x8c220000..0x8c230000`, 0 / 65,536 | Same |
| Client RX | `0x8c300000..0x8c301840`, 6,208 / 6,208 | `0x8c300000..0x8c300ea0`, 3,744 / 3,744 |
| Client stack RW | `0x8c310000..0x8c320000`, 0 / 65,536 | Same |

All six PT_LOAD segments are disjoint and cover allocated sections. Each client
object is retained only in its high client section, with zero client BSS. Three
NOLOAD stacks total 196,608 bytes. Each payload has exactly one SCI marker, at
offset `0x10780` / `0x10620`; undefined symbols: zero. Sections for 11/12 are
entry 140/140, text 48,512/48,128, rodata 18,768/18,796, data 20/20 and
BSS 72,800/55,744. Metadata maps, descriptor buffers and PCM/data state are
private engine BSS, separate from all suspended stacks.

The aligned physical data buffer is 2,352 bytes at `0x8c030a00` / `0x8c02c600`.
Mode 1 raw sectors are read in full and expose only bytes 16..2063 as the
2,048-byte logical payload; sync/mode checks precede publication. The unchanged
bootstrap admits both under its 4 MiB payload and 8 MiB memory limits, subject
to its existing staging/allocation/copy checks. Exact new-console admission is
still pending even though earlier large-envelope profiles passed.

## Static stack and instruction evidence

Fresh compilations with identical target flags plus GCC call graphs bind actual
storage, PCM, map, descriptor and client callbacks. Stack-switch edges are
separate roots; suspended caller saves stay on their own stacks. Bounds include
larger callable branches where indirect sites can be narrowed further, and
storage scan paths even when an already-mounted volume would avoid them.

| Profile / stack | Path including destination anchor | With 256-byte helper allowance | Usable after guard |
| --- | ---: | ---: | ---: |
| 11 engine | 23,944 | 24,200 | 65,472 |
| 11 client | 35,552 | 35,808 | 65,472 |
| 11 worker | 6,460 | 6,716 | 65,472 |
| 12 engine | 34,628 | 34,884 | 65,472 |
| 12 client | 4,328 | 4,584 | 65,472 |
| 12 worker | 6,460 | 6,716 | 65,472 |

All emitted C stack reports are static. Profile 11's client frame is 33,956;
profile 12's is 2,736. Main uses 136/128, worker 600 and dispatcher 60 bytes.
The selected-map path includes selected_audio 17,280, GDI layout 17,076,
parse 104 and filename 40, plus main 128. The complete-map engine bound includes
game_image_open 17,140, stat callback 268, private stat FIL frame 592 and the
conservative partition-scan chain. Large metadata builder frames are sequential
or on these explicit paths; summing every compiled frame is not a stack bound.

Actual linked libgcc helper maximum is 28 bytes: `__umoddi3` saves 12 plus 16
locals and calls a zero-stack quotient leaf. Division's maximum is 20; shifts
and copy helpers use zero. No `__udivmoddi4` is linked. The 256-byte allowance
therefore remains conservative. Host stack stubs are not console watermarks.

Client entries are `0x8c300c14` / `0x8c300604`; vector wrappers are
`0x8c01b890` / `0x8c01b704`; dispatchers are `0x8c0118b4` / `0x8c011834`.
The unchanged 42-byte wrapper saves 32 bytes, preserves r8..r14/PR, passes raw
r4..r7 and retains r0. Nested BUSY refusal adds 92 bytes (wrapper32 + dispatcher60)
and precedes SP/time/map/I/O; it never resets the worker stack. Both client and
forced worker calls use the actual `0x8c0000bc` slot. Generic bridges retain
their 32-byte caller save and 16-byte destination anchor, with three disjoint
guarded stacks. No IRQ-context, broken-SP or FPU preservation is established.

Literal-aware scans examined 24,591 / 23,373 decoded nonliteral instructions.
Only startup `lds r0,fpscr` at `0x8c01000c` configures the FPU; no executable
FPU arithmetic was found. PC-relative literal pools are excluded. The native
wrappers do not write SR/VBR/GBR or change interrupt configuration; ordinary
integer T/Q/M remain caller-clobbered. This is a decoded-code audit, not a claim
that every instruction executes.

## Disc/source contract and independent host checks

Map construction checks actual file sizes, non-PCM offsets, full-sector extents,
FAD arithmetic, ordering and overlap before key-on. A complete generated map
derives both TOC areas. The selected original-14 map preserves FAD
`[374351,377422)`, 3,071 sectors and 1,805,748 frames; next track starts at
377572, leaving 150 unbacked sectors. Missing tracks/data and complete TOC are
refused rather than inferred. Ranges cannot cross backed tracks or fill gaps.

PLAY binds an immutable checked PCM source/path. The existing command completion
stops/closes old audio before priming the new file; control epoch generation is
never reset to accommodate unequal lengths. Cache/source replacement therefore
cannot publish samples from the preceding track. Pause/resume uses actual played
position and retains the original repeat origin. Audio/data keep separate private
FILs under one serialized SCI owner; metadata stat uses an independent temporary
read-only FIL. Pure REQUEST/CHECK/DRIVE and metadata queries perform no physical
I/O. Whole source/output spans and exact pending identities are checked before
mapping/I/O and again after the complete EXEC lease. Each data EXEC reads at most
one physical sector; only full-lease success advances its confirmed prefix.

New `tools/test_cdda_disc_integration.py --profile 11/12 --sanitize` runs passed
all **25** strict ASan/UBSan scenarios: normal paths, PCM/data failures,
corruption, slow-read admission stall, 200 ms read/late-leave failure, later
resume-prime failure, failed source switch, frozen client clock, restore retry,
malformed descriptor, missing/truncated backing, overlap and raw Mode 1 refusal.
Metadata failures stop before any key-on, vector installation or client handoff.
The strengthened selected overlap case retains the correct file size and moves
the next descriptor track into the backed extent. No failed chunk gains progress.
Data faults after one committed chunk preserve exactly 2 KiB; canaries and suffix
are checked while the client buffer is live. Late copied-but-unconfirmed bytes
remain discardable. Prior chunk/request and forged-destination callbacks refuse
without mapping, clock, physical I/O or stack-switch effects.

The independent model uses per-track stereo square samples, track-tagged data,
64-bit time arithmetic, exact physical read ledgers, distinct source epochs and
whole guest-span snapshots. It checks every played sample, excludes 512/1,024-byte
audio prefixes, tests unequal 88,200/132,300-frame EOFs, both 408-byte TOCs and
all 64 cooked plus 32 raw data sectors. Normal 11 is 8/0 at 18.332 model seconds:
five key-ons, two EOFs, three source switches, 196,608 bytes/96 chunks and 2,428
vector calls. Normal 12 is 8/0 at 42.078 seconds: two key-ons, exact selected EOF,
no data/TOC and 5,124 vector calls. A numeric timer wrap occurs in both normal
runs; paused time is serviced and actual cursor remains frozen.

Optional profile 12 with the actual supplied raw file and original GDI also
passed ASan/UBSan, comparing every played sample to that stream. Raw size/hash
are 7,222,992 / `ae3d955fc817f433b4c5273581398215e3be5cf2cfd1dee2ec62aaaa677cf338`;
descriptor size/hash are 451 /
`96e3a54b9aa528c7172121ba3c6cfabf391aacb5a84f292e89843ff2d8853803`.
Private audio remains outside the source/package; generated host references
otherwise use synthetic samples. Physical timing, cache aliases, audible quality,
native guards and the two new console runs remain separate hardware gates.
