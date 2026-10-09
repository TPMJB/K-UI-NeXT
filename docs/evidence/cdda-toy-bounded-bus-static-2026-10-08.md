# Toy Commander hard-freeze review and bounded sound bus

The hardware run after the GD command-progress correction still froze at
the No Cliche intro logo, with repeating audio and no controller response.
The recording has no program counter or readable build identifier. That
result invalidates a claim that command acknowledgement alone fixed the
observed hang. The exact stalled instruction remains unknown.

Four independent reviews covered the installed ARM protocol and heap,
SH hook ABI and interrupt stack, reader/card progress, and sound-bus calls.
Only authored analysis and scalar contracts are recorded here. The original
executable, driver, decoded instructions and recordings are excluded.

## Findings and exclusions

| Path | Evidence and consequence |
|---|---|
| Pilot while stopped | Each service performed four installed-driver reads, a sound allocation if absent, then three port observations before examining the mailbox. This introduced AICA activity during the game's FMV startup even without CDDA. |
| New sound observations and copies | Original SDK helpers at `0x8c0840d6` and `0x8c083fae` contain FIFO/stable-read waits without a deadline. The pilot called them with IMASK 15. A worker deadline checked after return cannot recover a nonreturning helper. |
| New sound packets | SDK publisher `0x8c083d34` contains the same unbounded bus operations. Original close/open/pan/volume/group-start/stop wrappers all reach it. |
| New allocation | SDK allocator `0x8c069c00` mode 1 also invokes an unbounded G2 canary write. Replacing reads/copies/packets alone would leave this exposure. |
| Suspend callback | The helper callback pointer at `0x8c0b01c8` has no discovered registration reference in the exact boot. If null, suspend/resume writes shared SDK error 516. The error formatter is bounded and no fatal game observer was found; error pollution is not claimed as the freeze cause. |
| Packet format and channel ownership | Close, PCM16 open, pan, volume, finite group start/stop, byte counts and upper port masks match the installed driver. No protocol correction was justified. |
| Sound allocation overlap | SFX and FMV use tracked head allocations; the pilot uses the tracked tail. No fixed upper-heap FMV scratch overlap or unhooked reset was found. |
| Hook ABI | Correction after build `4b82d67d11c7`: the default native IRQ dispatcher inherits interrupted R15. An IRQ during a worker visit can therefore place GD scratch on the worker stack. The earlier separate-stack claim was incorrect; see [the recording/IRQ review](cdda-toy-recording-irq-2026-10-08.md). Full ROM/dynamic IRQ stack depth remains outside compiler evidence. |
| Reader and card | Claims, framing, data/token/DMA waits and stream cleanup are bounded. No new indefinite card lock was found. |

The native FMV ring has 65,536 PCM16 frames per channel. At 22,050 Hz that
is about 2.97 seconds; at 44,100 Hz it is about 1.49 seconds. A native ring
continuing after the SH4 stalls fits the reported loop. Its actual rate in
the recording was not established. The pilot's own 16,384-frame banks are
finite and approximately 371 ms at 44,100 Hz.

## Implemented correction

The pilot now returns before sound allocation or any G2 operation when the
mailbox is handled, both banks are empty, neither active nor pending bank
exists, no packet/stop/transaction is outstanding, and state is stopped,
paused or EOF. A pending quiet stop/reset/pause can complete through the
mailbox/model alone under that same ownership proof. Play/release wakes the
worker. Idle clears service-clock history so a long pause is not a clock
fault on wake.

All newly introduced sound operations use authored bounded adapters:

- Each transaction saves exact SR, masks interrupts briefly, refuses BL
  context, checks all four G2 DMA enable/start bits without writing them,
  and restores exact SR on every return.
- FIFO drain and stable paired reads share one transaction budget:
  10,000 polls and 1,563 ticks of the admitted, unchanged TMU0 clock.
  The fixed cap also terminates when the timer is stopped. The nominal
  time limit is 2 ms; physical clock calibration is not claimed.
- PCM copies use at most 32-byte bursts and a final FIFO drain. Source
  samples are read bytewise, avoiding an aliased 32-bit C load of int16 arrays.
- Packet publication preserves the original increment-before/wrap producer
  contract, writes words 3, 2 and 1 before command word 0, notifies the driver,
  and updates the original producer/counter. Occupied slots defer without
  modifying host state. Failure after command publication is separately
  reported and never causes a retry of that packet.
- The sound lease validates both bounded record stacks, head/tail frontiers,
  free bytes, alignment and overflow. It writes/drains the existing canary
  contract before publishing the new tracked tail record. Failure leaves
  heap metadata and output unchanged. For the admitted empty tail, 128 KiB
  aligned to 32 starts at `0xa09d3fe0`, reserving `0x20020` bytes.
- Repeated unresolved busy observations are bounded by one nominal second
  of active service. Timeout/state/publication failures latch a bus fault;
  the worker cannot keep calling the failing operation after it disables.

No original game SDK code is patched globally. No ARM driver upload/reset,
master-mixer replacement, timer programming, DMA programming or new hardware
loop is introduced. The working ordinary 1.8.5 reader remains unchanged.

GD mailbox completion still uses first EXEC acknowledgement. Successful
audio acknowledgement now runs the ordinary scalar GD EXEC, retaining
CMD24 datatype initialization and STOP33 drive state. It cannot enter an
image-read path for the admitted audio IDs. Terminal CHECK still owns the
handle until consumed; hardware application remains separate telemetry.

The 64-word telemetry version 2 adds the last bus result, busy deferral
count, and entry/return counts around the original updater. It is copied
before reset. These are diagnostic breadcrumbs, not an interrupt-independent
watchdog or a promise of a crash screen if original game code never returns.

## Verification and limits

The actual worker service regression runs 10,000 idle visits in each stopped,
paused and EOF state with zero G2 or lease calls. It checks quiet commands,
busy retries, lease/observation timeout, published-stalled fault latching,
no duplicate accepted stop packet, and exact SR restoration. Bus models
exercise FIFO stalls, a stopped timer, unstable paired reads, timer wrap,
all four DMA channels, copy burst limits, producer wrap, occupied queue,
and failure on either side of packet commit. Lease regressions exercise
head/tail metadata, canaries, all-record exhaustion, malformed records,
space/overflow admission and failure without metadata mutation.

Strict warning builds and address/undefined-behavior sanitizer runs pass.
The separate SH build, fixed-address fit and integer-only/pure-GD leaf audits
pass. The low resident ends at `0x8c0077e4`, below its guarded stack; the
worker ends at `0x8cfd6e80`, below known upper scratch. Conservative authored
stack sums are 1,200/1,232 bytes for GD and 2,668/8,096 for the worker; original
SDK, IRQ and ROM frames are excluded. Preservation confirms 334 ordinary
source files, 1,565 historical build files, 23 baseline archives and four
previous pilot ZIPs unchanged. Final source/package identity is checked by
the packager. These establish the implementation properties above. They do not establish
the exact hardware hang PC, audible quality, or a fix for the game's slow
FMVs. Original SDK calls made by the game itself remain unbounded.
