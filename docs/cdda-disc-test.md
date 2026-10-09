# Checked GDI disc and selected-track tests

Run **profile 11 disc map**, then **profile 12 Toy track 14**, once each and
photograph both final screens. Profile 11 takes roughly 15–25 seconds plus
initialization; profile 12 takes about 42 seconds plus initialization.
Both profiles are automatic and independent. Profiles 00–10 already passed
their requested numerical checks; no earlier test or 15-minute run needs
repeating. The [09/10 console results](evidence/cdda-batch-hardware-2026-10-07.md)
remain separate checkpoints. Both profiles now
[passed their numerical console gates](evidence/cdda-disc-hardware-2026-10-07.md)
at build `a0c36063a9ed`; neither needs a routine repeat. The installation
sequence below is retained for reproduction. The original archive's manifest
correctly records its pre-test status and remains unchanged.

Profile 11 opens a complete generated six-track GDI, derives both TOC areas,
plays distinct audio files and checks cooked and raw data extraction during
playback. Profile 12 uses the actual uploaded Toy Commander descriptor and
the existing owned track 14 backing to check its real FAD range, pause/resume
and file-backed EOF. Both use the temporarily owned GD BIOS vector at
`0x8c0000bc` from a separate controlled client. The released 1.8.5 readers
remain unchanged.

## Install and run

Use `K-UI-CDDA-Disc-Tests.zip` and its `build.json`. The archive contains only
these two new runtimes. Its default `KUI/runtime.kui` is identical to
`runtimes/11-disc-map.kui`; the second runtime is
`runtimes/12-toy-track14.kui`.

The archive supplies the generated files under `KUI/tests/cdda/disc/` and
the factual `KUI/tests/cdda/TOY_COMMANDER.gdi` descriptor. It includes no Toy
audio or executable. Keep the existing `/KUI/tests/cdda/track14.raw` on the
SCI card: it must be **7,222,992 bytes**. No new disc dump or upload is needed.
The existing `stereo.raw` and `stress.bin` may stay on the card, but these
profiles use their new disc files instead.

1. Power off the Dreamcast and attach its existing SCI card to the computer.
2. Preserve `/KUI/runtime-before-cdda.kui`, the original working-runtime
   backup. Do not overwrite it with a test. If the ordinary runtime is
   restored and that backup name is absent, copy the working
   `/KUI/runtime.kui` to the backup name before installing.
3. Copy the archive's `KUI` folder onto the card. Keep the existing owned
   `/KUI/tests/cdda/track14.raw`; the archive does not replace it.
4. Safely eject, install the card in SCI and use the usual SCI boot path.
5. Check profile 11 and the build ID against the manifest. Let the automatic
   sequence finish and photograph the whole final screen, including the
   profile/build labels and every result row.
6. If profile 11 passes, power off and reconnect the SCI card. Copy
   `runtimes/12-toy-track14.kui` over `/KUI/runtime.kui`, preserving the
   original backup and all fixtures.
7. Safely eject, reinstall and boot through the usual SCI path. Check profile
   12 and its build ID, let it finish and photograph its whole final screen.

If either profile reports an unexpected failure, photograph its screen,
power off and return that result before continuing. A wrong profile/build
label, hang beyond the documented limit, missing channel or unplanned audio
break should also be reported. Planned EOF, pause and track transitions can
produce silence. Casual listening is useful; the numerical result does not
establish listening quality on a proper stereo system.

No controller input, new CD, card format or Wi-Fi firmware is needed. The
harness reads the card without writing a log. Keep the original game dump
and previous passing-test archives.

## Profile 11 generated disc

`fixture.gdi` names six generated files. FAD is descriptor LBA plus 150;
all range upper endpoints below are exclusive. Each extent ends at the
actual file-backed sector count, rather than filling a gap to the next
descriptor track.

| Track | Type / sector bytes | Backed FAD range | File / leading non-PCM bytes |
| --- | --- | --- | --- |
| 1 | Cooked data / 2,048 | `[150, 166)` | `disc01.bin` / 0 |
| 2 | Audio / 2,352 | `[170, 245)` | `disc02.raw` / 0 |
| 3 | Cooked data / 2,048 | `[45150, 45214)` | `disc03.bin` / 0 |
| 4 | Audio / 2,352 | `[45300, 45450)` | `disc04.raw` / 512 bytes of `0xa4` |
| 5 | Audio / 2,352 | `[45450, 45675)` | `disc05.raw` / 1,024 bytes of `0xa5` |
| 6 | Raw Mode 1 data / 2,352 | `[45825, 45857)` | `disc06.bin` / 0; 2,048-byte payload begins at sector byte 16 |

