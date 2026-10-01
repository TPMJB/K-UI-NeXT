# Diagnostics storage tests

Install the updated `/KUI/runtime.kui`, then open **Diagnostics** from the main
menu and press **R** for **Storage tests**. This update works with the existing
boot CD and FAT32/exFAT card. No reformat is needed.

The target is the device selected at boot (SCIF, SCI or IDE/CF). These presets
do not read `bench.cfg` and do not fall back to another device. The original
Diagnostics disc test (A), storage integrity check (X) and save log (Y) remain
available; **Advanced benchmarks** opens the older `bench.cfg` workflow.

## Presets and controls

| Preset | Temporary file per cycle | Request size | Length |
| --- | --- | --- | --- |
| Quick | 4 MiB | 64 KiB | 1, 3 or 5 verified cycles |
| Compare | 16 MiB | 16, 64 and 256 KiB | 1, 3 or 5 samples at each size |
| Soak | 16 MiB | 64 KiB | 5, 15, 30 or 60 minutes |

Quick with one repeat is the default. Soak finishes its current verification
cycle when it reaches the time target, so the actual duration can be longer.
Allow at least the temporary-file size plus 1 MiB free. Filesystem scanning
and saving add time beyond the measured transfer calls.

Use the D-pad to select/change options. An optional **Card nickname** helps
identify results; it is a label you enter, not a detected card model. **Start
test** and **Repeat last test** show the selected recipe for confirmation.
Menu music pauses for the run, then resumes if it was playing. CD audio stops.
The display uses a 2 Hz refresh limit while measuring.

**B** requests a safe stop. Keep the card connected until the worker finishes
cleanup and saves the stopped result. A stopped run is never marked Passed.
Ordinary storage actions are locked while a test runs. Power off before
changing adapters or wiring.

Each run creates a new `/KUI/tests/tNNNNNN/` folder. Its `scratch.bin` contains
a pattern tied to byte position and cycle number. Each cycle writes, flushes,
closes, unmounts, remounts, then rereads and compares every byte. The scratch
file is removed on completion or normal stop. Existing files are never used
as test data. After power loss, an incomplete run is shown as interrupted;
its scratch file may remain in that run's folder.

## Reading and comparing results

The summary shows verified read/write rates, repeated-sample range, worst
read/write call, approximate p95 upper bound, verification outcome and baseline
comparison. Left/Right changes the displayed request size for Compare.
**X Details** shows build, transport, filesystem/cluster size, elapsed time,
verified bytes and error information. A failed save is visible on screen.

**History and baseline** lists the newest eight runs. A opens a result; Y makes
a saved passing result the baseline; X reloads history. All older reports remain
on the card, and a selected baseline remains available even after it leaves
the newest eight. Baseline selection survives reboot. Copy the test folders
with the card if you need their history on a different card.

Comparison requires passing runs with matching recipe, filesystem, cluster
size, music state and display rate. Inactive recipe options are ignored.
Build, transport and nickname may differ and are shown alongside the comparison,
so driver builds and adapters can be compared. Other variables such as card
free space, fragmentation and temperature still matter; the comparison is not
a claim that those conditions were identical.

For an initial session: run Quick once, then Compare with three repeats, select
that result as the baseline, and run a 15-minute Soak. After changing a build or
adapter, repeat the same Compare recipe. Power-cycle between sessions when you
want the same starting conditions.

## What is measured and saved

- Rates time the filesystem read/write calls. Pattern generation and checking
  are excluded; flush time is saved separately. Rates are not raw bus bandwidth
  or end-to-end loading speed. Remounting resets filesystem state but cannot
  guarantee a card's internal cache is cold.
- Quick/Compare report the median of complete verified samples for each request
  size, plus their range. Soak combines all complete verified cycles.
- Every measured call contributes to latency statistics, including calls after
  the first 512. A bounded log2 histogram gives an approximate p95 **upper bound**;
  exact minimum/maximum and counts over 20 ms and 100 ms are also saved.
- Error details distinguish I/O, timeout, CRC and command rejection where the
  transport exposes them. SCI may include the command/response; unsupported
  detail is left absent. Reports retain failure phase, byte offset and FatFs
  status. Cleanup failure is shown separately.

Each completed save contains `result.json`, `result.csv` and a versioned,
CRC-validated `result.bin` used by History. The `STARTED` marker permits detection
of interrupted runs. JSON includes metadata, all samples, latency counters and
errors; CSV has one row per complete sample. Reports omit unfinished samples
from rates while retaining whole-run counters and the failure outcome. Run IDs
do not depend on the console clock.

The first [SCIF soak baseline](evidence/scif-soak-baseline-2026-10-01.md) passed
on 2026-10-01: 21 cycles, 336 MiB written/verified and zero reported errors.

Host checks cover real FAT32/exFAT images, corruption and stale-cycle data,
short I/O, cancellation, flush/close/remount errors, scratch ownership and
interrupted result/baseline writes. SCI hardware validation is still required.
These tests exercise runtime filesystem I/O;
retail Games readers are separate programs and are not benchmarked here.
