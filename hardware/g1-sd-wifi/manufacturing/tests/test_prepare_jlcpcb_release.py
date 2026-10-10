"""Tooling tests with deliberately tiny CAD fixtures and a fake KiCad CLI.

These tests verify refusal/cleanup/packaging behavior, not any real PCB design.
Run: python3 -m unittest discover -s hardware/g1-sd-wifi/manufacturing/tests
"""
import csv
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "prepare_jlcpcb_release.py"
TOPICS = {
    "rigid_electrical", "rigid_layout", "flex_pin_mapping",
    "flex_geometry_stackup", "assembly_bom_cpl", "power_bus_isolation",
    "mechanical_clearance", "order_settings",
}
FAKE_CLI = r'''#!/usr/bin/env python3
import json
import os
import sys
from pathlib import Path
args = sys.argv[1:]
if args == ["version"]:
    print("10.0.7")
    raise SystemExit(0)
kind = args[1] if args[1] in {"erc", "drc"} else args[2]
fail = os.environ.get("FAKE_KICAD_FAIL", "")
if fail == kind or (fail == "flex-drill" and kind == "drill"
                    and Path(args[-1]).name == "flex.kicad_pcb"):
    print("deliberate mock KiCad failure", file=sys.stderr)
    raise SystemExit(5)
out = Path(args[args.index("--output") + 1])
if kind in {"erc", "drc"}:
    out.write_text(json.dumps({"mock": True}), encoding="utf-8")
else:
    out.mkdir(parents=True, exist_ok=True)
    if kind == "gerbers":
        layer = args[args.index("--layers") + 1]
        if "," in layer:
            raise SystemExit("Mock requires isolated layer exports")
        if os.environ.get("FAKE_KICAD_OMIT_LAYER") != layer:
            (out / ("board-" + layer.replace(".", "_") + ".gbr")).write_text(
                "mock layer " + layer + "\n", encoding="ascii")
        (out / "partial.gbrjob").write_text("mock partial job", encoding="ascii")
    elif kind == "drill":
        content = "" if os.environ.get("FAKE_KICAD_EMPTY_DRILL") else "mock drill\n"
        (out / "board-PTH.drl").write_text(content, encoding="ascii")
'''

