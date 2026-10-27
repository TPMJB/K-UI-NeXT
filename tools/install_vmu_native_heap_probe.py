#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install or restore the isolated allocator probe with a verified backup."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

ACTIVE = "retail-boot.kui"
BACKUP = "retail-boot.before-native-heap.kui"
RECEIPT = "native-heap-backup.json"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sync_directory(directory):
    fd = os.open(directory, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def exclusive(path, data):
    with path.open("xb") as out:
        out.write(data)
        out.flush()
        os.fsync(out.fileno())
    sync_directory(path.parent)


def replace(path, data):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix="kui-heap-", delete=False) as out:
            temporary = Path(out.name)
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        if temporary.read_bytes() != data:
            raise ValueError("Temporary module failed verification")
        os.replace(temporary, path)
        temporary = None
        sync_directory(path.parent)
        if path.read_bytes() != data:
            raise ValueError("Installed module failed verification")
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def install(sd_root, bundle, restore=False):
    root = Path(sd_root).resolve(strict=True)
    parent = root / "KUI/apps/games"
    parent.resolve(strict=True).relative_to(root)
    active, backup, receipt = (parent / name for name in (ACTIVE, BACKUP, RECEIPT))
    if any(path.is_symlink() for path in (active, backup, receipt)):
        raise ValueError("Module and backup paths must not be symbolic links")
    current = active.read_bytes()
    if not current.startswith(b"KUIRUN1\0") or len(current) < 64:
        raise ValueError("Existing retail-boot.kui is not a K-UI module; nothing replaced")
    if restore or backup.exists() or receipt.exists():
        if not backup.is_file() or not receipt.is_file():
            raise ValueError("Incomplete earlier backup; inspect it before replacing any module")
        original = backup.read_bytes()
        record = json.loads(receipt.read_text())
        if record.get("schema") != 1 or sha(original) != record.get("original_sha256"):
            raise ValueError("Backup integrity failed; nothing replaced")
        if sha(current) not in (record["original_sha256"], record.get("probe_sha256")):
            raise ValueError("Active module changed since this backup; nothing replaced")
        if restore:
            if current != original:
                replace(active, original)
            return "Original native loader restored and verified."
    else:
        original, record = current, None
    source = Path(bundle) / "KUI/apps/games" / ACTIVE
    probe = source.read_bytes()
    report = json.loads((Path(bundle) / "probe-report.json").read_text())
    if not probe.startswith(b"KUIRUN1\0") or len(probe) < 64 or sha(probe) != report.get("module_sha256"):
        raise ValueError("Probe package integrity failed; nothing replaced")
    if record is not None and record.get("probe_sha256") != sha(probe):
        if current != original:
            raise ValueError("Earlier probe is still installed; run this script with --restore first")
        # The verified original is active, so advancing the receipt is safe
        # even if interrupted: both receipts accept the unchanged original.
        record = {"schema": 1, "original_sha256": sha(original), "probe_sha256": sha(probe)}
        replace(receipt, (json.dumps(record, indent=2) + "\n").encode())
    if record is None:
        exclusive(backup, original)
        record = {"schema": 1, "original_sha256": sha(original), "probe_sha256": sha(probe)}
        exclusive(receipt, (json.dumps(record, indent=2) + "\n").encode())
    if current != probe:
        replace(active, probe)
    return "Probe installed and verified; original loader backed up."


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sd_root", type=Path, help="Mounted SD card root")
    parser.add_argument("--restore", action="store_true", help="Restore this probe's verified original loader")
    args = parser.parse_args()
    try:
        print(install(args.sd_root, Path(__file__).resolve().parent, args.restore))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(2, f"Install/restore refused: {error}\n")


if __name__ == "__main__":
    main()
