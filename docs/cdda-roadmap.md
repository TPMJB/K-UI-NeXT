# CDDA implementation roadmap

Status: the detached SCI/AICA harness passed its baseline, two controls runs,
15-minute soak and 15-minute serialized read stress with zero reported faults.
Targeted stereo listening, precise timer/audio calibration, broader job arbitration and retail
resource/command integration remain open.
Short profiles 04 and 05 implement matched clock observations and controlled
command/job checks; their console results are pending. Use the
[new two-profile checklist](cdda-calibration-commands-test.md) for this step.
Prepared 2026-10-07 UTC / 2026-10-06 America/Chicago.

This roadmap implements the [separate reader design](cdda-reader-design.md).
The released [1.8.5 reader](release-v1.8.5.md) stays the fallback. The first
target is native SCI microSD with complete, uncompressed disc-audio tracks;
Windows CE, IDE and compressed resident playback follow only after that
path is proven. Gates describe evidence required to advance, not delivery
dates or promises of broad game compatibility.

## Ground rules

- Work in the experimental branch; preserve the published 1.8.5 reader,
  release assets and ordinary package validation.
- Implement from primary hardware/BIOS documentation, independently licensed
  KallistiOS interfaces and K-UI's existing contracts. Record exact references
  and retain applicable notices in [the provenance record](../THIRD_PARTY.md).
- Do not copy, port or closely translate DreamShell code or binaries,
  including its refill, allocation, timer or channel-selection algorithms.
  Attribution does not change permission or make a port independent.
- Prior research inspected DreamShell source. This is source-aware
  development, not a formal clean-room claim. Its ISO Loader uses PolyForm
  Noncommercial 1.0.0 at the inspected revision, not K-UI's GPL-3.0-only
  license; the detailed boundary is in the design document.
- Reject unsafe or unsupported CDDA configurations explicitly. A memory
  address that happens to be idle, a currently inactive sound channel and an
  image containing audio are not compatibility guarantees.

## Gate 1: one metadata classification contract

**Implemented in the foundation branch.** One pure classification function
over the already parsed image replaces the launcher's two differing audio
heuristics. Games details and launch warnings use the same result.

Classify supplied audio separately from actual playback usage: intact GD
high-density audio is a candidate; ordinary low-density GD audio is normally
the warning track; audio in a CD image is ambiguous and may precede its data
session. Data-only images cannot supply absent tracks. Keep all classification
off the page-navigation path: no extra track opens, audio scans or descriptor
reads while paging.

**Exit evidence:** focused host tests cover warning-only GD, high-density GD
audio, CD audio before and after data, ambiguous CD tracks, data-only images
and Original/2048 audio-geometry parity. Existing launch preparation continues
using the same reader and package contracts. Code review confirms there is no
new per-page I/O.

**Scope of this foundation:** classification and launcher messages only. It
does not add playback, install a CDDA reader, enable automatic dispatch or
make an unsupported game play music. Auto / On / Off and package selection
remain part of Gate 5.

**Foundation validation:** the image unit target passed 1,027 checks with
AddressSanitizer and UndefinedBehaviorSanitizer. The Games, retail-preparation
and shell host targets compiled with strict warnings; shell tests passed.
Focused FAT32/exFAT runs passed 20 Games inspection/navigation checks and 12
retail-preparation checks, including audio before data, GD warning-only audio
and ISO/raw unknown inventory. Whole-card SHA-256 and filesystem checks
confirmed read-only operation. The image runner now accepts repeated `--case`
arguments to avoid running unrelated cases for a focused change.

Leak detection alone was disabled because the test runner's ptrace environment
prevents LeakSanitizer from inspecting `/proc`; address/undefined checks remained
enabled. This is host evidence, not an on-console CDDA test. No new Dreamcast
binary or audio-playing reader has been produced at this gate.

## Gate 2: isolated SCI harness and measured memory budget

**Initial implementation, measurements and first console counter result complete.**
The [first hardware evidence](evidence/cdda-hardware-2026-10-07.md) records
79.4 ms worst half refill, 106.1 ms minimum margin and 5,184 bytes of observed
stack use. The photograph does not establish audible channel order or quality.
The [isolated test instructions](cdda-harness-test.md) cover the actual built
runtime, generated fixture, sample placement and restoring the retained runtime.
The [memory evidence](evidence/cdda-harness-memory-2026-10-07.md) records a real
low-link failure without enlarging the ordinary reservation. The controlled
homebrew image owns its separate code/state/stack region and contains no KOS
kernel references. This is not a retail high-memory admission.

