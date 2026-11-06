# Smash launch timing diagnostic

The user confirmed the `2c53ceaf` save/menu-return fix. This diagnostic measures
the remaining launch delay; it does not promise a speed improvement. Keep the
same card, backing files, original image, save handling and return route.

## Clock requirements

The pinned KOS `kernel/arch/dreamcast/kernel/timer.c` maintains uptime seconds
in the TMU2 interrupt handler. Its pending-underflow compensation covers one
second, so ordinary `timer_us_gettime64()` cannot measure a long interval with
interrupts masked. The SCI/SCIF bus `ticks` callbacks count protocol work and
are also unsuitable as elapsed clocks.

KOS `kernel/init.c` starts performance counter0 in elapsed CPU-cycle mode.
Read its existing configuration and stable high/low/high count without starting,
clearing, stopping or reconfiguring it. A changed/stopped configuration or an
unstable split read must produce an unavailable sample. Check frequency state
before converting cycles to nominal milliseconds. Conversion is not a console
measurement until the user runs the build.

Use RTC seconds as an independent coarse check where a stable, safe sample is
available. RTC quantization prevents precise comparisons for short phases.
KOS `arch_rtc_unix_secs()` uses G2 locks that write DMA suspend registers; it is
not a read-only hardware observer. An optional raw RTC observer must refuse a
busy G2 path instead of changing producer ownership. Missing RTC validation must
be visible in the report.

## Required evidence

Record the UI preparation interval separately from the native child stages.
Include actual elapsed samples and read/byte/command counters for the costly
phases, including fresh card ownership, recovery and validation. Distinguish
logical filesystem requests from physical card sessions. Include invalid-clock
counts and the clock source/profile used; unavailable is different from zero.

This diagnostic stops at its native phase summary before entering the game.
Record the first RPG selection. It cannot measure a later selection after menu
return or any game/SGA-loading work after that stop point. Screen rendering,
intentional photo holds and report saving must not silently be charged to
another phase. Do not remove validation, reuse a stale card snapshot, change
read batching, or alter cache/IRQ ownership to improve the reported result.

Equal counter configurations at the endpoints cannot prove that a counter was
never reset or reprogrammed in between. Reject diagnostic phases over one hour
and flag disagreement with an available RTC delta, allowing coarse RTC
quantization and the nominal CPU conversion. A plausible transient reset with
no independent clock remains unobservable; preserve that limitation rather
than describing nominal cycle-derived time as calibrated wall time.

## Native summary

The native page reports nominal CPU milliseconds and independent RTC seconds.
`NA` means the corresponding elapsed value is unavailable. These intervals
cover every marker boundary from the first arrived MDE chunk to the stop:

| Label | Markers | Work inside the interval |
| --- | --- | --- |
| GIN MDE | 1–2 | First MDE payload chunk through the post-GIN handoff |
| GATE | 2–3 | Handoff, arguments, firmware and capsule-plan checks |
| CAPLOAD | 3–4 | Capsule card reads, including the pre-overwrite probe |
| CAPCHECK | 4–5 | Complete capsule CRC, seed setup and child handoff |
| COLD SD | 5–11 | Child entry, stage setup and card initialization |
| FS OWNER | 11–13 | Preparation preflight and fresh filesystem ownership scan |
| REC HASH | 13–14 | Journal recovery and current base validation/hashing |
| CHECKPT | 14–15 | Durable preparation checkpoint |
| VMU HASH | 15–17 | Fresh image hashes, final identity/marker checks and storage cleanup |
| IP LOAD | 17–18 | Original IP read and validation |
| MDE LOAD | 18–19 | Original engine read and validation |
| SETUP | 19–20 | Preparation before BOOT2 |
| BOOT2 | 20–21 | Firmware bootstrap through the native relay entry |
| RELAY | 21–23 | Immutable base/resident verification and resolution preflight |
| NATIVE | 23–24 | Current native cache/discovery and code-hash work |
| ARM | 24–25 | Hook preparation, arguments and return-gate publication |
| TOTAL | 1–25 | Entire measured native interval |
| CHILD | 5–25 | Child launch subset of TOTAL |

The filesystem counters describe the fresh ownership scan. `VMU REQUESTS`,
`VMU SD GOOD`, `VMU SINGLE`, `VMU RUNS` and `VMU WRITES` cover VMU preparation
callbacks; they do not count capsule, IP, MDE or later game/SGA reads.

The `CLK` row contains first and last PMCR/FRQCR, `F` followed by the hexadecimal
OR of sample and delta flags, and `N` followed by the invalid CPU-sample count.
In `F`, `0x2000` rejects an implausible interval and `0x4000` flags CPU/RTC
disagreement. `0x0100`, `0x0200` and `0x0400` identify bad CPU configuration,
torn count and stopped count; `0x0800` and `0x1000` identify busy/unavailable G2
RTC access and unstable RTC samples. The low bits `1`, `2` and `4` are valid
cycle, nominal-millisecond and RTC-source flags respectively.
Because `F` is an OR across records, a low valid bit shows that at least one
record supplied that source; each phase cell's value or `NA` determines that
interval's availability.

## Display ownership and installed profiles

The original Smash Pack menu allocates its own framebuffer addresses and can
blank scanout. Its inherited video state cannot drive the diagnostic renderer,
which writes RGB565 at VRAM offset zero with a 640-pixel stride. The first timing
delivery restored that inherited state; its terminal could therefore remain
invisible even when the diagnostic reached its intentional stop.

The optional timing profiles now carry the original outer K-UI display capture
through a separate `VID1` record. It follows the unchanged 992-byte `TIM1`
record, at native menu seed offset +1664. Its 80 bytes contain a 16-byte typed
header, the original 14 display words, CRC32 and a zero reserved word. The
outer capture must match KOS's enabled/unblanked 640x480 RGB565 framebuffer-zero
shape; cable-specific timing words remain bit-exact. The low loader fills a
blank VID1 only after the complete padded child capsule CRC passes. The child
validates/imports it before its first display restore and retains the capture
before the capsule is overwritten. The working 472-byte and 420-byte handoff
records are unchanged. Ordinary functional profiles contain neither VID1 nor
the intentional timing stop.

The UI report records both module payload CRCs/sizes, actual prepared capsule
CRCs/sizes and native profile flags. Flags zero means an ordinary pair; seven
means the matching outer timing marker and child TIM1/VID1 seeds were verified.
Mixed profiles refuse even when their build identifiers match. A capsule-write
metric of zero means every stored byte matched the newly prepared capsule;
reuse still requires complete read/compare and physical allocation checks.

## Verification

The clock test must execute the production observer with authored register
reads covering masked interrupts, split-counter rollover, counter wrap,
configuration changes, unsupported frequency, unstable/stopped counters and
RTC rollover/unavailability. All hardware writes must be rejected. A linked
audit must bind the actual diagnostic helper and phase call sites to the
reported artifact hashes.

Display checks execute actual outer capture/publication/readback and child
import/restore/render instructions. Include inherited nonzero framebuffer and
blanked-menu fixtures, malformed VID1 and a corrupted low copy. A visible
console result still requires the next hardware test.

Run the existing functional linked audit on the diagnostic outer/child pair so
timing does not replace save, return, card-admission or stack checks. Keep exact
`2c53ceaf` restore modules in the delivery. The user's timings and phase counts
will identify the next change; instruction-count estimates alone do not.
