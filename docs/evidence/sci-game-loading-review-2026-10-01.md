# SCI game loading and reference review — 2026-10-01

The owner reports that SWAT's build reaches DOA2's first fight in roughly
15 seconds, about half K-UI's time, but immediately stalls and crashes easily.
This is useful directional evidence, not a controlled or stable benchmark.
The exact timing boundary, build settings and reason for the stall are unknown.
Do not infer that CRC policy or read batching caused the failure.

## Read-only reference review

At the owner's explicit request, current DreamShell source was inspected for
ideas. No third-party code was added. Existing K-UI source provenance remains
unchanged. The four-byte reversal candidate was implemented before this review.

The examined DreamShell revision is `4a2b898cbc244b2fb9bd1698b45e5325056232fb`;
its pinned KOS revision is `579bb6a467146006bc54261fad9679f913705d39` and pinned
FatFs revision is `0441821e59cafc492990707e6429613e4630668f`.

- [SCI receive implementation](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/dev/sd/sci.c#L1128-L1215)
  reverses received bytes during DMA using a separate buffer and a 33-byte lag.
  It does not validate completed lines with DMATCR. The useful principle is
  overlapping work; this is not proof of safety for K-UI's proposed in-place
  completed-line approach. Both use CPU dummy feeding with receive DMA.
- [Game SD CRC policy](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/dev/sd/sd.c#L651-L694)
  uses the file's unconditional DISCARD_CRC16 definition to consume incoming
  CRC bytes without checking. The [runtime mount](https://github.com/DC-SWAT/FatFs/blob/0441821e59cafc492990707e6429613e4630668f/fatfs/src/dc_bdev.c#L217-L253)
  also defaults to CRC checking off, with a build opt-in. The KOS driver
  supports checking when requested. This does not establish the exact settings
  behind the blog's 1.5 MB/s claim. K-UI keeps per-block CRC.
- [Game request routing](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/syscalls.c#L559-L616)
  treats SD reads of at least 100 game sectors as loading and performs the
  entire request synchronously. Smaller requests can use configurable chunks.
  That can affect loading beyond raw transfer rate; K-UI keeps bounded steps.
- [Speedtest](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/applications/speedtest/modules/module.c#L443-L642)
  uses 256 KiB filesystem requests and a separate 512 KiB raw read, with the
  video thread stopped. K-UI's 64 KiB filesystem calls are a different recipe.
  K-UI's pattern verification is outside the timed f_read interval.
- [Multi-block writes](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/dev/sd/sd.c#L831-L845)
  issue ACMD23 before CMD25. This remains a card-dependent write experiment
  for K-UI; it would not explain DOA2 read stalls. Both readers already use CMD18.

## Independent correction: repeated module-wake delay

K-UI's retail reader acquires and releases SCI on each 2–8-game-sector step.
When the saved STBCR MSTP0 bit is set, acquisition previously executed a
200,000-iteration dt/bf delay after clearing that bit. Release restores the bit,
so the delay could repeat for every step. Its comment guaranteed at least
1 ms, not an exact measured duration. The runtime holds its SCI lease across
file calls; its DMA phase measurements do not expose this repeated game cost.
The actual standby state during DOA2 remains unmeasured.

The [Renesas SH7750/SH7750S/SH7750R hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware),
Rev. 7.02, Table 9.1 (p260), keeps the CPU and CPG operating during module
standby. Section 9.6.2 (p273) exits it by clearing the module-stop bit. The
STBCR description and SCI usage notes specify no millisecond startup wait for
this action. This is distinct from whole-chip standby and clock restart.

The correction replaces only that delay with a volatile STBCR readback.
Readback is conservative write confirmation, not an extra requirement claimed
from the manual. Figure 15.18 (p709) requires at least one serial bit after
programming BRR; the existing BRR settling delays remain. Register restoration,
channel ownership, CRC, bounded failures and game pacing are unchanged.

The existing SCI ASan/UBSan suite passes; it repeatedly acquires with MSTP0 set,
checks ownership refusal, data/CRC, failure cleanup and exact restoration.
Leak detection was disabled for this local run because the container prevents
LeakSanitizer's process inspection; address/undefined checks remained enabled.
Full native normal/benchmark audits pass at the existing limits: SCI payload
11,144 bytes, memory end 0x8c00bae8 (24 bytes free), stack 1,180/1,232 bytes.
The prior word-reversal commit passed the complete hosted filesystem suite;
this source-only correction uses the workflow's existing focused console gate.
Console speed and behavior acceptance remain pending.

## Game-specific measurements still needed

The runtime's DMA counters do not establish DMA use during a game. An occupied
or unacknowledged channel 1 can make the resident select polling. The image
buffer itself is aligned and qualifies for DMA.

The normal budget remains two game sectors; up to eight require 12 observed
frames without a framebuffer-address change. An unchanged picture can still
alternate buffers. At one EXEC per 60 Hz frame, two 2048-byte sectors provide
only 240 KiB/s, irrespective of faster serial transfer. There is no separate
artificial seek/completion timer in the service.

After timing the same first-fight load, A+B+X+Y+Start shows launch-total counters
briefly and then reboots. Record that screen on video or photograph it.
SECTORS READ divided by READ STEPS gives actual mean batch size. PACED STEPS
counts enlarged budgets, including short request tails, and cannot alone prove
eight-sector transfers. The next experiment should distinguish batching,
acquisition and actual DMA eligibility before broadening the pacing policy.
