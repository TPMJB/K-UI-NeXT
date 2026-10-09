# Toy recording, native IRQ scratch and refill audit

Date: 2026-10-08 UTC. Examined hardware build `4b82d67d11c7`, five report
photographs and the supplied `62052.mp4`. Private game input, disassembly,
recording/audio extracts and the external SWAT reference are excluded from
source and release archives. This document records independently authored
contracts and findings, not original game instructions.

## Demonstrated native command-retirement failure

The final report contains this sequence:

| Field | Hexadecimal value | Meaning |
|---|---|---|
| Fault / pause detail | `D` / `3` | Native pause retries exhausted their budget |
| Native owner / first work token | `0` / `0` | SDK no longer owns the first command record |
| Resident command / token | `14` / `129` | PLAY 20 still owns the GD slot |
| Native/GD state | `208` | Valid context, completed status 2; no pending/execute/entry lock |
| Last CHECK token | `129` | CHECK used the genuine resident token |
| Last CHECK output | `8CFD63B8` | Scratch inside the live worker stack |
| Last CHECK result | `FFFFFFFF` | Raw GD failure, before normal retirement |
| Worker end / stack watermark | `8CFD65C0` / `324` | Scratch is 520 bytes below the top; watermark 804 bytes |

The old `toy_map` rejects every overlap with the worker/heap header range.
The GD core maps CHECK's 16-byte result before token/status processing.
Rejected scratch therefore returns failure without consuming the completed
handle. The native wrapper translates that failure and drops its record;
subsequent PAUSE REQUEST returns zero because PLAY still occupies the core.
This is the demonstrated Start failure mechanism. The occupied GD slot can
also refuse subsequent file requests; the report does not establish how much
of the earlier FMV slowdown it caused.

The default SDK exception trampoline at `8C0C408C` saves integer/control and
FPU state directly on interrupted R15. It does not switch to a dedicated IRQ
stack. The CHECK veneer creates 16 bytes at its current SP and reaches our
resident with PR `8C0BD374`, parameter pointer equal to original SP. During
a worker visit that is legitimately the private worker stack. The low hook
already captures original PR/SP before switching to its guarded resident
stack. The reported destination is consistent with this exact path.

REQ_STAT36 has a companion legitimate use: PR `8C0BD57E`, 16-byte parameter
vector at original SP. Native subtype8 completes its previous record before
issuing that request, so it cannot require a nonzero old owner. Its pointed-to
outputs still pass the ordinary mapping rules. The earlier assertion that
all native IRQ work switches to a separate callback stack was incorrect.
The SDK's optional IRQ-stack mode requires a caller-supplied persistent stack;
changing its mode alone would select a zero stack pointer in this title.

## Scoped correction

`toy_pilot_scratch.h` implements one packed scratch capability for the current
serialized resident dispatch. It requires the exact CHECK-write or
REQ_STAT-read veneer, parameter==captured SP, alignment, and a complete 16-byte
span inside the actual linked 8192-byte stack. The bottom 64-byte guard,
36-byte resident caller-save reserve and top 16-byte bridge anchor remain
excluded. Protected stage admission now proves worker end equals stack top.
No ABI or report layout grows. The capability is cleared before dispatch and
after adapter return, including unsupported/uninstalled paths.

Wrong-token CHECK still maps its legitimate scratch so the core can return
error 5 while preserving the genuine owned handle. Token equality belongs to
the core, not the memory policy. BSS/code/heap headers, mismatched PR/SP,
wrong functions, directions, lengths and later calls remain denied. The
ordinary alias/cache handling remains in the existing mapper.

The production dispatcher and mapper are extracted into a host regression
with the real GD core and native-lifecycle model. It reproduces the blanket
failure and verifies matching retirement, next PAUSE admission, chained
REQ_STAT, wrong-token preservation, access bounds and allowance expiry.
A separate strict linked-instruction audit verifies capture before R15
switch, protected ownership and emitted capability/map behavior. It treats
original SDK calls as opaque; it does not claim to emulate physical IRQs.

## Recording and bandwidth evidence

The phone recording is 35.862 seconds and contains 860 frames at approximately
23.98 fps. That is the camera rate, not the game's update rate. The mouth image
is nearly unchanged at 12.60–19.35 seconds. Low-amplitude intervals include
12.47–19.73, 19.83–24.04 and 26.20–28.98 seconds. Supplied source track 14 itself
contains sparse transients and quiet passages near its beginning, so these
intervals cannot all be classified as underruns. Tentative source/recording
matches suggest slowed progression but have insufficient correlation to
support an exact speed ratio. The original FMV data was not available here.