Track 4 contains two seconds of stereo square tones: **441 Hz left / 294 Hz
right**. Track 5 contains three seconds: **551.25 Hz left / 367.5 Hz right**.
Both use signed amplitude 8,192. Their non-PCM prefixes must never play.
Track 2 supplies another distinct one-second audio backing for complete-map
metadata; this sequence does not play it. Data bytes use a deterministic
track-tagged, offset-dependent pattern. Track 6 has sync, BCD FAD and Mode 1
header bytes with a zero trailer; the fixture makes no EDC/ECC claim.

The derived TOC reports tracks 1–2 and leadout FAD 245 in area 0; tracks 3–6
and leadout FAD 45857 in area 1. GETTOC2 returns one checked 408-byte result
for each area. These are TOCs derived from the complete test backing, with
no claim of exact firmware behavior for every disc layout.

| Stage | Expected behavior |
| --- | --- |
| 1 | Open/stat the complete GDI map, validate ownership and install/read back the temporary BIOS vector |
| 2 | Check metadata and range boundaries, read both TOCs, refuse unsupported or out-of-range requests and complete NOP |
| 3 | Play track 4 once to its actual 88,200-frame EOF |
| 4 | Play track 5 once to its actual 132,300-frame EOF |
| 5 | Loop track 4 through at least two passes, refuse one nested EXEC, PAUSE for one second with regular service calls, then RELEASE and advance |
| 6 | Switch directly to the longer track 5 while audio is active, loop through at least two passes and verify all 64 cooked sectors of track 3 and all 32 raw sectors of track 6 |
| 7 | STOP, check the counters and report them to the engine |
| 8 | Return to the engine, restore/read back the original vector and validate final gates |

The audible order is track 4, track 5, looping track 4, a one-second pause,
resumed track 4, then looping track 5 until STOP. The run has a 60-second
limit. It checks unequal-length active track switching and keeps the audio
command epoch monotonic across those file changes.

## Profile 12 selected Toy backing

This profile admits **only track 14**, using the descriptor's LBA 374201 and
the inspected backing's 3,071 sectors. Its backed FAD range is
`[374351, 377422)`, containing exactly **1,805,748 stereo frames**. The next
descriptor track starts at FAD 377572, leaving a **150-sector unbacked gap**.
That next-track distance is not track 14's file size and is never played as
extra audio.

The client checks the first and final backed sectors, refuses the preceding
sector, the EOF boundary, a span crossing EOF and the next-track start. It
also refuses missing tracks 13/15, data reads from the audio track and both
complete-TOC requests. A selected backing does not establish a complete Toy
disc map or leadout.

| Stage | Expected behavior |
| --- | --- |
| 1 | Open the actual descriptor, stat the retained track 14 backing and install/read back the temporary vector |
| 2 | Check selected metadata and boundaries, refuse missing/unbacked requests and complete NOP |
| 3 | PLAY track 14 once, refuse one nested EXEC and reach one second of actual played audio |
| 4 | PAUSE for one second with regular service calls; verify the played cursor stays frozen, then RELEASE |
| 5 | Verify playback resumes on track 14 at the preserved actual cursor |
| 6 | Continue to the exact 1,805,748-frame file-backed EOF |
| 7 | STOP, check the counters and report them to the engine |
| 8 | Return, restore/read back the original vector and validate final gates |

Expect the retained track's audio, one deliberate one-second pause and then
continuation to EOF. Prefetched frames do not define the paused cursor. The
run has a 90-second limit. The backing used in the earlier test has SHA-256
`ae3d955fc817f433b4c5273581398215e3be5cf2cfd1dee2ec62aaaa677cf338`;
its required size and hash are also recorded in `build.json`. This test does
not boot Toy Commander or include its executable.

## Final screens and report

Each automatic pass requires **eight completed stages**, **zero failures**,
intact stack guards and zero unexpected service/test errors. Photograph all
15 rows. The seven paired result rows must read:

| Final row | Profile 11 | Profile 12 |
| --- | --- | --- |
| Mapped tracks / complete image | `6 / 1` | `1 / 0`, selected backing only |
| Client seconds / vector calls | At most 60 seconds; calls vary with card timing | At most 90 seconds; calls vary with card timing |
| Checked data bytes / chunks | `196608 / 96` | `0 / 0`, this profile has no data reads |
| TOC / map checks | `2 / 16` | `0 / 10` |
| Audio actions / EOF checks | `7 / 2` | `4 / 1` |
| Client / service stack bytes | Both nonzero, at most 65,472 each | Both nonzero, at most 65,472 each |
| ABI checks / vector restored | Checks equal vector calls minus 1; restored `1` | Checks equal vector calls minus 1; restored `1` |

