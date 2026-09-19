# Experiment plan: where the capture time goes, and what to change

The goal is to decide what to build next for capture speed *before* building it.
This document says what is known, what is only a hypothesis, which bench run
settles each open question, and what each outcome means for the work. The
measuring tools are in [benchmarks.md](benchmarks.md); ready-made config files for
every step are in `docs/bench-cfgs/`.

Status of the tooling: the parser, the bench logic (against a fake drive, clock
and scheduler, on real FAT32 and exFAT images) and the UI redraw policy are
tested on the PC, and every console-side file compiles against the pinned KOS
headers. **None of it has run on hardware.** Everything below marked
*hypothesis* or *model* is exactly that until a trip confirms it.

## 1. What the data says today

There are three regimes, and they have different bottlenecks. The percentages
are wall-clock shares from `docs/evidence/` (the builds that used paired reads).

| Regime | Measured | Bottleneck |
|---|---|---|
| **High-density data track** (almost all of a normal game's bytes) | ~373 KiB/s per capture; drive alone ~1450 KiB/s | The CPU: SD write, SHA-256, PIO transfer, in series |
| **Low-density data track** (tracks 1-2, small) | 187 KiB/s paired; disc 63.2%, write 23.3%, SHA 10.7%, CRC32 2.0%, EDC 0.8% | The drive, partly |
| **Audio track** | 57.0 KiB/s paired (MDK2 track 4); disc 88.5%, write 7.2%, SHA 3.6%, CRC32 0.7% | The drive |

Two facts about the audio row matter more than they look:

- **It predates single-read capture.** Commit `f9e8958` (2026-09-17) changed
  capture from paired to single reads; the audio evidence was taken on the
  earlier build. In that run each read of a pair ran at 134.0 KiB/s (read 1) and
  125.3 KiB/s (read 2), so a single read costs about half of a pair and *at HEAD,
  audio should be roughly 2x better than 57 KiB/s* (modelled ~106 KiB/s). As far
  as the repo shows, nobody has measured it since. One capture of one audio
  track answers it.
- **Part of the drive's time looks like per-command overhead.** A 32-sector
  command took 548.6 ms on average against 426.7 ms for 32 sectors at 1x CD speed
  (75 sectors/s): about 122 ms extra per command on average, and the slowest
  command took 2.29 s. Only 4.3% of command time was CPU in the firmware poll
  calls. If the extra time is per command, bigger reads amortise it (Trip 2e). If
  it is per sector (the drive re-reading marginal audio sectors, which the 2.29 s
  outlier hints at), they will not, and the lever is the drive's own retry
  behaviour instead (section 7).

What that means for priorities. For a 1.19 GB disc (Sword of the Berserk's
size), computed from the rates above:

| If the disc were all... | at | takes |
|---|---|---|
| data | 373 KiB/s (today) | 51.7 min |
| data | 680 KiB/s (DreamShell's reported figure) | 28.4 min |
| audio | 57 KiB/s (paired build) | 5.6 h |
| audio | ~106 KiB/s (modelled, single reads) | 3.0 h |
| audio | ~141 KiB/s (drive ceiling) | 2.3 h |

For a music-heavy disc, data-path work barely moves the total: audio dominates. For a
disc that is mostly one big data track, audio is a rounding error and the
data-path work is everything. **Which one your discs are decides what is worth
building.** The TOC lines at the top of any report give each track's type and
size; the split for a disc is one glance.

## 2. Hypotheses, and what would refute each

**H-UI: the UI thread takes 20-35% of the CPU during operations.** Model, not
measurement. `main.c`'s loop runs at the same priority as the worker, redraws
the whole screen in software, and busy-waits for vertical blank inside
`vid_waitvbl` (verified in the pinned KOS: it is `while(!(PVR_GET(...)))`, not a
sleep). Under KOS's 10 ms round-robin, a simulation of that loop takes 22-34% of
the CPU for draw costs of 4-28 ms, and the 28% of optical wait time that the
earlier analysis called "slack" fits. If true, every CPU-bound number measured so
far was taken with a fifth to a third of the machine missing, and *merely
throttling the redraw* would raise them with no restructuring. If false, the
measured numbers are the machine's numbers and only overlap can help.
*Refuted by:* `ui_pct` under ~3% on the hash bench (Trip 1).

**H-overlap: hiding the drive wait behind SD/hash work would help by a large
factor.** True in the model only if the wait is genuinely wait (drive busy, CPU
spinning) rather than PIO work. The model puts a second thread at +20% to +52%
over the best serial loop, depending on the CPU fraction of the optical read
(which nothing has measured) and on whether H-UI holds, and DMA at up to ~+68%.
*Settled by:* the poll-duration histogram (Trip 2a) and how long the drive
tolerates being left unserviced (Trip 2c).

**H-chunk: bigger optical commands are faster.** Today's 32-sector chunk is a
compile-time constant nobody has varied on the optical side. The SD side already
gains ~7% from 128 sectors. *Settled by:* Trip 2a (data) and 2e (audio).

**H-align: SD writes aligned to the 128 KiB exFAT cluster are faster.**
`sd_bytes` writes sizes that are not sector multiples on purpose. If aligned
wins, capture needs a small coalescing buffer that writes exactly 128 KiB at
128 KiB file offsets regardless of chunk boundaries. *Settled by:* Trip 3.

**H-audio: audio's ~122 ms per command is fixed overhead that larger commands
amortise.** *Settled by:* Trip 2e. If it holds, `KUI_CAPTURE_CHUNK` above 32
helps audio by about 20% (four times the sectors per command spreads the same
overhead over four times the data).

## 3. The model

`tools/throughput_model.py` predicts each candidate change from measured inputs.
Stages run in series on one CPU, so `time per KiB = drive + optical CPU + SD +
SHA + CRC32 + EDC`. Two parameters are exposed because the old benches could not
see them and the new ones can:

- `--ui-share` = the `ui_pct` of the hash census at `uihz=full`.
- `--opt-cpu` = share of optical command time spent in poll calls of 100 us or
  longer (the PIO transfer itself; shorter calls are the CPU spinning on a busy
  drive and count as waiting). From a sweep point: the sum of the last three
  numbers of its `BENCH poll ... ms=` line, divided by the command time
  (`bytes / rd`, converted to ms).

For the high-density data regime, with `--opt-cpu 0.35`:

| KiB/s | UI costs nothing | UI takes 28% |
|---|---|---|
| today (chunk 32, UI full, SHA and CRC in the loop) | 373 | 373 |
| UI quiet | 373 | 487 |
| + chunk 128 | 385 | 502 |
| + SHA-256 out of the loop | 494 | 631 |
| + a second thread (ideal) | 634 | 880 |
| + DMA (ideal) | 748 | 1039 |

The overlap rows are upper bounds: perfect scheduling, no switching cost. The
first four rows are serial and need no concurrency. Read across: if H-UI holds,
a quiet UI, chunk 128 and SHA out get to ~631, close to but still under 680,
without a rewrite; if it does not, only overlap (and then only DMA-class overlap)
gets there. For audio (`--optical 134 --opt-cpu 0.05`) the ceiling is the drive:
~106 today, ~114 with a quiet UI, and ~141 is the most anything can reach.

## 4. The trips

Each trip is one config file copied to the card as `KUI/bench.cfg`, then the R
trigger, then wait for READY. The report auto-saves to
`/KUI/probes/pNNNN/diagnostics.txt`. **Run each twice**; one run is not a number.
Paste the `BENCH` lines (or the file) back. B stops safely; lines already printed
stay valid.

### Trip 1: does the UI cost CPU? (`t1-ui-census.cfg`, ~4 min)

One pass of the optical, hash and SD benches per `ui_hz` value: `full`, 8, 2, 0.

Read first, in this order:

1. `BENCH cpu sha256 uihz=full ... ui_pct=` : the cleanest reading, because
   hashing never yields, so the UI gets only its scheduler share.
2. The same `sha256 cyc_b=` across the four passes. It should fall by about
   `ui_pct` from `full` to `0`. **If the census says 28% and `cyc_b` does not
   move, one of the two is wrong** and neither should be trusted.
3. `sd write kib_s` and `optical kib_s` across the passes.

| Outcome | Means | Do next |
|---|---|---|
| `ui_pct` >= 15% and rates rise as the cap drops | H-UI holds | Build the light UI (section 6, item 1), then run Trip 4 |
| 3-15% | Partly; worth the cheap fix only | Light UI, do not expect a jump |
| under 3% | H-UI is dead | Trip 2 decides the rest |
| `optical` ui_pct high but its kib_s flat | The UI only took drive-wait time; free | The UI matters for CPU stages, not the read |

The 8 / 2 / 0 spread is the dose-response: cost per redraw is
`ui ms / (hz x wall seconds)`, which is what the light UI has to fit inside.
Also read `ui_hz=2` as a candidate default: twice a second is a readable progress
line.

### Trip 2: the drive (`t2a`..`t2e`, ~1 min each)

`sections=sweep` reads sectors with the real PIO command in a bench-only buffer.
Each point prints:

```
BENCH sweep uihz=1 fad=45150 chunk=32 gap=0 svc=0 reads=64 retry=0 rd=.. min=.. max=.. bytes=.. us=.. kib_s=..
BENCH poll fad=45150 c=32 n=a/b/c/d/e      # poll calls by duration:
BENCH poll fad=45150 c=32 ms=a/b/c/d/e     #   <25us /<100us /<400us /<1.6ms /more
```

`rd` is KiB/s inside the read command only; `kib_s` includes the idle `gap`. A
sector-by-sector CRC gate runs per read size on data tracks:
`BENCH verify ... OK` or `MISMATCH`. **A MISMATCH disqualifies that read size
whatever its speed.**

- **2a chunks** (`8..128`): does bigger mean faster? Also gives `opt-cpu` for the
  model from the histogram.
- **2b gap** (0 / 2 / 10 / 40 ms between commands): the drive is idle for well over
  100 ms after every command in today's loop. If `rd` falls as the gap grows, the gap
  itself costs revolutions and back-to-back commands gain more than the model
  says; if it does not, the drive re-arms cheaply.
- **2c service** (0 / 1 / 4 / 10 ms spin after each poll): how long the CPU can be
  away from the drive mid-command. Where `rd` starts to fall is the longest slice
  of SD/hash work a *single-threaded* pipeline may do between polls. **As a rule of
  thumb, at 4 ms or more cooperative interleaving (hash a slice, poll, write a
  slice, poll) is enough and needs no thread; under 1 ms it is too fine and a
  thread or DMA is needed.**
- **2d radius** (four FADs across the big data track): the high-density area is
  constant angular velocity, so rate rises outward. Every earlier optical figure
  is one radius; this is the curve.
- **2e audio** (`8/32/128`, gap 0 / 40 ms): H-audio. Verification is off because
  raw audio can legitimately differ between reads.

The overlap decision, after Trip 2:

| Finding | Choose |
|---|---|
| service tolerance >= 4 ms and `rd` insensitive to the gap | Cooperative interleave: one thread, deterministic |
| tolerance under 1 ms, or `rd` collapses with the gap | Second thread, or DMA |
| most optical time in the >= 100 us buckets | Little wait to hide; only DMA removes the transfer CPU |

### Trip 3: SD write size and alignment (`t3-sd-sizes.cfg`, ~6 min)

`BENCH sd write ... wbytes=` for 64 KiB to 512 KiB, with and without
preallocation, plus `BENCH lat write` with per-call min / p50 / p95 / max. A size
can win on average and still stall: `slow=` counts calls over twice the median,
`slow_ms` is the time they added, and the largest `max` is the write-behind
buffer a pipeline would have to absorb. Adopt an aligned size only if it wins by
about 5% *and* its p95 is no worse.

### Trip 4: the same question on a real capture (`t4-capture-ui.cfg`, 2 x 4 min)

Start a NEW capture, let it run ~3 minutes, press B, save. Change only `ui_hz`
(`full`, then 2) and repeat on the same disc and card. The last census line,
`BENCH cpu operation (setup+capture+verify) ... ui_pct=`, and the CAPTURE phase
average are the answer. This is the check that the bench prediction holds on the
real loop. It will not reach a long audio track in three minutes; for audio on
the real loop, resume a capture that is already inside one (as the MDK2 evidence
run was) and repeat the two settings, and use Trip 2e for the drive alone.

### Trip 5: the capture engine's own choices (`t5a`, `t5b`, `t5c`)

`sections=capture` runs the **real capture engine** on `capture_sectors` sectors of
your disc at every combination of `capture_hash`, `end_readback`, `sample_readback`
and (for the resume timings) `resume_check`, once per `ui_hz`, deleting each job
afterwards. Read `total_kib_s` first: it is the rip plus the end read-back, the
number that decides how long a *finished* dump takes. Then `parts` for where the
capture phase went, `BENCH resume` for the prefix-check cost, and `ui_pct`.

- **5a matrix** (~10 min): hash both/crc32 x end read-back on/off x UI full/2. Settles
  SHA-256's real cost and what the end read-back really adds.
- **5b sampling** (~3 min): `sample_readback` 0/8/32/128, CRC-only. What in-flight
  checking costs at each rate.
- **5c audio** (~3 min): the same engine on an audio track, where the drive should
  dominate and these choices should matter far less. Compare with Trip 2e.

Decision rule: if `crc32` + `end=off` + a small `sample_readback` beats the old default
by the model's margin (about 2.5x in total time on a data-heavy disc), it becomes the
default. If it does not, the model is wrong somewhere and the `parts` lines say where.

## 5. Reading the new lines

- `BENCH pass n/m uihz=...`: the UI setting every line until the next pass line
  belongs to. Every result line also carries `uihz=`.
- `BENCH cpu <what> ... wall= wk= ui= oth= ui_pct=` (ms): the scheduler's
  per-thread CPU accounting over that measurement. `wk` includes busy-polling, so
  a high `wk` in an optical measurement is not necessarily work. It has 1 ms
  granularity that averages out over many switches: ignore intervals under ~1 s.
  `oth` is the idle, reaper and kernel threads.
- `BENCH capture|resume ...` (Trip 5): see [benchmarks.md](benchmarks.md#the-experiment-layer).
  `capture` is the capture phase alone; `total` adds the end read-back; `parts` is the
  split by stage in ms.
- `BENCH lat write|read ... n= min= p50= p95= max= slow= slow_ms=` (us): every
  `f_write`/`f_read` in the run, up to 512 calls.
- Lines wrap at 76 characters in the on-screen log, so a long line can appear as
  two; join them before parsing.
- The log holds 768 lines. A large sweep can overflow it (the report says
  `Log truncated`); split it across runs.
- Compare only runs with the same disc, card and drive temperature, and put those
  in `note=`.

## 6. Candidate changes, in the order the evidence would justify them

Gains are for the high-density data regime and come from the model, so they hold
only if the hypothesis they depend on does.

| # | Change | Model gain | Depends on | Risk |
|---|---|---|---|---|
| 1 | **Light UI**: redraw a small status region at 1-2 Hz, no vblank spin | +30% (if H-UI) | Trip 1 | Low; the throttle is already in the tree |
| 2 | Capture chunk 128 (SD write +7%, optical unknown) | +3% | Trip 2a | Low; buffers grow to 301 KB and 602 KB |
| 3 | SHA-256 out of the capture loop | +26-28% | Decided (section 6a) | Medium; a format change |
| 4 | Overlap the drive with SD/hash work | +20-50% more, ideal | Trip 2 | Medium to high |
| 5 | Aligned, coalesced SD writes | 0-8% | Trip 3 | Low |
| 6 | Table-driven CRC16 in KOS's `sd.c` | +3-4% | none | Low; a one-line patch to the pinned KOS, not an override (`net_crc.o` also defines `net_crc32le`/`be`, so overriding one function risks a duplicate-symbol link error) |
| 7 | Faster SHA-256 in C | +3% | none | Low; compiles to ~69 instructions per round today |
| 8 | Audio: larger commands | ~+20%? | Trip 2e | Low |

**Item 3 is decided: SHA-256 comes out of the per-byte path.** The maintainer's
call, after checking what it is for: CRC32 verifies well enough for these rips,
and the earlier GD Ripper (K-UI_DS) never had SHA-256 at all. Its verification is
the design to copy 1:1 (section 6a). SHA-256 stays only in the one-time disc
identity hash (about 0.1 s). Removing it is a format change (checkpoint,
manifest schema, profile version), so it is a patch of its own.

## 6a. Verification: how the old GD Ripper checked in seconds

The old ripper's end-of-rip verification took seconds because **it never re-read
the files.** Its report says so in words: `hash_origin: disc stream / saved
checkpoint (no storage read-back)`. It took the CRC32 accumulated during the rip
(kept in a CRC journal, `gd_crc_checkpoint`, so a resume did not re-read either)
and looked up `(track, size, CRC32)` in a bundled Redump and TOSEC catalogue
(1,245 games, 9,545 tracks; one streaming pass over two small files). A slower
"storage read-back" mode existed separately.

That is the "stream verification" of this project's discussions: the running
CRC32 *is* the verification, and the catalogue is the independent reference that
makes it mean something. It also shows why the console's end pass is the wrong
place for the exhaustive check. A re-read proves only that the card holds what
the drive returned; a catalogue match proves the bytes equal the canonical dump.

**It works on K-UI-NeXT's dumps today.** The complete Sword of the Berserk
capture in `docs/evidence/` matches TOSEC on all three tracks, sizes and CRC32s
identical (track 1 `bcec7767`, track 2 `0ff934e3`, track 3 `2cfb5dcb`), and the
Redump catalogue's USA entry for track 3 carries the same CRC32. So the capture
format, including the track boundaries the `typegap150` profile chooses, is
canonical for that disc, and the check the report has always said was missing
("No independent reference compared") costs a second. The MDK2 evidence run
cannot match yet: the Redump catalogue lists only MDK2's track 31, which that
stopped job never reached.

The end pass matters more than it looks. On the Sword disc the model puts it at
about half the whole job in every scenario (55 min after a 52 min rip today; 44
after 41 with SHA out), because it is bound by SD *read* speed (~458 KiB/s),
which is slower than SD write. Dropping SHA trims it by a fifth; only not doing
it on the console removes it. With SHA out and a catalogue check in place of the
end pass, the whole job is about 41 min instead of about 107 (model).

What is built and what is next:

| Step | State |
|---|---|
| Catalogue lookup at the end of every capture and verify (`src/core/known_dumps.c`, `data/known-dumps/`, packaged to `KUI/`) | **Built, host and image tested.** Additive: nothing removed |
| Drop per-track SHA-256: `capture_hash=crc32` (checkpoint flag, schema 2 manifest, `verify_dump.py` accepts both) | **Built as an option**; default unchanged |
| Skip the automatic end pass: `end_readback=off` (CRC-only jobs; Verify stays as the read-back mode) | **Built as an option**; default unchanged |
| Resume without re-reading the prefix: `resume_check=size` (CRC32 continues from the checkpoint; today 212 s per 72 MiB, about 50 min for a resume at 90% of Sword) | **Built as an option**; default unchanged. It cannot see a corrupted prefix (tested and documented) |
| In-flight sampled read-back: `sample_readback=N` | **Built as an option** |
| Measure all of it on the real engine: `sections=capture` (Trip 5) | **Built** |
| PC-side exhaustive check | Exists: `verify_dump.py` recomputes CRC32 from the files |

The approach, agreed: build every candidate as a runtime option that defaults to
today's behaviour, measure them all on the real engine in one boot (Trip 5), and
choose defaults from the numbers. Not yet built as options, and why: a runtime
capture chunk size and coalesced/aligned SD writes each restructure a buffer path
(the guarded disc read buffers; the write path with its checkpoint ordering), and
Trips 2a and 3 already measure their two halves in isolation. Their results say
whether either is worth that restructuring. The overlap architectures (cooperative
interleave, a second thread, GD-ROM DMA) are larger still, and Trip 2c decides which
of them is even viable.

Two limits to keep in mind. A whole-track CRC cannot locate a bad sector; that is
why the old ripper also checked each data sector's sync, address, EDC and ECC,
and re-read only flagged sectors. And on audio, single reads mean nothing checks
the read itself, so a catalogue match (or a second dump) is the check there.

## 7. Dead ends and corrections, so they are not retried

- **SCI/DMA SD transport.** Failed to initialise on hardware (errno 11): the
  adapter is SCIF-wired, so DMA for the *SD card* is ruled out. That leaves the
  SD side CPU-bound, so overlap can only come from hiding the drive behind it
  (a thread, cooperative interleaving, or DMA on the *GD-ROM* read, a separate
  and untested option).
- **Sending CRC 0xFFFF on writes.** An earlier analysis proposed it
  (~939 KiB/s). **It is wrong.** KOS's `kernel/arch/dreamcast/hardware/sd.c`
  (line 433) sends CMD59 with argument 1 at init, so the card *verifies* every block's CRC16, and
  `write_data()` always computes and sends one. `check_crc` governs only the read
  side. Sending 0xFFFF would fail every write block. Skipping the check would also
  need CMD59 with argument 0, which removes the card's own wire-level protection
  of every write. A faster CRC16 (item 6) recovers most of the time and keeps it.
- **The 28% "slack".** The earlier analysis called the worker's yielded time idle
  slack a second thread could reclaim. It was the UI thread running. That is the
  reason Trip 1 exists.
- **Drive speed.** The pinned KOS has no speed API. The BIOS has `CD_CMD_SET_MODE`
  (31) with four words `[speed, standby, read_flags, read_retry]` and
  `CD_CMD_REQ_MODE` (30), which writes those four words to a caller buffer
  (layout from Flycast's BIOS emulation, `core/reios/gdrom_hle.cpp`); the same
  code base notes speed "doesn't seem to be settable, or perhaps not for
  GD-Roms". Not attempted. If audio turns out to be the priority and Trip 2e shows
  the drive is not the limit, a read-only `REQ_MODE` query is the safe first step.
  `read_retry` is the more plausible lever (it may explain the 2.29 s outlier
  commands), but lowering it trades data quality for speed and needs a decision.

## 8. Questions only you can answer

1. **What share of your discs' bytes is audio?** This decides whether the data
   path or the audio path is where the hours go.
2. **Which regime was DreamShell's 680 KiB/s measured in, and what does it get
   on audio?** If it rips audio well above ~140 KiB/s, the drive has headroom
   this project has not found.
3. ~~Is SHA-256 in the verify pass acceptable?~~ Answered: SHA-256 is not
   needed; CRC32 is enough (section 6a).
4. **How much on-screen progress do you need during a capture?** That sets the UI
   rate the light UI has to fit inside.

## 9. Assumptions worth checking

- The UI scheduling simulation models KOS's round-robin and the loop in
  `main.c`; it shows the hypothesis is consistent with the code, not that it is
  true.
- `ui_hz=0` freezes the screen while an operation runs. B still works (the
  controller is polled every loop), but this is by code reading and the PC-side
  policy test, not a hardware check.
- The census reads KOS's per-thread CPU time (`thd_get_cpu_time`). It has 1 ms
  granularity and includes the running thread's current slice; both are handled,
  but neither has been observed on hardware.
- The catalogue check has never run on the console. It is tested on the PC
  against the real bundled catalogues and on FAT32/exFAT images, and every
  touched console file compiles against the pinned KOS headers.
- The host tests used a public FatFs mirror whose `ff.c` identifies itself as
  "R0.16 w/patch 2", the version `dependencies.json` pins. Byte equality with the
  pinned archive could not be checked (elm-chan.org was unreachable from the test
  environment); CI's `host` job will be the first run on the real archive.