Create a controlled homebrew program that explicitly grants the experiment
main RAM, sound RAM, two AICA channels, interrupt behavior and a timing source.
Exercise the detached reader after normal shell services have stopped;
success inside a running KOS music thread would not prove the retail path.

Build a candidate low resident and measure actual text/data/BSS, map storage,
stack bounds and generated instructions. Do not increase an ordinary reader's
reservation. If the candidate cannot fit, run the same engine in memory
explicitly reserved by the homebrew harness. That establishes a development
environment, not permission to place code in a retail game's high RAM.

**Exit evidence:** a reproducible link map, conservative stack report,
instruction audit and checked handoff layout; no overlap among copied code,
temporary stage/stack, final state or harness allocations. Ordinary reader
payloads are unchanged when compared at a fixed build ID. A runnable harness
and its SD installation instructions exist before asking for hardware tests.

## Gate 3: checked raw PCM into an AICA ping-pong ring

**Baseline, controls/recovery and 15-minute soak passed their numerical console checks.**
The [new hardware record](evidence/cdda-controls-soak-stress-hardware-2026-10-07.md)
includes two expected deadline recoveries across two controls runs and two
timer wraps per long run. The owner noticed no obvious problem during casual
listening, but could not distinguish left/right on the TV and did not monitor
continuity. Targeted audible verification and exact timer/audio endpoint
calibration remain open; controlled command/job work can proceed meanwhile.
Focused sanitizer
checks cover raw conversion, both byte orders, offsets, sector/cache boundaries,
2448-byte subchannel stripping, transactional I/O, seeks/EOF, ring deadlines and
AICA MMIO phase/FIFO/active-half refusal. The photographs establish observed
refill/service margins, completion and expected refusal/recovery counters;
they do not establish precise pitch, silence or glitch-free stereo by listening.

Play independently generated synthetic stereo samples first. Read raw audio
through checked 512-byte SD blocks, carry partial stereo frames, deinterleave
into left/right samples and fill only the inactive half of explicitly owned
AICA buffers. Start with bounded G2 PIO rather than introducing a second DMA
or timer owner. A 64 KiB total stereo ring gives approximately 185.8 ms to
refill each half; the measured refill deadline needs a safety margin.

Exercise channel order and phase, signed sample/byte order, 2352-byte sectors,
2448-byte sources with subchannels removed, nonzero backing offsets,
block/sector/half boundaries, track changes, pause/resume, seek, loop and EOF.
An underrun must stop or mute owned audio safely rather than loop stale data.

**Exit evidence:** correct output and position for the synthetic fixtures;
zero active-half overwrites or unexplained sample loss; measured worst refill
time and maximum service gap remain within the chosen ring budget with a
recorded margin. Raw CDDA consumes 176,400 bytes/s, about **172.3 KiB/s**.
Average card throughput alone is not sufficient: account for checked reads,
PIO work, tail latency, retries and service jitter.

## Gate 4: one card arbiter under concurrent read stress

**Initial serialized homebrew stress passed its numerical console checks.**
The same hardware record reports 300,892,160 independently verified data
bytes, 35 complete 8 MiB passes and zero read/check errors during 900 clock
seconds of audio. Worst complete data job was 32.959 ms; worst verified-job
completion gap was 151.441 ms. This workload uses two independent file cursors
on one serialized SCI lease. Randomized game-sized jobs, cancellation/retry
semantics, fragmented layouts and actual game command latency remain open.

Give game data and audio separate cursors/jobs but a single owner of the SCI
bus, card stream and receive buffers. Switch only after a complete checked
block, preserve speculative-block identity, and bound stream-stop/restart
work. Audio deadlines must remain separate from ordinary GD tokens; STOP or
PAUSE must not cancel an unrelated game read.

Run randomized and game-sized reads during playback, including fragmented
extents, retries, stream switching, prolonged periods without GD calls and
DMAC contention. Use independent fixture checksums to verify game data.
Measure both audio deadlines and the effect on game-read latency. Card
read headroom must cover audio plus game data and switching overhead.

**Exit evidence:** correct game bytes and completion counts, zero stale-job
copies or false completions, bounded service work and no underruns through
the stress set. Record maximum service/refill gaps and stream-switch/retry
costs. An average speed benchmark does not satisfy this gate.

## Gate 5: separate package, bootstrap and admission rules