The single refused nested EXEC explains the call/probe difference. The
engine also checks the hidden client ledger. Profile 11 requires 16 accepted
requests and 16 completions, 11 DRIVE checks, 12 expected protocol refusals,
three stale-handle checks, 90 positive-prefix processing checks and three
track switches. Profile 12 requires five accepted requests/completions, six
DRIVE checks, 10 expected refusals and two stale-handle checks. Both require
one nested refusal, eight descriptor refusals, a stopped final audio state,
an empty queue and the restored original vector. The manifest records the
full gate set; timing-dependent call counts have no fixed target.

Return both photos with any audible clicks, channel imbalance, unexpected
silence or corruption. Include the build/profile labels if a photo is hard
to read. A pass establishes this controlled checked-disc path on the console;
retail game handoff and shared AICA ownership remain later work.

## Service and memory contract

REQUEST copies and validates parameters without SD or AICA work. READ 16
accepts at most 16 sectors with the complete source/destination span checked.
Each EXEC admits at most one physical data sector; a raw source sector is
2,352 bytes and returns its 2,048-byte payload. Progress and terminal success
become visible only after the complete service lease succeeds. A failed READ
confirms only its previously committed whole-sector prefix; discard the
unconfirmed remainder even if its memory was touched. Terminal CHECK consumes
the result once. Cancellation is between synchronous operations; it does
not abort a transfer already in progress.

PLAY 20 accepts equal backed audio-track endpoints and repeat 0 or 15.
PAUSE 22, RELEASE 23, STOP 33 and NOP 29 use the controlled forms. PLAY21
remains refused pending a native endpoint experiment. Finite repeats 1–14,
DATATYPE, DMA and interrupt completion remain outside this contract.
These tests are explicitly cooperative and do not establish firmware
conformance, retail integration or shared game RAM/AICA ownership.

The client uses cached P1 buffers with 32-byte canaries. Physical/P2 caller
cache coherence is unproved. Integer probes check r8–r14 and PR; they cannot
recover a broken stack pointer or a nonreturning call and do not establish
shared SR, VBR, GBR, FPU or interrupt state with a game.

| Owner | Reserved range |
| --- | --- |
| Engine code/data | Begins at `0x8c010000`, below its stack |
| Engine private stack | `[0x8c200000, 0x8c210000)` |
| Worker private stack | `[0x8c220000, 0x8c230000)` |
| Controlled client code/data | `[0x8c300000, 0x8c310000)` |
| Controlled client private stack | `[0x8c310000, 0x8c320000)` |

The runtime envelope reserves 3,211,264 bytes through `0x8c320000`. The
packager checks initialized client code in the linked payload and all three
exact writable NOLOAD stack ranges. Timer conversions retain the documented
**12,468,720 Hz TMU reference**; AICA configured pitch stays 0, with no
automatic retuning or resampling.

## Restore and rebuild

After both tests, power off and reconnect the SCI card. Copy the preserved
`/KUI/runtime-before-cdda.kui` over `/KUI/runtime.kui`, safely eject and use
the ordinary SCI boot path. Keep the backup, generated fixture directory and
owned track 14 backing.

The archive contains the exact published-source snapshot, private read-only
FatFs inputs, licenses/notices, linked ELFs/maps, compiler stack reports,
fixture metadata and complete `SHA256SUMS`. `build.json` records the verified
source tree, source/build identities and each runtime/input hash. It marks
both new profiles as untested on hardware.

To reproduce the generated inputs and runtimes from that checkpoint with
the matching SH toolchain and private FatFs source:

```sh
python3 tools/cdda_disc_fixture.py build/cdda/disc-fixture --toy-descriptor PATH/TOY_COMMANDER.gdi
make -f Makefile.cdda cdda-disc-test CDDA_BUILD_ID=PUBLISHED_SHA_FIRST_12
make -f Makefile.cdda cdda-disc-toy-test CDDA_BUILD_ID=PUBLISHED_SHA_FIRST_12
python3 tools/package_cdda_disc.py K-UI-CDDA-Disc-Tests.zip --source-commit PUBLISHED_SHA --published-tree VERIFIED_TREE_SHA
```

Use the exact descriptor metadata recorded in the manifest. The generator
never copies game audio. Packaging requires a clean committed source tree
matching the independently verified published tree, exact ELF/envelope
payloads, distinct compiled clients and one unpatched AUTO boot marker per
runtime. Bootstrap selects SCI for that marker.
