#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fault-inject the actual capture/checkpoint/FatFs path on FAT32 and exFAT."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import verify_dump
from test_images import run

BINARY = str(ROOT / "build/capture-image")


def timings(output):
    phases = {}
    for line in output.splitlines():
        match = re.fullmatch(r"TIMING (\w+) wall_us=(\d+)", line)
        if match:
            name, wall = match.groups()
            assert name not in phases
            phase = phases[name] = {"wall": int(wall), "buckets": {}}
        match = re.fullmatch(r"(\w+) us=(\d+) pct=(\d+)\.(\d) bytes=(\d+) calls=(\d+)", line)
        if match:
            bucket, us, percent, tenth, size, calls = match.groups()
            us = int(us)
            assert bucket not in phase["buckets"] and us > 0
            assert int(percent) * 10 + int(tenth) == us * 1000 // phase["wall"]
            phase["buckets"][bucket] = {"us": us, "bytes": int(size), "calls": int(calls)}
        match = re.fullmatch(r"other us=(\d+)", line)
        if match:
            assert int(match[1]) + sum(b["us"] for b in phase["buckets"].values()) == phase["wall"]
    assert phases, output
    return phases


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def export(image, destination):
    destination.mkdir()
    run(BINARY, str(image), "export", str(destination))
    return sorted(destination.iterdir())


