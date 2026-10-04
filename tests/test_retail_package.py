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
from check_retail_loader_layout import check_directory, check_stack_usage, RESIDENTS, TRANSPORTS
import check_retail_stack
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
            "RESIDENT_ADDRESS": layout.RESIDENT_ADDRESS, "STANDARD_LIMIT": layout.RESIDENT_LIMIT,
            "ASYNC_LIMIT": layout.ASYNC_RESIDENT_LIMIT, "HOOK_STACK": layout.HOOK_STACK,
            "TRAMPOLINE_BYTES": layout.TRAMPOLINE_BYTES,
        }
        for suffix, value in names.items():
            with self.subTest(suffix=suffix):
                found = re.search(r"^#define KUI_RETAIL_" + suffix + r"\s+(\w+)\s*$", source, re.M)
                self.assertIsNotNone(found)
                self.assertEqual(int(found.group(1), 0), value)
        self.assertIn('#define KUI_RETAIL_PACKAGE_MAGIC "KUIRBT01"', source)
        self.assertIn('#define KUI_RETAIL_CE_PACKAGE_MAGIC "KUIRCE01"', source)
        self.assertEqual(layout.CE_MAGIC, b"KUIRCE01")
        found = re.search(r"^#define KUI_RETAIL_CE_STAGE_ADDRESS\s+(\w+)\s*$", source, re.M)
        self.assertEqual(int(found.group(1), 0), layout.CE_STAGE_ADDRESS)
        # The CE probe stage clears CE's 2 KiB boot prefix at 0x8ce01000.
        plan = (ROOT / "include/kui/ce_load_plan.h").read_text()
        prefix = int(re.search(r"^#define KUI_CE_LOAD_PREFIX_ADDRESS\s+(\w+)u\s*$", plan, re.M).group(1), 0)
        self.assertLessEqual(layout.STAGE_ADDRESS, prefix)
        self.assertLessEqual(prefix + 0x800, layout.CE_STAGE_ADDRESS)
        script = (ROOT / "src/loader/retail_stage_ce.ld").read_text()
        self.assertIn(f". = 0x{layout.CE_STAGE_ADDRESS:08x};", script)
        # Each resident's stack sits directly above its image limit.
        self.assertIn("#define KUI_RETAIL_HOOK_STACK_BOTTOM KUI_RETAIL_RESIDENT_LIMIT", source)
        self.assertEqual(layout.HOOK_STACK_BOTTOM, layout.RESIDENT_LIMIT)
        self.assertEqual(layout.ASYNC_HOOK_STACK_BOTTOM, layout.ASYNC_RESIDENT_LIMIT)
        script = (ROOT / "src/loader/retail_resident_async.ld").read_text()
        self.assertIn(f"__retail_hook_stack_bottom = 0x{layout.ASYNC_HOOK_STACK_BOTTOM:08x};", script)
        self.assertIn(f"__retail_resident_bss_end <= 0x{layout.ASYNC_RESIDENT_LIMIT:08x},", script)
        # The Windows CE reader: its own limit and larger stack, below the
        # bootstrap 2 it enters CE through.
        for suffix, value in (("CE_LIMIT", layout.CE_RESIDENT_LIMIT),
                              ("CE_HOOK_STACK", layout.CE_HOOK_STACK),
                              ("BOOT2_ADDRESS", 0x8C00E000)):
            with self.subTest(suffix=suffix):
                found = re.search(r"^#define KUI_RETAIL_" + suffix + r"\s+(\w+)\s*$", source, re.M)
                self.assertEqual(int(found.group(1), 0), value)
        self.assertLess(layout.CE_HOOK_STACK, 0x8C00E000)
        script = (ROOT / "src/loader/retail_resident_ce.ld").read_text()
        self.assertIn(f"__retail_hook_stack_bottom = 0x{layout.CE_RESIDENT_LIMIT:08x};", script)
        self.assertIn(f"__retail_hook_stack = 0x{layout.CE_HOOK_STACK:08x};", script)
        self.assertIn(f"__retail_resident_bss_end <= 0x{layout.CE_RESIDENT_LIMIT:08x},", script)
        # Both packages' headers record the standard reader limit.
        entry = (ROOT / "src/loader/retail_entry.S").read_text()
        self.assertIn(".long KUI_RETAIL_STANDARD_LIMIT", entry)

    def test_valid_minimum_normal_and_maximum(self):
        for size in (4, 16, layout.STAGE_MAX_BYTES):
            with self.subTest(size=size):
                result = layout.inspect_retail(self.packaged(self.payload(size)))
                self.assertEqual(result["stage_bytes"], size)
                self.assertEqual(result["resident_address"], "0x8c008300")
                self.assertEqual(result["resident_limit"], "0x8c00bb00")
                self.assertIn("title compatibility requires console testing", result["abi"])

    def test_ce_probe_package_needs_its_own_header(self):
        data = bytearray(layout.STAGE_BLOB_OFFSET + 16)
        data[layout.HEADER_OFFSET:layout.HEADER_OFFSET + layout.HEADER_BYTES] = \
            layout.relocation_header(16, ce=True)
        result = layout.inspect_retail(self.packaged(data), ce=True)
        self.assertEqual(result["stage_address"], "0x8ce10000")
        self.assertIn("stops before CE runs", result["abi"])
        with self.assertRaisesRegex(ValueError, "relocation header"):
            layout.inspect_retail(self.packaged(data))
        with self.assertRaisesRegex(ValueError, "relocation header"):
            layout.inspect_retail(self.packaged(self.payload()), ce=True)

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

    def lto_report(self, directory, extra=()):
        symbols = self.report(directory)
        target = Path(directory) / "lto"
        target.mkdir()
        report = target / "resident-sci.elf.ltrans0.ltrans.su"
        text = (Path(directory) / "resident.su").read_text()
        text += "".join(f"other.c:1:1:{name}\t{size}\t{kind}\n"
                        for name, size, kind in extra)
        report.write_text(text)
        return symbols, report

    def test_lto_counts_local_duplicates_clones_and_initialization(self):
        with tempfile.TemporaryDirectory() as tmp:
            symbols, _ = self.lto_report(tmp, (
                ("transfer", 40, "static"), ("transfer", 80, "static"),
                ("helper.constprop", 120, "static"),
                ("kui_retail_resident_init", 64, "static")))
            # Neither local transfer nor the compiler-renamed clone occurs
            # in the public-symbol map. Their frames must still be counted.
            symbols["_helper.constprop.0"] = layout.RESIDENT_ADDRESS
            result = check_stack_usage(tmp, symbols, "sci")
            self.assertEqual(result["conservative_bytes"], 256 + 256 + 40 + 80 + 120 + 64)
            self.assertEqual(result["retained_c_frames"], 8)

    def test_lto_missing_final_reports_and_unexported_bad_frames_reject(self):
        with tempfile.TemporaryDirectory() as tmp:
            symbols = self.report(tmp)
            with self.assertRaisesRegex(ValueError, "Missing compiler"):
                check_stack_usage(tmp, symbols, "sci")
            symbols, report = self.lto_report(tmp, (("local", 8, "dynamic,bounded"),))
            with self.assertRaisesRegex(ValueError, "dynamic"):
                check_stack_usage(tmp, symbols, "sci")
            text = report.read_text().replace("8\tdynamic,bounded", "800\tstatic")
            report.write_text(text)
            with self.assertRaisesRegex(ValueError, "exceeds"):
                check_stack_usage(tmp, symbols, "sci")
            report.write_text(text.replace("800\tstatic", "-1\tstatic"))
            with self.assertRaisesRegex(ValueError, "Invalid resident"):
                check_stack_usage(tmp, symbols, "sci")

    def test_lto_requires_audited_entry_frame_and_linked_symbol(self):
        with tempfile.TemporaryDirectory() as tmp:
            symbols, report = self.lto_report(tmp)
            del symbols["_kui_loader_sd_stream_next"]
            with self.assertRaisesRegex(ValueError, "Missing runtime"):
                check_stack_usage(tmp, symbols, "sci")
            symbols["_kui_loader_sd_stream_next"] = layout.RESIDENT_ADDRESS
            report.write_text(report.read_text().replace("kui_loader_sd_stream_next", "renamed"))
            with self.assertRaisesRegex(ValueError, "Missing runtime"):
                check_stack_usage(tmp, symbols, "sci")


