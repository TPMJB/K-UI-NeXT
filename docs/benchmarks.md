# Isolated benchmarks

This document covers the bench mode added by `bench.patch`: what it changes, how
to run a measurement session, and how to read the numbers. The goal is to
replace "run a 90-second capture and guess" with "measure each component alone
in seconds, build a model, confirm it with one capture".

## What was added

| File | Role |
|---|---|
| `include/kui/options.h`, `src/core/options.c` | `/KUI/bench.cfg` format and parser. Pure text in, struct out. Unit-tested on the PC. |
| `src/core/options_file.c` | The only FatFs-touching part of options: reads the file, hands the text to the parser. |
| `include/kui/bench.h`, `src/core/bench.c` | The four benches. Portable, talks to the drive only through a function pointer, like `capture.c`. |
| `src/dreamcast/bench.c` | Console side: connects the real drive, SD, timer and controller to the portable bench. Mirrors `src/dreamcast/capture.c`. |
| `tests/test_options.c` | Parser test; runs in `make test` under ASan/UBSan. |
| `docs/bench.cfg.example` | Starting config for the card. |

Hooks into existing files, all in `bench.patch`:

| File | Change |
|---|---|
| `src/dreamcast/main.c` | R trigger sets action 7; `worker()` dispatches it and auto-saves a report, exactly like a capture. Help text and one startup log line. |
| `src/dreamcast/disc.c` | The 2 ms PIO quantum becomes a variable with a setter; the OPTICAL report prints the live value. |
| `src/dreamcast/capture.c` | One call to `kui_options_refresh()` at the top, so every capture report carries the OPTIONS lines too. |
| `src/dreamcast/platform.h` | Declarations for the above. |
| `config/ffconf.h` | `FF_USE_EXPAND 1`, needed for the preallocation bench. |
| `Makefile.dc`, `Makefile` | Link the new objects into the SD runtime; add the parser test. |

`src/core/capture.c`, the ripper itself, is untouched. That is deliberate: this is
measurement, not a change to how rips work.

## Install

You do not need the Dreamcast toolchain on your PC; CI builds exactly as it does
today. You need git and the `kui-bench.zip` file. Each command below does one
thing; the comment says what.

```sh
# 1. Get a copy of the repository on your PC (skip if you already have one).
git clone https://github.com/TPMJB/K-UI-NeXT.git
cd K-UI-NeXT

# 2. Start from the branch these changes were written against, and make a new
#    branch for this work. A branch is a named line of work; if anything goes
#    wrong you delete it and the original is untouched.
git checkout milestone/independent-diagnostic
git checkout -b milestone/bench

# 3. Unpack the zip INTO the repository folder. The zip's folders (include/,
#    src/, tests/, docs/) match the repository's, so the new files land in
#    the right places. Nothing in the zip overwrites an existing file.
unzip /path/to/kui-bench.zip

# 4. Apply the seven small edits to existing files. git reads bench.patch and
#    changes exactly those lines. If it prints "patch does not apply", stop
#    and paste the message; it means a file changed since the patch was made.
git apply bench.patch
rm bench.patch

# 5. See what changed. New files show as "??", edited files as " M".
git status

# 6. Record it. --all stages new and edited files together.
git add --all
git commit -m "Isolated benchmarks and bench.cfg"

# 7. Send the branch to GitHub. This is what triggers the CI build.
git push -u origin milestone/bench
```

Then on GitHub, open **Actions**, wait for the run on `milestone/bench`, and
download `sd-update` as before. The `host` job runs the parser test for you.

If you prefer a window to a terminal, GitHub Desktop does steps 1, 2, 5, 6
and 7 with buttons; steps 3 and 4 still need the command line (or you can
make the seven edits from `bench.patch` by hand in an editor: every hunk is a
few lines).

Copy `docs/bench.cfg.example` to the card as `KUI/bench.cfg`. Optional: add
it to the `sd-update` file list in `tools/package.py` (the line that copies
`verify_dump.py` and friends) under the name `KUI/bench.cfg.example`, so future
updates ship it without ever overwriting a config you have edited.

