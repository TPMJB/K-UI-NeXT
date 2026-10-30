#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise source mutation and wrong-sector rejection through real FatFs."""
from pathlib import Path
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import verify_dump

BINARY = str(ROOT / "build/capture-image")
STARTS = (150, 650, 45150, 50000, 50400, 51000)


def run(image, *args, opts="", expected=0, selected=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith("KUI_TEST_")}
    env["KUI_TEST_OPTS"] = opts
    if selected is not None:
        env["KUI_TEST_SELECTED_JOB"] = selected
    result = subprocess.run([BINARY, str(image), *args], env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert result.returncode == expected, (args, opts, result.returncode, result.stdout[-6000:])
    return result.stdout


def export(image, destination):
    destination.mkdir()
    run(image, "export", str(destination))
    jobs = list(destination.iterdir())
    assert len(jobs) == 1
    return jobs[0]


def hashes(job):
    return {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(job.glob("track*"))}


def payload_hashes(job):
    return {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in job.iterdir()
            if f.is_file() and (f.name.startswith(("track", "checkpoint-")) or
                                f.name in ("manifest.json", "disc.gdi"))}


def image_hash(image):
    with image.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def committed(job, track):
    candidates = []
    for path in job.glob("checkpoint-*.bin"):
        record = path.read_bytes()
        assert len(record) == 4096
        if zlib.crc32(record[:4092]) == struct.unpack_from("<I", record, 4092)[0]:
            candidates.append((struct.unpack_from("<Q", record, 16)[0], record))
    assert candidates
    record = max(candidates)[1]
    return struct.unpack_from("<I", record, 96 + 40 * (track - 1))[0]


def main():
    with tempfile.TemporaryDirectory(prefix="kui-capture-integrity-") as temporary:
        base = Path(temporary)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            subprocess.run(["mkfs.fat", "-F", "32", str(clean)] if kind == "fat32" else
                           ["mkfs.exfat", str(clean)], check=True, stdout=subprocess.PIPE)
            run(clean, "seed")
            baseline = base / f"{kind}-baseline.img"
            shutil.copyfile(clean, baseline)
            run(baseline, "new", opts="crc32,noend")
            expected = hashes(export(baseline, base / f"{kind}-baseline"))
            for dma in (False, True):
                options = "crc32,noend" + (",dma" if dma else "")
                for fault in ("buffer-mutate", "buffer-mutate-writefail"):
                    for quick in (False, True):
                        name = f"{kind}-{dma}-{fault}-{quick}"
                        image = base / (name + ".img")
                        shutil.copyfile(clean, image)
                        output = run(image, "new", fault, opts=options, expected=1)
                        assert "FAULT_INJECTED 1" in output and "Source buffer changed during" in output
                        match = re.search(r"Source buffer changed during T(\d+) write at FAD=(\d+)", output)
                        assert match
                        track, fad = map(int, match.groups())
                        partial = export(image, base / (name + "-partial"))
                        assert committed(partial, track) <= fad - STARTS[track - 1]
                        assert not (partial / "manifest.json").exists() and not (partial / "disc.gdi").exists()
                        assert "PENDING 0" in output
                        run(image, "resume", opts=options + (",size" if quick else ""))
                        restored = export(image, base / (name + "-restored"))
                        assert hashes(restored) == expected
                        verify_dump.verify(restored)
                        image.unlink()
                        print(f"PASS {name}: mutated write stays uncommitted; resume byte-exact", flush=True)
                image = base / f"{kind}-{dma}-wrong-address.img"
                shutil.copyfile(clean, image)
                output = run(image, "new", "wrong-address", opts=options, expected=1)
                assert "requested address checked" in output and "BAD_ATTEMPTS 11" in output
                assert "PENDING 0" in output and "SAVED DATA VERIFIED" not in output
                partial = export(image, base / f"{kind}-{dma}-wrong-address")
                assert committed(partial, 3) <= 100
                run(image, "resume", opts=options)
                restored = export(image, base / f"{kind}-{dma}-wrong-address-resumed")
                assert hashes(restored) == expected
                verify_dump.verify(restored)
                image.unlink()
                print(f"PASS {kind} dma={dma}: valid-EDC wrong-address sector rejected with strict off", flush=True)
            # Use the production guard publisher, not a hand-authored marker.
            # Both partial and complete existing jobs must stay quarantined
            # across ordinary Resume, size-only Quick Resume and Verify.
            for partial in (False, True):
                name = f"{kind}-quarantine-{'partial' if partial else 'complete'}"
                image = base / (name + ".img")
                shutil.copyfile(clean if partial else baseline, image)
                if partial:
                    run(image, "new", "stop-middle", opts="crc32,noend", expected=3)
                before_job = export(image, base / (name + "-before"))
                payloads = payload_hashes(before_job)
                assert any(f.startswith("checkpoint-") for f in payloads)
                selected = "/KUI/dumps/" + before_job.name
                output = run(image, "quarantine", selected)
                assert "QUARANTINE_PUBLISHED 1" in output
                marked_job = export(image, base / (name + "-marked"))
                assert payload_hashes(marked_job) == payloads
                assert (marked_job / ".kui-readback-unstable").is_file()
                for action, options, label in (("resume", "crc32,noend", "Resume"),
                                                ("resume", "crc32,noend,size", "Quick Resume"),
                                                ("verify", "crc32,noend", "Verify")):
                    before = image_hash(image)
                    output = run(image, action, opts=options, expected=1, selected=selected)
                    assert "Saved reads previously disagreed" in output and "WRITES 0" in output
                    assert "SAVED DATA VERIFIED" not in output and "PENDING 0" in output
                    assert image_hash(image) == before
                    print(f"PASS {name}: {label} refuses quarantine without writes", flush=True)
                after_job = export(image, base / (name + "-after"))
                assert payload_hashes(after_job) == payloads
                assert (after_job / ".kui-readback-unstable").read_bytes() == (marked_job / ".kui-readback-unstable").read_bytes()
                image.unlink()


if __name__ == "__main__":
    main()
