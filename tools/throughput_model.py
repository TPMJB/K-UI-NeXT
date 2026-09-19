#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture-throughput model for K-UI on the single-CPU Dreamcast.

Feed it what the bench measured and it predicts what each candidate change
would do to a capture, so a change can be judged before it is written.
docs/experiment-plan.md says which measurements to feed it and how to read the
result.

Model: a capture chunk passes through the drive, the SD card, SHA-256, CRC32
and (data tracks) the sector EDC check. Everything except the drive itself is
CPU-bound on this machine (SD I/O is a bit-banged serial loop, the hashes are
software), and it all shares one CPU with the UI thread. Two things the bench
cannot see directly are exposed as parameters, and the census/poll lines the
new bench prints measure them:

  --ui-share  fraction of the CPU the UI thread takes during today's
              measurements (the `ui_pct` of the `BENCH cpu ... uihz=full`
              lines). Every CPU-bound stage was measured with that much of the
              machine missing, so its true cost is the measured cost times
              (1 - ui_share) once the UI is quiet.
  --opt-cpu   fraction of today's optical time that is CPU work (PIO transfer
              and firmware servicing) rather than the drive being busy. Comes
              from the poll histogram: time in long poll calls over total.

Per chunk, stage times add when the stages run one after another on one
worker. When the drive read overlaps the CPU work (a second thread, or DMA)
the chunk takes the longer of the two instead. Overlap results are IDEAL upper
bounds: they assume perfect scheduling and no cost to switching.
"""
import argparse

# What the hardware has measured (docs/benchmarks.md, docs/evidence/*.json).
# KiB/s with today's UI running. optical is the high-density DATA rate; an
# audio track is a different regime, see --optical.
MEASURED = dict(
    optical=1450.0,   # 32-sector PIO reads, data track, outer radius
    sd_w32=785.0,     # SD write, 32-sector chunk
    sd_w128=840.0,    # SD write, 128-sector chunk
    sha=1760.0,       # SHA-256, 111 cycles/byte at 200 MHz
    crc32=9300.0,     # 21 cycles/byte
    edc=26000.0,      # sector EDC, data tracks only
    sd_r=458.0,       # SD read with software CRC16 (resume and verify passes)
)


def ms_per_kib(rate):
    return 1000.0 / rate


def stage_ms(ui_share, opt_cpu, m, chunk128=False):
    """Per-KiB time of each stage with the UI thread NOT running.

    The measured costs include the UI's share, so a CPU-bound stage's true cost
    is the measured cost times (1 - ui_share)."""
    a = 1.0 - ui_share
    optical = ms_per_kib(m["optical"])
    return dict(
        drive=optical * (1 - opt_cpu),      # the drive itself; the UI does not slow it
        opt_cpu=optical * opt_cpu * a,
        sd=ms_per_kib(m["sd_w128"] if chunk128 else m["sd_w32"]) * a,
        sha=ms_per_kib(m["sha"]) * a,
        crc32=ms_per_kib(m["crc32"]) * a,
        edc=ms_per_kib(m["edc"]) * a,
    )


def predict(ui_share, opt_cpu, m=None, *, ui_quiet=False, chunk128=False,
            sha_in_loop=True, crc_in_loop=True, overlap=None):
    """KiB/s for a scenario. overlap: None (stages in series), 'pio' (drive
    time hidden behind CPU work, PIO transfer still costs CPU: a second thread)
    or 'dma' (drive time and transfer both hidden)."""
    m = m or MEASURED
    s = stage_ms(ui_share, opt_cpu, m, chunk128)
    if not ui_quiet:   # UI still running: every CPU stage stretched back to what was measured
        a = 1.0 - ui_share
        for key in ("opt_cpu", "sd", "sha", "crc32", "edc"):
            s[key] /= a
    cpu = s["sd"] + s["edc"] + (s["sha"] if sha_in_loop else 0.0) + (s["crc32"] if crc_in_loop else 0.0)
    if overlap is None:
        total = s["drive"] + s["opt_cpu"] + cpu
    elif overlap == "pio":
        total = max(s["drive"], s["opt_cpu"] + cpu)
    elif overlap == "dma":
        total = max(s["drive"], cpu)
    else:
        raise ValueError(f"unknown overlap {overlap!r}")
    return 1000.0 / total


SCENARIOS = [
    ("today (chunk 32, UI full, SHA+CRC in loop)", dict(ui_quiet=False)),
    ("UI quiet", dict(ui_quiet=True)),
    ("UI quiet + chunk 128", dict(ui_quiet=True, chunk128=True)),
    ("UI quiet + chunk 128 + SHA out of loop", dict(ui_quiet=True, chunk128=True, sha_in_loop=False)),
    ("  ...same + second thread (ideal)", dict(ui_quiet=True, chunk128=True, sha_in_loop=False, overlap="pio")),
    ("  ...same + DMA (ideal)", dict(ui_quiet=True, chunk128=True, sha_in_loop=False, overlap="dma")),
    ("UI quiet + chunk 128, SHA kept + second thread", dict(ui_quiet=True, chunk128=True, overlap="pio")),
]


def table(ui_shares, opt_cpu, m=None):
    lines = []
    head = f"{'':50}" + "".join(f"{f'UI takes {int(u * 100)}%':>15}" for u in ui_shares)
    lines.append(head)
    for name, kwargs in SCENARIOS:
        lines.append(f"{name:50}" + "".join(f"{predict(u, opt_cpu, m, **kwargs):>15.0f}" for u in ui_shares))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--ui-share", type=float, action="append",
                        help="UI thread CPU share, 0..0.6 (repeatable). Default: 0, 0.28, 0.35")
    parser.add_argument("--opt-cpu", type=float, default=0.35,
                        help="CPU fraction of today's optical time, 0..1. Default 0.35")
    for key, value in MEASURED.items():
        parser.add_argument(f"--{key.replace('_', '-')}", type=float, default=value,
                            help=f"measured KiB/s for {key} (default {value:g})")
    args = parser.parse_args()
    shares = args.ui_share or [0.0, 0.28, 0.35]
    measured = {key: getattr(args, key) for key in MEASURED}
    for u in shares:
        # The model must reproduce the measurement it is built from, or a
        # parameter is inconsistent with it (shares this large are not real).
        assert 0.0 <= u < 0.6, "ui share out of range"
    today = predict(shares[0], args.opt_cpu, measured, ui_quiet=False)
    print(f"optical CPU fraction {args.opt_cpu:.2f}; today's capture reproduces as {today:.0f} KiB/s")
    print(table(shares, args.opt_cpu, measured))


if __name__ == "__main__":
    main()
