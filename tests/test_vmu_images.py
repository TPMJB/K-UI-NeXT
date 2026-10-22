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
             "device-change", "cancel-start", "cancel-read", "stale-list", "unlisted", "invalid-row", "invalid-page", "connect-fail",
             "copy-game-source-nonzero", "copy-game-source-fragmented", "copy-game-source-multiple")
BACKUPS = ("backup-selected", "selected-page", "backup-all", "unsafe-name", "game-save", "contents-change", "write-fail",
           "sync-fail", "readback-fail", "readback-corrupt", "cancel-write")

RESTORES = ("restore-ok", "restore-game", "restore-page", "restore-existing", "restore-capacity", "restore-corrupt",
            "restore-bad-proof", "restore-bad-dir", "restore-legacy", "restore-invalid-path", "restore-unpreviewed",
            "restore-changed-card", "restore-changed-source", "restore-device-change", "restore-write-fail",
            "restore-data-corrupt", "restore-fat-fail", "restore-fat-corrupt", "restore-dir-fail",
            "restore-dir-corrupt", "restore-final-corrupt", "restore-cancel", "restore-stop-commit", "restore-remove",
            "restore-sd-write-fail", "restore-sd-sync-fail", "restore-orphan", "restore-orphan-link",
            "restore-duplicate", "restore-existing-tail", "restore-truncated", "restore-dir-existing-corrupt",
            "restore-game-blocked-zero", "restore-game-fragmented-prefix", "restore-game-existing")


MANAGED = ("delete-ok", "delete-preview-only", "delete-restore", "delete-unlisted", "delete-invalid-row", "delete-unpreviewed",
           "delete-orphan", "delete-duplicate", "delete-nameless", "delete-nul-first", "delete-invalid-protection", "delete-changed-card", "delete-changed-payload", "delete-device-change",
           "delete-sd-write-fail", "delete-sd-sync-fail", "delete-readback-fail", "delete-readback-corrupt", "delete-cancel-backup",
           "delete-dir-fail", "delete-dir-corrupt", "delete-fat-fail", "delete-fat-corrupt", "delete-stop-commit", "delete-remove",
           "copy-ok", "copy-preview-only", "copy-unpreviewed", "copy-existing", "copy-orphan", "copy-same-slot",
           "copy-changed-source", "copy-changed-payload", "copy-changed-destination", "copy-removed-destination",
           "copy-sd-write-fail", "copy-sd-sync-fail", "copy-readback-fail", "copy-readback-corrupt", "copy-cancel",
           "copy-write-fail", "copy-data-corrupt", "copy-dir-fail", "copy-dir-corrupt", "copy-fat-fail", "copy-fat-corrupt", "copy-final-corrupt",
           "copy-game", "copy-game-blocked-zero", "copy-game-fragmented-prefix", "copy-game-existing")


IMAGES = ("card-image", "card-image-empty", "card-image-read-fail", "card-image-removed", "card-image-changed",
          "card-image-write-fail", "card-image-sync-fail", "card-image-readback-fail", "card-image-readback-corrupt", "card-image-cancel",
          "image-browse", "image-empty", "image-uppercase", "image-unlisted", "image-proof-good", "image-proof-bad", "image-proof-mismatch", "image-cancel-read", "image-page", "image-extract-selected", "image-selected-page", "image-extract-all",
          "image-cycle", "image-range", "image-shared", "image-short-chain", "image-layout", "image-root", "image-header",
          "image-dirty", "image-protection", "image-nameless", "image-truncated", "image-oversized",
          "image-read-fail", "image-read-corrupt", "image-stale", "image-change-during", "image-partial", "image-invalid-path",
          "image-invalid-page", "image-invalid-row", "image-write-fail", "image-sync-fail", "image-readback-fail",
          "image-readback-corrupt", "image-cancel-start", "image-cancel-write")


