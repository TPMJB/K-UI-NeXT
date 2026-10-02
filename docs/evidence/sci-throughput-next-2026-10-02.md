# Follow-up throughput experiments

Recorded 2026-10-02 UTC after the owner supplied Claude's interpretation of the
DreamShell comparison. This records hypotheses and a controlled sequence;
it does not change the autonomous diagnostic or accepted game reader.

Run 18 measured 1,068.096839 KiB/s, or 468.122348 microseconds per SD sector.
The recorded phase split is 330.788544 transfer, 68.422000 check, 1.923746
setup, and 66.988058 microseconds outside those phases. That last number is
arithmetic residual, not measured token-wait time. It includes driver/framing,
filesystem and scheduling effects. Source inspection alone cannot allocate
all of it to timer calls or card latency.

The prior [pinned reference review](sci-game-loading-review-2026-10-01.md)
records a separate-buffer reversal loop lagging DMA by 33 bytes, default
game/runtime CRC skipping, and a 256 KiB filesystem test with video stopped.
Those defaults do not establish SWAT's actual benchmark configuration. The
owner separately relayed SWAT's CRC-enabled raw result of about 1450 and
filesystem result of about 1350; their exact units and recipes must remain
attached to the report rather than inferred from defaults. No code is copied.

## Existing comparison and an important stream limit

**Storage tests → Compare**, three repeats, already measures 16/64/256 KiB
filesystem calls with CRC retained. Pattern verification is outside the timed
read interval; UI work during that interval can still count. The SCI adapter
in `src/dreamcast/sd.c` caps each CMD18 run at 128 SD sectors (64 KiB), so a
256 KiB filesystem request still becomes at least four physical streams.
This existing test isolates outer request-size effects; it does not establish
the gain from a 256 KiB CMD18 stream. Fragmentation can split requests further.

Measure this baseline before separately testing a larger physical-run limit
or paused rendering. Keep music, card, filesystem and CRC policy consistent.
Neither change directly establishes native-game loading or responsiveness.

## Timer and framing measurements

The runtime installs `sci_ticks()`, which calls `timer_us_gettime64()`.
Token polling in `src/loader/sd_reader.c` can call it through both
`multi_active()` and `expired()` on each iteration. The resident SCI path uses
the cheaper `port.work` counter instead. Reducing KOS timer calls therefore
targets runtime overhead and does not directly remove game CPU blocking.

Before changing deadlines, count token-wait bytes, framing bytes, timer
callbacks and command starts, with elapsed-time reads at phase boundaries.
Then compare less frequent wall-clock checks while preserving finite per-byte
budgets and timeout bounds. Test these changes separately from stream size.

## Overlap limits

Hiding the entire 68.422-microsecond check phase with all other measured costs
unchanged implies about 1,251 KiB/s. Reaching 1,350 KiB/s additionally needs
about 29 microseconds per sector removed from other costs. These are arithmetic
bounds on that model, not predicted console measurements. Check work inserted
into the dummy-feed loop can lengthen its near-wire-floor transfer phase.

A 33-byte lag in a separate implementation is not proof that K-UI may safely
modify cache lines during active DMA or that DMATCR already guarantees each
reported byte is visible in RAM. Active-DMA in-place reversal/CRC remains
deferred until that contract is established. No data may be returned before
CRC validation.

Even successful feed-loop overlap still occupies the CPU and retains the
resident's interrupt-masked synchronous call. The first
[autonomous diagnostic](sci-async-probe-2026-10-02.md) instead tests whether
SCI can receive without CPU dummy feeding. A later game-facing state machine,
interrupt/reentrancy rules and guest completion behavior remain necessary.
