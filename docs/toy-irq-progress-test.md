# Toy Commander: AM and AN startup interrupt tests

AI, AJ, AK and AL produced very similar reported playback. The retained bulk
reads changed little across their DATA allowances. These two candidates test
whether startup card reception can make more progress between GD callbacks,
when the game is running, and whether that can reduce foreground work. They
are hardware experiments; neither is an established fix for the second intro.

| Profile | Startup SCI priority | Foreground engine steps | DATA blocks / timer | Purpose |
|---|---:|---:|---|---|
| AI reference | 1 | 2,048 | 24 / 12,000 ticks (15.36 ms) | Exact previously tested binary |
| AM | 7 | 2,048 | 24 / 12,000 ticks (15.36 ms) | Change interrupt priority only |
| AN | 7 | 1 | 24 / 12,000 ticks (15.36 ms) | Test reception with minimal foreground pumping |

Try **AM, then AN**, cold booting each. AM retains AI's foreground limits.
AN then limits each foreground service visit to one engine step, so continued
reception depends much more on opportunities for the interrupt handler to run.
A step can process a finite operation or make no progress; it is not one byte
and does not promise a particular duration. Each interrupt still performs one
bounded engine pump. The game can mask priority 7 too, so the higher priority
does not guarantee background progress.

AN exits after its single foreground step, before a second timer check could
run. Its displayed timer allowance does not bound that single operation's
elapsed time; the finite engine step and retained protocol waits provide the
actual bound.

The new priority applies only while the startup SCI receiver is enabled.
GD callbacks retain their existing interrupt mask. Forwarded game handlers
receive the original game vector and saved SCI priority, and the original
priority is restored at retirement. The startup receiver retires at the first
accepted PLAY and is not enabled again. **These settings cannot change DATA
reception after that retirement.**

The present captures contain no accepted DATA in report phase 1. The report
combination includes Start and can advance the game, so we cannot align the
PLAY boundary with either visible intro from those terminal reports. A frozen
report showing completed requests and no active handle does not establish the
cause or pre-report state of a visible stall.

All original audio transport, refill policy, raw-sector handling, CRC and cache
policy remain those of AI: **4 audio quanta / 12,500 ticks (16 ms)**. One DATA
block is a verified physical 512-byte card block; a raw 2352-byte image sector
supplies 2048 cooked DATA bytes. The clock runs at 781,250 ticks per second.
Limits are checked between finite operations, so one operation or cleanup may
cross a timer allowance. These are not frame deadlines or request-latency
guarantees.

## Install and capture

With power off, copy one file from `profiles/` to
`/KUI/apps/games/retail-boot.kui`. Keep the same original uncompressed Toy
Commander GDI, microSD card, display mode, K-UI 1.8.5 launcher and
**A / standard SCI reader**. Film the build line and both intros with sound.

When the second intro appears, record the elapsed time and the number of
distinct visible pictures before it holds. If the picture freezes, leave it
visible for **10–15 seconds**, then press **A+B+X+Y+Start** for the report. If
the first intro stalls, use the same wait and capture there. Record any
automatic recovery during the wait and the exact time the combination is
pressed. Keep the pre-combination held picture visible in the recording;
post-combination changes can result from Start advancing the game.

Capture before any separate deliberate skip. If both intros complete, record
that and capture at a consistent visible point afterward. The recent SCHED
records form an eight-request ring, so later activity can replace the requests
near an earlier held picture. If the report combination does not work, cold
boot and capture just before the failure, identifying that as a different
stopping point. Reports do not survive power loss.

## Report pages and comparisons

Film **SCHED 00–0B**, **DATA 00–0B**, **PILOT 00–07** and **TRACE 00–11**.
Keep headings, hexadecimal page numbers and all sixteen words readable. These
50 pages take about 100 seconds at 60 Hz or 120 seconds at 50 Hz. Continue
through TRACE 19 if later pages contain values. If the displayed later pages
are zero, filming can stop there; absent pages will remain explicitly missing
in the decode. Reports freeze before display, so longer report filming adds
no playback time to their measurements.

SCHED remains version 2 with 192 words. Budget profiles **13 and 14** identify
AM and AN. The build line and included legend also identify interrupt priority
and the foreground step limit, which are profile metadata rather than extra
wire words. Compare matched LBA/count/destination reads, their CRCs, foreground
and IRQ verified blocks, maximum foreground call time, pending gaps, and the
budget exit partition. An increased IRQ share is evidence of where reception
ran; it does not alone prove smoother video.

Original synchronous DATA counters can remain zero while the startup engine
works. Use SCHED and TRACE for that engine's progress. PILOT reports the
retained audio worker; terminal PAUSE can result from report capture. Zero
transport errors do not establish playback quality or full-image correctness.
The CRC covers only the first credited sector.

The package includes the complete current AI/AJ/AK/AL capture evidence, with
observed zeros distinguished from unseen words and explicit glyph corrections.
Exact AI and AF references are available if a direct comparison is useful;
no additional reference rerun is required. Restore
`restore/R-private-write-through.kui` to `/KUI/apps/games/retail-boot.kui` when
finished.
