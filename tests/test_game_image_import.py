# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic compressed bytes and controlled chdman; originals stay untouched."""
import ctypes
import ctypes.util
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("game_image_import", ROOT / "tools/game_image_import.py")
importer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(importer)


def deflate(data):
    stream = zlib.compressobj(level=9, wbits=-15)
    return stream.compress(data) + stream.flush()


def lz4_run(data):
    """One literal, one overlapping match, five final literals; original fixture."""
    assert len(data) >= 25 and data == data[:1] * len(data)
    extra = len(data) - 25
    extension = bytearray()
    while extra >= 255:
        extension.append(255)
        extra -= 255
    extension.append(extra)
    return b"\x1f" + data[:1] + b"\x01\x00" + extension + b"\x50" + data[-5:]


def cso(payloads, methods, *, magic=b"CISO", version=1, shift=0, block_size=2048):
    align = 1 << shift
    start = 24 + 4 * (len(payloads) + 1)
    start = (start + align - 1) // align * align
    indices, stored = [], bytearray(b"\xad" * (start - 24 - 4 * (len(payloads) + 1)))
    for data, method in zip(payloads, methods):
        flagged = method == "raw" if version != 2 else method == "lz4"
        indices.append(start >> shift | (0x80000000 if flagged else 0))
        block = (method if isinstance(method, bytes) else data if method == "raw"
                 else deflate(data) if method == "deflate" else lz4_run(data))
        if version == 2 and method == "raw":
            block = block.ljust(block_size, b"\0")
        stored.extend(block)
        padding = (-len(block)) % align
        stored.extend(b"\xad" * padding)
        start += len(block) + padding
    indices.append(start >> shift)
    header = struct.pack("<4sIQIBB2s", magic, 24, sum(map(len, payloads)), block_size, version, shift, b"\0\0")
    return header + struct.pack(f"<{len(indices)}I", *indices) + stored


def chd(*, version=5, tag=b"CHGD", subtype=b"NONE", logical=19584, cycle=False):
    length = {3: 120, 4: 108, 5: 124}[version]
    header = bytearray(length)
    struct.pack_into(">8sII", header, 0, b"MComprHD", length, version)
    struct.pack_into(">Q", header, 32 if version == 5 else 28, logical)
    struct.pack_into(">Q", header, 48 if version == 5 else 36, length)
    struct.pack_into(">I", header, {3: 76, 4: 44, 5: 56}[version], 19584)
    payload = b"TRACK:1 TYPE:MODE1_RAW SUBTYPE:" + subtype + b" FRAMES:1\0"
    if tag in (b"CHGD", b"CHGT"):
        payload = payload[:-1] + b" PAD:0 PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0\0"
    return header + struct.pack(">4sIQ", tag, len(payload), length if cycle else 0) + payload


class GameImageImportTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="kui-import-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.original = self.root / "original"
        self.original.mkdir()
        self.source = self.original / "Synthetic.cso"
        self.output = self.root / "expanded"
        self.calls = self.root / "calls.jsonl"
        self.payloads = [b"A" * 2048, bytes(range(256)) * 8, b"Z" * 2048]

    def assert_not_published(self):
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.root.glob(".*.staging-*")), [])

    def write_cso(self, **kwargs):
        self.source.write_bytes(cso(self.payloads, ["deflate", "raw", "deflate"], **kwargs))

    def fake_chdman(self, *, fail=None, gdrom=True, unsafe=False, missing=False, extra=False):
        executable = self.root / "fake-chdman"
        executable.write_text(f'''#!{sys.executable}
import json
from pathlib import Path
import sys
args = sys.argv[1:]
with Path({str(self.calls)!r}).open("a") as stream:
    stream.write(json.dumps(args) + "\\n")
if args[0] == {fail!r}:
    print("synthetic failure")
    sys.exit(7)
if args[0] == "extractcd":
    output = Path(args[args.index("-o") + 1])
    name = "../outside.bin" if {unsafe!r} else "track01.bin"
    if {gdrom!r} and output.suffix == ".gdi":
        output.write_text('1\\n1 0 4 2352 "' + name + '" 0\\n')
    else:
        output.write_text('REM SESSION 01\\nFILE "' + name + '" BINARY\\n'
                          '  TRACK 01 MODE1/2352\\n    INDEX 01 00:00:00\\n')
    if not {missing!r}:
        (output.parent / "track01.bin").write_bytes(bytes([71]) * 2352)
    if {extra!r}:
        (output.parent / "unexpected.bin").write_bytes(bytes([72]) * 2352)
print("synthetic chdman completed")
''', encoding="utf-8")
        executable.chmod(0o755)
        return executable

    def test_cso_v1_mixed_exact_bytes_original_hash_and_saved_report(self):
        self.write_cso(shift=4)
        original = self.source.read_bytes()
        report = importer.import_image(self.source, self.output)
        data = b"".join(self.payloads)
        self.assertEqual((self.output / "disc.iso").read_bytes(), data)
        self.assertEqual(self.source.read_bytes(), original)
        self.assertEqual(report["source"]["sha256"], hashlib.sha256(original).hexdigest())
        self.assertEqual(report["outputs"], [{"file": "disc.iso", "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}])
        self.assertEqual(json.loads((self.output / "import-report.json").read_text()), report)
        self.assertFalse(report["verification"]["game_compatibility_checked"])

    def test_cso_v1_unreliable_header_and_reserved_fields_are_ignored(self):
        self.write_cso(version=0)
        data = bytearray(self.source.read_bytes())
        struct.pack_into("<I", data, 4, 0xdeadbeef)
        data[22:24] = b"xy"
        self.source.write_bytes(data)
        importer.import_image(self.source, self.output)
        self.assertEqual((self.output / "disc.iso").read_bytes(), b"".join(self.payloads))

    def test_cso_v2_deflate_lz4_and_uncompressed_with_flag_ignored_for_raw(self):
        self.payloads = [b"A" * 2048, b"B" * 2048, bytes(range(256)) * 8]
        data = bytearray(cso(self.payloads, ["deflate", "lz4", "raw"], version=2, shift=3))
        # v2 rawness comes from stored length; its high bit is ignored.
        struct.pack_into("<I", data, 32, struct.unpack_from("<I", data, 32)[0] | 0x80000000)
        self.source.write_bytes(data)
        report = importer.import_image(self.source, self.output)
        self.assertEqual(report["image"]["format"], "CSO v2")
        self.assertEqual((self.output / "disc.iso").read_bytes(), b"".join(self.payloads))

    def test_zso_lz4_overlapping_match_padding_and_raw(self):
        self.payloads = [b"Q" * 2048, bytes(range(256)) * 8]
        self.source = self.original / "Synthetic.zso"
        self.source.write_bytes(cso(self.payloads, ["lz4", "raw"], magic=b"ZISO", shift=5))
        report = importer.import_image(self.source, self.output)
        self.assertEqual(report["image"]["format"], "ZSO (LZ4)")
        self.assertEqual((self.output / "disc.iso").read_bytes(), b"".join(self.payloads))

    def test_partial_last_block_and_non_power_of_two_block_size(self):
        self.payloads = [b"a" * 6144, b"z" * 2048]
        self.source.write_bytes(cso(self.payloads, ["deflate", "deflate"], block_size=6144))
        importer.import_image(self.source, self.output)
        self.assertEqual((self.output / "disc.iso").read_bytes(), b"".join(self.payloads))

    def test_system_lz4_encoder_interoperability(self):
        library = ctypes.util.find_library("lz4")
        if not library:
            self.skipTest("system liblz4 is unavailable")
        codec = ctypes.CDLL(library)
        codec.LZ4_compressBound.argtypes = (ctypes.c_int,)
        codec.LZ4_compressBound.restype = ctypes.c_int
        codec.LZ4_compress_default.argtypes = (ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
        codec.LZ4_compress_default.restype = ctypes.c_int
        for size in (2048, 6144, 65536, 1024 * 1024):
            data = (b"interoperability" + bytes(range(256))) * (size // 272 + 1)
            data = data[:size]
            destination = ctypes.create_string_buffer(codec.LZ4_compressBound(size))
            actual = codec.LZ4_compress_default(data, destination, size, len(destination))
            self.assertGreater(actual, 0)
            decoded, consumed = importer.decode_lz4(destination.raw[:actual], size)
            self.assertEqual(decoded, data)
            self.assertEqual(consumed, actual)

    def test_invalid_lz4_offsets_lengths_and_end_conditions(self):
        for data, message in ((b"\x00\x00\x00", "offset"), (b"\x00\x01\x00", "offset"),
                              (b"\xf0\xff", "length"), (b"\xf0\xff" * 10, "length"),
                              (b"\x10x\x01\x00", "incomplete|Truncated")):
            with self.subTest(data=data):
                with self.assertRaisesRegex(ValueError, message):
                    importer.decode_lz4(data, 32)
        with self.assertRaisesRegex(ValueError, "incorrect output"):
            importer.decode_deflate(deflate(b"x" * 4096), 2048)

    def test_lzo_dependency_failure_is_clear_and_does_not_publish(self):
        self.source.write_bytes(cso([b"Q" * 2048], ["lz4"], magic=b"ZISO"))
        importer.load_lzo_decoder.cache_clear()
        self.addCleanup(importer.load_lzo_decoder.cache_clear)
        with patch.object(importer.ctypes.util, "find_library", return_value=None):
            with self.assertRaisesRegex(ValueError, "installed liblzo2"):
                importer.import_image(self.source, self.output, zso_codec="lzo")
        self.assert_not_published()
        with self.assertRaisesRegex(ValueError, "Cannot load safe LZO decoder"):
            importer.import_image(self.source, self.output, zso_codec="lzo",
                                  lzo_library="nonexistent-liblzo2-kui-test")
        self.assert_not_published()
        with self.assertRaisesRegex(ValueError, "automatic guessing"):
            importer.import_image(self.source, self.output, zso_codec="auto")
        self.assert_not_published()

    def test_lzo_safe_encoder_interoperability_padding_and_explicit_dialect(self):
        library = os.environ.get("KUI_TEST_LZO_LIBRARY") or ctypes.util.find_library("lzo2")
        if not library:
            self.skipTest("liblzo2 unavailable; KUI_TEST_LZO_LIBRARY can select a test oracle")
        codec = ctypes.CDLL(library)
        codec.lzo1x_1_compress.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                          ctypes.POINTER(ctypes.c_size_t), ctypes.c_void_p)
        codec.lzo1x_1_compress.restype = ctypes.c_int
        for size in (2048, 6144, 65536, 1024 * 1024):
            data = ((b"LZO dialect" + bytes(range(256))) * (size // 267 + 1))[:size]
            destination = ctypes.create_string_buffer(size + size // 16 + 67)
            written = ctypes.c_size_t(len(destination))
            workspace = ctypes.create_string_buffer(1024 * 1024)
            status = codec.lzo1x_1_compress(data, size, destination, ctypes.byref(written), workspace)
            self.assertEqual(status, 0)
            encoded = destination.raw[:written.value]
            for padding in (0, 1, 15, 65535):
                decoded, consumed = importer.decode_lzo(encoded + b"\xad" * padding, size, padding, library)
                self.assertEqual(decoded, data)
                self.assertEqual(consumed, len(encoded))
            with self.assertRaisesRegex(ValueError, "oversized"):
                importer.decode_lzo(encoded, size - 1, library=library)
            with self.assertRaisesRegex(ValueError, "excess padding"):
                importer.decode_lzo(encoded + b"padding", size, 1, library)
            if size == 2048:
                self.source.write_bytes(cso([data], [encoded], magic=b"ZISO", shift=4))
                with self.assertRaisesRegex(ValueError, "LZ4"):
                    importer.import_image(self.source, self.output)
                self.assert_not_published()
                report = importer.import_image(self.source, self.output, zso_codec="lzo", lzo_library=library)
                self.assertEqual(report["image"]["zso_codec"], "lzo")
                self.assertEqual(report["image"]["format"], "ZSO (LZO1X)")
                self.assertEqual((self.output / "disc.iso").read_bytes(), data)
                shutil.rmtree(self.output)

    def test_indices_into_header_repeated_backward_and_past_eof_are_rejected(self):
        self.write_cso()
        original = self.source.read_bytes()
        first, second = struct.unpack_from("<II", original, 24)
        for offset, value, message in ((24, 0, "header/index"), (28, first, "increase"),
                                       (28, first - 1, "increase"), (36, len(original) + 1, "within the file")):
            with self.subTest(offset=offset, value=value):
                data = bytearray(original)
                struct.pack_into("<I", data, offset, value)
                self.source.write_bytes(data)
                with self.assertRaisesRegex(ValueError, message):
                    importer.import_image(self.source, self.output)
                self.assert_not_published()

    def test_bad_headers_size_limits_truncation_and_unsupported_extensions(self):
        self.write_cso()
        original = self.source.read_bytes()
        cases = [(8, "<Q", 0, "Uncompressed"), (8, "<Q", 2**40, "Uncompressed"),
                 (16, "<I", 0, "Block size"), (16, "<I", 2049, "Block size"),
                 (21, "<B", 17, "shift"), (20, "<B", 7, "ZSO")]
        for offset, encoding, value, message in cases:
            with self.subTest(offset=offset, value=value):
                data = bytearray(original)
                struct.pack_into(encoding, data, offset, value)
                self.source.write_bytes(data)
                with self.assertRaisesRegex(ValueError, message):
                    importer.import_image(self.source, self.output)
                self.assert_not_published()
        self.source.write_bytes(original[:26])
        with self.assertRaisesRegex(ValueError, "index"):
            importer.import_image(self.source, self.output)
        self.source.write_bytes(b"unsupported nrg contents")
        with self.assertRaisesRegex(ValueError, "Unsupported image"):
            importer.import_image(self.source, self.output)
        self.assert_not_published()

    def test_deflate_damage_and_extra_padding_do_not_publish_partial_output(self):
        self.write_cso()
        data = bytearray(self.source.read_bytes())
        first = struct.unpack_from("<I", data, 24)[0]
        data[first:first + 4] = b"\xff" * 4
        self.source.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "Block 0"):
            importer.import_image(self.source, self.output)
        self.assert_not_published()
        self.write_cso()
        self.source.write_bytes(self.source.read_bytes() + b"trailing")
        with self.assertRaisesRegex(ValueError, "after final"):
            importer.import_image(self.source, self.output)
        self.assert_not_published()

    def test_source_mutation_aborts_and_original_directory_remains_unwritten(self):
        self.write_cso()
        names = sorted(path.name for path in self.original.iterdir())

        def change_source(_line):
            with self.source.open("r+b") as stream:
                stream.seek(-1, 2)
                stream.write(b"!")

        with self.assertRaisesRegex(ValueError, "Source changed"):
            importer.import_image(self.source, self.output, progress=change_source)
        self.assert_not_published()
        self.assertEqual(sorted(path.name for path in self.original.iterdir()), names)

    def test_existing_target_source_descendant_and_symlink_refused(self):
        self.write_cso()
        self.output.mkdir()
        with self.assertRaisesRegex(ValueError, "already exists"):
            importer.import_image(self.source, self.output)
        self.output.rmdir()
        with self.assertRaisesRegex(ValueError, "outside"):
            importer.import_image(self.source, self.original / "copy")
        link = self.root / "source-link.cso"
        link.symlink_to(self.source)
        with self.assertRaisesRegex(ValueError, "regular file"):
            importer.import_image(link, self.output)
        self.assert_not_published()

    def test_target_created_during_import_is_not_overwritten(self):
        self.write_cso()
        publish = importer.publish_directory

        def concurrent_create(stage, target):
            target.mkdir()
            publish(stage, target)

        with patch.object(importer, "publish_directory", concurrent_create):
            with self.assertRaises(FileExistsError):
                importer.import_image(self.source, self.output)
        self.assertTrue(self.output.is_dir())
        self.assertEqual(list(self.output.iterdir()), [])
        self.assertEqual(list(self.root.glob(".*.staging-*")), [])

    def test_chd_gd_versions_keep_gdi_addresses_and_hash_normalized_outputs(self):
        executable = self.fake_chdman()
        for version in (3, 4, 5):
            with self.subTest(version=version):
                self.source.write_bytes(chd(version=version))
                original = self.source.read_bytes()
                report = importer.import_image(self.source, self.output, chdman=executable)
                self.assertEqual(report["launch_file"], "disc.gdi")
                self.assertTrue(report["image"]["gd_geometry_verified"])
                self.assertTrue(report["image"]["chdman_verified"])
                self.assertEqual(report["image"]["version"], version)
                self.assertEqual(self.source.read_bytes(), original)
                self.assertEqual((self.output / "track01.bin").read_bytes(), b"G" * 2352)
                for record in report["outputs"]:
                    data = (self.output / record["file"]).read_bytes()
                    self.assertEqual(record["sha256"], hashlib.sha256(data).hexdigest())
                calls = [json.loads(line) for line in self.calls.read_text().splitlines()]
                self.assertEqual([call[0] for call in calls[-2:]], ["verify", "extractcd"])
                self.assertNotIn("-sb", calls[-1])
                self.assertEqual(Path(calls[-1][calls[-1].index("-o") + 1]).suffix,
                                 ".gdi")
                self.assertNotIn("--fix", calls[-2])
                shutil.rmtree(self.output)

    def test_chd_cd_cue_session_and_parent_are_preserved_in_workflow(self):
        self.source.write_bytes(chd(tag=b"CHT2"))
        parent = self.original / "Parent.chd"
        parent.write_bytes(chd(tag=b"CHT2"))
        executable = self.fake_chdman(gdrom=False)
        report = importer.import_image(self.source, self.output, chdman=executable, parent=parent)
        self.assertEqual(report["launch_file"], "disc.cue")
        self.assertIn("REM SESSION 01", (self.output / "disc.cue").read_text())
        self.assertEqual(report["parent"]["sha256"], hashlib.sha256(parent.read_bytes()).hexdigest())
        for call in map(json.loads, self.calls.read_text().splitlines()):
            self.assertEqual(call[call.index("-ip") + 1], str(parent))

    def test_chd_subchannel_drop_requires_explicit_opt_in(self):
        self.source.write_bytes(chd(subtype=b"RW_RAW"))
        executable = self.fake_chdman()
        with self.assertRaisesRegex(ValueError, "--drop-subchannels"):
            importer.import_image(self.source, self.output, chdman=executable)
        self.assertFalse(self.calls.exists())
        self.assert_not_published()
        report = importer.import_image(self.source, self.output, chdman=executable, drop_subchannels=True)
        self.assertTrue(report["image"]["subchannel_omitted"])
        self.assertFalse(report["image"]["subchannel_preserved"])

    def test_chd_verify_and_extract_failures_do_not_publish(self):
        self.source.write_bytes(chd())
        for operation in ("verify", "extractcd"):
            with self.subTest(operation=operation):
                executable = self.fake_chdman(fail=operation)
                with self.assertRaisesRegex(ValueError, f"{operation} failed.*synthetic failure"):
                    importer.import_image(self.source, self.output, chdman=executable)
                self.assert_not_published()

    def test_chd_invalid_metadata_non_cd_missing_tool_and_size_limit(self):
        for data, message in ((chd(cycle=True), "cyclic"), (chd(tag=b"GDDD"), "not a CD/GD"),
                              (chd(logical=importer.MAX_IMAGE_BYTES + 1), "logical size")):
            with self.subTest(message=message):
                self.source.write_bytes(data)
                with self.assertRaisesRegex(ValueError, message):
                    importer.import_image(self.source, self.output)
                self.assert_not_published()
        self.source.write_bytes(chd())
        with self.assertRaisesRegex(ValueError, "requires MAME chdman"):
            importer.import_image(self.source, self.output, chdman="nonexistent-chdman-kui-test")
        self.assert_not_published()

    def test_chd_unsafe_missing_and_unreferenced_files_rejected(self):
        self.source.write_bytes(chd())
        for options, message in (({"unsafe": True}, "Unsafe or missing"),
                                  ({"missing": True}, "Unsafe or missing"),
                                  ({"extra": True}, "Unreferenced")):
            with self.subTest(options=options):
                executable = self.fake_chdman(**options)
                with self.assertRaisesRegex(ValueError, message):
                    importer.import_image(self.source, self.output, chdman=executable)
                self.assert_not_published()

    def test_chd_gdi_unsupported_stride_and_geometry_are_not_published(self):
        self.source.write_bytes(chd())
        executable = self.fake_chdman()
        run = importer.run_chdman
        for row, size in (("1 45000 4 2336 track01.bin 0", 2336),
                          ("1 45000 4 2448 track01.bin 0", 2448),
                          ("1 719850 4 2352 track01.bin 0", 2352)):
            with self.subTest(row=row):
                def malformed(command, stage, timeout):
                    result = run(command, stage, timeout)
                    if command[1] == "extractcd":
                        (stage / "disc.gdi").write_text("1\n" + row + "\n")
                        (stage / "track01.bin").write_bytes(b"G" * size)
                    return result
                with patch.object(importer, "run_chdman", malformed):
                    with self.assertRaises(ValueError):
                        importer.import_image(self.source, self.output, chdman=executable)
                self.assert_not_published()

    def test_chd_cue_missing_duplicate_reversed_and_out_of_bounds_indices_rejected(self):
        self.source.write_bytes(chd(tag=b"CHT2"))
        executable = self.fake_chdman(gdrom=False)
        run = importer.run_chdman
        prefix = 'FILE "track01.bin" BINARY\n TRACK 01 MODE1/2352\n'
        suffixes = ("", "INDEX 01\n", "INDEX 01 invalid\n", "INDEX 01 00:60:00\n",
                    "INDEX 01 00:00:75\n", "INDEX 01 00:00:01\n",
                    "INDEX 02 00:00:00\n", "INDEX 01 00:00:00\nINDEX 01 00:00:00\n",
                    "INDEX 01 00:00:00\nINDEX 00 00:00:00\n",
                    "INDEX 01 00:00:00\nINDEX 02 00:00:01\n",
                    "INDEX 01 00:00:00\nREM SESSION 03\n",
                    "INDEX 01 00:00:00\nREM SESSION 02\nREM SESSION 01\n",
                    "INDEX 01 00:00:00\nPREGAP 159:57:74\nPOSTGAP 00:00:02\n",
                    "INDEX 01 00:00:00\nPREGAP 00:00:00\nPREGAP 00:00:00\n",
                    "INDEX 01 00:00:00\nREM LEAD-IN 00:00:01\n",
                    "INDEX 01 00:00:00\nREM LEAD-OUT 00:00:01\n",
                    "INDEX 01 00:00:00\nBOGUS data\n",
                    "INDEX 01 00:00:00\nREM nul\0\n")
        for suffix in suffixes:
            with self.subTest(suffix=suffix):
                def malformed(command, stage, timeout):
                    result = run(command, stage, timeout)
                    if command[1] == "extractcd":
                        (stage / "disc.cue").write_text(prefix + suffix)
                    return result
                with patch.object(importer, "run_chdman", malformed):
                    with self.assertRaises(ValueError):
                        importer.import_image(self.source, self.output, chdman=executable)
                self.assert_not_published()

    def test_extracted_cue_valid_indices_gaps_and_two_sessions(self):
        text = ('REM SESSION 01\nTITLE "metadata café"\nREM\n'
                'FILE "first.bin" BINARY\nTRACK 01 AUDIO\nINDEX 00 00:00:00\n'
                'INDEX 01 00:00:01\nINDEX 02 00:00:02\nPOSTGAP 00:00:01\n'
                'REM LEAD-OUT 00:00:01\nREM SESSION 02\nREM LEAD-IN 00:00:01\n'
                'FILE "second.bin" BINARY\nTRACK 02 MODE2/2336\nPREGAP 00:00:01\n'
                'INDEX 01 00:00:00\n')
        count, references = importer.validate_cue(text, {"first.bin": ("first.bin", 2352 * 3),
                                                       "second.bin": ("second.bin", 2336 * 2)})
        self.assertEqual(count, 2)
        self.assertEqual(references, {"first.bin", "second.bin"})
        with self.assertRaisesRegex(ValueError, "split-track"):
            importer.validate_cue(text + "TRACK 03 AUDIO\nINDEX 01 00:00:00\n",
                                  {"first.bin": ("first.bin", 2352 * 3),
                                   "second.bin": ("second.bin", 2336 * 2)})

    def test_chd_timeout_cleanup(self):
        self.source.write_bytes(chd())
        executable = self.fake_chdman()
        with patch.object(importer.subprocess, "run", side_effect=subprocess.TimeoutExpired("chdman", 1)):
            with self.assertRaisesRegex(ValueError, "time limit"):
                importer.import_image(self.source, self.output, chdman=executable, timeout=1)
        self.assert_not_published()

    def test_real_chdman_round_trip_when_installed(self):
        executable = shutil.which("chdman")
        if not executable:
            self.skipTest("MAME chdman unavailable; controlled subprocess integration is tested separately")
        payload = b"".join(self.payloads)
        (self.original / "track.bin").write_bytes(payload)
        cue = self.original / "input.cue"
        cue.write_text('FILE "track.bin" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n')
        self.source = self.original / "Synthetic.chd"
        created = subprocess.run([executable, "createcd", "-i", str(cue), "-o", str(self.source),
                                  "-c", "none"], capture_output=True, text=True, check=False)
        self.assertEqual(created.returncode, 0, created.stdout + created.stderr)
        report = importer.import_image(self.source, self.output, chdman=executable)
        self.assertEqual(report["launch_file"], "disc.cue")
        bins = [record for record in report["outputs"] if record["file"].endswith(".bin")]
        self.assertEqual(len(bins), 1)
        self.assertEqual((self.output / bins[0]["file"]).read_bytes(), payload)

    def test_cli_success_and_failure(self):
        self.write_cso()
        command = [sys.executable, str(ROOT / "tools/game_image_import.py"), str(self.source), str(self.output)]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("disc.iso", result.stdout)
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("Output already exists", result.stderr)


if __name__ == "__main__":
    unittest.main()