Define a distinct CDDA package identity and schema rather than extending a
reserved stable-header word. Validate segment bounds, aliases, overlap,
stack, cache publication and complete data/audio extent coverage before
handoff. Required audio extents must never be silently dropped to fit a map.

Add per-image Auto / On / Off selection and explain unavailable or refused
support. Off selects the stable reader. Initially Auto selects CDDA only for
a validated profile/backend; On cannot bypass missing tracks, map capacity,
memory ownership, sound ownership or scheduling requirements.

Establish the periodic service bridge without stealing the game's timer,
forcing its interrupt mask down, acknowledging its events or resetting its
sound driver. Preserve original VBR/interrupt dispatch and the full helper
call ABI. Game-owned main RAM and AICA allocations need a demonstrated
contract; guards detect corruption but do not reserve memory.

**Exit evidence:** focused package/map/ABI tests reject invalid and incomplete
launches; link/stack/instruction checks pass; controlled handoff and interrupt
tests preserve state; standard-reader payload checks remain unchanged. A
profile must own every resident/code/stack/ring/channel resource it needs.
No generic high-RAM retail island is admitted by this gate.

## Gate 6: Toy Commander compatibility and a validated retail profile

The user owns **Toy Commander** among the identified CDDA titles, so it is
our retail target; another game purchase is not a prerequisite. Inspect its
complete GDI with its raw audio tracks. Pin executable/version/layout identity;
do not infer support for other regions, revisions or repacked images from a
title name. Suitability still depends on proving its main/sound-memory and
service contracts in Gate 5.

Toy Commander has a [reported sound-channel conflict](https://dc-swat.ru/www/forum/thread-4042-post-43645.html) in other loaders, making
it a harder first retail target. First prove the engine with independently
generated audio in the controlled harness. Then observe Toy Commander's own
sound-memory and channel reconfiguration during boot, menus, loading and busy
gameplay. Establish an independent coexistence method from those observations
and documented hardware behavior. Reserving a channel in our code does not
prevent the game from rewriting it; repeatedly forcing our settings back could
also break its effects. Both music and effects must pass before admitting the
profile. If safe coexistence remains unproved, keep this title experimental
rather than weakening admission or requiring a different purchase.

Compare stable-reader and experimental-reader behavior. Check title/menu,
music, sound effects, gameplay, loading and transitions, controls, pause and
extended play. Record worst refill/service gaps, ring low-water, underruns,
ownership conflicts, game-read time and guard failures using bounded
diagnostics. Save reports after a safe return to the shell, not from an IRQ.

**Exit evidence:** that exact image/profile meets the ownership and timing
rules and passes the documented hardware checks with audible music and
working effects. Only then admit it in Auto. One successful game is not a
claim that CDDA is generally supported.

## Expansion after the first profile

Expand title/revision coverage and fragmented image layouts before changing
defaults. Consider smaller rings or G2 DMA only when measurements justify
them. Add CE, IDE/ATA and additional formats as separate tested profiles;
their interrupt, storage and sound contracts may differ. Compressed or
lossy audio sidecars are optional later work, not a requirement for accurate
raw playback. Revisit provenance and release checks when distributing a
candidate reader.

## First steps and ownership

**Agent next:** review the console results from profiles 04 and 05, which
implement matched timer/audio diagnostics and controlled command semantics.
Preserve the successful earlier checkpoint while expanding audio/data
arbitration. Establish periodic service and resource admission before
Toy Commander retail integration. The classifier, standalone engine, controls,
soak/stress fixtures and memory audit are implemented and numerically tested.

**User next:** run [04 calibration and 05 commands](cdda-calibration-commands-test.md)
in order and photograph their final screens, then restore the preserved 1.8.5
runtime. Each test uses the existing generated stereo fixture; neither needs
another game track. A later brief
stereo/control check can confirm channel order and silence; no additional
routine soak is requested now. The [test checklist](cdda-next-test.md) remains available
for reproducing the earlier successful results. No routine soak repetition
is requested for this pair of short tests.
This stage does not require another boot CD or firmware reflash. Retain a complete, uncompressed
**Toy Commander** GDI with every referenced `.raw` track; do not buy another
title for this experiment.
Retain the original image and descriptor; a data-only ISO/CSO/ZSO copy cannot
test missing disc audio. When the harness is ready, follow its exact SD
installation and logging instructions before testing a retail reader.

**Agent before requesting a test:** supply the build identity, installation
paths, expected output, minimal test checklist and a simple return to the
stable reader. Do not describe a build as available before it exists.
