#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact standalone physical-journal program on disposable real filesystems."""
from pathlib import Path
import os
import re
import shutil
import struct
import sys
import tempfile
import zlib

import test_vmu_persist_harness_images as prep

ROOT = Path(__file__).resolve().parents[1]
PROOF = ROOT / "build/vmu-physical-proof/host-sequence"
sys.path.insert(0, str(ROOT / "tools"))
from vmu_persist_profile import decode_profile  # noqa: E402


def profile_extents(wire, role):
    record = 512 + role * 448
    count = struct.unpack_from("<I", wire, record + 40)[0]
    return [struct.unpack_from("<III", wire, record + 64 + index * 12)
            for index in range(count)]


def permitted_lbas(wire):
    return {lba + index for role in (0, 1)
            for _, lba, blocks in profile_extents(wire, role) for index in range(blocks)}


def outside_digest(image, allowed):
    """Hash every byte except the258 admitted useful image/journal sectors."""
    import hashlib
    result = hashlib.sha256()
    with image.open("rb") as stream:
        for first in range(0, image.stat().st_size // 512, 2048):
            data = bytearray(stream.read(2048 * 512))
            for lba in allowed:
                at = (lba - first) * 512
                if 0 <= at < len(data):
                    data[at:at + 512] = bytes(512)
            result.update(data)
    return result.digest()


def crc16(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
    return crc


def expected_base(original, generation):
    data = bytearray(original)
    package = bytearray(data[199 * 512:200 * 512] + data[198 * 512:199 * 512])
    package[14:16] = f"{generation:02X}".encode()
    package[70:72] = bytes(2)
    struct.pack_into("<H", package, 70, crc16(package[:704]))
    data[199 * 512:200 * 512] = package[:512]
    assert package[512:] == original[198 * 512:199 * 512]
    return bytes(data)


def journal_record(image, wire, base, sequence):
    journal = prep.mapped_bytes(image, wire, 1)[0]
    payload, marker = journal[:512], journal[512:]
    assert payload == base[199 * 512:200 * 512]
    assert marker[:8] == b"KUIVMUJ1" and struct.unpack_from("<II", marker, 8) == (1, 512)
    assert marker[16:32] == wire[72:88]
    assert struct.unpack_from("<Q", marker, 32)[0] == image.stat().st_size // 512
    mapping = bytearray(wire[72:88])
    mapping.extend(struct.pack("<QII", image.stat().st_size // 512,
                               len(profile_extents(wire, 0)), len(profile_extents(wire, 1))))
    for role in (0, 1):
        for _, lba, blocks in profile_extents(wire, role):
            mapping.extend(struct.pack("<QI", lba, blocks))
    assert struct.unpack_from("<IIIIQ", marker, 40) == (
        zlib.crc32(mapping), 199, zlib.crc32(payload), 1, sequence)
    assert not any(marker[64:508])
    assert struct.unpack_from("<I", marker, 508)[0] == zlib.crc32(marker[:508])


def run_proof(image, mode, wire):
    before = outside_digest(image, permitted_lbas(wire))
    output = prep.run(PROOF, image, mode)
    assert outside_digest(image, permitted_lbas(wire)) == before, "proof changed non-admitted sectors"
    writes = re.findall(r"^RAW-WRITE ([0-9a-f]{8}) (\d+)$", output, re.MULTILINE)
    for address, count in writes:
        assert int(address, 16) in permitted_lbas(wire) and int(count) == 1
    assert re.search(r"^RAW-SUMMARY READS \d+ WRITES \d+ SYNCS \d+ CID \d+$", output, re.MULTILINE)
    return output, writes


def replace_wire(image, old, changed):
    data = image.read_bytes()
    offset = data.find(old)
    assert offset >= 0 and data.find(old, offset + 1) < 0, "actual fixture profile must have one contiguous copy"
    changed = bytearray(changed)
    changed[16:20] = bytes(4)
    struct.pack_into("<I", changed, 16, zlib.crc32(changed))
    prep.change_at(image, offset, changed)
    return bytes(changed)


def wrong_mapping(wire):
    """A codec-valid map to free space must still fail fresh ownership."""
    changed = bytearray(wire)
    assert struct.unpack_from("<II", wire, 512 + 40) == (1, 1)
    blocks = struct.unpack_from("<I", wire, 512 + 256 + 8)[0]
    spc = struct.unpack_from("<I", wire, 296)[0]
    clusters = struct.unpack_from("<I", wire, 336)[0]
    first = clusters - blocks // spc - 8
    lba = struct.unpack_from("<Q", wire, 328)[0] + (first - 2) * spc
    struct.pack_into("<I", changed, 512 + 8, first)
    for at in (512 + 64 + 4, 512 + 256 + 4):
        struct.pack_into("<I", changed, at, lba)
    changed[16:20] = bytes(4)
    struct.pack_into("<I", changed, 16, zlib.crc32(changed))
    decode_profile(bytes(changed))
    return changed


def refusal_cases(image, wire, files, temporary):
    for name in ("profile-crc", "wrong-map", "crosslink", "identity-file", "wrong-marker", "cid", "raw", "profile-release"):
        bad = temporary / f"refuse-{name}.img"
        shutil.copyfile(image, bad)
        if name == "profile-crc":
            data = bad.read_bytes()
            offset = data.find(wire)
            assert offset >= 0
            prep.change_at(bad, offset + 88, bytes([wire[88] ^ 1]))
        elif name == "wrong-map":
            replace_wire(bad, wire, wrong_mapping(wire))
        elif name == "crosslink":
            prep.crosslink_journal(bad, wire)
        elif name == "identity-file":
            prep.change_at(bad, prep.logical_lba(wire, 2, 0) * 512, b"\xff")
        elif name == "wrong-marker":
            prep.change_at(bad, prep.logical_lba(wire, 1, 1) * 512,
                           prep.synthetic_marker(wire, files[1][:512]))
        before = prep.digest(bad)
        output = prep.run(PROOF, bad, f"refuse-{name}" if name in ("cid", "raw", "profile-release") else "refuse-commit")
        assert "WRITES 0" in output and prep.digest(bad) == before, f"{name} refusal changed storage"
        bad.unlink()


def final_release_failure(image, wire, files, temporary):
    failed = temporary / "post-commit-release-failure.img"
    shutil.copyfile(image, failed)
    output, writes = run_proof(failed, "refuse-release", wire)
    assert writes and "FAIL: final SCI release / restoration" in output
    assert not re.search(r"^READY:", output, re.MULTILINE)
    expected = expected_base(files[0], 1)
    assert prep.mapped_bytes(failed, wire, 0)[0] == expected
    journal_record(failed, wire, expected, 1)
    # A cleanup fault suppresses success but cannot undo an already durable
    # commit. Preserve the bound pair and recover it through a new session.
    before = prep.digest(failed)
    run_proof(failed, "check", wire)
    assert prep.digest(failed) == before
    failed.unlink()


def exercise(image, wire, files, temporary):
    # X is verification, not an implicit first commit.
    before = prep.digest(image)
    output, writes = run_proof(image, "refuse-check", wire)
    assert not writes and prep.digest(image) == before and "WRITES 0" in output
    refusal_cases(image, wire, files, temporary)
    final_release_failure(image, wire, files, temporary)
    outside = outside_digest(image, permitted_lbas(wire))
    for generation in (1, 2):
        output, writes = run_proof(image, "commit", wire)
        assert writes, output
        base = prep.mapped_bytes(image, wire, 0)[0]
        expected = expected_base(files[0], generation)
        assert base == expected
        changed = {block for block in range(256)
                   if base[block * 512:(block + 1) * 512] != files[0][block * 512:(block + 1) * 512]}
        assert changed == {199}
        journal_record(image, wire, base, generation)
        # Fresh process/card connection validates current bytes, not frozen
        # preparation hashes; ordinary sync retains the valid redo marker.
        before = prep.digest(image)
        run_proof(image, "check", wire)
        assert prep.digest(image) == before
        journal_record(image, wire, base, generation)
    # Replay is tested with an actually stale base sector and the real-map
    # durable record, not merely by observing an already-correct base.
    prep.change_at(image, prep.logical_lba(wire, 0, 199) * 512, files[0][199 * 512:200 * 512])
    _, writes = run_proof(image, "check", wire)
    assert writes and prep.mapped_bytes(image, wire, 0)[0] == expected_base(files[0], 2)
    assert outside_digest(image, permitted_lbas(wire)) == outside


def main():
    for name in ("mkfs.fat", "mkfs.exfat", "fsck.fat", "fsck.exfat"):
        if not shutil.which(name):
            raise SystemExit(f"Missing test prerequisite: {name}")
    with tempfile.TemporaryDirectory(prefix="kui-vmu-physical-proof-") as directory:
        temporary = Path(directory)
        for kind in ("fat32", "exfat"):
            volume = temporary / f"{kind}-volume.img"
            with volume.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            prep.run("mkfs.fat", "-F", "32", volume) if kind == "fat32" else prep.run("mkfs.exfat", volume)
            image = temporary / f"{kind}.img"
            first = 2048
            mbr = bytearray(512)
            mbr[450] = 0x0c if kind == "fat32" else 0x07
            struct.pack_into("<II", mbr, 454, first, volume.stat().st_size // 512)
            mbr[510:512] = b"\x55\xaa"
            with image.open("wb") as out, volume.open("rb") as source:
                out.write(mbr);out.seek(first * 512);shutil.copyfileobj(source, out)
            prep.partition_boot_geometry(image, first, kind)
            volume.unlink()
            prep.run(prep.SEED, image, "create")
            path, wire, files = prep.exported(image, prep.run(prep.PREP, image, "export-persist"), first)
            assert path == "0:/KUI/vmu-test/p0001/retail-persistent-profile-0001.bin"
            exercise(image, wire, files, temporary)
            # The original filesystem remains healthy after actual mapped
            # journal commits and replay, without opening any file for write.
            partition = temporary / f"{kind}-fsck.img"
            with image.open("rb") as source, partition.open("wb") as out:
                source.seek(first * 512);shutil.copyfileobj(source, out)
            prep.run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", partition)
            partition.unlink()
            print(f"PASS {kind}: real-map one-block commit, retained journal, fresh reboot replay, unchanged filesystem/profile/identity/slack, and zero-write refusals", flush=True)


if __name__ == "__main__":
    main()