class CallGraphStack(unittest.TestCase):
    """The background reader's stack bound from GCC -fcallgraph-info=su."""
    frames = {"kui_retail_resident_dispatch": 48, "kui_retail_gd_dispatch": 172,
              "guest": 0, "kui_retail_async_irq": 16, "deliver": 64,
              "kui_sci_stream_fetch.constprop.0": 40, "byte": 0, "kui_retail_menu_return": 12,
              "retail_display_line": 48, "map_guest": 8, "check_sectors": 4,
              "transfer": 24, "select_card": 16, "write_out": 12, "reverse.lto_priv.0": 0}
    calls = [("kui_retail_resident_dispatch", "kui_retail_gd_dispatch", ""),
             ("kui_retail_resident_dispatch", "deliver", ""),
             ("kui_retail_gd_dispatch", "guest", ""),
             ("guest", "__indirect_call", "retail_gd.c:35:12"),
             ("kui_retail_gd_dispatch", "__indirect_call", "retail_gd.c:131:16"),
             ("kui_retail_async_irq", "deliver", ""),
             ("deliver", "kui_sci_stream_fetch.constprop.0", ""),
             ("deliver", "__indirect_call", "retail_cursor.c:57:9"),
             ("deliver", "reverse.lto_priv.1", ""),
             ("write_out", "__indirect_call", "retail_async.c:111:20"),
             ("kui_sci_stream_fetch.constprop.0", "byte", ""),
             ("byte", "__indirect_call", "sci_stream.c:72:45"),
             ("kui_retail_menu_return", "retail_display_line", ""),
             ("kui_retail_menu_return", "__indirect_call", "retail_resident.c:214:5"),
             ("deliver", "__udivsi3", "")]

    @staticmethod
    def graph(frames, calls, kind="static"):
        prefix = "/tmp/x/resident.elf.ltrans0.o:"
        title = lambda f: f if f.startswith("__") or f.startswith("kui_retail_") else prefix + f
        lines = ['graph: { title: "x"']
        for function, frame in frames.items():
            lines.append(f'node: {{ title: "{title(function)}" label: "{function.split(".")[0]}'
                         f'\\n../src/x.c:1:1\\n{frame} bytes ({kind})" }}')
        lines.append('node: { title: "__indirect_call" label: "Indirect Call Placeholder" shape : ellipse }')
        for source, target, label in calls:
            lines.append(f'edge: {{ sourcename: "{title(source)}" targetname: "{title(target)}" '
                         f'label: "../../src/loader/{label or "x.c:1:1"}" }}')
        return "\n".join(lines) + "\n}\n"

    def check(self, frames=None, calls=None, kind="static", available=464):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "lto").mkdir()
            (Path(tmp) / "lto/r.ltrans0.ltrans.ci").write_text(
                self.graph(frames or self.frames, calls or self.calls, kind))
            return check_retail_stack.check(tmp, available)

    def test_deepest_path_per_entry_with_callbacks(self):
        result = self.check()
        # dispatch 48 + gd 172 + guest 0 + deepest GD callback (map 8, check 4).
        self.assertEqual(result["paths"]["kui_retail_resident_dispatch"]["bytes"], 228)
        # entry 44 + irq 16 + deliver 64 + fetch 40 + byte 0 + transfer 24.
        self.assertEqual(result["paths"]["kui_retail_async_irq"]["bytes"], 188)
        self.assertEqual(result["paths"]["kui_retail_menu_return"]["bytes"], 60)
        self.assertEqual(result["worst_bytes"], 228)
        with self.assertRaisesRegex(ValueError, "exceeds"):
            self.check(available=228 + 63)

    def test_unknown_indirect_recursion_dynamic_and_missing_reject(self):
        with self.assertRaisesRegex(ValueError, "Unexpected indirect"):
            self.check(calls=self.calls + [("deliver", "__indirect_call", "other.c:1:1")])
        with self.assertRaisesRegex(ValueError, "Recursion"):
            self.check(calls=self.calls + [("transfer", "deliver", "")])
        with self.assertRaisesRegex(ValueError, "Dynamic"):
            self.check(kind="dynamic")
        frames = dict(self.frames); del frames["write_out"]
        with self.assertRaisesRegex(ValueError, "Missing callback"):
            self.check(frames=frames)
        with self.assertRaisesRegex(ValueError, "without a frame"):
            self.check(calls=self.calls + [("deliver", "unknown_function", "")])
        with self.assertRaisesRegex(ValueError, "Missing compiler call-graph"):
            with tempfile.TemporaryDirectory() as tmp:
                check_retail_stack.check(tmp, 464)