BULK = ("bulk-ok", "bulk-preview-only", "bulk-unpreviewed", "bulk-same-slot", "bulk-invalid-source", "bulk-invalid-destination",
        "bulk-conflict-last", "bulk-capacity", "bulk-directory-full", "bulk-source-cycle", "bulk-source-shared", "bulk-source-orphan",
        "bulk-destination-orphan", "bulk-source-duplicate", "bulk-destination-duplicate", "bulk-source-nameless", "bulk-source-protection",
        "bulk-source-removed", "bulk-destination-removed", "bulk-vmu-read-fail", "bulk-changed-source", "bulk-changed-payload-last",
        "bulk-changed-destination", "bulk-source-device-change", "bulk-destination-device-change", "bulk-source-removed-before-commit",
        "bulk-destination-removed-before-commit", "bulk-cancel-start", "bulk-cancel-after-one", "bulk-source-removed-after-one",
        "bulk-destination-removed-after-one", "bulk-write-fail-after-one", "bulk-fat-fail-after-one", "bulk-dir-fail-after-one",
        "bulk-sd-write-fail", "bulk-sd-sync-fail", "bulk-readback-fail", "bulk-readback-corrupt", "bulk-write-fail", "bulk-data-corrupt",
        "bulk-fat-fail", "bulk-fat-corrupt", "bulk-dir-fail", "bulk-dir-corrupt", "bulk-final-corrupt", "bulk-final-read-fail", "bulk-game", "bulk-empty-source", "bulk-cancel-data",
        "bulk-source-payload-change-during-stage", "bulk-destination-change-during-stage",
        "bulk-source-removed-during-stage", "bulk-destination-removed-during-stage",
        "bulk-game-blocked-zero", "bulk-game-fragmented-prefix", "bulk-game-existing")
BULK_READ_ONLY = BULK[1:20] + ("bulk-changed-source", "bulk-changed-payload-last", "bulk-changed-destination", "bulk-source-device-change",
                            "bulk-destination-device-change", "bulk-source-removed-before-commit", "bulk-destination-removed-before-commit", "bulk-cancel-start", "bulk-empty-source",
                            "bulk-game-blocked-zero", "bulk-game-fragmented-prefix", "bulk-game-existing")


PUBLISH = ("publish-ok", "publish-type", "publish-protection", "publish-first-block", "publish-nameless", "publish-header",
           "publish-dirty", "publish-reserved", "publish-count", "publish-zero", "publish-short", "publish-large",
           "publish-null-entry", "publish-null-data", "publish-write-fail", "publish-sync-fail", "publish-readback-fail",
           "publish-readback-corrupt", "publish-cancel-start", "publish-cancel-write")
PUBLISH_READ_ONLY = tuple(case for case in PUBLISH[1:14] if case != "publish-first-block") + ("publish-cancel-start",)


COLLECTION = ("collection-all", "collection-selected-page", "collection-game", "collection-preview-only",
              "collection-unlisted", "collection-unpreviewed", "collection-invalid-slot", "collection-invalid-page",
              "collection-invalid-row", "collection-short-block", "collection-oversize-block", "collection-read-fail",
              "collection-cancel-read", "collection-orphan", "collection-system-fat", "collection-changed-payload",
              "collection-changed-metadata", "collection-device-changed", "collection-source-removed",
              "collection-cancel-publish", "collection-write-fail", "collection-sync-fail", "collection-readback-corrupt",
              "collection-commit-twice", "collection-root-geometry", "collection-sort-flag",
              "collection-user-240", "collection-relocated-metadata", "collection-inactive-padding",
              "collection-active-reserved", "collection-control-name", "collection-over-capacity",
              "collection-capture-read-fail")
COLLECTION_READ_ONLY = tuple(case for case in COLLECTION[3:19] if case!="collection-system-fat") + (
    "collection-active-reserved", "collection-control-name", "collection-over-capacity", "collection-capture-read-fail")
DESCRIPTIONS = ("description-physical", "description-fragmented", "description-malformed",
                "description-read-fail", "description-image", "description-image-fragmented")
DESCRIPTION_READ_ONLY = DESCRIPTIONS[:3]


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
            for case in READ_ONLY + BACKUPS + RESTORES + MANAGED + IMAGES + BULK + PUBLISH + COLLECTION + DESCRIPTIONS:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                assert f"PASS VMU {case}" in run(BINARY, str(image), case)
                if case in READ_ONLY + BULK_READ_ONLY + PUBLISH_READ_ONLY + COLLECTION_READ_ONLY + DESCRIPTION_READ_ONLY:
                    assert digest(image) == original, f"{case} unexpectedly wrote to the SD card"
                if case not in ("write-fail", "sync-fail", "card-image-write-fail", "card-image-sync-fail", "image-write-fail", "image-sync-fail", "publish-write-fail", "publish-sync-fail", "collection-write-fail", "collection-sync-fail") and not case.endswith(("sd-write-fail", "sd-sync-fail")):
                    run(checker, "-n", str(image))
                image.unlink()
                print(f"PASS {kind} VMU: {case}", flush=True)


if __name__ == "__main__":
    main()
