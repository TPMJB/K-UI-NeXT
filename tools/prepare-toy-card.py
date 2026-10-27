#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare a separate Toy Commander card from a current owned VMU pair.

Reads the current image, journal, identity and original P2 descriptor. Does
not repair or modify a source pair, replay a pending journal or install a card.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import struct
import sys
import tempfile
import zlib

HERE = Path(__file__).resolve().parent


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


SUBSET = module("toy_subset", HERE / "vmu-image-subset.py")
PROFILE = module("toy_profile", HERE / "vmu_persist_profile.py")
HOST = HERE / "vmu-image-subset"
if not HOST.is_file():
    HOST = HERE.parent / "build/vmu-subset-tool/vmu-image-subset"


def read_regular(path, size):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size != size:
            raise ValueError(f"Expected a regular {size}-byte file: {path}")
        data = stream.read(size + 1)
        after = os.fstat(stream.fileno())
        fields = ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")
        if len(data) != size or any(getattr(before, f) != getattr(after, f) for f in fields):
            raise ValueError(f"Source changed while reading: {path}")
    return data


def mapping_crc(profile):
    maps = [profile["files"][role]["useful"] for role in (0, 1)]
    crc = zlib.crc32(bytes.fromhex(profile["image_id"]))
    crc = zlib.crc32(struct.pack("<QII", profile["card_sectors"], len(maps[0]), len(maps[1])), crc)
    for extents in maps:
        for extent in extents:
            crc = zlib.crc32(struct.pack("<QI", extent["lba"], extent["blocks"]), crc)
    return crc


def journal_state(image, journal, identity, profile):
    if len(image) != 131072 or len(journal) != 1024 or len(identity) != 16 or not any(identity):
        raise ValueError("Wrong image, journal or identity length")
    if identity != bytes.fromhex(profile["image_id"]):
        raise ValueError("Source identity differs from its ownership profile")
    marker, payload = journal[512:], journal[:512]
    if marker == bytes(512):
        return "clear"
    u32 = lambda offset: struct.unpack_from("<I", marker, offset)[0]
    if (marker[:8] != b"KUIVMUJ1" or u32(8) != 1 or u32(12) != 512
            or marker[16:32] != identity
            or struct.unpack_from("<Q", marker, 32)[0] != profile["card_sectors"]
            or u32(40) != mapping_crc(profile) or u32(44) >= 256 or u32(52) != 1
            or marker[64:508] != bytes(444) or u32(508) != zlib.crc32(marker[:508])
            or u32(48) != zlib.crc32(payload)):
        raise ValueError("Journal marker, payload or ownership check failed; preserve the original pair")
    block = u32(44)
    if image[block * 512:(block + 1) * 512] != payload:
        raise ValueError("Pending journal recovery: launch the original card once, then power off before retrying")
    return "committed_already_applied"


def prepare(pair, output, host=None):
    pair, output = Path(pair).resolve(), Path(output).absolute()
    if output == pair or pair in output.resolve().parents:
        raise ValueError("Output must be outside the source pair")
    if output.exists() or output.is_symlink():
        raise ValueError("Output directory already exists; choose a fresh path")
    profiles = list(pair.glob("retail-persistent-profile-*.bin"))
    if len(profiles) != 1:
        raise ValueError("Source pair must contain exactly one retail-persistent-profile-*.bin")
    sizes = {"card.vmd": 131072, "card.journal": 1024, "identity.bin": 16, profiles[0].name: 4096}
    originals = {name: read_regular(pair / name, size) for name, size in sizes.items()}
    profile = PROFILE.decode_profile(originals[profiles[0].name])
    if pair.name != profile["pair_path"].rsplit("/", 1)[-1]:
        raise ValueError("Source folder name differs from its ownership profile")
    image = originals["card.vmd"]
    state = journal_state(image, originals["card.journal"], originals["identity.bin"], profile)
    # Preparation hashes are historical; successful game saves change them.
    # The current filesystem and retained payloads are checked independently.
    host = Path(host or HOST).resolve()
    with tempfile.TemporaryDirectory(prefix="kui-toy-card-") as temporary:
        snapshot = Path(temporary) / "current-card.vmd"
        snapshot.write_bytes(image)
        before = SUBSET.inventory(host, snapshot)
        selected = [e for e in before["entries"] if bytes.fromhex(e["name_hex"]).startswith(b"TOY_COMM.")]
        names = [e["name"] for e in selected]
        if "TOY_COMM.___" not in names or len(selected) < 2:
            raise ValueError("Need Toy Commander options and at least one saved profile; nothing was created")
        derived = SUBSET.run_host(host, "--construct", snapshot, *(e["slot"] for e in selected))
        proof = SUBSET.retained_proof(image, derived, selected)
        check = Path(temporary) / "derived-card.vmd"
        check.write_bytes(derived)
        after = SUBSET.inventory(host, check)
        if len(after["entries"]) != len(selected):
            raise ValueError("Derived inventory does not match the selected files")
    # Refuse if any input changed during the complete operation.
    for name, data in originals.items():
        if read_regular(pair / name, sizes[name]) != data:
            raise ValueError("Source pair changed during preparation; nothing was created")
    report = {"kind": "toy-commander-dedicated-card-preview", "loader_pair_created": False,
              "source_pair": str(pair), "source_profile_pair": profile["pair_path"],
              "source_sha256": hashlib.sha256(image).hexdigest(), "journal_state": state,
              "source_summary": before["summary"], "output_summary": after["summary"],
              "output_sha256": hashlib.sha256(derived).hexdigest(), "selection": proof,
              "source_files_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in originals.items()},
              "source_preserved": True, "next_step": "Card tools Start preview, then A creates a fresh pair"}
    output.mkdir(mode=0o700)
    target = output / "KUI/vmu-import"
    target.mkdir(parents=True)
    SUBSET.write_new(target / "card.vmd", derived)
    SUBSET.write_new(output / "report.json", (json.dumps(report, indent=2, sort_keys=True) + "\n").encode())
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pair", type=Path, help="current p0004 folder, after full console power-off")
    parser.add_argument("output", type=Path, help="new local output folder, outside the source pair")
    parser.add_argument("--host-tool", type=Path, default=HOST)
    args = parser.parse_args()
    try:
        report = prepare(args.pair, args.output, args.host_tool)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Toy card preparation refused: {error}\n")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
