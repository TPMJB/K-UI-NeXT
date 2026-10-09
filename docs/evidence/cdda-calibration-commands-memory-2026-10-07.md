# CDDA calibration and controlled-command memory audit

Measured independently on 2026-10-07 UTC for the two new detached homebrew
profiles. This is linked-build and source-review evidence. Neither new
profile has passed a console run yet; the earlier profiles' photographed
stack watermarks do not certify these programs.

## Build checkpoint and ordinary reader preservation

Both programs were built with `Makefile.cdda`, SH GCC 15.2.0 and provisional
`CDDA_BUILD_ID=000000000000`. The recovered pinned FatFs `ff.c` has SHA-256
`3c30280ffe2d0fb19a5241d9ed9f20a2c3911c2861652ebf6ad57f74fb369250`.
Private copied headers/configuration keep the harness filesystem read-only.
The published build substitutes a fixed-length source identifier; compare
its section geometry with this checkpoint and use the final package manifest
for delivered hashes.

An independent source archive rebuilt both ordinary native SCI payloads with
the unmodified `Makefile.dc`, native linker scripts and fixed
`BUILD_ID=cdda-audit00`. Only an empty temporary KOS make-rules stub was needed
to parse the native-only targets. No KOS program or released binary was rebuilt
in the working tree. Both results exactly match the foundation/1.8.5 identities
recorded in the [previous audit](cdda-next-memory-2026-10-07.md).

| Ordinary reader | Bytes | SHA-256 |
| --- | ---: | --- |
| Standard SCI | 11,140 | `0e9b3df402f6488701333f8574c543e489cb479225de22040637bc69d8230d99` |
| Background SCI | 12,292 | `e0ecdea9a89a12428fcc1312a83f5d1e2df2a6fb16602f64a8d9bb0fb7cacf6c` |

The stable low-reader reservation is unchanged. Its previously measured
CDDA fit failure is not overridden by this explicit homebrew allocation.

## Linked layout and resource ownership

| Profile | Payload | Entry section | Text | Read-only data | Data | Live BSS | BSS end |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 4 calibration | 47,612 | 140 | 28,760 | 18,672 | 20 | 3,744 | `0x8c01c8a0` |
| 5 commands | 52,344 | 140 | 33,336 | 18,828 | 20 | 3,808 | `0x8c01db60` |

Each ELF enters at `0x8c010000` and has three disjoint LOAD segments: RX
entry/code/read-only data, RW initialized data/BSS, and the RW NOLOAD private
stack at `0x8c200000..0x8c210000`. Each runtime envelope declares exactly
**2,097,152 bytes**, including the address gap and 65,536-byte stack. No live
BSS overlaps the stack and no symbol remains unresolved in either ELF.

Each payload has exactly one complete initialized AUTO transport marker:
offset `0xb9e8` for calibration and `0xcc64` for commands. The bootstrap
validates the envelope before patching that marker to SCI; main and storage
share the same definition. Both profiles refuse an invalid or non-SCI marker.
They use one private read-only mount and one synchronous SCI lease, without
retaining pointers or services from the old shell.

The existing startup still enters through P2, masks interrupts, initializes
FPSCR, clears private BSS, fills the stack watermark and 64-byte guard, writes
CCR from P2, waits eight instructions and installs the private stack pointer
before entering cached C code. The existing bootstrap's `arch_exec` handoff
removes KOS first; no additional handoff or retail stage was introduced.

AICA ownership remains channels 0/1 with two 32,768-byte sound-RAM rings at
offsets `0x100000..0x108000` and `0x108000..0x110000`. The AICA ARM is held in
reset and G2 DMA is suspended before bounded G2 PIO. The programs own TMU1;
the calibration profile additionally reads TCR1 and FRQCR. These allocations
belong to this standalone homebrew runtime and establish no retail-game
coexistence or BIOS command hooks.

## Stack and generated instructions

Independent compiles used the production flags with only
`-fcallgraph-info=su` added. Every compiler stack record is static. Direct
calls and the actual PCM, boot-volume and SCI bus callbacks were resolved;
the unused SCI profiling callback remains null. No callback recursion or
unknown generated call was found. Linked integer shift/division helpers were
inspected and receive a conservative additional 256-byte allowance.

| Profile | Main frame | Conservative main chain | With allowance | Sum of all compiled frames | Audited instructions |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4 calibration | 92 | 5,936 | 6,192 | 10,820 | 13,005 |
| 5 commands | 32 | 6,540 | 6,796 | 11,700 | 15,028 |

Usable stack is **65,472 bytes**, excluding the guard. The largest conservative
chain plus allowance is 6,796 bytes. Even summing every compiled frame,
including unused functions discarded by link GC, gives at most
11,700 + 256 = 11,956 bytes. The largest single frame remains the partition
scanner at 4,788 bytes; FatFs `f_open` uses 1,280 bytes.

The conservative command path is:

