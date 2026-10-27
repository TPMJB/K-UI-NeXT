#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cross-check read-only projection with both production C codecs."""
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))
from test_vmu_persist_profile_tool import fixture, sector, seal
from vmu_persist_profile import decode_profile as decode_persistent
from vmu_persist_profile import encode_profile as encode_persistent
from vmu_sd_profile import decode_profile as decode_readonly
from vmu_persist_to_readonly import project_profile, write_new


C_PROJECT = r'''
#include "kui/vmu_persist_profile.h"
#include "kui/vmu_sd_profile.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    uint8_t input[4096],output[1536];
    struct kui_vmu_persist_profile p;
    struct kui_vmu_sd_profile r={0},check;
    if(argc!=3) return 1;
    FILE *f=fopen(argv[1],"rb");
    if(!f) return 2;
    size_t n=fread(input,1,sizeof(input),f);
    int extra=fgetc(f),closed=fclose(f);
    if(n!=sizeof(input) || extra!=EOF || closed ||
       kui_vmu_persist_profile_decode(&p,input,sizeof(input))) return 3;
    r.card_sectors=p.card_sectors;r.partition_first=p.partition_first;
    r.partition_end=p.partition_end;r.transport=p.transport;
    r.high_capacity=p.high_capacity;r.image_bytes=131072;
    memcpy(r.cid,p.cid,16);memcpy(r.image_id,p.image_id,16);
    memcpy(r.sha256,p.base_sha256,32);memcpy(r.block_crc,p.block_crc,sizeof(r.block_crc));
    r.extent_count=p.files[KUI_VMU_PERSIST_BASE].useful_count;
    for(unsigned i=0;i<r.extent_count;i++) {
        const struct kui_vmu_persist_extent *e=&p.files[KUI_VMU_PERSIST_BASE].useful[i];
        r.extents[i]=(struct kui_vmu_sd_profile_extent){e->logical_block,e->lba,e->blocks};
    }
    if(kui_vmu_sd_profile_encode(&r,output) ||
       kui_vmu_sd_profile_decode(&check,output,sizeof(output))) return 4;
    f=fopen(argv[2],"wb");if(!f) return 5;
    n=fwrite(output,1,sizeof(output),f);closed=fclose(f);
    return n!=sizeof(output) || closed;
}
'''


class PersistToReadonlyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temporary.name)
        source = cls.directory / "project.c"
        source.write_text(C_PROJECT)
        cls.binary = cls.directory / "project"
        subprocess.run([os.environ.get("HOST_CC", "cc"), "-std=c11", "-O0",
                        "-Wall", "-Wextra", "-Werror", "-pedantic", "-Iinclude",
                        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                        str(source), "src/core/vmu_persist_profile.c",
                        "src/core/vmu_sd_profile.c", "src/core/hash.c",
                        "src/core/retail_image.c", "-o", str(cls.binary)],
                       cwd=ROOT, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def cli(self, *arguments):
        return subprocess.run([sys.executable, str(ROOT / "tools/vmu_persist_to_readonly.py"),
                               *map(str, arguments)], capture_output=True, text=True)

    def test_exact_projection_matches_independent_production_codecs(self):
        profiles = [fixture(1, 1), fixture(2, 256), fixture(2, 65536)]
        fragmented = fixture(2, 1)
        base = fragmented["files"][0]
        base["useful"] = [dict(logical_block=i * 16, lba=8200 + i * 32, blocks=16)
                          for i in range(16)]
        base["allocated"] = copy.deepcopy(base["useful"])
        profiles.append(fragmented)
        for profile in profiles:
            with self.subTest(spc=profile["geometry"]["sectors_per_cluster"],
                              extents=len(profile["files"][0]["useful"])):
                wire = encode_persistent(profile)
                source, output = self.directory / "p2.bin", self.directory / "r1.bin"
                source.write_bytes(wire)
                subprocess.run([str(self.binary), str(source), str(output)], check=True)
                result = project_profile(wire)
                self.assertEqual(result, output.read_bytes())
                summary = decode_readonly(result)
                self.assertEqual(summary["extents"], profile["files"][0]["useful"])
                self.assertEqual(summary["image_sha256"], profile["base_sha256"])
                self.assertEqual(result[56:88], wire[56:88])
                self.assertEqual(result[320:1344], wire[1856:2880])
                self.assertEqual(sum(e["blocks"] for e in summary["extents"]), 256)
                self.assertEqual(source.read_bytes(), wire)

    def test_optional_raw_image_and_mutable_inputs_are_preserved(self):
        wire = bytearray(encode_persistent(fixture()))
        image = bytearray(b"".join(sector(0, block) for block in range(256)))
        before_wire, before_image = bytes(wire), bytes(image)
        self.assertEqual(project_profile(wire, image), project_profile(before_wire))
        self.assertEqual(bytes(wire), before_wire)
        self.assertEqual(bytes(image), before_image)

    def test_raw_image_size_sha_and_each_block_crc_are_required(self):
        profile = fixture()
        image = b"".join(sector(0, block) for block in range(256))
        wire = encode_persistent(profile)
        for invalid in (b"", image[:-1], image + b"\0", image[:32] + b"x" + image[33:]):
            with self.assertRaises(ValueError):
                project_profile(wire, invalid)
        # A matching whole-image SHA does not bypass a stale per-sector CRC.
        for block in range(256):
            changed = copy.deepcopy(profile)
            changed["block_crc"][block] ^= 1
            with self.subTest(block=block), self.assertRaisesRegex(ValueError, "CRC differs"):
                project_profile(encode_persistent(changed), image)

    def test_resealed_unclean_or_invalid_p2_never_becomes_readonly(self):
        wire = encode_persistent(fixture())
        mutations = ((2880, "Q", 1), (384, "I", 1),  # sequence and zero-marker digest
                     (20, "I", 2), (28, "I", 0), (32, "Q", 0),
                     (512 + 40, "I", 17), (512 + 64, "I", 1),
                     (512 + 68, "I", 2047), (512 + 72, "I", 255),
                     (960 + 8, "I", 10), (188, "I", 1))
        for at, fmt, value in mutations:
            changed = bytearray(wire)
            struct.pack_into("<" + fmt, changed, at, value)
            changed = bytearray(seal(changed))
            before = bytes(changed)
            with self.subTest(at=at), self.assertRaises(ValueError):
                project_profile(changed)
            self.assertEqual(bytes(changed), before)
        for at, count in ((56, 16), (72, 16), (88, 32), (152, 32)):
            changed = bytearray(wire);changed[at:at + count] = bytes(count)
            with self.subTest(at=at), self.assertRaises(ValueError):
                project_profile(seal(changed))

    def test_corrupt_truncated_wrong_format_and_nonbinary_inputs_refuse(self):
        wire = encode_persistent(fixture())
        corrupt = bytearray(wire);corrupt[2000] ^= 1
        for invalid in (b"", wire[:-1], wire + b"\0", corrupt,
                        project_profile(wire), "profile", None):
            with self.assertRaises(ValueError):
                project_profile(invalid)
        with self.assertRaises(ValueError):
            project_profile(wire, "image")

    def test_cli_creates_fresh_verified_output_without_changing_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            profile, image, output = [folder / leaf for leaf in ("p2.bin", "card.vmd", "r1.bin")]
            wire = encode_persistent(fixture())
            raw = b"".join(sector(0, block) for block in range(256))
            profile.write_bytes(wire);image.write_bytes(raw)
            result = self.cli(profile, output, "--image", image)
            self.assertEqual(result.returncode, 0, result.stderr)
            summary = json.loads(result.stdout)
            self.assertTrue(summary["image_verified"] and summary["live_verification_required"])
            self.assertEqual(summary["source_profile_sha256"], hashlib.sha256(wire).hexdigest())
            self.assertEqual(summary["output_format"], "KUIVMUR1")
            self.assertNotIn("cid", summary)
            self.assertEqual(output.read_bytes(), project_profile(wire, raw))
            self.assertEqual(profile.read_bytes(), wire)
            self.assertEqual(image.read_bytes(), raw)
            self.assertEqual(decode_persistent(profile.read_bytes())["base_sha256"],
                             decode_readonly(output.read_bytes())["image_sha256"])

    def test_cli_refuses_existing_paths_links_and_failed_admission(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / "p2.bin";source.write_bytes(encode_persistent(fixture()))
            existing = folder / "existing.bin";existing.write_bytes(b"preserve")
            symlink = folder / "symlink.bin";symlink.symlink_to(existing)
            hardlink = folder / "hardlink.bin";hardlink.hardlink_to(source)
            before = source.read_bytes()
            for target in (source, existing, symlink, hardlink):
                result = self.cli(source, target)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(source.read_bytes(), before)
                self.assertEqual(existing.read_bytes(), b"preserve")
            output = folder / "new.bin"
            bad = folder / "bad.bin";bad.write_bytes(before[:-1])
            self.assertNotEqual(self.cli(bad, output).returncode, 0)
            self.assertFalse(output.exists())
            bad_image = folder / "bad.vmd";bad_image.write_bytes(bytes(131072))
            self.assertNotEqual(self.cli(source, output, "--image", bad_image).returncode, 0)
            self.assertFalse(output.exists())

    def test_output_sync_or_readback_failure_removes_only_new_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "new.bin"
            with mock.patch("vmu_persist_to_readonly.os.fsync", side_effect=OSError("sync")):
                with self.assertRaises(OSError):
                    write_new(output, b"new")
            self.assertFalse(output.exists())
            with mock.patch.object(Path, "read_bytes", return_value=b"wrong"):
                with self.assertRaises(OSError):
                    write_new(output, b"new")
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