## Run a session

1. Boot the SD runtime with a known-good disc in the drive.
2. Press the **R trigger**. The log shows `OPTIONS ...` lines, then the TOC, then
   `BENCH ...` lines as each measurement finishes. Total time with the default
   config is about two minutes. B stops safely; lines already printed stay valid.
3. The report auto-saves to `/KUI/probes/pNNNN/diagnostics.txt` when the bench
   ends. Wait for READY.
4. **Run it again.** Two runs per config gives a variance estimate; one run gives
   a number you cannot trust.
5. Edit one line in `bench.cfg`, put the card back, repeat.

Order inside a run is optical, then hash, then SD. Optical goes first so the
drive is always in the same state: fresh INIT plus one discarded warm-up read.

What to hold fixed between runs you intend to compare: same card, same disc,
same `optical_fad`, and write the card and drive into `note=` so the report
says so.

## Read the lines

Every measurement is one line of the same shape:

```
BENCH optical fad=45150 sectors=4096 retries=0 bytes=9633792 us=6510000 kib_s=1445.1
BENCH hash sha256 cyc_b=132 bytes=9633792 us=6380000 kib_s=1474.5
BENCH hash crc32 cyc_b=22 bytes=9633792 us=1090000 kib_s=8630.2
BENCH sd expand chunk=32 expand=1 us=41000
BENCH sd write chunk=32 expand=1 bytes=8355840 us=9980000 kib_s=817.6
BENCH sd sync chunk=32 expand=1 us=12000
BENCH sd read chunk=32 expand=1 bytes=8355840 us=17820000 kib_s=457.9
```

(Illustrative values, taken from your MDK2 capture report.)

- **optical** — the drive's steady read rate at a fixed radius. `retries` above 0
  means the range has a weak spot; pick another range rather than compare
  runs with different retry counts. The `OPTICAL capture` block that follows
  is disc.c's submit/poll/wait breakdown for these same reads.
- **hash** — pure CPU. `cyc_b` is cycles per byte on the 200 MHz SH4, which is
  the number to compare hash implementations by. Bytes per second here also
  tells you the ceiling of any pipeline that hashes inline.
- **sd write / sd read** — the card through the serial adapter, no disc
  involved. Compare the same `chunk=` with `expand=0` and `expand=1` to see
  what contiguous preallocation buys; compare chunks at the same `expand=` to
  see what larger writes buy. `sd expand` is the one-time cost of
  preallocating; `sd sync` is the flush at close.

## Turn the lines into a prediction

Capture stages run one after another on a single worker, so their rates
combine reciprocally, not additively:

```
predicted_kib_s = 1 / (1/optical + 1/sd_write + 1/sha256 + 1/crc32)
```

With the illustrative numbers above: 1/(1/1445 + 1/818 + 1/1474 + 1/8630)
= 370 KiB/s, which is the 369 KiB/s the MDK2 capture actually achieved. When
your benches predict your capture to within a few percent, the model is
complete and you can evaluate any change on the bench alone. When they do
not, there is a cost the capture is paying that none of the benches measure,
and that gap is the next thing to instrument.

Some things the model already tells you, before you change anything:

- Dropping SHA-256 from the loop: 1/(1/1445 + 1/818 + 1/8630) = 492 KiB/s.
- Overlapping the optical read (DMA) with everything else: max(optical, rest)
  instead of the sum, so 1/(1/818 + 1/1474 + 1/8630) = 495 KiB/s.
- Both: 1/(1/818 + 1/8630) = 747 KiB/s.
- The SD write rate is the ceiling nothing can pass.

## The SD transport experiment

`src/dreamcast/sd.c` asks KOS for a transport by name instead of calling plain
`sd_init()`, which hardcodes `SD_IF_SCIF` with CRC checking on. Two knobs drive it:

| bench.cfg | KOS call | What it does |
|---|---|---|
| `sd_if=scif` | `SD_IF_SCIF` | SPI emulated by bit-banging the SCIF pins. Works on the common jj1odm-style adapter. |
| `sd_if=sci` | `SD_IF_SCI` | SH4 synchronous serial, DMA capable. Needs an adapter wired to the SCI pins. |
| `sd_crc=on/off` | `check_crc` | Software CRC16 verification. KOS computes it on writes regardless, so this only moves the read rate. |