```text
cdda_main(32) -> commands(480) -> command_stale(112)
 -> command_finish(72) -> cdda_storage_open(36) -> cdda_storage_init(68)
 -> kui_boot_volume_scan(44) -> scan(4788) -> header.isra.0(568)
 -> raw_read(40) -> kui_loader_sd_read_multi(60)
 -> kui_loader_sd_stream_start(60) -> stream_finish(44)
 -> multi_command.constprop.0(40) -> transfer_block(76) -> prepare(20)
```

This graph is deliberately conservative: a stale completion actually returns
before reopening storage, and ordinary playback has already mounted the
filesystem. Including those impossible or already-satisfied paths increases
the bound. The runtime watermark measures observed stack writes and excludes
later final-screen work; retain the static bound and guard check and record
each new profile's own console watermark.

The literal-aware linked instruction audit permits exactly one
`lds r0,fpscr` at `0x8c01000c` in each image. No other instruction uses FPU
operations or registers. PC-relative literal pools were excluded before
classification, including address constants whose bits decode as FPU opcodes.

## Paired timing and command-state review

Calibration records the same hardware position accepted by the ring between
two timer reads. Its paired frame difference uses cumulative `ring.played`,
not the PCM prefetch cursor or a second position query. Initialization,
prefill, key-off and final display are outside the paired interval. The
lower tick bound must reach 750,000,000; the upper bound is at most
752,521,995, with each endpoint read window at most 100,000 ticks. Subtraction
avoids overflowing the sum of read windows. The endpoint interval remains
below half the 32-bit timer period, so one numerical wrap is supported;
regular ring observations maintain the unwrapped frame count and deadline.

Integer hardware positions have a conservative ±1-frame difference
uncertainty. The tick brackets and that quantization must accompany later
relative-rate analysis. The raw clock controls are checked for stability,
allowing only the TMU sticky underflow flag to differ. The displayed
12,500,000 Hz and pitch zero are nominal/programmed values, not frequency
measurement or pitch readback. There is no independent oscillator reference
and no automatic clock or pitch retuning.

The portable command core validates ranges by subtraction, clamps
nonrepeating EOF and uses quotient/remainder loop arithmetic without adding
two potentially overflowing frame counts. An additional independent host
property check compared one million randomized selected ranges, interior
seek origins and 32-bit played counts against a 64-bit reference; all passed.
Loop counters and cumulative frame counts reject overflow.

Command actions stage a monotonically increasing generation. STOP/PLAY can
replace pending work; canceled, duplicate or old completions cannot commit it.
Generation exhaustion refuses further actions rather than reusing a token.
The adapter checks the pending epoch before any mute, stop, read or reprime,
then uses the stored validated action. Its canceled-completion tests exercise
that adapter refusal as well as the pure core. Matching active or pending
fatal failures commit FAULT before owned-resource cleanup; stale failures
cannot fault a later generation.

Status and pause use actual observed playback progress, including the initial
hardware position. Matching PLAYING completion credits the initial position
already captured during start before returning, so even immediate pure status
includes that observation without a second hardware query. Pause confirms
mute/stop, preserves that cursor and
discards queued PCM. Resume/seek reopens and reprimes from the saved or selected
frame while retaining the original loop start and exclusive end. Loop changes
also reprime from the observed cursor. These controlled transitions do not
claim sample-exact key-off latency or seamless command changes. Paused status
does not query stopped channel phase. A command run that synchronizes into
EOF returns without trying to pump stopped hardware.

Profile 5 requires seven completed stages, two expected invalid/state refusals,
four expected stale-token refusals and at least four observed loop passes.
Unexpected PCM, AICA, ring or clock failures still fail the program; they are
not included among those expected refusals. The synchronous generated-source
test exercises its own command semantics, not retail BIOS IDs or asynchronous
game callbacks. Host models cannot establish real SCI/G2 latency, audible
channel order, pitch or retail sharing.

## Provisional package identities

| Profile | SHA-256 of envelope, build `000000000000` |
| --- | --- |
| 4 calibration | `61487ff7ee7c4a78bbd4ee7523047e46612a76c14320e658560753dfc288cca1` |
| 5 commands | `d9469e17a6485bfa0eaa7113c8581824f1ffc1c9cacdbb452e269d559e727365` |

The final ASan/UBSan integration runner passed all 19 cases. It compares
accepted timing endpoints and command status against independent simulated
hardware observations, checks nominal and +2,200 ppm relative AICA rates
across a numerical timer wrap, and exercises observation/read failures,
later seek-reprime failure, paused/EOF behavior and stale adapter calls with
no model or hardware mutation. This simulation evidence does not replace
the two pending console runs.

Reproduce both links with the pinned dependency/toolchain:

```sh
make -f Makefile.cdda CDDA_BUILD_ID=000000000000 cdda-next-tests
python3 tools/test_cdda_host.py --sanitize
python3 tools/test_cdda_harness.py --sanitize
```

Use the [short console checklist](../cdda-calibration-commands-test.md) to
collect the new hardware results. Preserve the earlier runtime backup and
readers; a safe failure is useful evidence but does not pass either profile.
