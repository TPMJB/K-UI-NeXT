#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent SH fixtures exercise import-preparation identity and stale-output gates."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import package_vmu_import_prep as proof
from check_loader_layout import EH, PH, SH, SYM
from runtime_package import ADDRESS, envelope, flatten_elf

COMMIT = "0123456789ab" + "c" * 28


def fixture(*, mode=1, missing=None, wrong_type=None, local=None, forbidden=None,
            title=proof.TITLE, writable_marker=False):
    payload = bytearray(256)
    payload[96:100] = struct.pack("<I", mode)
    payload[112:112 + len(title)] = title
    labels = [("_start", ADDRESS, 4, 0x12, 1)]
    labels += [(name, ADDRESS + 4 + 4 * index, 4,
                0x11 if name == wrong_type else (0x02 if name == local else 0x12), 1)
               for index, name in enumerate(proof.REQUIRED_FUNCTIONS) if name != missing]
    labels += [(proof.MODE_SYMBOL, ADDRESS + 96, 4,
                0x01 if local == proof.MODE_SYMBOL else 0x11, 2)]
    if forbidden:
        labels += [(forbidden, ADDRESS + 40, 4, 0x12, 1)]
    strings = bytearray(b"\0")
    symbols = bytearray(bytes(SYM.size))
    for name, address, size, info, section in labels:
        name_at = len(strings)
        strings.extend(name.encode("ascii") + b"\0")
        symbols.extend(SYM.pack(name_at, address, size, info, 0, section))
    load_at = 0x100
    symbol_at = load_at + len(payload)
    string_at = symbol_at + len(symbols)
    section_at = (string_at + len(strings) + 3) & ~3
    data = bytearray(section_at + 5 * SH.size)
    data[:EH.size] = EH.pack(b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1,
                            ADDRESS, EH.size, section_at, 0, EH.size,
                            PH.size, 2, SH.size, 5, 0)
    data[EH.size:EH.size + PH.size] = PH.pack(1, load_at, ADDRESS, ADDRESS, 96, 96, 5, 4)
    data[EH.size + PH.size:EH.size + 2 * PH.size] = PH.pack(
        1, load_at + 96, ADDRESS + 96, ADDRESS + 96, 160, 160,
        6 if writable_marker else 4, 4)
    data[load_at:load_at + len(payload)] = payload
    data[symbol_at:symbol_at + len(symbols)] = symbols
    data[string_at:string_at + len(strings)] = strings
    sections = [bytes(SH.size), SH.pack(0, 1, 6, ADDRESS, load_at, 96, 0, 0, 4, 0),
                SH.pack(0, 1, 2, ADDRESS + 96, load_at + 96, 160, 0, 0, 4, 0),
                SH.pack(0, 2, 0, 0, symbol_at, len(symbols), 4, 1, 4, SYM.size),
                SH.pack(0, 3, 0, 0, string_at, len(strings), 0, 0, 1, 0)]
    data[section_at:] = b"".join(sections)
    return bytes(data)


class ImportPreparationPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.guide = self.directory / "guide.md"
        self.guide.write_text("Read-only physical VMU import. Install KUI/tools.kui.\n")
        self.write_fixture()

    def tearDown(self):
        self.temporary.cleanup()

    def write_fixture(self, **kwargs):
        elf = fixture(**kwargs)
        (self.directory / "vmu-harness.elf").write_bytes(elf)
        (self.directory / "prep-config").write_bytes(b"PERSIST_IMPORT=1\n")
        (self.directory / "vmu-harness.compile.json").write_text(json.dumps(
            {"commit": COMMIT, "source_dirty": False, "elf_sha256": hashlib.sha256(elf).hexdigest()}))
        (self.directory / "KUI").mkdir(exist_ok=True)
        payload, memory = flatten_elf(elf)
        (self.directory / "KUI/tools.kui").write_bytes(envelope(payload, memory, COMMIT[:12]))
        self.graph = self.directory / "project/main.ci"
        self.graph.parent.mkdir(exist_ok=True)
        self.graph.write_text('node: { title: "kui_vmu_import_source_read" label: "reader" }\n'
            'edge: { sourcename: "kui_vmu_import_source_read" targetname: "kui_vmu_read_bounded" label: "main.c:1" }\n'
            'edge: { sourcename: "kui_vmu_read_bounded" targetname: "maple_queue_frame" label: "reader.c:1" }\n')

    def inspect(self):
        return proof.inspect_import_preparation(self.directory, COMMIT)

    def test_complete_reproducible_package_and_checksums(self):
        result = proof.package(self.directory, COMMIT, self.guide)
        initial = Path(result["zip"]).read_bytes()
        proof.package(self.directory, COMMIT, self.guide)
        self.assertEqual(initial, Path(result["zip"]).read_bytes())
        with zipfile.ZipFile(result["zip"]) as zipped:
            modules = [name for name in zipped.namelist() if name.endswith(".kui")]
            self.assertEqual(["KUI/tools.kui"], modules)
            for row in zipped.read("SHA256SUMS.txt").decode("ascii").splitlines():
                digest, name = row.split("  ")
                self.assertEqual(digest, hashlib.sha256(zipped.read(name)).hexdigest())
            audit = json.loads(zipped.read(proof.AUDIT))
            self.assertEqual(1, audit["compile_macro"]["KUI_VMU_PERSIST_IMPORT"])
            self.assertFalse(audit["source_image_journal_writes_enabled"])
            self.assertFalse(audit["physical_vmu_writes_enabled"])
            self.assertTrue(audit["new_pair_output_create_new_only"])
            self.assertFalse(audit["retail_sd_saving_enabled"])
            self.assertTrue(audit["filesystem_metadata_writes_enabled"])
            self.assertIn("native-proof/callgraph/project/main.ci", zipped.namelist())
            self.assertEqual(COMMIT + "\n", zipped.read("SOURCE-COMMIT.txt").decode())

    def test_dirty_stale_and_changed_elf_rejected(self):
        provenance = self.directory / "vmu-harness.compile.json"
        original = json.loads(provenance.read_text())
        for key, value in (("source_dirty", True), ("source_dirty", 0),
                           ("commit", "f" * 40), ("elf_sha256", "0" * 64)):
            with self.subTest(key=key, value=value):
                provenance.write_text(json.dumps({**original, key: value}))
                with self.assertRaisesRegex(ValueError, "compile provenance"):
                    self.inspect()
        provenance.write_text("[]")
        with self.assertRaisesRegex(ValueError, "compile provenance"):
            self.inspect()

    def test_mode_marker_must_be_one_readonly_global_object(self):
        for kwargs in ({"mode": 0}, {"local": proof.MODE_SYMBOL}, {"writable_marker": True}):
            with self.subTest(kwargs=kwargs):
                self.write_fixture(**kwargs)
                with self.assertRaises(ValueError):
                    self.inspect()

    def test_every_required_public_api_is_typed_and_global(self):
        for name in proof.REQUIRED_FUNCTIONS:
            for key in ("missing", "wrong_type", "local"):
                with self.subTest(name=name, key=key):
                    self.write_fixture(**{key: name})
                    with self.assertRaises(ValueError):
                        self.inspect()

    def test_format_and_synthetic_harness_compiler_clones_rejected(self):
        for name in proof.FORBIDDEN_FUNCTIONS:
            for suffix in ("", ".constprop.0", ".isra.2"):
                with self.subTest(name=name, suffix=suffix):
                    self.write_fixture(forbidden="_" + name + suffix)
                    with self.assertRaisesRegex(ValueError, "remains linked"):
                        self.inspect()

    def test_valid_envelope_from_another_build_or_elf_rejected(self):
        elf = (self.directory / "vmu-harness.elf").read_bytes()
        payload, memory = flatten_elf(elf)
        module = self.directory / "KUI/tools.kui"
        for other in (envelope(payload, memory, "f" * 12),
                      envelope(bytes([payload[0] ^ 1]) + payload[1:], memory, COMMIT[:12])):
            module.write_bytes(other)
            with self.assertRaisesRegex(ValueError, "source-bound ELF"):
                self.inspect()

    def test_preparation_app_config_and_title_cannot_be_relabelled(self):
        (self.directory / "prep-config").write_bytes(b"PERSIST_PREP=1\n")
        with self.assertRaisesRegex(ValueError, "configuration"):
            self.inspect()
        self.write_fixture(title=b"Virtual VMU - persistence preparation\0")
        with self.assertRaisesRegex(ValueError, "application identity"):
            self.inspect()

    def test_failed_readmission_removes_previous_deliverables(self):
        proof.package(self.directory, COMMIT, self.guide)
        (self.directory / "prep-config").write_bytes(b"PERSIST_IMPORT=0\n")
        with self.assertRaises(ValueError):
            proof.package(self.directory, COMMIT, self.guide)
        self.assertFalse((self.directory / proof.ARCHIVE).exists())
        self.assertFalse((self.directory / "package").exists())

    def test_unsafe_output_symlink_does_not_touch_target(self):
        target = self.directory / "preserved"
        target.mkdir()
        (target / "keep").write_text("keep")
        (self.directory / "package").symlink_to(target, target_is_directory=True)
        with self.assertRaises(ValueError):
            proof.package(self.directory, COMMIT, self.guide)
        self.assertEqual("keep", (target / "keep").read_text())


    def test_project_vm_write_calls_are_rejected_even_when_sdk_symbol_is_dormant(self):
        self.write_fixture(forbidden="_vmu_block_write")
        audit, _ = self.inspect()
        self.assertEqual(audit["physical_vmu_access"]["sdk_write_symbols_present"], ["_vmu_block_write"])
        for callee in ("vmu_block_write", "vmu_block_write_ex.isra.0",
                       "vmufs_write", "vmufs_delete", "vmufs_format", "vmufs_raw_write",
                       "vmufs_root_write", "vmufs_dir_write.constprop.0",
                       "vmufs_fat_write", "vmufs_file_write.isra.0", "vmu_toggle_241_blocks",
                       "vmu_set_icon_shape", "vmufs_unknown_future_writer", "vmu_block_read"):
            with self.subTest(callee=callee):
                self.write_fixture(forbidden="_vmu_block_write")
                with self.graph.open("a") as graph:
                    graph.write('edge: { sourcename: "app.c:bad_writer.constprop.0" targetname: "' +
                                callee + '" label: "main.c:2" }\n')
                with self.assertRaisesRegex(ValueError, "Project call can write"):
                    self.inspect()

    def test_mutex_calls_are_read_only_but_missing_reader_evidence_is_refused(self):
        with self.graph.open("a") as graph:
            for target in ("vmufs_mutex_lock", "vmufs_mutex_unlock"):
                graph.write('edge: { sourcename: "kui_vmu_import_prepare_export" targetname: "' +
                            target + '" label: "main.c:2" }\n')
        self.inspect()
        self.graph.write_text('node: { title: "kui_vmu_import_source_read" label: "reader" }\n')
        with self.assertRaisesRegex(ValueError, "bounded block-read/frame"):
            self.inspect()
        self.graph.unlink()
        with self.assertRaisesRegex(ValueError, "Missing project compiler"):
            self.inspect()

    def test_owned_filesystem_helper_requires_qualified_static_definition(self):
        report = self.directory / proof.FILESYSTEM_REPORT
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(
            'graph: { title: "src/core/vmu_fs_admission.c"\n'
            'node: { title: "src/core/vmu_fs_admission.c:vmu_fs_u32" label: '
            '"vmu_fs_u32\\nsrc/core/vmu_fs_admission.c:49:17\\n0 bytes (static)" }\n'
            'node: { title: "src/core/vmu_fs_admission.c:vmu_fs_u64.isra.0" label: '
            '"vmu_fs_u64.isra\\nsrc/core/vmu_fs_admission.c:52:17\\n16 bytes (static)" }\n'
            'edge: { sourcename: "src/core/vmu_fs_admission.c:vmu_fs_u64.isra.0" '
            'targetname: "src/core/vmu_fs_admission.c:vmu_fs_u32" '
            'label: "src/core/vmu_fs_admission.c:52:54" }\n'
            'edge: { sourcename: "kui_vmu_fs_collect" '
            'targetname: "src/core/vmu_fs_admission.c:vmu_fs_u64.isra.0" '
            'label: "src/core/vmu_fs_admission.c:52:78" }\n')
        audit, _ = self.inspect()
        self.assertEqual([
            "src/core/vmu_fs_admission.c:vmu_fs_u32",
            "src/core/vmu_fs_admission.c:vmu_fs_u64.isra.0",
        ], audit["physical_vmu_access"]["owned_filesystem_helpers"])

    def test_external_or_disguised_vmu_calls_are_not_owned_filesystem_helpers(self):
        unit = proof.FILESYSTEM_UNIT
        ordinary = unit + ":vmu_fs_u32"
        cases = [
            ("unqualified", "vmu_fs_u32", unit, "static", proof.FILESYSTEM_REPORT, True),
            ("foreign-unit", "other.c:vmu_fs_u32", "other.c", "static", proof.FILESYSTEM_REPORT, True),
            ("external-declaration", ordinary, unit, "static", proof.FILESYSTEM_REPORT, False),
            ("dynamic-frame", ordinary, unit, "dynamic", proof.FILESYSTEM_REPORT, True),
            ("foreign-frame", ordinary, "other.c", "static", proof.FILESYSTEM_REPORT, True),
            ("foreign-report", ordinary, unit, "static", "project/vmu_fs_admission.ci", True),
            ("qualified-writer", unit + ":vmu_block_write", unit, "static", proof.FILESYSTEM_REPORT, True),
        ]
        for label, target, source_unit, kind, relative, defined in cases:
            with self.subTest(label=label):
                for name in (proof.FILESYSTEM_REPORT, "project/vmu_fs_admission.ci"):
                    (self.directory / name).unlink(missing_ok=True)
                report = self.directory / relative
                report.parent.mkdir(parents=True, exist_ok=True)
                graph = 'graph: { title: "' + unit + '"\n'
                if defined:
                    function = target.rsplit(":", 1)[-1]
                    graph += ('node: { title: "' + target + '" label: "' + function +
                              '\\n' + source_unit + ':49:17\\n0 bytes (' + kind + ')" }\n')
                graph += ('edge: { sourcename: "kui_vmu_fs_collect" targetname: "' +
                          target + '" label: "' + unit + ':52:54" }\n')
                report.write_text(graph)
                with self.assertRaisesRegex(ValueError, "Project call can write"):
                    self.inspect()

    def test_make_selects_isolated_import_and_refuses_combined_app_modes(self):
        root = Path(__file__).resolve().parents[1]
        kos = self.directory / "kos-stub"
        kos.mkdir()
        (kos / "Makefile.rules").write_text("")
        query = ('show-import:\n'
                 '\t@printf "%s\\n" "$(HARNESS_MAIN)" "$(HARNESS_HOST)" "$(PERSIST_SOURCES)" "$(CFLAGS)"\n')
        command = ["make", "--no-print-directory", "-s", "-f", "Makefile.vmu",
                   "--eval=" + query, "KOS_BASE=" + str(kos)]
        result = subprocess.run([*command, "PERSIST_IMPORT=1", "show-import"],
                                cwd=root, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], "src/experimental/vmu_import_prep/main.c")
        self.assertEqual(lines[1], "src/experimental/vmu_import_prep/host.c")
        self.assertIn("src/core/vmu_image_clone.c", lines[2])
        self.assertIn("-fcallgraph-info=su", lines[3])
        for arguments in (["PERSIST_IMPORT=2"], ["PERSIST_IMPORT=yes"],
                          ["PERSIST_IMPORT=1", "PERSIST_PREP=1"],
                          ["PERSIST_IMPORT=1", "PERSIST_PROOF=1"]):
            with self.subTest(arguments=arguments):
                refused = subprocess.run([*command, *arguments, "show-import"],
                                         cwd=root, text=True, capture_output=True)
                self.assertNotEqual(refused.returncode, 0)
                self.assertIn("PERSIST_IMPORT", refused.stderr)


if __name__ == "__main__":
    unittest.main()