Both keys take a list, and the SD benches are swept over every combination in
one run, dropping and reopening the SD link between each. `sd_if=scif,sci` with
`sd_crc=on,off` measures all four configurations from a single boot, with no
card removal. Every SD line then carries its own `if=` and `crc=`, so the lines
stand alone:

```
BENCH sd write if=scif crc=on chunk=128 expand=0 bytes=8128512 us=9449030 kib_s=840.0
```

Run time multiplies, so pair a transport sweep with one chunk size and
`expand=off`; four transports at `chunks=128`, `sd_mib=8` takes about two
minutes. Optical and hash run once, before any mount, and are unaffected.

A setting that cannot be opened is skipped rather than measured: if SCI will
not initialise, the platform falls back to SCIF, the log says
`BENCH sd if=sci skipped: fell back to scif`, and no line is recorded under the
wrong name. The same guard drops a configuration whose actual transport was
already measured, so a fallback can never produce two contradictory `if=sci`
rows.

If `sci` is not wired on your adapter, `sd_init_ex` fails, the log says so, and
the run falls back to `scif` and continues. Nothing is at risk, and the
`SD transport:` line in every report names what the numbers were actually
measured on, so a fallback can never be mistaken for a result.

Reading `bench.cfg` always happens over `scif` with CRC on, before the file's
own setting is applied. That is deliberate: a card cannot be made unreadable by
the setting stored on it.

What the outcomes mean:

- **`sci` works and writes jump well above ~820 KiB/s** — the write ceiling was
  the bit-bang driver, not the card. Every prediction in this document needs
  redoing with the new number, and the SD write stops being the thing worth
  hiding work behind.
- **`sci` works but writes barely move** — ~820 KiB/s really is the card or the
  adapter, and the pipeline restructuring is the remaining path.
- **`sci` fails to initialise** — your adapter is SCIF-wired. The `sd_crc=off`
  result still matters on its own for the read path.
- **`sd_crc=off` lifts reads much above ~460 KiB/s** — software CRC16 is what
  makes reads slower than writes, which mostly costs you on resume, where
  prefix verification reads back everything already written.

## The experiment layer

[experiment-plan.md](experiment-plan.md) is the plan: what is known, which
question each run settles, and what each outcome means. This section is the
reference for what was added to the bench to carry it out. **All of it is off by
default, with one deliberate exception: the UI is capped at 2 redraws a second
while an operation runs (`ui_hz=2`).** Trip 1 measured the unthrottled UI taking
about a third of the CPU ([evidence](evidence/t1-ui-census-2026-09-19.json)), and
2 Hz recovers nearly all of it (SD write +43%). `ui_hz=full` restores the old
loop. With no other new keys in `bench.cfg` the bench runs the same sections in
the same order as before and adds only the `BENCH cpu` and `BENCH lat` lines and
the CRC16 lines in the hash section, described below.

**Two guards on a mis-aimed run:** before a capture section the bench checks `capture_fad` against the
disc's TOC and warns (`BENCH WARNING: ...`) if it is not inside a track of the configured `capture_type`,
or is not on the disc at all; and a run in which every section was skipped ends
`BENCH complete but NOTHING was measured` rather than `BENCH complete`.

**Saving `bench.cfg`:** plain text, UTF-8 or ANSI, with LF or Windows (CRLF) line endings. A UTF-8 byte-order mark (some editors add one) is skipped; UTF-16 is rejected and named; a bad line is reported with what it starts with, as text and as bytes, and an unknown key is named.

