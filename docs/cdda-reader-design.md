# Separate CDDA reader design for K-UI 2.0

Status: researched design, not an implemented or hardware-tested reader.
Prepared 2026-10-07 UTC / 2026-10-06 America/Chicago.

Baseline: [K-UI 1.8.5](release-v1.8.5.md), remote commit
`e4c521ddd8b5afe018dfbb82e7a9c5974d9de0b2`.
The local checkout used for research has the identical source tree
`639a773461b864712feaa3194ba7ddc5cda2c9b5`.
No runtime source, published reader, release asset or main-branch file changes
are part of this design.

## Decision

Use a separate, optional CDDA package selected before game handoff. Preserve
the released 1.8.5 data reader. Detect audio tracks from the parsed image
layout; treat detection as a candidate signal, then apply compatibility
admission. A per-image Auto / On / Off setting handles unusual layouts.

The first memory experiment should stream checked raw PCM directly into an
inactive stereo ring in AICA sound RAM. This avoids a large additional PCM
buffer in SH-4 main RAM. First attempt a lean native SCI-only reader within
the current low reservation. Whether its extra code, state and stack fit is
unproved and must be established by an actual link/stack audit.

If that fit fails, prove the audio/card engine in a controlled homebrew
harness with an explicitly reserved high-memory island. Do not turn that
harness into a generic retail loader by assuming top RAM is unused.
Retail high placement requires a verified memory contract/profile or a
genuinely understood game allocator.

Getting music to play and establishing broad retail compatibility are
different milestones. The latter also requires sound-memory ownership and
periodic servicing, even if the code fits.

## Which images need the other reader?

Not every game uses disc audio for music. Many encode music and movie audio
in data files instead. Even an intact GD image without gameplay CDDA commonly
has a low-density warning/silence audio track. Thus "any audio track" is the
wrong universal test.

The image parser already exposes audio/data control, start/end LBAs, sector
stride, backing offsets, boot-data LBA and CD/GD classification. No audio
payload scan is necessary to identify candidate tracks.

| Parsed image layout | Classification | Mature Auto behavior |
| --- | --- | --- |
| Known GD layout, audio at/after LBA 45000 | Gameplay-audio candidate | CDDA package if its profile/backend is admitted |
| Known GD layout, only low-density audio | Warning/bonus audio only by the ordinary rule | Released reader; allow explicit On for unusual usage |
| CD CUE/CDI, any audio | CDDA candidate; actual use ambiguous | CDDA only for an admitted profile, otherwise explain experimental support |
| Multi-track descriptor with no audio | No audio represented | Released reader |
| ISO or standalone raw BIN/IMG | Data-only image; original-disc audio unknown | Released reader; On cannot recover absent tracks |
| CSO/ZSO imported to ISO | Data-only supplied representation | Released reader; K-UI's ripper retains original audio in its raw capture, but standalone imports cannot supply it |
| CHD imported to GDI/CUE | Classify the extracted track/session layout | Same policy as the corresponding uncompressed layout |

For CD images, do not require audio to begin after the boot-data session:
Audio/Data MIL-CD can put actual music before that session. For GD CUE, use
the parser's GD geometry/density classification, not the extension or the
IP media field alone. Some CD rips retain a GD-ROM IP field.

Presence is not proof of executable usage. Dummy CD tracks, low-density
bonus tracks and unusual playback requests need overrides or exact profiles.
Track length, filename, a short silence sample or an executable byte-pattern
search is not a reliable usage classifier. No title-name heuristic is needed.

Current integration:
- `src/apps/games.c` inspection counts audio tracks and sets
  `high_density_audio` using `start_lba >= image->data_lba`. That is not a
  valid general CDDA classifier for CD images.
- `src/apps/games_retail.c` currently warns on audio LBA >= 45000.
  Replace these separate heuristics with one canonical classifier in the
  experimental launcher.
