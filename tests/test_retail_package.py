# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import retail_package as layout
from check_retail_loader_layout import check_directory, check_stack_usage
from image_probe_package import inspect_image_probe
from loader_package import inspect_probe
from package import release_metadata
from runtime_package import envelope
from test_image_loader_layout import executable


class RetailPackage(unittest.TestCase):
    def payload(self, stage_bytes=16):
        data = bytearray(layout.STAGE_BLOB_OFFSET + stage_bytes)
        data[layout.HEADER_OFFSET:layout.HEADER_OFFSET + layout.HEADER_BYTES] = \
            layout.relocation_header(stage_bytes)
        return data

    @staticmethod
    def packaged(data):
        return envelope(data, len(data), "012345abcdef")

    def test_constants_match_native_contract(self):
        source = (ROOT / "include/kui/retail_loader_layout.h").read_text()
        names = {
            "PACKAGE_VERSION": layout.VERSION, "HEADER_OFFSET": layout.HEADER_OFFSET,
            "HEADER_BYTES": layout.HEADER_BYTES, "MAP_OFFSET": layout.MAP_OFFSET,
            "MAP_BYTES": layout.MAP_BYTES, "STAGE_BLOB_OFFSET": layout.STAGE_BLOB_OFFSET,
            "STAGE_ADDRESS": layout.STAGE_ADDRESS, "STAGE_MAX_BYTES": layout.STAGE_MAX_BYTES,
            "STAGE_MEMORY_END": layout.STAGE_MEMORY_END, "STAGE_STACK": layout.STAGE_STACK,
            "EXEC_ADDRESS": layout.EXEC_ADDRESS, "EXEC_MAX_BYTES": layout.EXEC_MAX_BYTES,
            "RESIDENT_ADDRESS": layout.RESIDENT_ADDRESS, "RESIDENT_LIMIT": layout.RESIDENT_LIMIT,
            "HOOK_STACK_BOTTOM": layout.HOOK_STACK_BOTTOM, "HOOK_STACK": layout.HOOK_STACK,
            "TRAMPOLINE_BYTES": layout.TRAMPOLINE_BYTES,
        }
        for suffix, value in names.items():
            with self.subTest(suffix=suffix):
                found = re.search(r"^#define KUI_RETAIL_" + suffix + r"\s+(\w+)\s*$", source, re.M)
                self.assertIsNotNone(found)
                self.assertEqual(int(found.group(1), 0), value)
        self.assertIn('#define KUI_RETAIL_PACKAGE_MAGIC "KUIRBT01"', source)

    def test_valid_minimum_normal_and_maximum(self):
        for size in (4, 16, layout.STAGE_MAX_BYTES):
            with self.subTest(size=size):
                result = layout.inspect_retail(self.packaged(self.payload(size)))
                self.assertEqual(result["stage_bytes"], size)
                self.assertEqual(result["resident_address"], "0x8c008300")
                self.assertEqual(result["resident_limit"], "0x8c00bb00")
                self.assertIn("title compatibility requires console testing", result["abi"])

    def test_every_inner_header_byte_is_checked(self):
        clean = self.payload()
        for offset in range(layout.HEADER_OFFSET, layout.HEADER_OFFSET + layout.HEADER_BYTES):
            data = clean.copy(); data[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "relocation header"):
                layout.inspect_retail(self.packaged(data))

    def test_manifest_cannot_contain_card_or_game_bytes(self):
        for offset in (0, 1, 511, 512, 2047, 4095):
            data = self.payload(); data[layout.MAP_OFFSET + offset] = 0x55
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "blank card-specific"):
                layout.inspect_retail(self.packaged(data))

    def test_staging_and_relocation_bounds(self):
        clean = self.payload()
        for size in (0, layout.STAGE_MAX_BYTES + 4):
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, "staging size"):
                layout.inspect_retail(self.packaged(self.payload(size)))
        with self.assertRaisesRegex(ValueError, "staging size"):
            layout.inspect_retail(envelope(clean, len(clean) + 4, "012345abcdef"))
        for data in (clean[:-4], clean + bytes(4)):
            with self.assertRaisesRegex(ValueError, "relocation header"):
                layout.inspect_retail(self.packaged(data))
        data = clean.copy(); struct.pack_into("<I", data, 0x100 + 28, 15)
        with self.assertRaisesRegex(ValueError, "relocation header"):
            layout.inspect_retail(self.packaged(data))

    def test_truncation_outer_checksum_and_accepted_probe_separation(self):
        clean = self.payload()
        for size in (4, 0x100, 0x140, 0x1000, 0x2000):
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, "staging size"):
                layout.inspect_retail(self.packaged(clean[:size]))
        package = self.packaged(clean)
        for offset in (0, 60, 64, len(package) - 1):
            data = bytearray(package); data[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                layout.inspect_retail(data)
        for inspect in (inspect_probe, inspect_image_probe):
            with self.assertRaises(ValueError):
                inspect(package)


class ReleaseMetadata(unittest.TestCase):
    def test_canonical_names_preserve_accent_and_match_artifact_version(self):
        release = release_metadata()
        self.assertEqual(release["version"], "1.5.1")
        self.assertIn("Dáinsleif", release["name"])
        self.assertTrue(release["short_name"].isascii())
        self.assertEqual(release["artifact_prefix"], "kui-1.5.1-dainsleif")

    def test_missing_duplicate_control_character_and_unsafe_artifact_names_reject(self):
        source = (ROOT / "include/kui/version.h").read_text(encoding="utf-8")
        invalid = (
            (source.replace('#define KUI_VERSION "1.5.1"', ''), "Missing"),
            (source + '\n#define KUI_VERSION "1.5.1"\n', "repeated"),
            (source.replace('"1.5.1"', '"1.5.1\\n"'), "Invalid"),
            (source.replace('"kui-1.5.1-dainsleif"', '"../escape"'), "artifact prefix"),
            (source.replace('"kui-1.5.1-dainsleif"', '"kui-1.4.0-dainsleif"'), "canonical version"),
        )
        with tempfile.TemporaryDirectory() as tmp:
            header = Path(tmp) / "version.h"
            for text, error in invalid:
                with self.subTest(error=error):
                    header.write_text(text, encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, error):
                        release_metadata(header)


class ResidentStackReports(unittest.TestCase):
    required = ("kui_retail_resident_dispatch", "kui_retail_gd_dispatch",
                "kui_retail_image_read", "kui_loader_sd_stream_next")

    def report(self, directory, extra=(), frame=64, kind="static"):
        names = self.required + tuple(name for name, _, _ in extra)
        lines = [f"test.c:1:1:{name}\t{frame}\t{kind}" for name in self.required]
        lines += [f"test.c:1:1:{name}\t{size}\t{style}" for name, size, style in extra]
        (Path(directory) / "resident.su").write_text("\n".join(lines) + "\n")
        return {"_" + name: layout.RESIDENT_ADDRESS for name in names}

    def test_conservative_bound_and_high_stage_initialization_exclusion(self):
        with tempfile.TemporaryDirectory() as tmp:
            symbols = self.report(tmp, [("kui_retail_resident_init", 6000, "static")])
            result = check_stack_usage(tmp, symbols)
            self.assertEqual(result["conservative_bytes"], 4 * 64 + 256)
            self.assertEqual(result["available_bytes"], 1280 - 16 - 32)

    def test_oversized_dynamic_and_missing_reports_reject(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "Missing compiler"):
                check_stack_usage(tmp, {})
            symbols = self.report(tmp, frame=1024)
            with self.assertRaisesRegex(ValueError, "exceeds"):
                check_stack_usage(tmp, symbols)
            symbols = self.report(tmp, kind="dynamic,bounded")
            with self.assertRaisesRegex(ValueError, "dynamic"):
                check_stack_usage(tmp, symbols)
            symbols = self.report(tmp)
            del symbols["_kui_loader_sd_stream_next"]
            with self.assertRaisesRegex(ValueError, "Missing runtime"):
                check_stack_usage(tmp, symbols)


class RetailLinkedLayout(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.bases = {"resident": layout.RESIDENT_ADDRESS, "stage": layout.STAGE_ADDRESS,
                      "entry": layout.EXEC_ADDRESS}
        self.payload = {"resident": bytearray(128), "stage": bytearray(256)}
        self.symbols = {"resident": {}, "stage": {}, "entry": {}}
        self.memory = {"resident": 512, "stage": 1024}
        rs, ss, es = (self.symbols[name] for name in ("resident", "stage", "entry"))
        for index, name in enumerate(("kui_retail_resident_init", "kui_retail_resident_hook",
                                     "kui_retail_resident_dispatch", "kui_retail_gd_dispatch",
                                     "kui_retail_image_read", "kui_loader_sd_stream_next",
                                     "kui_retail_sd_acquire", "kui_retail_sd_release")):
            rs["_" + name] = layout.RESIDENT_ADDRESS + 4 + index * 4
        rs.update({"__retail_hook_stack": layout.HOOK_STACK,
                   "__retail_hook_stack_bottom": layout.HOOK_STACK_BOTTOM,
                   "_kui_retail_hook_active": layout.RESIDENT_ADDRESS + 128,
                   "_kui_retail_hook_fault": layout.RESIDENT_ADDRESS + 132})
        for index, name in enumerate(("kui_retail_stage_main", "kui_retail_stage_relay",
                                     "kui_retail_bootstrap_enter", "kui_retail_game_resume")):
            ss["_" + name] = layout.STAGE_ADDRESS + 4 + index * 4
        ss["__retail_trampoline_start"] = layout.STAGE_ADDRESS + 128
        ss["__retail_trampoline_end"] = layout.STAGE_ADDRESS + 256
        struct.pack_into("<I", self.payload["stage"], 240,
                         ss["_kui_retail_game_resume"] | 0x20000000)
        ss["__retail_resident_blob_start"] = layout.STAGE_ADDRESS + 256
        self.payload["stage"] += self.payload["resident"]
        ss["__retail_resident_blob_end"] = layout.STAGE_ADDRESS + len(self.payload["stage"])
        for name in ("resident", "stage"):
            base = self.bases[name]; syms = self.symbols[name]
            syms[f"__retail_{name}_binary_end"] = base + len(self.payload[name])
            syms[f"__retail_{name}_bss_begin"] = base + len(self.payload[name])
            syms[f"__retail_{name}_bss_end"] = base + self.memory[name]
        self.payload["entry"] = bytearray(layout.STAGE_BLOB_OFFSET) + self.payload["stage"]
        self.payload["entry"][0x100:0x140] = layout.relocation_header(len(self.payload["stage"]))
        self.memory["entry"] = len(self.payload["entry"])
        es.update({"__retail_map": layout.EXEC_ADDRESS + layout.MAP_OFFSET,
                   "__retail_stage_blob_start": layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET,
                   "__retail_stage_blob_end": layout.EXEC_ADDRESS + len(self.payload["entry"])})
        report = "\n".join(f"test.c:1:1:{name}\t32\tstatic" for name in ResidentStackReports.required)
        (self.directory / "resident.su").write_text(report + "\n")
        self.write()

    def tearDown(self):
        self.temp.cleanup()

    def write(self):
        for name, payload in self.payload.items():
            (self.directory / (name + ".elf")).write_bytes(executable(
                self.bases[name], payload, self.memory[name], self.symbols[name]))
            if name != "entry":
                (self.directory / (name + ".bin")).write_bytes(payload)

    def test_valid_resident_stage_and_entry(self):
        result = check_directory(self.directory)
        self.assertEqual(result["resident"]["payload_bytes"], 128)
        self.assertEqual(result["resident_stack"]["conservative_bytes"], 384)

    def test_embedded_identity_timer_import_and_guard(self):
        self.payload["resident"][0] = 1; self.write()
        with self.assertRaisesRegex(ValueError, "different/invalid low resident"):
            check_directory(self.directory)
        self.payload["resident"][0] = 0
        self.symbols["resident"]["_native_ticks"] = layout.RESIDENT_ADDRESS + 4
        self.write()
        with self.assertRaisesRegex(ValueError, "Forbidden"):
            check_directory(self.directory)
        del self.symbols["resident"]["_native_ticks"]
        self.symbols["resident"]["__retail_hook_stack"] += 4; self.write()
        with self.assertRaisesRegex(ValueError, "hook stack"):
            check_directory(self.directory)

    def test_relay_size_bss_and_blank_manifest(self):
        self.symbols["stage"]["__retail_trampoline_end"] += 4; self.write()
        with self.assertRaisesRegex(ValueError, "trampoline"):
            check_directory(self.directory)
        self.symbols["stage"]["__retail_trampoline_end"] -= 4
        self.payload["stage"][240] ^= 1; self.write()
        with self.assertRaisesRegex(ValueError, "uncached high-stage relay"):
            check_directory(self.directory)
        self.payload["stage"][240] ^= 1
        self.symbols["resident"]["__retail_resident_bss_begin"] -= 4; self.write()
        with self.assertRaisesRegex(ValueError, "BSS"):
            check_directory(self.directory)
        self.symbols["resident"]["__retail_resident_bss_begin"] += 4
        self.payload["entry"][layout.MAP_OFFSET] = 1; self.write()
        with self.assertRaisesRegex(ValueError, "blank"):
            check_directory(self.directory)


if __name__ == "__main__":
    unittest.main()