| Recorded metric | Value |
|---|---:|
| Maximum worker visit | 12.344ms (`25AC` ticks) |
| Maximum service gap | 61.669ms (`BC33` ticks) |
| Maximum measured post-retirement handoff gap | 47.151ms (`8FE5` ticks) |
| Raw reads / bytes | 1,010 / 2,375,520 |
| Filled frames | 573,440, equivalent to 13.003 s at 44.1 kHz |
| Retired frames | 540,672, equivalent to 12.260 s of programmed duration |
| Starts / paired active observations / ends | 34 / 18 / 33 |
| Raw errors / queue errors / bus deferrals / active-bank writes | 0 / 0 / 0 / 0 |

Timing uses the admitted nominal 781,250 Hz TMU profile, not an independent
oscillator calibration. Retired frames are ownership accounting, not proof
that those frames reached the speakers. Counters began before recording;
dividing cumulative updater counts by recording length is not a valid
steady-state update rate.

CDDA requires 75 sectors/s, or 176,400 bytes/s (172.3 KiB/s). That is 17.64% of a
hypothetical 1 MB/s sustained payload rate. It does not prove the mixed workload
fits: this SCI path reacquires the card, reads one 2352-byte sector, stops the
stream and releases it while interrupts are masked. Movie reads and sound
copies share CPU/bus time. The trace does not establish a physical SCI ceiling.

The former fixed two one-sector visits per updater require at least 37.5
updates/s before deferrals. A 30 Hz updater cannot supply 75 sectors/s that way.
No such cadence guarantee was established. The recorded raw bytes also exceed
useful filled PCM by 81,760 bytes, approximately 3.56% extra, consistent with
rereading partial sectors at bank boundaries.

## Implemented refill changes

- Keep the existing single-sector transfer quantum. Admit extra quanta only
  while under 3125 ticks (nominal 4 ms) from visit entry, at most four quanta.
  Each restores SR before another begins. A slow first quantum is allowed
  but prevents another. The budget can overrun by one quantum; it is not a
  promise that the whole worker takes at most 4 ms.
- Stop on readiness, no progress, new generation, data priority or bus
  deferral. Existing callbacks and four-quantum cap bound a stopped clock.
- Reuse the existing raw buffer when both LBA and nonzero generation match.
  Invalidate before reads; failures leave it invalid. No additional PCM
  buffer is allocated. A 100-sector fixture now performs 100 reads, not 103.
- Start an already-ready successor before refilling the just-retired bank.
  A new raw transaction no longer sits in front of that START.

Actual-worker regressions cover 30 Hz and irregular 25/41 ms frames with 2.5 ms
fixture reads and data-priority interruptions. Every full bank's successor
becomes ready during its predecessor; all 100 sectors are read once. Separate
8 ms read and zero-cost cases verify the time admission and four-quantum caps.
Stereo samples across the 508/80-frame boundary split, deferred plane copies,
generation changes and failed reads are checked directly. Fixture costs are
explicit assumptions, not predicted card performance.

## Remaining audio architecture limit

Independent inspection of the native ARM driver reveals that MultiPlay
initializes current position to 0 and previous position to FFFFFFFF. Its first
position service compares hardware CA against current 0. If CA remains 0, the
nonlooping branch can clear hardware KEYON and the software active byte
immediately. The previous sentinel does not prevent this. Eighteen paired
active observations out of 34 starts, despite a maximum service gap far below
a 371.5 ms bank, support investigating early retirement. The exact console
key-off cause is not captured in this report.

Finite playback also has periodic restart and 20 ms retirement-guard gaps by
design. The SWAT reference uses a continuous looping stereo pair and optional
interrupt service, as detailed in the [design comparison](../cdda-toy-dreamshell-comparison.md).
No code was copied or translated. Simply turning our loops on would remove
the finite expiration guarantee and could repeat stale samples when SH
service stops. Toy's RequestEvent is a host notification, not an independent
ARM stop/deadline. The published cursor has no proven maximum age, so a stale
opposite-half observation is insufficient to authorize a live refill.

A complete continuous solution therefore needs an independently established
service/underrun mechanism and cursor-age/half-ownership proof, or a separately
reviewed correction of native finite-voice handling. This build implements the
proven command mapping and refill corrections; it does not claim to finish
that sound-driver work or establish smooth hardware playback. Source, host
regressions and strict unchanged linked memory/stack limits accompany it.

## Final local validation

All 15 host suites pass with AddressSanitizer and UndefinedBehaviorSanitizer.
The exact production scratch path, real GD retirement, sample contents and
variable-cadence worker are exercised. Leak detection is disabled because the
host process environment cannot inspect proc task state; address and undefined
behavior checks remain enabled.

The compiled scratch audit passes 24 dispatcher scenarios, 90 map cases and 12
mutation rejections. The existing pause audit also retains its mutation
checks. The resident ends at `8C0077E8`, 24 bytes below its unchanged limit;
the conservative authored GD stack sum is 1232/1232 bytes. These fixed limits
were not enlarged. Original SDK/ROM stack use remains outside that sum.
Exact final worker size, emitted instruction evidence and source identity
are recorded in the packaged build metadata. This revision has not yet run
on the user's Dreamcast.
