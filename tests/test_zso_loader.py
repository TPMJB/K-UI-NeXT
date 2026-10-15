# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import check_zso_loader as audit
import retail_package
import zso_package as layout
from test_image_loader_layout import executable


class ZsoLinkedLoader(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.bases = {"resident-sci": layout.RESIDENT_ADDRESS,
                      "stage": layout.STAGE_ADDRESS, "entry": layout.EXEC_ADDRESS}
        self.payloads = {"resident-sci": bytearray(256), "stage": bytearray(512)}
        self.memory = {"resident-sci": 512, "stage": 768}
        self.symbols = {"resident-sci": {}, "stage": {}, "entry": {}}
        rs, ss, es = (self.symbols[name] for name in audit.IMAGES)
        for index, symbol in enumerate(audit.REQUIRED_RESIDENT):
            rs[symbol] = layout.RESIDENT_ADDRESS + 64 + index * 4
        rs.update(__retail_hook_stack=layout.HOOK_STACK,
                  __retail_hook_stack_bottom=layout.RESIDENT_LIMIT,
                  _kui_retail_hook_active=layout.RESIDENT_ADDRESS + 256,
                  _kui_retail_hook_fault=layout.RESIDENT_ADDRESS + 260)
        for name, offset in (("kui_retail_stage_main", 96), ("kui_retail_stage_relay", 152),
                             ("kui_retail_bootstrap_enter", 144), ("kui_retail_game_resume", 156),
                             ("kui_retail_zso_manifest_decode", 160), ("kui_retail_zso_verify_source", 164)):
            ss["_" + name] = layout.STAGE_ADDRESS + offset
        ss["__retail_trampoline_start"] = layout.STAGE_ADDRESS + 128
        ss["__retail_trampoline_end"] = layout.STAGE_ADDRESS + 256
        struct.pack_into("<I", self.payloads["stage"], 240, ss["_kui_retail_game_resume"] | 0x20000000)
        for transport in ("scif", "sci", "ide", "scia"):
            label = "__retail_resident_" + transport + "_blob_"
            ss[label + "start"] = layout.STAGE_ADDRESS + 256
            ss[label + "end"] = layout.STAGE_ADDRESS + 512
        self.payloads["stage"][256:] = self.payloads["resident-sci"]
        self.payloads["entry"] = bytearray(layout.STAGE_BLOB_OFFSET) + self.payloads["stage"]
        self.payloads["entry"][layout.HEADER_OFFSET:layout.HEADER_OFFSET + layout.HEADER_BYTES] = layout.relocation_header(512)
        self.memory["entry"] = len(self.payloads["entry"])
        es.update(__retail_map=layout.EXEC_ADDRESS + layout.MAP_OFFSET,
                  __retail_stage_blob_start=layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET,
                  __retail_stage_blob_end=layout.EXEC_ADDRESS + self.memory["entry"])
        for name, prefix in (("resident-sci", "resident"), ("stage", "stage")):
            base = self.bases[name]
            self.symbols[name].update({
                f"__retail_{prefix}_binary_end": base + len(self.payloads[name]),
                f"__retail_{prefix}_bss_begin": base + len(self.payloads[name]),
                f"__retail_{prefix}_bss_end": base + self.memory[name],
            })
        # Independently encode the three short assembly bootstrap contracts.
        self.payloads["resident-sci"][:36] = struct.pack("<12H3I", *(
            0xD005, 0xD106, 0xE200, 0x3012, 0x8903, 0x2022, 0x7004, 0xAFFA,
            0x0009, 0xD003, 0x402B, 0x0009), layout.RESIDENT_ADDRESS + 256,
            layout.RESIDENT_ADDRESS + 512, rs["_kui_retail_resident_init"])
        self.payloads["stage"][:92] = struct.pack("<32H7I", *(
            0xD012, 0x402B, 0x0009, 0xD013, 0xD113, 0x2012, *([0x0009] * 8),
            0xD00C, 0x402B, 0x0009, 0xD007, 0xD107, 0xE200, 0x3012, 0x8903,
            0x2022, 0x7004, 0xAFFA, 0x0009, 0xD004, 0x400B, 0x0009, 0xAFFE, 0x0009, 0x0009),
            layout.STAGE_ADDRESS + 512, layout.STAGE_ADDRESS + 768, layout.STAGE_ADDRESS + 96,
            layout.STAGE_ADDRESS + 0x20000006, layout.STAGE_ADDRESS + 34, 0xFF00001C, 0x909)
        struct.pack_into("<I", self.payloads["stage"], 128, layout.RESIDENT_ADDRESS)
        struct.pack_into("<I", self.payloads["stage"], 132, ss["_kui_retail_bootstrap_enter"])
        self.payloads["stage"][256:] = self.payloads["resident-sci"]
        self.payloads["entry"][layout.STAGE_BLOB_OFFSET:] = self.payloads["stage"]
        self.payloads["entry"][:56] = struct.pack("<16H6I", *(
            0xD007, 0x400E, 0xD007, 0xD108, 0xD208, 0x4209, 0x6306, 0x2132,
            0x7104, 0x4210, 0x8BFA, 0xDF06, 0xD406, 0xD003, 0x402B, 0x0009),
            0x500000F0, layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET, layout.STAGE_ADDRESS,
            512, layout.STAGE_STACK, layout.EXEC_ADDRESS + layout.MAP_OFFSET)
        (self.directory / "layout-config").write_text(
            "ZSO=1 LOW=1 TRANSPORT=1 READER=0 SLOTS=32 PATCHES=0 BUILD=012345abcdef\n")
        (self.directory / "sci/lto").mkdir(parents=True)
        self.report = self.directory / "sci/lto/resident-sci.elf.ltrans0.ltrans.su"
        self.report.write_text("".join(f"source.c:1:1:{name}\t32\tstatic\n" for name in
                                      (*audit.REQUIRED_FRAMES, "kui_retail_resident_init")))
        self.graph = self.directory / "sci/lto/resident-sci.elf.ltrans0.ltrans.ci"
        self.graph.write_text('graph: {\nnode: { title: "kui_retail_resident_init" '
                              'label: "kui_retail_resident_init\\nsource.c:1:1\\n32 bytes (static)" }\n}\n')
        (self.directory / "stage").mkdir()
        self.high_report = self.directory / "stage/stage.su"
        self.high_report.write_text("source.c:1:1:kui_retail_stage_main\t64\tstatic\n")
        self.write()

    def write(self):
        for name, payload in self.payloads.items():
            (self.directory / (name + ".elf")).write_bytes(
                executable(self.bases[name], payload, self.memory[name], self.symbols[name]))
            if name != "entry":
                (self.directory / (name + ".bin")).write_bytes(payload)

    def disassembly(self, args, **kwargs):
        name = Path(args[2]).stem
        base = self.bases[name]
        if name == "stage":
            return (f" {base + 96:08x}: 07 da mov.l literal,r10\n {base + 98:08x}: 09 00 nop\n"
                    f" {base + 100:08x}: 0b 4a jsr @r10\n {base + 102:08x}: 09 00 nop\n"
                    f" {base + 104:08x}: 06 d1 mov.l literal,r1\n {base + 106:08x}: 0b 41 jsr @r1\n"
                    f" {base + 108:08x}: 09 00 nop\n{base + 144:08x} <__retail_boot_fpscr_init>:\n"
                    f" {base + 144:08x}: 6a 40 lds r0,fpscr\n {base + 146:08x}: 0b 00 rts\n")
        return f" {base:08x}: 0b 00 rts\n {base + 2:08x}: 09 00 nop\n"

    def check(self):
        # ELF/layout checks still use real generated files. Only the unavailable
        # cross-objdump process is modeled; its returned instructions go through
        # the production integer audit, including the named FPSCR exception.
        with patch("check_zso_loader.subprocess.check_output", side_effect=self.disassembly):
            return audit.check_directory(self.directory)

    def test_valid_dedicated_three_image_contract(self):
        result = self.check()
        self.assertEqual(result["contract"]["manifest_slots"], 32)
        self.assertEqual(result["resident_stack"]["conservative_bytes"], 416)
        self.assertEqual(result["instruction_audit"]["instructions"], 13)
        for name in audit.IMAGES:
            self.assertTrue((self.directory / (name + ".dis")).exists())

    def test_profile_rejects_other_transports_readers_layouts_and_patches(self):
        file = self.directory / "layout-config"
        clean = file.read_text()
        for before, after in (("ZSO=1", "ZSO=0"), ("LOW=1", "LOW=0"),
                              ("TRANSPORT=1", "TRANSPORT=0"), ("READER=0", "READER=1"),
                              ("SLOTS=32", "SLOTS=160"), ("PATCHES=0", "PATCHES=1")):
            file.write_text(clean.replace(before, after))
            with self.subTest(after=after), self.assertRaisesRegex(ValueError, "build profile"):
                self.check()

    def test_exact_header_and_blank_manifest_are_not_optional(self):
        clean = self.payloads["entry"].copy()
        for offset in range(layout.HEADER_OFFSET, layout.HEADER_OFFSET + layout.HEADER_BYTES):
            self.payloads["entry"] = clean.copy()
            self.payloads["entry"][offset] ^= 1
            self.write()
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "relocation header"):
                self.check()
        self.payloads["entry"] = clean
        self.payloads["entry"][layout.MAP_OFFSET + layout.MAP_BYTES - 1] = 1
        self.write()
        with self.assertRaisesRegex(ValueError, "blank card-specific"):
            self.check()

    def test_binary_and_embedded_resident_identity(self):
        path = self.directory / "resident-sci.bin"
        data = bytearray(path.read_bytes())
        data[0] ^= 1
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "differs from linked"):
            self.check()
        self.write()
        self.payloads["stage"][-1] ^= 1
        self.payloads["entry"][layout.STAGE_BLOB_OFFSET:] = self.payloads["stage"]
        self.write()
        with self.assertRaisesRegex(ValueError, "embedded SCI resident"):
            self.check()

    def test_high_stage_cannot_embed_additional_transport_residents(self):
        self.symbols["stage"]["__retail_resident_ide_blob_start"] += 4
        self.write()
        with self.assertRaisesRegex(ValueError, "exactly one aliased"):
            self.check()

    def test_ram_patch_seed_rejects_even_when_all_linked_bytes_match(self):
        self.payloads["stage"][64:128] = retail_package.RAM_PATCH_SEED
        self.payloads["entry"][layout.STAGE_BLOB_OFFSET:] = self.payloads["stage"]
        self.write()
        with self.assertRaisesRegex(ValueError, "RAM patch"):
            self.check()

    def test_packed_stage_and_uncached_relay_identity(self):
        self.payloads["entry"][-1] ^= 1
        self.write()
        with self.assertRaisesRegex(ValueError, "packed ZSO"):
            self.check()
        self.payloads["entry"][-1] ^= 1
        self.payloads["stage"][240:244] = bytes(4)
        self.payloads["entry"][layout.STAGE_BLOB_OFFSET:] = self.payloads["stage"]
        self.write()
        with self.assertRaisesRegex(ValueError, "uncached high-stage relay"):
            self.check()

    def test_low_bounds_bss_and_hook_stack(self):
        self.symbols["resident-sci"]["__retail_hook_stack"] += 4
        self.write()
        with self.assertRaisesRegex(ValueError, "hook stack"):
            self.check()
        self.symbols["resident-sci"]["__retail_hook_stack"] -= 4
        self.memory["resident-sci"] = layout.RESIDENT_LIMIT - layout.RESIDENT_ADDRESS + 4
        self.write()
        with self.assertRaisesRegex(ValueError, "reservation"):
            self.check()

    def test_required_decoder_and_driver_symbols_are_checked(self):
        for name, symbol in (("resident-sci", "_kui_zso_read_block"),
                             ("resident-sci", "_kui_retail_zso_image_read"),
                             ("resident-sci", "_kui_sci_sd_acquire"),
                             ("stage", "_kui_retail_zso_manifest_decode")):
            old = self.symbols[name].pop(symbol)
            self.write()
            with self.subTest(symbol=symbol), self.assertRaisesRegex(ValueError, "executable symbol"):
                self.check()
            self.symbols[name][symbol] = old

    def test_forbidden_runtime_and_other_profile_symbols(self):
        for symbol in ("_f_write", "_malloc", "_kui_ata_read_run", "_kui_retail_async_irq", "_kui_game_ram_patches_apply"):
            self.symbols["resident-sci"][symbol] = layout.RESIDENT_ADDRESS + 4
            self.write()
            with self.subTest(symbol=symbol), self.assertRaisesRegex(ValueError, "Forbidden"):
                self.check()
            del self.symbols["resident-sci"][symbol]

    def test_final_stack_reports_are_required_and_count_every_local_row(self):
        clean = self.report.read_text()
        self.report.unlink()
        (self.directory / "sci/pre-lto.su").write_text(clean)
        with self.assertRaisesRegex(ValueError, "Missing final"):
            self.check()
        self.report.write_text(clean + "x.c:1:1:local\t100\tstatic\nx.c:2:1:local\t120\tstatic\n")
        self.assertEqual(self.check()["resident_stack"]["conservative_bytes"], 636)
        for extra, error in (("x.c:1:1:hidden\t8\tdynamic,bounded\n", "dynamic"),
                              ("x.c:1:1:hidden\t-1\tstatic\n", "Invalid"),
                              ("x.c:1:1:hidden\t2000\tstatic\n", "exceeds")):
            self.report.write_text(clean + extra)
            with self.subTest(extra=extra), self.assertRaisesRegex(ValueError, error):
                self.check()
        self.report.write_text(clean.replace("kui_zso_read_block", "different"))
        with self.assertRaisesRegex(ValueError, "Missing ZSO runtime"):
            self.check()

    def test_each_bootstrap_byte_is_part_of_the_high_stack_proof(self):
        snapshots = {name: data.copy() for name, data in self.payloads.items()}
        for name, size, error in (("entry", 56, "mask SR"), ("stage", 92, "preserve the high stack"),
                                  ("resident-sci", 36, "must not switch stacks")):
            for offset in range(size):
                self.payloads = {key: value.copy() for key, value in snapshots.items()}
                self.payloads[name][offset] ^= 1
                if name == "resident-sci":
                    self.payloads["stage"][256:] = self.payloads["resident-sci"]
                if name != "entry":
                    self.payloads["entry"][layout.STAGE_BLOB_OFFSET:] = self.payloads["stage"]
                self.write()
                with self.subTest(name=name, offset=offset), self.assertRaisesRegex(ValueError, error):
                    self.check()

    def test_initializer_reference_graph_and_frame_cannot_be_waived(self):
        clean_graph = self.graph.read_text()
        self.graph.unlink()
        with self.assertRaisesRegex(ValueError, "Missing final ZSO initializer"):
            self.check()
        self.graph.write_text(clean_graph + 'edge: { sourcename: "kui_retail_resident_dispatch" '
                              'targetname: "kui_retail_resident_init" }\n')
        with self.assertRaisesRegex(ValueError, "runtime call graph enters"):
            self.check()
        self.graph.write_text(clean_graph)
        self.report.write_text(self.report.read_text().replace("kui_retail_resident_init\t32", "kui_retail_resident_init\t36"))
        with self.assertRaisesRegex(ValueError, "frame and final call graph differ"):
            self.check()

    def test_additional_init_pointer_and_high_stack_overflow_reject(self):
        struct.pack_into("<I", self.payloads["resident-sci"], 128,
                         self.symbols["resident-sci"]["_kui_retail_resident_init"] | 0x20000000)
        self.payloads["stage"][256:] = self.payloads["resident-sci"]
        self.payloads["entry"][layout.STAGE_BLOB_OFFSET:] = self.payloads["stage"]
        self.write()
        with self.assertRaisesRegex(ValueError, "exactly one pinned entry reference"):
            self.check()
        self.payloads["resident-sci"][128:132] = bytes(4)
        self.payloads["stage"][256:] = self.payloads["resident-sci"]
        self.payloads["entry"][layout.STAGE_BLOB_OFFSET:] = self.payloads["stage"]
        self.write()
        self.high_report.write_text("source.c:1:1:kui_retail_stage_main\t70000\tstatic\n")
        with self.assertRaisesRegex(ValueError, "existing high-stack budget"):
            self.check()

    def test_stage_init_call_and_callee_saved_target_are_checked(self):
        original = self.disassembly
        for before, after in (("0b 4a jsr @r10", "0b 49 jsr @r9"),
                              ("09 00 nop", "03 6a mov r0,r10")):
            def changed(args, **kwargs):
                value = original(args, **kwargs)
                return value.replace(before, after, 1) if Path(args[2]).stem == "stage" else value
            with self.subTest(after=after), patch("check_zso_loader.subprocess.check_output", side_effect=changed):
                with self.assertRaisesRegex(ValueError, "initialize its resident on the high stack"):
                    audit.check_directory(self.directory)

    def test_integer_instruction_audit_is_mandatory_for_all_three_images(self):
        with patch("check_zso_loader.subprocess.check_output", side_effect=FileNotFoundError("objdump")):
            with self.assertRaisesRegex(ValueError, "integer-instruction"):
                audit.check_directory(self.directory)
        for name in audit.IMAGES:
            def mutated(args, **kwargs):
                value = self.disassembly(args, **kwargs)
                if Path(args[2]).stem == name:
                    value += f" {self.bases[name] + 4:08x}: 00 f0 fadd fr0,fr0\n"
                return value
            with self.subTest(name=name), patch("check_zso_loader.subprocess.check_output", side_effect=mutated):
                with self.assertRaisesRegex(ValueError, "FPU use"):
                    audit.check_directory(self.directory)


if __name__ == "__main__":
    unittest.main()