| Key | What it does |
|---|---|
| `ui_hz=full,8,2,0` | One full pass of the selected sections per value: the UI is capped to that many redraws per second while it measures. **Default 2**; `full` is the old unthrottled loop. Also applies to real captures, resumes and verifies (the first value). |
| `sections=optical,hash,sd,sweep` | Which sections run. |
| `sweep_chunks`, `sweep_fads`, `sweep_gap_us`, `sweep_service_us`, `sweep_sectors`, `sweep_verify` | The optical sweep: read size, start sector, idle gap after each command, spin after each firmware poll, and a byte-identity check across read sizes. |
| `sweep_mode=pio,dma` | **EXPERIMENTAL.** Read with PIO (as capture does) and/or GD-ROM DMA. Needs an even sector count; skips `sweep_service_us`. Details below. |
| `sweep_spin=off,on` | Run a competing CPU-bound thread during each point and report the CPU it got (`free=`) and how the read fared. |
| `sd_bytes=65536,131072,...` | Extra SD write sizes in bytes, to test alignment with the 128 KiB clusters. |
| `sections=capture`, `capture_hash`, `end_readback`, `resume_check`, `sample_readback`, `capture_sectors`, `capture_fad`, `capture_type` | The **real capture engine**, at every combination, on the real disc, with each job deleted afterwards. The same keys are options of a real capture (its first value of each list). |

New lines (formats and how to read them: experiment-plan.md section 5):

```
BENCH pass 2/4 uihz=8
BENCH cpu sha256 uihz=8 wall=4580 wk=4390 ui=175 oth=12 ui_pct=3.8
BENCH lat write chunk=128 e=0 n=27 min=.. p50=.. p95=.. max=.. slow=0 slow_ms=0
BENCH sweep uihz=1 fad=45150 chunk=32 gap=0 svc=0 reads=64 retry=0 rd=.. min=.. max=.. bytes=.. us=.. kib_s=..
BENCH poll fad=45150 c=32 n=a/b/c/d/e
BENCH verify fad=45150 chunk=64 sectors=512 crc32=........ ref=........ OK
BENCH dma check fad=45150 sectors=32 result=OK crc32=........ pio=........ reported_bytes=75264 us=..
BENCH spin calibration: 412 iterations/ms with the worker asleep
BENCH sweep uihz=1 fad=45150 chunk=128 gap=0 svc=0 reads=16 retry=0 rd=.. min=.. max=.. mode=dma free=99.0 bytes=.. us=.. kib_s=..
BENCH verify fad=45150 chunk=128 mode=dma sectors=512 crc32=........ ref=........ OK
BENCH hash uihz=0 crc16-kos cyc_b=.. bytes=.. us=.. kib_s=..
BENCH hash uihz=0 crc16-table cyc_b=.. (also crc16-slice2, crc16-nibble)
BENCH hash uihz=0 crc16 agree=OK (4 implementations, identical results)
BENCH capture uihz=full hash=crc32 end=off sample=0 sectors=4096 result=ok bytes=.. us=.. kib_s=..
BENCH capture total hash=crc32 end=off sample=0 verify_us=0 total_kib_s=.. sampled=0 verified=0
BENCH capture parts hash=crc32 end=off sample=0 ms disc=.. edc=.. write=.. read=.. sha=.. crc=.. ckpt=..
BENCH resume uihz=full hash=crc32 check=size sectors=4096 result=ok bytes=.. us=.. kib_s=..
```
`capture` is the capture phase alone (the rip rate); `total` adds the end read-back
if one ran (the cost of a finished dump); `parts` is where the capture phase went;
`resume` times the prefix check on the job just made. A run's setup (three sample
reads) is outside every rate.

(Illustrative shapes, not measurements.) The census is the important one: every
rate in this file is bytes over wall-clock time on a single CPU that the UI
thread also uses, so `ui_pct` says how much of each measurement the stage did
not have. Captures print one for the whole operation as well.

**The CRC16 lines** (hash section, always) time KOS's own `net_crc16ccitt`, the loop
the SD driver runs over every block it writes, against three table-driven versions
in `src/core/crc16.c`, in 512-byte blocks each from state 0, on the same data.
`cyc_b` is cycles per byte. `agree=OK` says every version returned the identical
value; on `MISMATCH` ignore the timings. This is what decides whether patching KOS's
`sd.c` is worth it (a saving of S cycles a byte speeds an SD write by S/165).
`docs/bench-cfgs/t1b-crc16.cfg` runs just this in about half a minute.

