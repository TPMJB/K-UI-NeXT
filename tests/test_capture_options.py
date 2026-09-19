#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The capture engine's runtime options, on real FAT32 and exFAT images.

Every option defaults to the engine as it has always been (the other capture tests
cover that). Here each alternative is exercised through the real engine and FatFs,
and its consequences, including the uncomfortable ones, are asserted:

  crc32      CRC-only job: no SHA-256, a schema 2 manifest, the PC verifier still passes
  noend      skip the end read-back (CRC-only jobs): CAPTURED, never SAVED DATA VERIFIED,
             and a later Verify still works because the manifest makes no claim
  sample=N   re-read 1 chunk in N while capturing; a bad write stops the capture there
  size       resume checks sizes only; catches a wrong size, NOT a corrupted prefix
"""
from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import verify_dump

BINARY = str(ROOT / "build/capture-image")


def run(image, *args, opts="", expected=0):
    env = dict(os.environ, KUI_TEST_OPTS=opts)
    result = subprocess.run([BINARY, str(image), *args], text=True, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode != expected:
        raise RuntimeError(f"{args} opts={opts!r}: expected exit {expected}, got {result.returncode}\n{result.stdout[-1500:]}")
    return result.stdout


def stats(output):
    line = re.search(r"^STATS (.*)$", output, re.M)
    assert line, output[-500:]
    return {k: (int(v) if v.isdigit() else v) for k, v in (p.split("=") for p in line.group(1).split())}


def export(image, destination):
    destination.mkdir()
    subprocess.run([BINARY, str(image), "export", str(destination)], check=True, stdout=subprocess.PIPE)
    jobs = sorted(destination.iterdir())
    assert len(jobs) == 1, jobs
    return jobs[0]


def digests(job):
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(job.glob("track*"))}


def main():
    with tempfile.TemporaryDirectory(prefix="kui-options-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            subprocess.run(["mkfs.fat", "-F", "32", str(clean)] if kind == "fat32" else ["mkfs.exfat", str(clean)],
                           check=True, stdout=subprocess.PIPE)
            run(clean, "seed")
            checker = "fsck.fat" if kind == "fat32" else "fsck.exfat"
            count = [0]

            def fresh():
                count[0] += 1
                image = base / f"{kind}-{count[0]}.img"
                shutil.copyfile(clean, image)
                return image

            # Reference: the default engine (SHA-256 and CRC32, full read-back).
            image = fresh()
            output = run(image, "new")
            assert "SAVED DATA VERIFIED: all 6 tracks; CRC32 and SHA-256" in output
            baseline = digests(export(image, base / f"{kind}-base"))
            assert len(baseline) == 6

            # --- crc32: no SHA-256 anywhere, and the data is byte-identical -------------
            image = fresh()
            output = run(image, "new", opts="crc32")
            assert "SAVED DATA VERIFIED: all 6 tracks; CRC32\n" in output and "SHA-256" not in output
            assert not re.search(r"^sha256 us=", output, re.M)
            assert stats(output)["crc_only"] == 1 and stats(output)["verified"] == 1
            job = export(image, base / f"{kind}-crc")
            assert digests(job) == baseline
            results = verify_dump.verify(job)
            assert all(not r["sha256_recorded"] for r in results)
            assert '"schema":2' in (job / "manifest.json").read_text()
            assert '"sha256"' not in (job / "manifest.json").read_text()
            subprocess.run([checker, "-n", str(image)], check=True, stdout=subprocess.PIPE)

            # --- noend: CAPTURED, not verified, and a later Verify still works ---------
            image = fresh()
            output = run(image, "new", opts="crc32,noend")
            assert "CAPTURED: all 6 tracks; stream CRC32 recorded; saved data NOT re-read" in output
            assert "SAVED DATA VERIFIED" not in output and "Rereading all saved tracks" not in output
            s = stats(output)
            assert s["verified"] == 0 and s["verify_us"] == 0 and s["capture_us"] > 0
            job = export(image, base / f"{kind}-noend")
            assert digests(job) == baseline and verify_dump.verify(job)   # the PC verifies it fully
            output = run(image, "verify")   # the manifest made no claim, so it can be upgraded
            assert "SAVED DATA VERIFIED: all 6 tracks; CRC32\n" in output and stats(output)["verified"] == 1
            subprocess.run([checker, "-n", str(image)], check=True, stdout=subprocess.PIPE)

            # --- sample=N: in-flight read-back -------------------------------------------
            image = fresh()
            output = run(image, "new", opts="crc32,noend,sample=3")
            assert re.search(r"chunks re-read while capturing \(1 in 3\); saved data NOT fully re-read", output)
            sampled = stats(output)["sampled"]
            assert 20 < sampled < 120, sampled
            assert digests(export(image, base / f"{kind}-sample")) == baseline
            image = fresh()
            output = run(image, "new", opts="sample=2")   # a SHA-256 job also samples, and still re-reads all
            assert stats(output)["sampled"] > 0 and stats(output)["verified"] == 1
            assert "SAVED DATA VERIFIED: all 6 tracks; CRC32 and SHA-256" in output
            # A write the card corrupts is caught where it happens, not an hour later.
            image = fresh()
            output = run(image, "new", "corrupt-write", opts="crc32,sample=1", expected=1)
            assert "Sampled read-back mismatch" in output and "Rereading all saved tracks" not in output
            assert "SAVED DATA VERIFIED" not in output
            image = fresh()
            output = run(image, "new", "corrupt-write", expected=1)   # default: found at the end
            assert "Saved data mismatch" in output and "Sampled read-back mismatch" not in output

            # --- a SHA-256 job cannot skip its read-back (schema 1 promises one) ---------
            image = fresh()
            output = run(image, "new", opts="noend")
            assert "End read-back stays on: this job records SHA-256" in output
            assert "SAVED DATA VERIFIED: all 6 tracks; CRC32 and SHA-256" in output

            # --- resume: the job keeps the mode it started with --------------------------
            image = fresh()
            run(image, "new", "stop-middle", opts="crc32", expected=3)
            output = run(image, "resume", opts="crc32,size,noend")
            assert "Checking saved file sizes before any resume writes (bytes not re-read)" in output
            assert "CAPTURED: all 6 tracks" in output
            job = export(image, base / f"{kind}-rs")
            assert digests(job) == baseline and verify_dump.verify(job)

            image = fresh()
            run(image, "new", "stop-middle", opts="crc32", expected=3)
            output = run(image, "resume")   # options now say SHA-256; the job is CRC-only
            assert "This job records CRC32 only; resuming it that way" in output
            assert "Checking saved bytes before any resume writes..." in output
            assert "SAVED DATA VERIFIED: all 6 tracks; CRC32\n" in output

            image = fresh()
            run(image, "new", "stop-middle", expected=3)   # a SHA-256 job
            output = run(image, "resume", opts="crc32,size,noend")
            assert "This job records SHA-256 and CRC32; resuming it that way" in output
            assert "every committed byte is re-read to rebuild it" in output
            assert "SAVED DATA VERIFIED: all 6 tracks; CRC32 and SHA-256" in output
            job = export(image, base / f"{kind}-rsha")
            assert digests(job) == baseline and '"schema":1' in (job / "manifest.json").read_text()

            # Size-only resume still refuses a file of the wrong size ...
            image = fresh()
            run(image, "new", "stop-middle", opts="crc32", expected=3)
            run(image, "mutate", "short")
            output = run(image, "resume", opts="crc32,size", expected=1)
            assert "Unexpected size/file for track 01; saved job preserved" in output

            # ... but NOT a corrupted prefix. That is the trade for not re-reading it: the
            # end read-back catches it, and with the read-back off only the PC verifier does.
            image = fresh()
            run(image, "new", "stop-middle", opts="crc32", expected=3)
            run(image, "mutate", "prefix")
            output = run(image, "resume", opts="crc32,size", expected=1)
            assert "Saved data mismatch" in output and "Rereading all saved tracks" in output
            image = fresh()
            run(image, "new", "stop-middle", opts="crc32", expected=3)
            run(image, "mutate", "prefix")
            output = run(image, "resume", opts="crc32,size,noend")
            assert "CAPTURED: all 6 tracks" in output   # the console cannot know
            job = export(image, base / f"{kind}-tamper")
            try:
                verify_dump.verify(job)
                raise AssertionError("the PC verifier must reject the corrupted dump")
            except ValueError as error:
                assert "hash mismatch" in str(error)
            # And the default resume (full prefix check) still catches it at resume.
            image = fresh()
            run(image, "new", "stop-middle", expected=3)
            run(image, "mutate", "prefix")
            output = run(image, "resume", expected=1)
            assert "Saved data mismatch" in output and "Capturing T" not in output

            # --- the benchmark entry point: real engine, nothing published ---------------
            image = fresh()
            output = run(image, "bench", "new", "512", opts="crc32,noend")
            assert stats(output)["bytes"] == 512 * 2352 and stats(output)["job"].startswith("0:/KUI/dumps/")
            assert "Benchmark run complete; no metadata published" in output
            job = export(image, base / f"{kind}-bench")
            assert not (job / "manifest.json").exists() and not (job / "disc.gdi").exists()
            assert (job / "track03.bin").stat().st_size == 512 * 2352
            full = stats(run(image, "bench", "resume", "512", opts="crc32"))["resume_us"]
            fast = stats(run(image, "bench", "resume", "512", opts="crc32,size"))["resume_us"]
            assert full > 0 and fast * 10 < full, (full, fast)   # not re-reading is the whole difference
            print(f"PASS {kind} capture options: crc32, noend + later verify, sample, resume modes, "
                  "trade-offs, bench entry", flush=True)


if __name__ == "__main__":
    main()
