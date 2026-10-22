#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the bench core (src/core/bench.c) against a fake drive/clock/scheduler and
real FAT32/exFAT images, and check what it reports as well as what it does."""
from pathlib import Path
import re
import shutil
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/bench-image")
RAW = 2352


def lines(output):
    return output.splitlines()


def matching(output, pattern):
    return [line for line in lines(output) if re.search(pattern, line)]


def field(line, name):
    found = re.search(rf"\b{name}=(\d+(?:\.\d+)?)", line)
    assert found, f"{name} missing in: {line}"
    return float(found.group(1))


def rd_tenths(bytes_, cmd_us):
    """The same integer arithmetic bench.c uses for the read-only rate."""
    return bytes_ * 10000000 // (cmd_us * 1024)


def check_default(out):
    assert "BENCH complete" in out and "BENCH pass 1/1 uihz=full" in out
    assert matching(out, r"BENCH optical uihz=full fad=45150 sectors=64 retries=0 bytes=")
    assert matching(out, r"BENCH hash uihz=full sha256 cyc_b=")
    assert matching(out, r"BENCH hash uihz=full crc32 cyc_b=")
    for chunk in (32, 128, 512):
        assert matching(out, rf"BENCH sd write uihz=full if=scif crc=on chunk={chunk} expand=0 bytes=")
        assert matching(out, rf"BENCH sd read uihz=full if=scif crc=on chunk={chunk} expand=0 bytes=")
        assert matching(out, rf"BENCH lat write chunk={chunk} e=0 n=")
    # Nothing new was requested, so nothing new runs.
    assert not matching(out, r"BENCH sweep|BENCH verify|BENCH poll|wbytes=")
    # The fake census gives the UI 25% of every interval; the census must say so.
    for what in ("optical", "sdw", "sdr"):
        for line in matching(out, rf"BENCH cpu {what} "):
            assert 24.0 <= field(line, "ui_pct") <= 26.0, line
            assert field(line, "wall") > 20, line
    assert "UI_CALLS 2 UI_SEQ 1000 1000" in out


def check_ui(out):
    assert "UI_CALLS 4 UI_SEQ 1000 4 0 1000" in out
    for n, tag in ((1, "full"), (2, "4"), (3, "0")):
        assert f"BENCH pass {n}/3 uihz={tag}" in out
        assert matching(out, rf"BENCH optical uihz={tag} fad=")
        assert matching(out, rf"BENCH hash uihz={tag} sha256")


def check_sweep(out):
    points = matching(out, r"^BENCH sweep uihz=full fad=")
    assert len(points) == 12, len(points)   # 2 fads x 3 chunks x 2 gaps
    expected = {}
    for chunk in (8, 32, 128):
        reads = -(-256 // chunk)
        cmd = reads * (800 + chunk * 1500)
        expected[chunk] = (reads, cmd, rd_tenths(256 * RAW, cmd))
    for line in points:
        chunk, gap = int(field(line, "chunk")), int(field(line, "gap"))
        reads, cmd, tenths = expected[chunk]
        assert int(field(line, "reads")) == reads, line
        assert f"rd={tenths // 10}.{tenths % 10}" in line, (line, tenths)
        assert field(line, "retry") == 0 and field(line, "svc") == 0
        # Wall time is the command time plus a gap after every read but the last.
        assert field(line, "us") >= cmd + (reads - 1) * gap, line
        if gap:
            assert field(line, "us") >= cmd + (reads - 1) * gap + 0
    assert len(matching(out, r"BENCH verify fad=\d+ chunk=\d+ .* OK$")) == 6
    assert not matching(out, "MISMATCH")
    # Poll buckets only for back-to-back, normal-policy points.
    assert len(matching(out, r"BENCH poll fad=\d+ c=\d+ n=")) == 6
    assert len(matching(out, r"BENCH poll fad=\d+ c=\d+ ms=")) == 6
    assert len(matching(out, r"BENCH cpu sweep ")) == 12
    assert "BENCH poll buckets:" in out
    assert "UI_CALLS 2 UI_SEQ 1000 1000" in out


def check_corrupt(out):
    assert matching(out, r"BENCH verify fad=45150 chunk=32 .* OK$")
    assert matching(out, r"BENCH verify fad=45150 chunk=64 .* MISMATCH$")
    assert "BENCH complete" in out   # reported, not fatal: the report is the point


def check_retry(out):
    assert len(matching(out, r"BENCH sweep skipped fad=300000 chunk=\d+: warm-up read failed")) == 6
    assert len(matching(out, r"^BENCH sweep uihz=full fad=45150 ")) == 6
    assert not matching(out, r"^BENCH sweep uihz=full fad=300000 ")
    assert "BENCH verify fad=300000: reference read failed; skipped" in out
    assert "BENCH complete" in out


def check_fatal(out):
    assert "BENCH sweep warm-up failed fad=45150 chunk=8" in out and "BENCH FAILED" in out


def check_cancel(out):
    assert "BENCH stopped" in out and "lines already printed are valid" in out
    assert matching(out, r"^BENCH sweep uihz=full fad=")   # what finished is still reported


def check_noprobe(out):
    assert "BENCH sweep skipped: no readable disc" in out and "BENCH complete" in out


def check_svc(out):
    points = matching(out, r"^BENCH sweep uihz=full fad=")
    assert len(points) == 2
    slow = [line for line in points if "svc=500" in line][0]
    fast = [line for line in points if "svc=0" in line][0]
    # A service spin makes each command longer, so the read-only rate drops.
    assert field(slow, "rd") < field(fast, "rd"), (slow, fast)
    # The bucket lines describe normal servicing, so only the svc=0 point has them.
    assert len(matching(out, r"BENCH poll fad=\d+ c=\d+ n=")) == 1


def check_sd_bytes(out):
    for size, calls in ((65536, 32), (131072, 16), (1048576, 2)):
        assert matching(out, rf"BENCH sd write uihz=full if=scif crc=on wbytes={size} expand=0 bytes={size * calls} ")
        assert matching(out, rf"BENCH sd read uihz=full if=scif crc=on wbytes={size} expand=0 ")
        latency = matching(out, rf"BENCH lat write wbytes={size} e=0 n={calls} ")
        assert latency, size
        lo, p50, p95, hi = (field(latency[0], name) for name in ("min", "p50", "p95", "max"))
        assert lo <= p50 <= p95 <= hi, latency[0]
    assert matching(out, r"BENCH sd write .* chunk=32 expand=0")


def check_expand(out):
    # FF_USE_EXPAND decides whether the run happens; either way it must end cleanly.
    assert "BENCH complete" in out
    assert matching(out, r"BENCH sd expand ") or "f_expand not compiled in" in out
    assert matching(out, r"BENCH sd write .* expand=1") or "f_expand not compiled in" in out


def check_mount_fail(out):
    assert "MOUNT_FAIL_OK" in out


def check_capture_nolink(out):
    # The card will not come back: the section says so and stops. It must not have touched the
    # card (that is what crashed on the console), started a run, or left anything behind.
    assert "BENCH capture skipped: card did not come back" in out
    assert not matching(out, r"^BENCH capture uihz") and not matching(out, r"Mount failed")
    assert re.search(r"^RECONNECTS 1 LAST sci=0 crc=1$", out, re.M) and re.search(r"^RESULT 0 ", out, re.M)   # 0 = KUI_BENCH_FAILED


def check_capture(out):
    check_capture_measured_something(out)
    # The bench opens the card itself, on the default transport a real capture uses.
    assert re.search(r"^RECONNECTS 1 LAST sci=0 crc=1$", out, re.M), out
    assert not matching(out, r"Mount failed|not connected")
    combos = [(h, e, s) for h in ("both", "crc32") for e in ("on", "off") for s in (0, 3)]
    # Both read modes run: the overlapped engine must produce the same shape of result.
    assert len(matching(out, r"^BENCH capture uihz=full .* read=dma sectors=256 result=ok ")) == 8
    for h, e, s in combos:
        brief = f"hash={h} end={e} sample={s} read=pio"
        run_line = matching(out, rf"^BENCH capture uihz=full {brief} sectors=256 result=ok bytes=602112 ")
        assert len(run_line) == 1 and field(run_line[0], "us") > 0, brief
        total = matching(out, rf"^BENCH capture total {brief} ")
        assert len(total) == 1, brief
        # The end read-back is what makes a run "verified"; skipping it means neither.
        assert f"verified={1 if e == 'on' else 0}" in total[0], total[0]
        if e == "off":
            assert "verify_us=0 " in total[0], total[0]
        else:
            assert field(total[0], "verify_us") > 0, total[0]
        sampled = int(field(total[0], "sampled"))
        assert sampled == 0 if s == 0 else sampled >= 2, total[0]
        parts = matching(out, rf"^BENCH capture parts {brief} ms ")
        assert len(parts) == 1 and (h != "crc32" or " sha=0 " in parts[0]), parts
        assert matching(out, rf"^BENCH cpu capture {brief} ")
    # Resume: every job gets the full check; only CRC-only jobs can do the size check.
    full = matching(out, r"^BENCH resume uihz=full hash=\w+ check=full sectors=256 result=ok")
    size = matching(out, r"^BENCH resume uihz=full hash=crc32 check=size sectors=256 result=ok")
    skipped = matching(out, r"^BENCH resume uihz=full hash=both check=size skipped")
    # Doubled against the pre-DMA suite: every combination now runs with read=pio and read=dma.
    assert (len(full), len(size), len(skipped)) == (16, 8, 8), (len(full), len(size), len(skipped))
    # Not re-reading the prefix is the whole difference, and it must show.
    crc_full = [field(line, "us") for line in full if "hash=crc32" in line]
    assert max(field(line, "us") for line in size) * 10 < min(crc_full), (crc_full, size)
    assert "JOBS_LEFT 0" in out and "BENCH complete" in out   # the card is left as it was found


def pipeline_rows(out):
    return {m[1]: float(m[2]) for m in re.finditer(r"^BENCH pipeline \S+ (\S+) chunk=\d+ .*? kib_s=([\d.]+)$", out, re.M)}


def check_pipeline(out):
    """PIO, DMA and overlapped DMA, each reading, writing and CRC32-ing the same bytes.
    The fakes charge the drive only for the time the CPU had not already spent, so overlapping
    must be faster than either sequential row, and every row must produce the same CRC32."""
    rows = pipeline_rows(out)
    assert set(rows) == {"pio-sequential", "dma-sequential", "dma-overlapped"}, rows
    assert rows["dma-overlapped"] > rows["dma-sequential"] * 1.2, rows
    assert rows["dma-overlapped"] > rows["pio-sequential"] * 1.2, rows
    crcs = set(matching(out, r"crc32=([0-9a-f]{8})"))
    assert len(set(re.findall(r"BENCH pipeline .*? crc32=([0-9a-f]{8})", out))) == 1, "the three rows must read identical bytes"
    assert crcs
    # Every begun read was ended: nothing may be left owning a buffer.
    assert re.search(r"^DMA_BEGINS (\d+) ENDS \1 PENDING 0$", out, re.M), out
    assert "BENCH complete" in out and "NOTHING was measured" not in out
    # The scratch file is not left behind.
    assert not matching(out, r"pipeline.*failed")


def check_pipeline_nodma(out):
    # The ordinary build has no split DMA: PIO only, and the log says why.
    rows = pipeline_rows(out)
    assert set(rows) == {"pio-sequential"}, rows
    assert "the DMA rows need the experimental build" in out
    assert re.search(r"^DMA_BEGINS 0 ENDS 0 PENDING 0$", out, re.M)
    assert "BENCH complete" in out


def check_pipeline_beginfail(out):
    # A drive that will not start a DMA: the PIO row still stands, the DMA rows stop early,
    # and nothing is left in flight.
    assert pipeline_rows(out).get("pio-sequential")
    assert matching(out, r"BENCH pipeline dma-sequential: stopped early")
    assert re.search(r"^DMA_BEGINS 0 ENDS 0 PENDING 0$", out, re.M)
    assert "BENCH complete" in out


def check_capture_noop(out):
    # sections=capture with no readable disc: this is the shape of the T5B run of 2026-09-20 that
    # ended "BENCH complete" having measured nothing at all. It must now say so.
    assert "BENCH capture skipped: no readable disc" in out
    assert "BENCH complete but NOTHING was measured" in out
    assert not matching(out, r"^BENCH capture uihz=")


def check_capture_measured_something(out):
    # The opposite guard: a section that DID measure must not carry the warning.
    assert "NOTHING was measured" not in out and "BENCH complete" in out


def sweep_lines(out):
    return matching(out, r"^BENCH sweep uihz=full fad=")


def sweep_by_mode(out):
    """{(chunk, 'pio'|'dma', competing thread?): line} for the sweep points."""
    found = {}
    for line in sweep_lines(out):
        key = (int(field(line, "chunk")), "dma" if " mode=dma" in line else "pio", " free=" in line)
        assert key not in found, key
        found[key] = line
    return found


def check_dma(out):
    """Chunk 32/128 x PIO/DMA x with/without a competing thread, against the fakes' models:
    DMA is faster and leaves ~99% of the CPU, PIO leaves ~40%."""
    assert re.search(r"^BENCH dma check fad=45150 sectors=32 result=OK crc32=(\w+) pio=\1 reported_bytes=75264 ", out, re.M)
    calibration = int(re.search(r"BENCH spin calibration: (\d+) iterations/ms", out).group(1))
    assert 990 <= calibration <= 1000, calibration
    found = sweep_by_mode(out)
    assert len(found) == 8, sorted(found)
    for chunk in (32, 128):
        pio, dma = found[(chunk, "pio", False)], found[(chunk, "dma", False)]
        assert field(dma, "rd") > field(pio, "rd") * 1.3, (pio, dma)
        assert 38.0 <= field(found[(chunk, "pio", True)], "free") <= 41.0, found[(chunk, "pio", True)]
        assert 97.0 <= field(found[(chunk, "dma", True)], "free") <= 100.0, found[(chunk, "dma", True)]
    # Poll buckets only mean something for PIO with nothing else running.
    assert len(matching(out, r"^BENCH poll fad=\d+ c=\d+ n=")) == 2
    # PIO and DMA both return the reference bytes at both sizes.
    assert len(matching(out, r"^BENCH verify fad=45150 chunk=\d+ sectors=256 .* OK$")) == 2
    assert len(matching(out, r"^BENCH verify fad=45150 chunk=\d+ mode=dma sectors=256 .* OK$")) == 2
    assert not matching(out, "MISMATCH")
    assert len(matching(out, r"^BENCH cpu sweep c=\d+ g=0 s=0 dma spin ")) == 2   # each point is census'd


def check_dma_fail(out):
    assert re.search(r"^BENCH dma check fad=45150 sectors=32 result=FAILED; DMA stays off until reboot", out, re.M)
    found = sweep_by_mode(out)
    assert len(found) == 4 and all(mode == "pio" for _, mode, _ in found), sorted(found)   # PIO carries on
    assert "BENCH complete" in out and not matching(out, r"mode=dma")


def check_dma_mismatch(out):
    assert re.search(r"^BENCH dma check .* result=MISMATCH ", out, re.M)
    assert len(sweep_by_mode(out)) == 4 and not matching(out, r"mode=dma")   # wrong bytes: never measured
    assert "BENCH complete" in out


def check_dma_odd(out):
    # 257 sectors is odd: PIO measures it, DMA (which needs whole 32-byte multiples) skips.
    assert len(matching(out, r"^BENCH sweep dma fad=45150 chunk=\d+ skipped: needs an even sector count")) == 2
    assert len(sweep_by_mode(out)) == 4 and not matching(out, r"mode=dma sectors=")
    assert "BENCH complete" in out


def check_dma_midfail(out):
    # The check passes, then a DMA read fails: that point is skipped, DMA turns off, PIO carries on.
    assert re.search(r"^BENCH dma check .* result=OK ", out, re.M)
    assert matching(out, r"DMA off until reboot")
    found = sweep_by_mode(out)
    assert len(found) == 4 and all(mode == "pio" for _, mode, _ in found), sorted(found)
    assert not matching(out, r"mode=dma sectors=") and "BENCH complete" in out


def check_dma_none(out):
    assert "BENCH dma skipped: this platform has no GD-ROM DMA probe" in out
    assert len(sweep_by_mode(out)) == 4 and "BENCH complete" in out


def check_spin_none(out):
    assert "BENCH spin skipped: no competing-thread support here" in out
    found = sweep_by_mode(out)
    assert len(found) == 4 and not any(spin for _, _, spin in found), sorted(found)   # no free= anywhere


def check_crc16(out):
    for name in ("crc16-kos", "crc16-table", "crc16-slice2", "crc16-nibble"):
        assert matching(out, rf"^BENCH hash uihz=full {name} cyc_b=\d+ bytes="), name
        assert matching(out, rf"^BENCH cpu {name} uihz=full "), name
    assert "BENCH hash uihz=full crc16 agree=OK (4 implementations, identical results)" in out


def check_crc16_bad(out):
    assert "BENCH hash uihz=full crc16 agree=MISMATCH" in out and "BENCH complete" in out


def check_crc16_nokos(out):
    assert not matching(out, r"crc16-kos")
    assert "BENCH hash uihz=full crc16 agree=OK (3 implementations, identical results)" in out


def check_default_ui(out):
    # With nothing configured the UI is capped at 2 Hz while the bench measures.
    assert "BENCH pass 1/1 uihz=2" in out and "UI_CALLS 2 UI_SEQ 2 1000" in out
    assert matching(out, r"^BENCH optical uihz=2 fad=")


def check_bare(out):
    assert "BENCH complete" in out and "(no UI control on this platform)" in out
    assert not matching(out, r"BENCH cpu ")
    assert "UI_CALLS 0" in out


CASES = {
    "default": (0, check_default, True),
    "ui": (0, check_ui, False),
    "sweep": (0, check_sweep, False),
    "sweep-corrupt": (0, check_corrupt, False),
    "sweep-retry": (0, check_retry, False),
    "sweep-fatal": (1, check_fatal, False),
    "sweep-cancel": (3, check_cancel, False),
    "sweep-noprobe": (0, check_noprobe, False),
    "svc": (0, check_svc, False),
    "sd-bytes": (0, check_sd_bytes, True),
    "expand": (0, check_expand, True),
    "capture": (0, check_capture, True),
    "capture-noop": (0, check_capture_noop, False),
    "pipeline": (0, check_pipeline, False),
    "pipeline-nodma": (0, check_pipeline_nodma, False),
    "pipeline-beginfail": (0, check_pipeline_beginfail, False),
    "capture-nolink": (1, check_capture_nolink, False),
    "mount-fail": (0, check_mount_fail, False),
    "dma": (0, check_dma, False),
    "dma-fail": (0, check_dma_fail, False),
    "dma-mismatch": (0, check_dma_mismatch, False),
    "dma-odd": (0, check_dma_odd, False),
    "dma-midfail": (0, check_dma_midfail, False),
    "dma-none": (0, check_dma_none, False),
    "spin-none": (0, check_spin_none, False),
    "crc16": (0, check_crc16, False),
    "crc16-bad": (0, check_crc16_bad, False),
    "crc16-nokos": (0, check_crc16_nokos, False),
    "default-ui": (0, check_default_ui, False),
    "bare": (0, check_bare, False),
}


def main():
    with tempfile.TemporaryDirectory(prefix="kui-bench-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            for name, (code, check, touches_sd) in CASES.items():
                image = base / f"{kind}-{name}.img"
                shutil.copyfile(seed, image)
                out = run(BINARY, str(image), name, expected=code)
                check(out)
                if touches_sd:
                    # The scratch file must be gone and the volume still consistent.
                    run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                print(f"PASS {kind} bench: {name}", flush=True)


if __name__ == "__main__":
    main()