- Keep selection/launch-time inspection and existing catalogue invalidation.
  Do not open every track, hash audio or reparse descriptors while paging.
- Remember a small classification result/profile reference per selected image.
  Original/2048 variants must retain equivalent audio geometry, but distinct
  image fingerprints must prevent an override applying to a replaced rip.

Proposed selection order:
1. Parse and validate the selected image.
2. Determine supplied audio tracks and requested Auto/On/Off.
3. Check experimental reader availability, transport and compatible profile.
4. Map all required data/audio extents and verify package/reservations.
5. Choose one package and perform handoff.

Initially Auto should use the new reader only for validated profiles. On
requests experimental CDDA but must not bypass missing-file, extent,
memory or sound-ownership admission. Off deliberately selects 1.8.5.
An unavailable CDDA package must not silently pretend music is enabled.

Reserve the larger reader before bootstrap. Loading extra code on the first
PLAY without already protected storage/memory is unsafe. A later module
loaded into legitimately allocated game memory is a separate design below.

## Preserve the stable path

Give CDDA its own target, installed filename, package identity/version and
validation contract. Proposed names are `retail-cdda-sci.bin` and
`KUIRCD01`; final names are implementation choices, not existing files.

Do not reinterpret a reserved 1.8.5 header word or weaken the current exact
legacy/low tuple checks. Leave native SCIF, SCI standard/background, IDE and
CE package admission unchanged. Changes to shared code must compile away
for ordinary targets; compare their detached payloads at a constant build ID.
Preserve the published 1.8.5 bytes/checksums as the operational fallback.

Use the existing GD command contract and SCI source-DMA/error handling as
the foundation. Initially scope new playback to native SCI, uncompressed
raw PCM audio, and an explicit profile. CE, IDE DMA and compressed resident
audio are later work; all existing launch formats retain their stable reader.

The CDDA package should declare segment addresses/lengths, stack bounds,
track/extent capacity, required capabilities, audio format and any high-memory
reservation. Validate arithmetic, P0/P1/P2 aliases, segment overlaps and
cache publication before copying. This is a new schema, not a v3 map silently
extended beyond its encoded fields.

## Memory: avoid a large main-RAM PCM buffer first

The current native reservation starts at `0x8c004000`, with private-stack
top `0x8c007d00`: 15,616 bytes including stack.
The initial full IP image remains `0x8c008000..0x8c010000`.
The audited standard SCI code/data/BSS has 12 bytes remaining; background
SCI ends exactly at its limit. SCIF has 496 bytes and IDE has 768.

Sonic Adventure and Grandia II previously used parameter/stack memory inside
the old reader reservation. Extending back above the IP boundary would
reintroduce that class of collision. A small executable also says nothing
about later BSS, heap, stack, overlays or DMA destinations.

### Experiment A: compact low reader, direct-to-AICA PCM

Use the same bounded low interval initially. Separate candidate-only builds
may simplify diagnostics and restrict the first test's formats/map capacity.
Existing native SCI already excludes CE and other transport implementations;
removing them again provides no savings.

Existing symbol inspection identifies roughly 1,448 bytes standard / 1,480
bytes background in named terminal display/fault/menu code, glyph/offset tables
and display BSS. These are upper-bound removal candidates: a minimal fatal/
return path must remain. Reducing the standard map from 160 to 32 slots saves
1,536 bytes; reducing background 64 to 32 saves only 384. A more useful first
target keeps 64 slots: standard SCI then saves 1,152 bytes from its map, while
background saves none. Together with the 12 existing free bytes, the gross
ceilings are 2,612 bytes standard / 1,480 bytes background, before replacement
code/state/stack. String merging, LTO and alignment change actual results.

Thirty-two slots admit at most 16 tracks with one extent per track, fewer
with fragmentation; 64 slots admit roughly twice that shape. Resident CUE/CDI
parser removal yields no savings: those parsers already run in the launcher.