**The GD-ROM DMA probe and the competing thread** (`docs/bench-cfgs/t6a-dma-probe.cfg`).
`sweep_mode=dma` reads raw sectors with the drive's DMA command instead of PIO.
It is **experimental and bench-only**; the capture engine never uses it. **It is not in the
default build**: it has never run on a console and it failed to build twice on the CI, so it
compiles only with `make diagnostic KUI_EXPERIMENTAL=1` (add `KUI_EXPERIMENTAL=1` to the `make`
line in `.github/workflows/diagnostic.yml`). Without it the bench prints `BENCH dma skipped: this
platform has no GD-ROM DMA probe` and `BENCH spin skipped`, and everything else runs as usual.
Before any DMA point it does one checked read against PIO (`BENCH dma check`); a
drive that cannot do it costs one line, and DMA stays off for the run. What to know:

- It **polls** for completion instead of taking an interrupt. This runtime does not
  initialize KOS's CD-ROM subsystem (no `INIT_CDROM`), so nothing installs the DMA-end
  interrupt handler; KOS's own handler just calls `exec_server` and reads the command
  status, which the polling loop does anyway. If a drive never completes a DMA that
  way, the probe says so, and an interrupt-driven variant is the next thing to try.
- Between polls the worker **sleeps**, so the CPU is really free, but completion is
  noticed up to one 10 ms scheduler tick late: DMA rates are low by about 8% at 32
  sectors and 2% at 128. Compare at chunk 128.
- Any DMA that fails to complete, mismatches, or corrupts its guard bytes turns DMA off
  until reboot: an unfinished transfer may still own the buffer. Every wait is bounded
  (5 s), so a bad DMA cannot hang the console.
- Sector counts must be even: 2352-byte sectors make a multiple of 32 bytes only in pairs.
- `sweep_spin=on` starts a CPU-bound thread at the worker's priority for each point.
  `free=` is the share of the CPU it got, against its own rate with the worker asleep
  (`BENCH spin calibration`). During a DMA read it should be near 100%; during PIO it
  shows what an SD-writing thread would actually be given, and that point's `rd=`
  against the same PIO point without a competitor shows what it would cost the drive.

Hooks added for it: `src/dreamcast/main.c` (the redraw cap, decided by
`include/kui/ui_rate.h`; the per-thread CPU snapshot), `src/dreamcast/disc.c`
(poll-duration buckets in the OPTICAL report, and the bench-only probe read with
its own guarded buffer), `src/dreamcast/capture.c` (the whole-operation census), `src/dreamcast/bench.c`
(the competing-thread spinner and the KOS CRC16 wrapper), and `disc.c`'s
`kui_disc_read_probe_dma`. `src/core/capture.c` and `kui_disc_read_raw` are unchanged. The three firmware
callbacks that read uses (`submit`, `poll`, `pause_worker`) gained counters, a
few instructions per call, which is what feeds the poll buckets in captures. Tests: `tests/test_ui_rate.c`, `tests/test_crc16.c`, the DMA probe against a fake
firmware and a recording cache double (`test-disc dma`, `dma-fail`, `dma-timeout`,
`dma-guard`) and the shipped-config check in `tests/test_options.c` run in
`make test`; `tests/bench_image.c` drives the
whole bench against a fake drive and a real FAT32/exFAT image
(`make test-images`).

## What to test first

> The experiment plan supersedes this list for capture speed: start with its
> Trip 1. The list below remains the right order for the SD experiments alone.

1. Defaults, twice. This gives you all four ceilings and a variance number.
2. `chunks=32,64,128,256,512` with `expand=on`, `sd_mib=16`. This is the
   cheapest test of your largest cost, and the `sd read` lines will show
   whether the 458 vs 820 KiB/s read/write gap is fragmentation.
3. Any SHA-256 change, judged by `cyc_b` alone. If it does not move, stop
   optimising the hash and move it out of the capture loop instead.
4. One full capture on `optical_fad`'s track with the best settings, compared
   against the prediction. Only then decide what to change in `capture.c`.