def main():
    with tempfile.TemporaryDirectory(prefix="kui-capture-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(clean)) if kind == "fat32" else run("mkfs.exfat", str(clean))
            run(BINARY, str(clean), "seed")
            checker = "fsck.fat" if kind == "fat32" else "fsck.exfat"
            baseline = base / f"{kind}-complete.img"
            shutil.copyfile(clean, baseline)
            output = run(BINARY, str(baseline), "new")
            assert "SAVED DATA VERIFIED" in output
            run(checker, "-n", str(baseline))
            original = export(baseline, base / f"{kind}-original")[0]
            expected = verify_dump.verify(original)
            total = sum(t["bytes"] for t in expected)
            data_bytes = sum(t["bytes"] for t in expected if t["file"].endswith(".bin"))
            stats = timings(output)
            assert "resume" not in stats
            for bucket in ("disc", "write", "sha256", "crc32"):
                assert stats["capture"]["buckets"][bucket]["bytes"] == total
            assert stats["capture"]["buckets"]["edc"]["bytes"] == data_bytes
            assert stats["capture"]["buckets"]["checkpoint"]["calls"] > 0
            for bucket in ("read", "sha256", "crc32"):
                assert stats["verify"]["buckets"][bucket]["bytes"] == total
            assert "read" not in stats["capture"]["buckets"]
            assert "disc" not in stats["verify"]["buckets"]
            before = digest(baseline)
            output = run(BINARY, str(baseline), "verify")
            assert "WRITES 0" in output and digest(baseline) == before
            stats = timings(output)
            assert "capture" not in stats and "resume" not in stats
            assert stats["verify"]["buckets"]["sha256"]["bytes"] == total
            print(f"PASS {kind}: six-track capture, independent hashes/GDI, fsck, read-only verification", flush=True)

            # Stop at early/middle/complete-capture/readback points. The resumed
            # files must equal an uninterrupted run under independent hashing.
            for stop in ("stop-early", "stop-middle", "stop-late", "stop-verify"):
                image = base / f"{kind}-{stop}.img"
                shutil.copyfile(clean, image)
                output = run(BINARY, str(image), "new", stop, expected=3)
                assert "SAVED DATA VERIFIED" not in output
                stopped = timings(output)
                saved = stopped["capture"]["buckets"]["write"]["bytes"]
                assert stopped["capture"]["buckets"]["sha256"]["bytes"] == saved
                incomplete = export(image, base / f"{kind}-{stop}-partial")[0]
                assert not (incomplete / "manifest.json").exists() and not (incomplete / "disc.gdi").exists()
                if stop == "stop-middle":
                    # Extra bytes after the synced checkpoint must be removed
                    # only after the committed prefix is checked in full.
                    run(BINARY, str(image), "mutate", "tail")
                    tail_image = base / f"{kind}-checkpoint-fallback.img"
                    shutil.copyfile(image, tail_image)
                    run(BINARY, str(tail_image), "mutate", "checkpoint")
                    run(BINARY, str(tail_image), "resume")
                    recovered = export(tail_image, base / f"{kind}-fallback")[0]
                    assert verify_dump.verify(recovered) == expected
                output = run(BINARY, str(image), "resume")
                stats = timings(output)
                assert stats["resume"]["buckets"]["sha256"]["bytes"] == saved
                if saved < total:
                    assert stats["capture"]["buckets"]["sha256"]["bytes"] == total - saved
                run(checker, "-n", str(image))
                resumed = export(image, base / f"{kind}-{stop}-resumed")[0]
                assert verify_dump.verify(resumed) == expected
            print(f"PASS {kind}: controlled stop/resume equality, uncommitted suffix, damaged-newest checkpoint", flush=True)

            for fail in ("disc-fail", "edc-fail", "disc-change", "write-fail", "sync-fail", "read-fail", "corrupt-write", "full-during", "cancel-before"):
                image = base / f"{kind}-{fail}.img"
                shutil.copyfile(clean, image)
                before = digest(image)
                output = run(BINARY, str(image), "new", fail, expected=3 if fail == "cancel-before" else 1)
                assert "SAVED DATA VERIFIED" not in output
                timings(output)
                if fail == "cancel-before":
                    assert digest(image) == before
                else:
                    if fail in ("disc-fail", "edc-fail"):
                        assert "BAD_ATTEMPTS 11" in output, output
                    if fail == "disc-change":
                        assert "FATAL 1" in output, output
                    # Media is reconnected only in a fresh harness process.
                    files = export(image, base / f"{kind}-{fail}-export")[0]
                    assert not (files / "manifest.json").exists() and not (files / "disc.gdi").exists()
            print(f"PASS {kind}: bounded retries, EDC errors, disc change, SD write/sync/read faults, short write/full card, pre-cancel", flush=True)

            for change in ("prefix", "short", "both-checkpoints", "manifest", "wrong-disc"):
                image = base / f"{kind}-reject-{change}.img"
                shutil.copyfile(baseline, image)
                if change != "wrong-disc":
                    run(BINARY, str(image), "mutate", change)
                before = digest(image)
                output = run(BINARY, str(image), "resume", "wrong-disc" if change == "wrong-disc" else "none", expected=1)
                assert "SAVED DATA VERIFIED" not in output
                assert "WRITES 0" in output and digest(image) == before, (kind, change, output)
            print(f"PASS {kind}: corrupt prefix/metadata, short track, invalid checkpoints and wrong-disc resume preserve card", flush=True)

            transient = base / f"{kind}-transient.img"
            shutil.copyfile(clean, transient)
            output = run(BINARY, str(transient), "new", "transient")
            assert "Read retry" in output
            assert verify_dump.verify(export(transient, base / f"{kind}-transient")[0]) == expected
            # A new capture never replaces the previous completed directory.
            run(BINARY, str(baseline), "new")
            captures = export(baseline, base / f"{kind}-two-jobs")
            assert len(captures) == 2
            assert all(verify_dump.verify(path) == expected for path in captures)
            run(checker, "-n", str(baseline))
            # Reference coverage is explicit; data corruption is rejected by PC.
            reference = json.loads((captures[0] / "manifest.json").read_text())
            assert verify_dump.compare_reference(expected, reference) == (True, 6)
            reference["tracks"] = reference["tracks"][:2]
            assert verify_dump.compare_reference(expected, reference) == (False, 2)
            print(f"PASS {kind}: transient recovery equality, existing dump preservation, complete/partial reference coverage", flush=True)


if __name__ == "__main__":
    main()