A reduced map is an explicit pilot limitation, never a silent audio omission.
The extra audio engine, shared arbiter and IRQ bridge may still exceed this
budget. The goal is a measured fitting experiment, not a promise that deleting
diagnostics solves the problem. Do not count the gap above `0x8c007d00` as
established spare firmware memory without separate validation.

Bulk PCM lives in sound RAM. Small SH-4 scratch includes checked SD receive
areas, frame carry, audio cursor, playback state and bounded PIO staging.
An estimated 1–2 KiB of additional scratch/state is a design target, not a
compiled footprint. Reuse existing receive areas only through the one card
arbiter; no concurrent aliasing of buffers.

### Experiment B: controlled high-memory harness

If low fit fails, use a maximum 128 KiB development island, provisionally
`0x8cfe0000..0x8d000000`, for engine/state/private stacks and bounded maps.
Keep a low BIOS/vector entry shim. The harness must explicitly exclude that
island from its linker, heap, stack and DMA allocations.

The current temporary stage stack at `0x8cff0000` lies inside this island.
A new package must relocate or partition its temporary stack before final
segments overwrite it. Prove stage BSS, source blobs, trampoline, temporary
stack and final allocations stay disjoint throughout copying and handoff.

The island is not universally free retail RAM. Excluding it from GD read
destination checks protects only emulated reads: direct CPU stores, GPU DMA
and other game DMA can still overwrite it. Canaries/checksums detect some
damage; they do not reserve memory or establish future ownership.

### Later: verified game allocation

A validated Katana profile could allocate a contiguous region after the SDK
allocator is initialized, then load a helper module from launcher-prepared
read-only extents into that allocation. Keep enough low code for dormant
module loading and GD dispatch, so the helper need not survive in temporary
high-stage memory until first PLAY.

Verify the exact allocator ABI, initialization point, alignment, lifetime,
locking/reentrancy and legal call context. Never invoke a game allocator
from an arbitrary IRQ. SWAT's allocator lookup is useful research, but its
eight-byte signature and fixed function offsets explicitly marked FIXME are
not a universal ABI guarantee.

This can solve ownership for admitted SDK builds, not all binaries. Allocator
failure should disable/refuse the feature according to the explicit policy;
it must not fall back to an unprotected guessed address.

## Audio: stream into the inactive AICA half

Raw CDDA is 44,100 stereo frames/s, two signed 16-bit samples per frame:
176,400 bytes/s, approximately 172.3 KiB/s. Each 2352-byte disc-audio sector
contains 588 stereo sample frames.

A new raw audio cursor walks file extents/container offsets and strips the
96 subchannel bytes of 2448-byte sources. Reuse the proven cursor geometry,
but give audio independent progress/state. Do not round a 2352-byte boundary
to a card block or discard a partial stereo frame.

After a 512-byte SD block passes CRC:
1. Feed only the audio bytes represented by its cursor.
2. Carry 0–3 bytes until a complete stereo sample frame is available.
3. Split signed little-endian PCM into left/right samples, packing pairs for
   short 32-bit writes where alignment permits.
4. Write only the currently inactive AICA half.
5. Preserve remaining bytes when a half fills mid-sector or mid-card-block.

Ring halves are not multiples of 2352 bytes. A short-write/byte-consumption
contract is necessary; blindly writing a whole sector count into a half can
overflow it or drop samples. Track boundaries, gaps, looping and exclusive
end positions must be handled explicitly.

| Total stereo sound-RAM ring | Total playback duration | Half refill window |
| --- | --- | --- |
| 32 KiB | 185.8 ms | 92.9 ms |
| 64 KiB | 371.5 ms | 185.8 ms |

For a controlled first proof, reserve a 64 KiB stereo ring and prefill both
halves before starting channels. This gives each channel 32 KiB, split into
two 16 KiB halves. Measure smaller rings later.