class RetailLinkedLayout(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.bases = {"stage": layout.STAGE_ADDRESS, "entry": layout.EXEC_ADDRESS}
        self.payload = {"stage": bytearray(256)}
        self.symbols = {"stage": {}, "entry": {}}
        self.memory = {"stage": 1024}
        ss, es = self.symbols["stage"], self.symbols["entry"]
        for transport in RESIDENTS:
            name = "resident-" + transport
            self.bases[name] = layout.RESIDENT_ADDRESS
            self.payload[name] = bytearray(128)
            # Distinct bytes expose accidentally swapped transport blobs.
            self.payload[name][0] = RESIDENTS.index(transport)
            self.memory[name] = 0x900 if transport == "scia" else 512
            rs = self.symbols[name] = {}
            required = ["kui_retail_resident_init", "kui_retail_resident_hook",
                        "kui_retail_resident_dispatch", "kui_retail_gd_dispatch"]
            if transport == "scia":
                required += ["kui_retail_async_irq", "kui_retail_irq_entry",
                             "kui_retail_vector_forward", "kui_retail_vector_interrupt",
                             "kui_retail_release_100", "kui_retail_release_400",
                             "kui_retail_release_600", "kui_retail_rehook"]
            else:
                required += ["kui_retail_image_read"]
            if transport == "scia":
                pass
            elif transport == "ide":
                required += ["kui_ata_read"]
            else:
                required += ["kui_loader_sd_stream_next"]
                prefix = "kui_retail_sd_" if transport == "scif" else "kui_sci_sd_"
                required += [prefix + "acquire", prefix + "release"]
            for index, symbol in enumerate(required):
                rs["_" + symbol] = layout.RESIDENT_ADDRESS + 4 + index * 4
            rs.update({"__retail_hook_stack": layout.HOOK_STACK,
                       "__retail_hook_stack_bottom": layout.ASYNC_HOOK_STACK_BOTTOM
                       if transport == "scia" else layout.HOOK_STACK_BOTTOM,
                       "_kui_retail_hook_active": layout.RESIDENT_ADDRESS + 128,
                       "_kui_retail_hook_fault": layout.RESIDENT_ADDRESS + 132})
            frames = list(ResidentStackReports.required)
            if transport == "ide":
                frames[-1] = "kui_ata_read"
            report = "\n".join(f"test.c:1:1:{frame}\t32\tstatic" for frame in frames)
            (self.directory / transport).mkdir()
            if transport == "scia":
                rs["_kui_retail_async_region"] = layout.RESIDENT_ADDRESS + 0x100
                (self.directory / transport / "lto").mkdir()
                (self.directory / transport / "lto/resident-scia.elf.ltrans0.ltrans.ci").write_text(
                    CallGraphStack.graph(CallGraphStack.frames, CallGraphStack.calls))
                continue
            if transport == "sci":
                (self.directory / transport / "lto").mkdir()
                report_path = self.directory / transport / "lto/resident-sci.elf.ltrans0.ltrans.su"
            else:
                report_path = self.directory / transport / "resident.su"
            report_path.write_text(report + "\n")
        for index, name in enumerate(("kui_retail_stage_main", "kui_retail_stage_relay",
                                     "kui_retail_bootstrap_enter", "kui_retail_game_resume")):
            ss["_" + name] = layout.STAGE_ADDRESS + 4 + index * 4
        ss["__retail_trampoline_start"] = layout.STAGE_ADDRESS + 128
        ss["__retail_trampoline_end"] = layout.STAGE_ADDRESS + 256
        struct.pack_into("<I", self.payload["stage"], 240,
                         ss["_kui_retail_game_resume"] | 0x20000000)
        for transport in RESIDENTS:
            label = "__retail_resident_" + transport + "_blob_"
            ss[label + "start"] = layout.STAGE_ADDRESS + len(self.payload["stage"])
            self.payload["stage"] += self.payload["resident-" + transport]
            ss[label + "end"] = layout.STAGE_ADDRESS + len(self.payload["stage"])
        for name in self.payload:
            base = self.bases[name]; syms = self.symbols[name]
            prefix = "resident" if name.startswith("resident-") else name
            syms[f"__retail_{prefix}_binary_end"] = base + len(self.payload[name])
            syms[f"__retail_{prefix}_bss_begin"] = base + len(self.payload[name])
            syms[f"__retail_{prefix}_bss_end"] = base + self.memory[name]
        self.payload["entry"] = bytearray(layout.STAGE_BLOB_OFFSET) + self.payload["stage"]
        self.payload["entry"][0x100:0x140] = layout.relocation_header(len(self.payload["stage"]))
        self.memory["entry"] = len(self.payload["entry"])
        es.update({"__retail_map": layout.EXEC_ADDRESS + layout.MAP_OFFSET,
                   "__retail_stage_blob_start": layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET,
                   "__retail_stage_blob_end": layout.EXEC_ADDRESS + len(self.payload["entry"])})
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
        for transport in TRANSPORTS:
            self.assertEqual(result["resident-" + transport]["payload_bytes"], 128)
            self.assertEqual(result["resident_stacks"][transport]["conservative_bytes"], 384)
        self.assertEqual(result["resident-scia"]["payload_bytes"], 128)
        self.assertEqual(result["resident_stacks"]["scia"]["worst_bytes"], 48 + 172 + 8)

    def test_ce_probe_stage_is_checked_at_its_own_address(self):
        shift = layout.CE_STAGE_ADDRESS - layout.STAGE_ADDRESS
        self.bases["stage"] += shift
        ss = self.symbols["stage"]
        for name in ss:
            ss[name] += shift
        struct.pack_into("<I", self.payload["stage"], 240, ss["_kui_retail_game_resume"] | 0x20000000)
        self.payload["entry"][layout.STAGE_BLOB_OFFSET:] = self.payload["stage"]
        self.write()
        # Its SCI reader has its own limit and larger stack.
        with self.assertRaisesRegex(ValueError, "resident-sci hook stack"):
            check_directory(self.directory, ce=True)
        self.symbols["resident-sci"].update({"__retail_hook_stack": layout.CE_HOOK_STACK,
                                             "__retail_hook_stack_bottom": layout.CE_RESIDENT_LIMIT})
        self.write()
        with self.assertRaisesRegex(ValueError, "relocation header mismatch"):
            check_directory(self.directory, ce=True)
        self.payload["entry"][0x100:0x140] = layout.relocation_header(len(self.payload["stage"]), ce=True)
        self.write()
        result = check_directory(self.directory, ce=True)
        self.assertEqual(result["stage"]["payload_bytes"], len(self.payload["stage"]))
        with self.assertRaises(ValueError):
            check_directory(self.directory)

    def test_background_reader_stack_region_and_symbols(self):
        rs = self.symbols["resident-scia"]
        rs["__retail_hook_stack_bottom"] = layout.HOOK_STACK_BOTTOM; self.write()
        with self.assertRaisesRegex(ValueError, "hook stack"):
            check_directory(self.directory)
        rs["__retail_hook_stack_bottom"] = layout.ASYNC_HOOK_STACK_BOTTOM
        rs["_kui_retail_async_region"] += 4; self.write()
        with self.assertRaisesRegex(ValueError, "vector region"):
            check_directory(self.directory)
        rs["_kui_retail_async_region"] -= 4
        del rs["_kui_retail_irq_entry"]; self.write()
        with self.assertRaisesRegex(ValueError, "Missing retail executable symbol"):
            check_directory(self.directory)
        rs["_kui_retail_irq_entry"] = layout.RESIDENT_ADDRESS + 8
        report = self.directory / "scia/lto/resident-scia.elf.ltrans0.ltrans.ci"
        frames = dict(CallGraphStack.frames, deliver=400)
        report.write_text(CallGraphStack.graph(frames, CallGraphStack.calls)); self.write()
        with self.assertRaisesRegex(ValueError, "worst-case stack"):
            check_directory(self.directory)

    def test_embedded_identity_timer_import_and_guard(self):
        self.payload["resident-scif"][0] = 1; self.write()
        with self.assertRaisesRegex(ValueError, "different/invalid low resident"):
            check_directory(self.directory)
        self.payload["resident-scif"][0] = 0
        self.symbols["resident-scif"]["_native_ticks"] = layout.RESIDENT_ADDRESS + 4
        self.write()
        with self.assertRaisesRegex(ValueError, "Forbidden"):
            check_directory(self.directory)
        del self.symbols["resident-scif"]["_native_ticks"]
        self.symbols["resident-scif"]["__retail_hook_stack"] += 4; self.write()
        with self.assertRaisesRegex(ValueError, "hook stack"):
            check_directory(self.directory)

    def test_lto_source_filename_does_not_exempt_real_device_function(self):
        self.symbols["resident-sci"]["_sd_reader.c.12345678"] = layout.RESIDENT_ADDRESS + 4
        self.write()
        with self.assertRaisesRegex(ValueError, "Forbidden runtime/device symbol"):
            check_directory(self.directory)

    def test_every_transport_has_independent_blob_and_stack_check(self):
        for transport in TRANSPORTS:
            name = "resident-" + transport
            self.payload[name][0] ^= 0x80; self.write()
            with self.subTest(transport=transport), self.assertRaisesRegex(ValueError, "different/invalid low resident"):
                check_directory(self.directory)
            self.payload[name][0] ^= 0x80; self.write()
            report = self.directory / transport / (
                "lto/resident-sci.elf.ltrans0.ltrans.su" if transport == "sci" else "resident.su")
            text = report.read_text()
            report.write_text(text.replace("\t32\t", "\t2048\t"))
            with self.subTest(transport=transport), self.assertRaisesRegex(ValueError, "exceeds"):
                check_directory(self.directory)
            report.write_text(text)

    def test_relay_size_bss_and_blank_manifest(self):
        self.symbols["stage"]["__retail_trampoline_end"] += 4; self.write()
        with self.assertRaisesRegex(ValueError, "trampoline"):
            check_directory(self.directory)
        self.symbols["stage"]["__retail_trampoline_end"] -= 4
        self.payload["stage"][240] ^= 1; self.write()
        with self.assertRaisesRegex(ValueError, "uncached high-stage relay"):
            check_directory(self.directory)
        self.payload["stage"][240] ^= 1
        self.symbols["resident-scif"]["__retail_resident_bss_begin"] -= 4; self.write()
        with self.assertRaisesRegex(ValueError, "BSS"):
            check_directory(self.directory)
        self.symbols["resident-scif"]["__retail_resident_bss_begin"] += 4
        self.payload["entry"][layout.MAP_OFFSET] = 1; self.write()
        with self.assertRaisesRegex(ValueError, "blank"):
            check_directory(self.directory)


if __name__ == "__main__":
    unittest.main()