class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.base = self.root / "hardware/g1-sd-wifi"
        self.tool = self.base / "manufacturing/prepare_jlcpcb_release.py"
        self.tool.parent.mkdir(parents=True)
        shutil.copyfile(SOURCE, self.tool)
        self.library = self.base / "Test.pretty"
        self.library.mkdir()
        (self.library / "tap.kicad_mod").write_text('(footprint "tap")\n')
        self.write("notes.txt", "Mock engineering pin plan, not hardware.\n")
        self.write("coverlay.gbr", "mock coverlay drawing\n")
        self.fake_cli = self.root / "fake-kicad-cli"
        self.fake_cli.write_text("#!" + sys.executable + "\n" +
                                 FAKE_CLI.split("\n", 1)[1], encoding="utf-8")
        self.fake_cli.chmod(0o755)
        self.output = self.root / "order-output"
        self.manifest_path = self.tool.with_name("release-manifest.json")
        self.review_path = self.tool.with_name("engineering-review.json")
        boards = {}
        for name in ("rigid", "flex"):
            folder = self.base / name
            folder.mkdir()
            for suffix, text in (
                (".kicad_sch", '(kicad_sch (version 20260306))\n'),
                (".kicad_pro", '{}\n'),
                (".kicad_pcb",
                 '(kicad_pcb (version 20260306)'
                 ' (footprint "Test:tap" (property "Reference" "C1"))'
                 ' (segment (start 0 0) (end 1 1)))\n'),
            ):
                (folder / (name + suffix)).write_text(text, encoding="utf-8")
            settings = {
                "manufacturer": "JLCPCB", "board_type": name,
                "assembly": name == "rigid", "released": True,
                "origin": "plot", "drill_files_required": True,
                "copper_layers": 2, "gerber_layers": ["F.Cu", "B.Cu", "Edge.Cuts"],
            }
            (folder / "order-settings.json").write_text(json.dumps(settings))
            boards[name] = {
                "schematic": self.rel(folder / (name + ".kicad_sch")),
                "pcb": self.rel(folder / (name + ".kicad_pcb")),
                "project": self.rel(folder / (name + ".kicad_pro")),
                "assembly": name == "rigid",
                "order_settings": self.rel(folder / "order-settings.json"),
                "extra_fabrication_files": [],
            }
        self.bom = self.base / "rigid/bom.csv"
        self.cpl = self.base / "rigid/cpl.csv"
        self.bom.write_text("Comment,Designator,Footprint,LCSC Part #\n"
                            "Fixture capacitor,C1,tap,C12345\n")
        self.cpl.write_text("Designator,Mid X,Mid Y,Layer,Rotation\n"
                            "C1,1.0,2.0,top,0\n")
        boards["rigid"].update(bom_csv=self.rel(self.bom), cpl_csv=self.rel(self.cpl))
        boards["flex"]["extra_fabrication_files"] = [
            {"path": self.rel(self.base / "coverlay.gbr"), "name": "flex-coverlay.gbr"}]
        self.manifest = {
            "schema_version": 1, "release_id": "mock-test",
            "status": "approved_for_prototype_fabrication", "boards": boards,
            "footprint_libraries": [{"name": "Test", "path": self.rel(self.library)}],
            "extra_inputs": [self.rel(self.base / "notes.txt")],
            "review_evidence": self.rel(self.review_path),
        }
        self.save_manifest()
        snapshot = self.audit()
        self.assertEqual(snapshot.returncode, 2)
        self.evidence = {
            "schema_version": 1, "release_id": "mock-test", "approved": True,
            "reviews": [{"topic": topic, "status": "accepted", "reviewer": "test fixture",
                         "reviewed_at": "2026-10-09T21:00:00-05:00",
                         "notes": "Mock-only design-closure record."}
                        for topic in sorted(TOPICS)],
            "input_sha256": json.loads(snapshot.stdout)["input_sha256"],
        }
        self.review_path.write_text(json.dumps(self.evidence))

    def tearDown(self):
        self.temporary.cleanup()

    def rel(self, path):
        return path.relative_to(self.root).as_posix()

    def write(self, name, text):
        (self.base / name).write_text(text, encoding="utf-8")

    def save_manifest(self):
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")

    def call(self, *args, fail=None, omit_layer=None, empty_drill=False):
        env = os.environ.copy()
        for key in ("FAKE_KICAD_FAIL", "FAKE_KICAD_OMIT_LAYER", "FAKE_KICAD_EMPTY_DRILL"):
            env.pop(key, None)
        if fail is not None:
            env["FAKE_KICAD_FAIL"] = fail
        if omit_layer is not None:
            env["FAKE_KICAD_OMIT_LAYER"] = omit_layer
        if empty_drill:
            env["FAKE_KICAD_EMPTY_DRILL"] = "1"
        return subprocess.run([sys.executable, str(self.tool), *args],
                              cwd=self.root, text=True, capture_output=True,
                              env=env, check=False, timeout=30)

    def audit(self):
        return self.call("--audit", "--json")

    def export(self, fail=None, omit_layer=None, empty_drill=False):
        return self.call("--export", "--output", str(self.output),
                         "--kicad-cli", str(self.fake_cli), fail=fail,
                         omit_layer=omit_layer, empty_drill=empty_drill)

    def test_audit_passes_fixture_without_invoking_kicad(self):
        self.fake_cli.unlink()
        result = self.audit()
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertFalse(json.loads(result.stdout)["blocked"])
        self.assertFalse(self.output.exists())

    def test_ready_flag_cannot_replace_missing_cad(self):
        self.manifest["ready"] = True
        self.save_manifest()
        (self.base / "rigid/rigid.kicad_pcb").unlink()
        (self.base / "flex/flex.kicad_sch").unlink()
        result = self.audit()
        self.assertEqual(result.returncode, 2)
        text = result.stdout
        self.assertIn("rigid.kicad_pcb", text)
        self.assertIn("flex.kicad_sch", text)

    def test_blocked_or_missing_manifest_status_is_rejected(self):
        for status in ("blocked_missing_circuit_and_layout", None):
            with self.subTest(status=status):
                if status is None:
                    self.manifest.pop("status", None)
                else:
                    self.manifest["status"] = status
                self.save_manifest()
                result = self.audit()
                self.assertEqual(result.returncode, 2)
                self.assertIn("Manifest status", result.stdout)

    def test_review_invalidated_by_input_edit(self):
        self.write("notes.txt", "Changed pin plan\n")
        result = self.audit()
        self.assertEqual(result.returncode, 2)
        self.assertIn("missing/stale", result.stdout)

    def test_review_topics_cannot_be_omitted(self):
        self.evidence["reviews"] = []
        self.review_path.write_text(json.dumps(self.evidence))
        result = self.audit()
        self.assertEqual(result.returncode, 2)
        self.assertIn("Missing design-closure", result.stdout)

    def test_csv_nan_layer_and_reference_mismatch_are_rejected(self):
        for row, message in [
            ("C1,nan,2.0,top,0", "nonfinite"),
            ("C1,1.0,2.0,inner,0", "Layer"),
            ("C2,1.0,2.0,top,0", "BOM/CPL mismatch"),
        ]:
            with self.subTest(row=row):
                self.cpl.write_text("Designator,Mid X,Mid Y,Layer,Rotation\n" + row + "\n")
                result = self.audit()
                self.assertEqual(result.returncode, 2)
                self.assertIn(message, result.stdout)

    def test_repository_path_escape_is_rejected(self):
        self.manifest["extra_inputs"] = ["../../outside.txt"]
        self.save_manifest()
        result = self.audit()
        self.assertEqual(result.returncode, 2)
        self.assertIn("Unsafe input path", result.stdout)

    def test_passive_flex_has_no_fake_assembly_files(self):
        self.manifest["boards"]["flex"]["bom_csv"] = self.rel(self.bom)
        self.save_manifest()
        result = self.audit()
        self.assertEqual(result.returncode, 2)
        self.assertIn("Passive flex", result.stdout)

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        sentinel = self.output / "sentinel"
        sentinel.write_text("keep")
        result = self.export()
        self.assertEqual(result.returncode, 2)
        self.assertEqual(sentinel.read_text(), "keep")
        self.assertEqual(list(self.output.iterdir()), [sentinel])

    def test_erc_violation_blocks_and_removes_staging(self):
        result = self.export(fail="erc")
        self.assertEqual(result.returncode, 2)
        self.assertIn("KiCad failed (5)", result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".g1-jlc-stage-*")))

    def test_drc_violation_blocks_and_removes_staging(self):
        result = self.export(fail="drc")
        self.assertEqual(result.returncode, 2)
        self.assertIn("KiCad failed (5)", result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".g1-jlc-stage-*")))

    def test_missing_requested_layer_blocks_export(self):
        result = self.export(omit_layer="B.Cu")
        self.assertEqual(result.returncode, 2)
        self.assertIn("requested layer B.Cu", result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".g1-jlc-stage-*")))

    def test_empty_drill_file_blocks_export(self):
        result = self.export(empty_drill=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("empty drill file", result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".g1-jlc-stage-*")))

    def test_late_failure_publishes_no_partial_zip(self):
        result = self.export(fail="flex-drill")
        self.assertEqual(result.returncode, 2)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".g1-jlc-stage-*")))
        self.assertFalse(list(self.root.rglob("*-gerbers.zip")))

    def test_mock_export_packages_separate_boards_and_reviewed_extras(self):
        result = self.export()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.output / "rigid/jlcpcb-bom.csv").is_file())
        self.assertTrue((self.output / "rigid/jlcpcb-cpl.csv").is_file())
        self.assertFalse((self.output / "flex/jlcpcb-bom.csv").exists())
        self.assertFalse((self.output / "flex/jlcpcb-cpl.csv").exists())
        for name in ("rigid", "flex"):
            with zipfile.ZipFile(self.output / name / (name + "-gerbers.zip")) as archive:
                self.assertIn("board-PTH.drl", archive.namelist())
                for layer in ("F_Cu", "B_Cu", "Edge_Cuts"):
                    self.assertIn("board-" + layer + ".gbr", archive.namelist())
                self.assertNotIn("partial.gbrjob", archive.namelist())
                if name == "flex":
                    self.assertIn("flex-coverlay.gbr", archive.namelist())
            command = json.loads((self.output / name / "checks/drc.log").read_text().splitlines()[0])
            for flag in ("--severity-all", "--exit-code-violations",
                         "--schematic-parity", "--refill-zones"):
                self.assertIn(flag, command)
        self.assertTrue((self.output / "SHA256SUMS").is_file())
        self.assertTrue((self.output / "engineering-review.json").is_file())
        self.assertFalse(list(self.root.glob(".g1-jlc-stage-*")))

if __name__ == "__main__":
    unittest.main()