Use bounded uncached G2 PIO with documented FIFO discipline; burst size is
not permission to overfill the FIFO. Preserve the exact interrupt and G2
suspend state. Initially avoid G2 DMA, SQ/QACR changes, a new TMU owner or
replacement ARM sound firmware. This trades CPU/G2 work for smaller main-RAM
buffers and less DMA/IRQ ownership complexity. Its performance is unmeasured.

Sound RAM is not automatically ours. The game owns AICA channels, sample
memory and its ARM driver. Channels 62/63 and top-of-sound-RAM buffers are
conventions, not universal reservations. A currently inactive channel does
not prove future non-use. Start with explicit harness/profile allocations.

Configure both channels before synchronized key-on; verify channel phase.
Query actual channel playback position, with monitor-selector/G2 state
preserved, rather than estimating solely from bytes prefetched. If ownership
changes, stop only still-owned CDDA resources and report a conflict; do not
reset AICA, repeatedly steal active channels or restore stale register state
over a new game owner. AICA DMA can still conflict with PIO bus access, so
validate suspend/wait behavior against the game's actual driver.

## One card engine, two independent jobs

Current background SCI has one cursor/job and may start the next block
before checking/copying the previous one. Audio cannot independently call
the synchronous storage layer while that stream is active.

Introduce one arbiter owning the SCI bus, SD command stream and receive areas:
- Game job: existing GD token, destination, read cursor and completion count.
- Audio job: source cursor, ring-half fill, playback cursor and deadline.
- Switch only at a fully received, checked 512-byte block.
- Tag speculative/ready blocks with job and physical LBA; deliberately retain
  or discard them on switching. Never reinterpret another job's block.
- Continue consecutive LBAs cheaply. Stop/restart CMD18 when switching to a
  different physical run, with bounded CMD12/token handling.
- Prioritize audio when its refill safety margin is low; otherwise advance
  game reads in bounded bursts. Cap both burst duration and work per service.
- Retain CRC validation, retry policy and SCI/DMAC collision detection.
- Report game progress only after bytes reach the game's destination.

Read ahead only as far as ring space permits. Card traffic adds audio plus
game data, padding, retries and stream-switch overhead. Nominal throughput
does not prove deadlines: fragmentation and card command/tail latency matter.
Measure refill latency and game-load cost before tuning priorities.

The PLAY request can finish while playback persists. Its long-lived audio
state must not occupy the one ordinary GD request slot. Keep next read tokens,
audio controls and status independent. STOP/PAUSE must safely cancel audio
prefetch without canceling an unrelated game read.

## Periodic service and preserving the game's interrupts

Audio must refill even when the game issues no GD calls. SCI completions alone
are also insufficient if no transfer has been started.

The native background reader already forwards other interrupts using the
game's original VBR and returns through a rehook trampoline. Preserve that
behavior; DOA2 previously failed when its handler ran under the reader's VBR.

Candidate service opportunities:
1. Detect an already-enabled ASIC/vsync event before forwarding, without
   acknowledging the game's event.
2. Run the original handler under its original VBR.
3. On its return trampoline, service a bounded audio/card step.
4. Also service through GD calls and SCI completions.
5. Keep interception active while audio is active, even if game reads are idle.

The current trampoline saves only R0–R3 and makes no C call. The experimental
bridge needs a proven private-stack switch, complete preservation of registers
its helper clobbers, PR/MAC/SR and exception/return state, nesting and recursion
protection. Use integer-only resident work and audit generated instructions.
Budget token waits for native calls as well; currently sliced token polling is
CE-specific.

Vsync is an opportunity, not a guarantee. A game may disable it or mask
interrupts longer than a half's refill window. Never force its interrupt mask
down. Measure maximum service gaps and worst refill time under loading, FMVs
and transitions. Require a safety margin, not simply average success.

