#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FatFs backups/restores with simulated VMU commit and block faults."""
from pathlib import Path
import hashlib
import shutil
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/test-vmu-app")
READ_ONLY = ("missing", "empty", "list", "slot-d2", "invalid-slot", "cycle", "range", "root-layout", "duplicate-chain",
             "short-chain", "bad-header", "vmu-read-fail", "short-block", "oversize-block",
             "device-change", "cancel-start", "cancel-read", "stale-list", "unlisted", "invalid-row", "invalid-page", "connect-fail")
BACKUPS = ("backup-selected", "selected-page", "backup-all", "unsafe-name", "game-save", "contents-change", "write-fail",
           "sync-fail", "readback-fail", "readback-corrupt", "cancel-write")

RESTORES = ("restore-ok", "restore-game", "restore-page", "restore-existing", "restore-capacity", "restore-corrupt",
            "restore-bad-proof", "restore-bad-dir", "restore-legacy", "restore-invalid-path", "restore-unpreviewed",
            "restore-changed-card", "restore-changed-source", "restore-device-change", "restore-write-fail",
            "restore-data-corrupt", "restore-fat-fail", "restore-fat-corrupt", "restore-dir-fail",
            "restore-dir-corrupt", "restore-final-corrupt", "restore-cancel", "restore-stop-commit", "restore-remove",
            "restore-sd-write-fail", "restore-sd-sync-fail", "restore-orphan", "restore-orphan-link",
            "restore-duplicate", "restore-existing-tail", "restore-truncated", "restore-dir-existing-corrupt")


MANAGED = ("delete-ok", "delete-preview-only", "delete-restore", "delete-unlisted", "delete-invalid-row", "delete-unpreviewed",
           "delete-orphan", "delete-duplicate", "delete-nameless", "delete-nul-first", "delete-invalid-protection", "delete-changed-card", "delete-changed-payload", "delete-device-change",
           "delete-sd-write-fail", "delete-sd-sync-fail", "delete-readback-fail", "delete-readback-corrupt", "delete-cancel-backup",
           "delete-dir-fail", "delete-dir-corrupt", "delete-fat-fail", "delete-fat-corrupt", "delete-stop-commit", "delete-remove",
           "copy-ok", "copy-preview-only", "copy-unpreviewed", "copy-existing", "copy-orphan", "copy-same-slot",
           "copy-changed-source", "copy-changed-payload", "copy-changed-destination", "copy-removed-destination",
           "copy-sd-write-fail", "copy-sd-sync-fail", "copy-readback-fail", "copy-readback-corrupt", "copy-cancel",
           "copy-write-fail", "copy-data-corrupt", "copy-dir-fail", "copy-dir-corrupt", "copy-fat-fail", "copy-fat-corrupt", "copy-final-corrupt")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


def main():
    with tempfile.TemporaryDirectory(prefix="kui-vmu-") as temporary:
        base = Path(temporary)
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            run(BINARY, str(seed), "seed")
            original = digest(seed)
            checker = "fsck.fat" if kind == "fat32" else "fsck.exfat"
            for case in READ_ONLY + BACKUPS + RESTORES + MANAGED:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                assert f"PASS VMU {case}" in run(BINARY, str(image), case)
                if case in READ_ONLY:
                    assert digest(image) == original, f"{case} unexpectedly wrote to the SD card"
                if case not in ("write-fail", "sync-fail") and not case.endswith(("sd-write-fail", "sd-sync-fail")):
                    run(checker, "-n", str(image))
                print(f"PASS {kind} VMU: {case}", flush=True)


if __name__ == "__main__":
    main()
