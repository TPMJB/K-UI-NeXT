#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Separate salvage producer/repair worker on real FAT32/exFAT fault images."""
from pathlib import Path
import shutil
import importlib.util
import zlib
import tempfile
from test_images import run
from make_recovery_vectors import sector

ROOT = Path(__file__).resolve().parents[1]
CASES = (
    "healthy", "zero-off", "fatal", "cancel", "repair", "bad-parity",
    "audio-mismatch", "wrong-fad", "bad-ecc", "repair-fatal", "repair-stop",
    "queue-write", "queue-sync", "commit-write", "torn-commit",
    "baseline-write", "baseline-sync", "ready-write", "backup-write",
    "backup-sync", "backup-publish", "patch-write", "readback-fail",
    "readback-mismatch", "repaired-write", "torn-repaired", "complete-write",
    "complete-sync", "final-publish", "corrupt-header", "corrupt-journal",
    "wrong-disc", "placeholder-changed", "header-readback", "journal-readback",
    "backup-substitute", "queue-error-cancel", "five-passes", "invalid-limit",
    "latest-wrong-disc", "corrupt-patch",
)


def check_verifier(exported):
    spec = importlib.util.spec_from_file_location("salvage_verifier", ROOT / "tools/verify_salvage.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = module.verify(exported)
    assert result["saved_files_verified"] and result["original_targets"] == 2
    for filename, offset, checksum in (("track03.bin", 17, False),
                                        ("track01.bin", 2 * 2352 + 17, False),
                                        ("patch-0001.bin", 64 + 2352 + 17, True),
                                        ("journal.bin", -1, False)):
        path = exported / filename
        original = path.read_bytes()
        damaged = bytearray(original)
        damaged[offset] ^= 1
        if checksum:
            damaged[28:32] = zlib.crc32(damaged[64 + 2352:64 + 2 * 2352]).to_bytes(4, "little")
            damaged[-4:] = zlib.crc32(damaged[:-4]).to_bytes(4, "little")
        path.write_bytes(damaged)
        try:
            module.verify(exported)
        except ValueError:
            pass
        else:
            raise AssertionError(f"Verifier accepted changed {filename}")
        path.write_bytes(original)
    path = exported / "journal.bin"
    original = path.read_bytes()
    path.write_bytes(original[:-512])
    try:
        module.verify(exported)
    except ValueError:
        pass
    else:
        raise AssertionError("Verifier accepted a missing COMPLETE event")
    path.write_bytes(original)


def main():
    with tempfile.TemporaryDirectory(prefix="kui-salvage-") as temporary:
        base = Path(temporary)
        fixture = base / "fixture"
        fixture.mkdir()
        (fixture / "track01.bin").write_bytes(b"".join(sector(fad, fad) for fad in range(150, 190)))
        (fixture / "track02.raw").write_bytes(bytes((i * 31 + 5) & 255 for i in range(4 * 2352)))
        (fixture / "track03.bin").write_bytes(b"".join(sector(fad, fad) for fad in range(45150, 45190)))
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            for case in CASES:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                exported = base / f"{kind}-saved"
                extra = []
                if case == "repair":
                    exported.mkdir()
                    extra = [str(exported)]
                output = run(str(ROOT / "build/salvage-image"), str(image), str(fixture), case, *extra)
                if extra:
                    check_verifier(exported)
                assert f"PASS salvage {case}" in output
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} salvage: {case}", flush=True)


if __name__ == "__main__":
    main()
