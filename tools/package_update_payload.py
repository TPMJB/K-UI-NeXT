#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Put a checked current-engine update in a test or release delivery.

Test packagers call this before computing their final manifest. Every staged
KUI file is passed to the central producer; unknown paths and old GUIs fail
before the outer archive is published.
"""
import hashlib
import os
from pathlib import Path, PurePosixPath
import tempfile

from make_update import make_update


def add_update_payload(files, filename, *, release=False, channel="testing"):
    """Append one .kup containing the exact KUI members in a file mapping."""
    if (not isinstance(filename, str) or PurePosixPath(filename).name != filename or
            not filename.endswith(".kup") or "\\" in filename):
        raise ValueError("The embedded update needs a simple .kup filename")
    if any(name.lower().endswith(".kup") for name in files):
        raise ValueError("A delivery must contain exactly one generated .kup")
    members = {}
    for name, data in files.items():
        if not isinstance(name, str):
            raise ValueError("Unsafe delivery path: " + str(name))
        path = PurePosixPath(name)
        if (path.is_absolute() or str(path) != name or
                ".." in path.parts or "\\" in name):
            raise ValueError("Unsafe delivery path: " + str(name))
        if name == "KUI" or name.startswith("KUI/"):
            relative = name[4:]
            if not relative or not isinstance(data, bytes):
                raise ValueError("Expected a regular KUI software file: " + name)
            members[relative] = data
    if not members:
        raise ValueError("The test delivery has no staged KUI software")
    with tempfile.TemporaryDirectory(prefix="kui-package-payload-") as temporary:
        root = Path(temporary)
        stage = root / "KUI"
        stage.mkdir()
        for name, data in sorted(members.items()):
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        output = root / filename
        report = make_update(stage, output, release=release, channel=channel)
        payload = output.read_bytes()
    # Only mutate the caller's member set after the actual consumer accepts it.
    files[filename] = payload
    return report


def publish_update_payload(payload, output):
    """Publish the identical checked payload separately from optional evidence."""
    output = Path(output).absolute()
    if (output.suffix.lower() != ".kup" or output.is_symlink() or
            any(parent.is_symlink() for parent in output.parents)):
        raise ValueError("The standalone update needs a regular .kup output path")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix="." + output.name + ".", dir=output.parent,
                                         delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        if temporary.read_bytes() != payload:
            raise ValueError("Standalone update differs from the checked ZIP payload")
        os.replace(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return {"output": str(output), "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}


def write_release_update(stage, output, *, channel="stable"):
    """Package an already assembled official software tree without a catalog."""
    return make_update(stage, output, release=True, channel=channel)
