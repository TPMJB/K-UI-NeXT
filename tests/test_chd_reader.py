#!/usr/bin/env python3
"""Real raw/DEFLATE CHDv4 readback, independent zlib oracle and sanitizer checks.

This is a portable restricted-profile decoder test; it does not admit CHD
launch or claim arbitrary v4/v5 codec support. Uses the existing capture oracle
fixture so source audio, density gaps, PAD and four-frame alignment stay real.
"""
import hashlib
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest
import zlib

from test_capture_chd_oracle import FIXTURE, ROOT

FLAGS = ["-std=c11", "-O1", "-g", "-Iinclude", "-I.deps/fatfs/source", "-Isrc/core",
         "-Ithird_party/miniz", "-Ithird_party/minilzo", "-DMINIZ_NO_ARCHIVE_APIS",
         "-DMINIZ_NO_STDIO", "-DMINIZ_NO_TIME", "-DMINIZ_NO_ZLIB_APIS", "-DMINIZ_NO_MALLOC"]


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, timeout=120, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command}\n{result.stdout.decode(errors='replace')}\n{result.stderr.decode(errors='replace')}")
    return result


class CHDReader(unittest.TestCase):
    def test_capture_fixture_and_corruption_with_sanitizers(self):
        with tempfile.TemporaryDirectory(prefix="kui-chd-reader-") as temporary:
            folder = Path(temporary)
            fixture = folder / "fixture.c"
            fixture.write_text(FIXTURE)
            compiler = shlex.split(os.environ.get("CC", "cc"))
            writer = folder / "writer"
            sources = ["src/core/capture_export.c", "src/core/capture_chd.c", "src/core/data.c",
                       "src/core/hash.c", "third_party/miniz/miniz.c", "third_party/minilzo/minilzo.c"]
            run([*compiler, *FLAGS, str(fixture), *sources,
                 "-Wl,--wrap=kui_export_deflate", "-o", str(writer)], cwd=ROOT)
            image = folder / "fixture.chd"
            run([str(writer), str(image)], cwd=folder)
            reader = folder / "reader"
            sanitizers = shlex.split(os.environ.get("KUI_CHD_SANITIZERS", "-fsanitize=address,undefined -fno-omit-frame-pointer"))
            run([*compiler, *FLAGS, *sanitizers, "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                 "tests/test_chd_reader.c", "src/core/chd.c", "src/core/data.c", "src/core/hash.c",
                 "third_party/miniz/miniz.c", "-o", str(reader)], cwd=ROOT)
            result = run([str(reader), str(image)], cwd=folder)
            lines = result.stdout.decode().splitlines()
            self.assertEqual(lines[-1], "CHD reader checks passed")
            actual = {tuple(line.split()[:2]): line.split()[2] for line in lines if line.startswith(("canonical ", "track "))}
            data = image.read_bytes()
            count = struct.unpack_from(">I", data, 24)[0]
            logical = struct.unpack_from(">Q", data, 28)[0]
            digest = hashlib.sha256()
            left = logical
            for hunk in range(count):
                offset, crc, low, high, kind = struct.unpack_from(">QIHBB", data, 108 + 16 * hunk)
                size = low | (high << 16)
                packed = data[offset:offset + size]
                decoded = zlib.decompress(packed, -15) if kind == 1 else packed
                self.assertEqual(zlib.crc32(decoded), crc)
                digest.update(decoded[:left])
                left -= min(left, len(decoded))
            self.assertEqual(left, 0)
            self.assertEqual(actual[("canonical", "0")], digest.hexdigest())
            for track in range(5):
                original = (folder / f"track{track + 1:02}.bin").read_bytes()
                self.assertEqual(actual[("track", str(track))], hashlib.sha256(original).hexdigest())


if __name__ == "__main__":
    unittest.main()
