#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Post-salvage saved-byte and catalogue checks using real engine receipts."""
from pathlib import Path
import shutil
import tempfile
from make_recovery_vectors import sector
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
CASES = ("cd-audio", "cd-profile-changed", "lookup", "readback", "no-database", "catalogue-mismatch", "catalogue-error",
         "data-changed", "data-changed-no-readback", "length-changed", "cancel-readback", "cancel-catalogue",
         "invalid-receipt", "invalid-job", "identity-changed", "plan-changed", "repeat", "report-write", "report-sync", "report-rename", "report-readback")


def main():
    with tempfile.TemporaryDirectory(prefix="kui-salvage-completion-") as temporary:
        base = Path(temporary)
        fixture = base / "fixture"
        fixture.mkdir()
        (fixture / "track01.bin").write_bytes(b"".join(sector(fad, fad) for fad in range(150, 152)))
        (fixture / "track02.raw").write_bytes(bytes((i * 31 + 5) & 255 for i in range(2 * 2352)))
        (fixture / "track03.bin").write_bytes(b"".join(sector(fad, fad) for fad in range(45150, 45152)))
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            for case in CASES:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                output = run(str(ROOT / "build/salvage-completion-image"), str(image), str(fixture), case)
                assert f"PASS salvage completion {case}" in output
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} salvage completion: {case}", flush=True)


if __name__ == "__main__":
    main()