Playback position alone cannot detect that an entire ring elapsed between
observations. Use a verified monotonic measurement source in the harness and
each timing profile; do not quietly take a game-owned timer. A title failing
that timing contract remains unsupported until another scheduling profile is
proved.

## CDDA commands and observable behavior

Verify the command/status ABI against primary implementations before coding:
PLAY by track range, PLAY2 by sector range, STOP, PAUSE, RELEASE/resume, SEEK,
repeat limits, GETSCD/position and drive status. Preserve FAD/LBA conversion,
track/index reporting, end conventions and callback/token timing.

Current PLAY/PLAY2/PAUSE/RELEASE complete silently and GETSCD reports audio
unavailable. CDDA must report actual playing/paused/completed/error state.
GETSCD should reflect samples played, not the card prefetch cursor. Pause
must retain an exact sample position; resume, loop and end handling must not
skip or repeat arbitrary buffered data.

The initial prefill policy is a harness experiment. Determine whether a
retail SDK expects immediate command completion with playback starting later,
or can tolerate a bounded pending request; do not change it merely to hide
slow reads.

On underrun, avoid endless stale audio looping: mute/stop our channels safely,
record the event and use an explicit recovery policy. Do not claim a read was
successful before valid bytes exist. The existing FMV stall/skip issue is a
separate compatibility investigation; CDDA is not a blanket fix for it.

## Maps and image integrity

A CDDA launch must map every required playable audio extent. Current launch
preparation can drop all audio file extents to fit while retaining the tracks
in the TOC. That fallback is allowed by the silent data reader and forbidden
for required CDDA audio.

The existing manifest has 160 standard / 64 background shared track+extent
slots. With one contiguous file per track, 99 tracks need roughly 198 slots.
A larger CDDA reader therefore also needs suitable map capacity, or explicit
pilot admission limits.

For broader support, consider a separate bounded audio/file map with shared
file extent deduplication and 16-bit indices. Do not retain the current packed
8-bit extent index while claiming support for 512 slots. A map with 99 tracks
and 512 extents needs a newly specified wire layout and a measured memory
budget, not only a larger array constant.

Respect safe file names, offsets, sector stride, source lengths and partition
bounds. Original/2048 conversion preserves audio; data-only ISO/CSO/ZSO does
not invent it. CHD imports already restore historical GD audio byte order.
Start with raw PCM; reject unsupported WAV/MP3/compressed sidecars explicitly.

## Implementation order and acceptance gates

1. **Detection and dispatch foundation.** Pure classifier plus Auto/On/Off,
   separate package identity/capabilities and honest unsupported states.
   Test GD warning-only, HD audio, GD CUE, CD audio-before-data, dummy/ambiguous
   CD audio, missing tracks, data-only imports and Original/2048 parity.
   Verify page navigation adds no audio/descriptor I/O.

2. **Memory-fit experiment.** Compile a compact low target with measured
   code/BSS/stack/map budgets and direct-AICA PCM state. Preserve baseline
   payload identity at a fixed build ID. If it cannot fit, use the controlled
   high-island harness; retail high reservation remains a separate gate.

3. **Controlled audio proof.** Use the same detached/bootstrapped resident
   path, not a still-running KOS music thread. Harness explicitly grants main/sound RAM,
   channels, interrupt behavior and clocks. Use synthetic waveform tracks:
   endian/channel order, half/sector/block boundaries, 2448 stripping,
   nonzero offsets, gaps, track changes, pause/resume/loop/seek and EOF.

4. **Concurrent read stress.** Random/game-sized data requests during audio,
   fragmented extents, retries, SD stream switching, DMAC contention and
   prolonged idle-GD periods. Check data CRCs, no active-half writes, no false
   completion and bounded interrupt work.

5. **One admitted retail profile.** Choose from an actually inspected image
   with playable audio. Record executable/version/layout identity, memory
   and sound-resource contract, longest pump gap and worst refill latency.
   Verify title/menu/gameplay, effects, loading/transitions and lengthy play.
   Different rips/revisions are not automatically the same profile.

