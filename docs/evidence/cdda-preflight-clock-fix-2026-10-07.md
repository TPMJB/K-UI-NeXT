# Profile 13 timer correction

The initial hardware run with build `324c330bdb6c` completed the six image
checks with zero failures and admitted all fifteen backings in 44 slots.
Its report stayed on page 1. The successful image/map result remains evidence
for that run; it did not establish the timed report or hardware deadline.

## Cause

`cdda_preflight_main.c` sampled TMU1 at `0xffd80018` and assumed that its
SCI storage lease initialized the timer. The selected SCI bus uses software
wire-work ticks and never starts TMU1. The older SCIF adapter and separate
audio harness explicitly initialize TMU1, but neither initializer is called
by this read-only SCI preflight. A stopped counter therefore prevented report
rotation and also left the nominal 180-second wall-clock deadline inactive.

The original host test replaced the tick source with an always advancing
value and rendered all six pages directly instead of running the timed
dispatcher. That test verified report contents but missed timer ownership
and rotation.

## Scope

The correction explicitly saves and initializes TMU channel 1 before any
card discovery or image reads. It selects the peripheral clock divided by
four, disables that channel's timer interrupt and starts its down-counter.
It preserves the other channel start bits, refuses module standby without
waking the whole TMU, checks the configured registers and requires observable
counter movement within 1,024 polls. An unavailable clock produces a static
failure before card I/O. Failure cleanup restores the saved channel control,
reload, count and start bit; it does not recreate an earlier pending IRQ.

The Renesas [SH7750 hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware),
sections 9.2.1 and 12.2.5, documents the TMU standby bit, timer control bits
and peripheral-clock prescaler used here. Timing uses the project's existing
12,468,720 Hz reference; this is not a new absolute clock calibration.

The correction belongs to the detached profile 13 runtime. It retains the
existing image, raw-sector, identity and full-map checks, starts no audio and
launches no game. SCI storage, the ordinary reader and observer 14 remain
unchanged. Observer 14 times its own reports using PowerVR scanline wraps;
it does not depend on this timer initializer.

The replacement package contains only profile 13. It does not overwrite
`preflight.cfg`, backing files, the working runtime backup or observer 14.
The existing observer remains build `324c330bdb6c`, SHA-256
`110140acc2f8b77b12260e0d1eaa3f911902819bbd3ff49889db8f222de69677`.

## Validation

The provisional native build with identifier `000000000000` passes the ELF
envelope, writable NOLOAD stack, retained read-only checker and instruction
checks. Its initialized payload is 71,240 bytes; memory reservation remains
2,097,152 bytes, with the same 65,536-byte stack at
`0x8c200000..0x8c210000`. The live BSS ends at `0x8c034ac0`.

The updated main frame is 372 bytes, twelve more than the historical audit.
The unchanged largest nested core chain therefore totals 32,612 bytes, or
32,868 with the existing conservative 256-byte helper allowance, below the
65,472-byte guarded stack budget. The literal-aware disassembly scan examines
22,324 instructions; only the existing startup FPSCR setup is permitted.
No AICA driver, retail vector, client bridge or game-launch code is linked.

All 39 actual-main adapter scenarios pass in both strict optimized and
ASan/UBSan builds. Successful runs drive the shared native/host dispatcher
through thirteen reports: two six-page cycles and the first page again.
Thirty-six polls verify no advance at zero elapsed time or one tick short of
15 seconds, advancement at the independent 15-second threshold, and a
numerical counter wrap. No filesystem/card operation occurs after PASS.

The added stopped-counter, rejected-configuration and module-standby cases
refuse before card reads. Stopped-counter verification is bounded to 1,025
observations including the baseline read. Failure cases verify saved
channel control/count/start restoration and preservation of changes to
other channel start bits. PASS retains the running clock. The original
image-corruption, extent-admission, wrapped deadline and stack-guard cases
remain in the same suite. Independent review found no blocking clock or
paging issue.

The final twelve-character source/build identity and exact binary checksums
are recorded in the replacement archive's `build.json` and `SHA256SUMS`.
The supplied console photos now show all six PASS pages for corrected build
`9fb65a33f235`, in the order 2–6 followed by 1. The reported 32,612-byte private
stack watermark matches the updated static main/core chain. Report rotation
is confirmed. The photos do not establish an absolute 15-second calibration
or an injected 180-second hardware timeout. Full fields and track geometry
are recorded in [the hardware evidence](cdda-preflight-hardware-2026-10-07.md).