6. **Expand and only then default Auto.** Record support per title/profile,
   backend and format. Consider G2 DMA, adaptive channels, verified allocator
   loading, CE and additional formats only when native PIO is reliable.

Diagnostics should include underruns, ownership conflicts, ring low-water,
maximum service/refill gap, SD switches/retries, channel phase, game-read time
and memory guard failures. Keep them bounded and do not write storage from
resident/IRQ context. A post-return report can persist them in the shell.

Do not run unrelated full compression/capture suites for a design-only change.
For code stages, run focused classifier/map/arbiter/ABI tests and required
Dreamcast link/stack/instruction checks. Full release checks belong when a
candidate is actually being prepared for release.

## Alternatives and deferred work

- A larger loader selected before boot is sensible; it does not by itself
  reserve memory against a retail game's future allocations.
- Stealing Maple DMA memory or using sound RAM/cache as generic SH-4 code
  storage creates new ownership/cache contracts rather than solving them.
- Replacing the game's ARM firmware would sacrifice its sound driver.
  Adding an ARM helper later requires an independently verified coexistence
  design and still leaves SD/card scheduling on SH-4.
- Optional PC-generated Yamaha ADPCM audio sidecars could reduce storage
  bandwidth in a later design, but are lossy and require explicit metadata,
  sample/LBA mapping and original raw-track retention. They are not required
  to prove accurate raw CDDA first.
- SWAT's mature code is valuable reference and a possible credited reuse
  input after exact license/provenance review. It depends on DreamShell
  allocator, filesystem, exception/timer and global GD state, so importing
  `cdda.c` alone is not a drop-in implementation. K-UI's main source is
  GPL-3.0-only; the MIT Wi-Fi component is a separate project boundary.
  No upstream code was copied into runtime source for this design.

## Research evidence

K-UI sources inspected:
- [Image metadata/parser](../include/kui/game_image.h) and
  [implementation](../src/core/game_image.c).
- [Games inspection/catalogue](../src/apps/games.c) and
  [launch preparation](../src/apps/games_retail.c).
- [Current map format](../include/kui/retail_image.h),
  [raw cursor](../src/core/retail_cursor.c) and
  [GD command/status contract](../src/core/retail_gd.c).
- [SCI stream](../src/loader/sci_stream.c),
  [background engine](../src/loader/retail_async.c) and
  [IRQ/ABI assembly](../src/loader/retail_resident.S).
- [Memory constants](../include/kui/retail_loader_layout.h),
  [stage linker](../src/loader/retail_stage.ld) and
  [native placement evidence](evidence/native-low-resident-2026-10-06.md).
- [Format/import limits](games-formats.md) and
  [source provenance](../THIRD_PARTY.md).

SWAT source pinned to
`4a2b898cbc244b2fb9bd1698b45e5325056232fb`
(upstream master observed during this research):
- [CDDA implementation](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/cdda.c):
  PCM/sound buffers, channel checks, PIO/DMA, refill state and CE caveats.
- [CDDA context](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/include/cdda.h).
- [Allocator](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/malloc.c):
  internal/auto/Maple/Katana modes and fixed SDK lookup offsets.
- [Main handoff](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/main.c) and
  [separate loader targets](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/Makefile).

Primary mastering documentation:
- [mkdcdisc GD geometry](https://gitlab.com/simulant/mkdcdisc/-/blob/main/docs/gd-image.md):
  low-density warning/silence track, HD data at LBA 45000 and GD-DA tracks.
- [mkdcdisc CD/GD examples](https://gitlab.com/simulant/mkdcdisc/-/blob/main/docs/examples.md):
  Audio/Data CD audio preceding the data session and GD-DA mastering.

Some upstream documentation paths moved while being indexed; follow that
project's current documentation tree if a link redirects.
