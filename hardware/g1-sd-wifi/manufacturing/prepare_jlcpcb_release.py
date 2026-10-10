#!/usr/bin/env python3
"""Fail-closed JLCPCB preflight/export. Python 3.10+, stdlib only.

Default mode audits files and recorded design reviews; it does not run KiCad.
Export additionally requires KiCad 10 ERC/DRC, then creates separate rigid/flex
packages. Passing this tool is not evidence of working ATA, RF or power-cut
behavior. Fabricated prototypes still require electrical and assembled tests.
"""
import argparse
import csv
import hashlib
import json
import math
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from datetime import datetime
from pathlib import Path, PurePosixPath

TOPICS = {
    "rigid_electrical", "rigid_layout", "flex_pin_mapping",
    "flex_geometry_stackup", "assembly_bom_cpl", "power_bus_isolation",
    "mechanical_clearance", "order_settings",
}
BOM_HEADERS = {"Comment", "Designator", "Footprint", "LCSC Part #"}
CPL_HEADERS = {"Designator", "Mid X", "Mid Y", "Layer", "Rotation"}
NATIVE_SUFFIXES = {
    ".kicad_sch", ".kicad_pcb", ".kicad_pro", ".kicad_dru",
    ".kicad_sym", ".kicad_mod",
}
LAYERS = {
    "F.Cu", "B.Cu", "F.Mask", "B.Mask", "F.Paste", "B.Paste",
    "F.SilkS", "B.SilkS", "Edge.Cuts", "F.Fab", "B.Fab", "Dwgs.User",
} | {f"In{i}.Cu" for i in range(1, 31)} | {f"User.{i}" for i in range(1, 10)}


class Blocked(Exception):
    pass


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative_path(root, value):
    if not isinstance(value, str) or not value or "\\" in value:
        raise Blocked(f"Expected a repository-relative POSIX path: {value!r}")
    rel = PurePosixPath(value)
    if rel.is_absolute() or ".." in rel.parts or value in {".", ""}:
        raise Blocked(f"Unsafe input path: {value!r}")
    path = root.joinpath(*rel.parts).resolve()
    if not path.is_relative_to(root):
        raise Blocked(f"Input is outside the repository: {value!r}")
    return path


def load_json(path):
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise Blocked(f"JSON object required: {path.name}")
    return value


def sexpr(path):
    """Read enough native KiCad structure to check referenced footprint files."""
    stack, root = [], None
    tokens = re.finditer(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+',
                         path.read_text(encoding="utf-8"))
    for match in tokens:
        token = match.group(0)
        if token == "(":
            node = []
            if stack:
                stack[-1].append(node)
            elif root is not None:
                raise Blocked(f"Multiple S-expression roots: {path.name}")
            else:
                root = node
            stack.append(node)
        elif token == ")":
            if not stack:
                raise Blocked(f"Unbalanced S-expression: {path.name}")
            stack.pop()
        elif stack:
            stack[-1].append(json.loads(token) if token.startswith('"') else token)
        else:
            raise Blocked(f"Data outside S-expression: {path.name}")
    if stack or not root:
        raise Blocked(f"Incomplete native CAD: {path.name}")
    return root


def csv_records(path, headers):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or set(reader.fieldnames) != headers:
            raise Blocked(f"{path.name}: required headers are {sorted(headers)}")
        if len(reader.fieldnames) != len(headers):
            raise Blocked(f"{path.name}: duplicate CSV headers")
        records = list(reader)
    for row in records:
        if None in row or any(v is None for v in row.values()):
            raise Blocked(f"{path.name}: malformed CSV row")
    return records


def assembly_references(bom, cpl):
    """Exact populated ref match; no automatic coordinate/rotation correction."""
    bom_refs, cpl_refs = set(), set()
    for row in csv_records(bom, BOM_HEADERS):
        if not all(row[k].strip() for k in BOM_HEADERS):
            raise Blocked(f"{bom.name}: blank BOM field")
        if not re.fullmatch(r"C[0-9]+", row["LCSC Part #"].strip()):
            raise Blocked(f"{bom.name}: exact LCSC code required")
        refs = re.split(r"[,\s]+", row["Designator"].strip())
        if len(set(refs)) != len(refs) or bom_refs.intersection(refs):
            raise Blocked(f"{bom.name}: duplicate designator")
        if any(not re.fullmatch(r"[A-Za-z][A-Za-z0-9_.-]*", ref) for ref in refs):
            raise Blocked(f"{bom.name}: invalid designator")
        bom_refs.update(refs)
    for row in csv_records(cpl, CPL_HEADERS):
        ref = row["Designator"].strip()
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_.-]*", ref) or ref in cpl_refs:
            raise Blocked(f"{cpl.name}: invalid/duplicate designator")
        cpl_refs.add(ref)
        for key in ("Mid X", "Mid Y", "Rotation"):
            try:
                number = float(row[key])
            except ValueError as exc:
                raise Blocked(f"{cpl.name}: nonnumeric {key}") from exc
            if not math.isfinite(number):
                raise Blocked(f"{cpl.name}: nonfinite {key}")
        if row["Layer"].strip().lower() not in {"top", "bottom"}:
            raise Blocked(f"{cpl.name}: Layer must be top or bottom")
    if not bom_refs or bom_refs != cpl_refs:
        raise Blocked(
            f"BOM/CPL mismatch: missing CPL={sorted(bom_refs-cpl_refs)}, "
            f"extra CPL={sorted(cpl_refs-bom_refs)}; empty assembly not allowed")
    return bom_refs


def review_errors(evidence, release_id, inputs):
    errors = []
    if (evidence.get("schema_version") != 1
            or evidence.get("release_id") != release_id
            or evidence.get("approved") is not True):
        errors.append("Engineering review is not approved for this release.")
    if evidence.get("input_sha256") != inputs:
        errors.append("Engineering review input hashes are missing/stale.")
    reviews = evidence.get("reviews")
    if not isinstance(reviews, list):
        return errors + ["Engineering review records are missing."]
    seen = set()
    for item in reviews:
        if not isinstance(item, dict):
            errors.append("Malformed engineering review record.")
            continue
        topic = item.get("topic")
        if not isinstance(topic, str) or topic in seen:
            errors.append("Missing/duplicate engineering review topic.")
            continue
        seen.add(topic)
        if item.get("status") != "accepted":
            errors.append(f"Design review is not accepted: {topic}")
        for key in ("reviewer", "reviewed_at", "notes"):
            if not isinstance(item.get(key), str) or not item[key].strip():
                errors.append(f"Review {topic} needs {key}.")
        try:
            datetime.fromisoformat(item.get("reviewed_at", "").replace("Z", "+00:00"))
        except (ValueError, AttributeError):
            errors.append(f"Review {topic} has invalid reviewed_at.")
    if not TOPICS.issubset(seen):
        errors.append(f"Missing design-closure reviews: {sorted(TOPICS-seen)}")
    return errors


class Audit:
    def __init__(self, root, manifest_path):
        self.root, self.manifest_path = root, manifest_path
        self.errors, self.inputs, self.boards = [], {}, {}
        self.release_id, self.review_path, self.review_sha256 = None, None, None

    def input(self, value):
        path = relative_path(self.root, value)
        if not path.is_file() or path.stat().st_size == 0:
            raise Blocked(f"Missing or empty required input: {value}")
        self.inputs[path.relative_to(self.root).as_posix()] = sha256(path)
        return path

    def attempt(self, callback):
        try:
            return callback()
        except (Blocked, OSError, ValueError, KeyError, TypeError) as exc:
            self.errors.append(str(exc))
            return None

    def run(self):
        manifest = self.attempt(lambda: load_json(self.input(
            self.manifest_path.relative_to(self.root).as_posix())))
        if manifest is None:
            return self
        self.attempt(lambda: self.input(Path(__file__).resolve().relative_to(
            self.root).as_posix()))
        self.release_id = manifest.get("release_id")
        if manifest.get("status") != "approved_for_prototype_fabrication":
            self.errors.append("Manifest status must be approved_for_prototype_fabrication.")
        if (manifest.get("schema_version") != 1
                or not isinstance(self.release_id, str) or not self.release_id.strip()):
            self.errors.append("Manifest needs schema_version 1 and a release_id.")
        libraries = {}
        entries = manifest.get("footprint_libraries", [])
        if not isinstance(entries, list) or not entries:
            self.errors.append("Native local footprint libraries are required.")
            entries = []
        for item in entries:
            def add_library(item=item):
                name, value = item["name"], item["path"]
                if (not isinstance(name, str) or not name or ":" in name
                        or name in libraries):
                    raise Blocked("Invalid/duplicate footprint library name.")
                directory = relative_path(self.root, value)
                files = sorted(directory.glob("*.kicad_mod"))
                if not directory.is_dir() or not files:
                    raise Blocked(f"Missing/empty footprint library: {value}")
                libraries[name] = directory
                for path in files:
                    self.input(path.relative_to(self.root).as_posix())
            self.attempt(add_library)
        extra_inputs = manifest.get("extra_inputs")
        if not isinstance(extra_inputs, list) or not extra_inputs:
            self.errors.append("Explicit extra_inputs are required.")
        else:
            for value in extra_inputs:
                self.attempt(lambda value=value: self.input(value))
        boards = manifest.get("boards", {})
        if not isinstance(boards, dict) or set(boards) != {"rigid", "flex"}:
            self.errors.append("Manifest must define exactly rigid and flex boards.")
            boards = {}
        for name, board in boards.items():
            if not isinstance(board, dict):
                self.errors.append(f"{name}: board must be an object.")
                continue
            paths = {}
            fields = ("schematic", "pcb", "project", "order_settings")
            if board.get("assembly") is True:
                fields += ("bom_csv", "cpl_csv")
            elif board.get("assembly") is not False:
                self.errors.append(f"{name}: explicit assembly boolean is required.")
            if name == "rigid" and board.get("assembly") is not True:
                self.errors.append("Rigid release requires its reviewed PCBA BOM/CPL.")
            if name == "flex" and (board.get("assembly") is not False
                                  or "bom_csv" in board or "cpl_csv" in board):
                self.errors.append("Passive flex has assembly:false and no BOM/CPL.")
            for field in fields:
                paths[field] = self.attempt(lambda field=field: self.input(board[field]))
            native = [paths.get(k) for k in ("schematic", "pcb", "project")]
            if all(native):
                if (len({p.parent for p in native}) != 1
                        or len({p.stem for p in native}) != 1
                        or [p.suffix for p in native] != [
                            ".kicad_sch", ".kicad_pcb", ".kicad_pro"]):
                    self.errors.append(f"{name}: matching native project filenames required.")
                for path in sorted(native[0].parent.rglob("*")):
                    if (path.is_file() and (path.suffix in NATIVE_SUFFIXES
                                          or path.name in {"fp-lib-table", "sym-lib-table"})):
                        self.attempt(lambda path=path: self.input(
                            path.relative_to(self.root).as_posix()))
            if paths.get("pcb"):
                def check_pcb():
                    tree = sexpr(paths["pcb"])
                    if tree[0] != "kicad_pcb":
                        raise Blocked(f"{name}: native PCB required.")
                    footprints = [n for n in tree if isinstance(n, list) and n
                                  and n[0] == "footprint"]
                    if not footprints or not any(
                        isinstance(n, list) and n and n[0] in {"segment", "arc", "zone"}
                        for n in tree
                    ):
                        raise Blocked(f"{name}: populated and routed PCB required.")
                    for fp in footprints:
                        if len(fp) < 2 or ":" not in fp[1]:
                            raise Blocked(f"{name}: footprint lacks local library ID.")
                        library, component = fp[1].split(":", 1)
                        directory = libraries.get(library)
                        if (directory is None or "/" in component or "\\" in component
                                or not (directory / (component + ".kicad_mod")).is_file()):
                            raise Blocked(f"{name}: unresolved footprint {fp[1]}")
                self.attempt(check_pcb)
            if paths.get("schematic"):
                def check_schematic():
                    if sexpr(paths["schematic"])[0] != "kicad_sch":
                        raise Blocked(f"{name}: native schematic required.")
                self.attempt(check_schematic)
            if paths.get("bom_csv") and paths.get("cpl_csv"):
                self.attempt(lambda: assembly_references(paths["bom_csv"], paths["cpl_csv"]))
            settings = {}
            if paths.get("order_settings"):
                settings = self.attempt(lambda: load_json(paths["order_settings"])) or {}
                layers = settings.get("gerber_layers", [])
                valid_layers = (isinstance(layers, list) and bool(layers)
                                and all(isinstance(x, str) and x in LAYERS for x in layers)
                                and len(set(layers)) == len(layers))
                if (settings.get("manufacturer") != "JLCPCB"
                        or settings.get("board_type") != name
                        or settings.get("assembly") is not board.get("assembly")
                        or settings.get("released") is not True
                        or settings.get("origin") != "plot"
                        or not isinstance(settings.get("drill_files_required"), bool)
                        or not valid_layers or not {"F.Cu", "Edge.Cuts"}.issubset(layers)
                        or type(settings.get("copper_layers")) is not int
                        or sum(x.endswith(".Cu") for x in layers) != settings.get("copper_layers")):
                    self.errors.append(f"{name}: released order/export settings are incomplete.")
            extras, names = [], set()
            entries = board.get("extra_fabrication_files", [])
            if not isinstance(entries, list):
                self.errors.append(f"{name}: extra_fabrication_files must be a list.")
                entries = []
            if name == "flex" and not entries:
                self.errors.append("Flex requires reviewed coverlay/stiffener fabrication files.")
            for item in entries:
                def add_extra(item=item):
                    destination = item["name"]
                    if (not isinstance(destination, str) or not destination
                            or destination in {".", ".."} or "/" in destination
                            or "\\" in destination or ":" in destination or destination in names):
                        raise Blocked(f"{name}: unsafe/duplicate fabrication basename.")
                    source = self.input(item["path"])
                    names.add(destination)
                    extras.append((source, destination))
                self.attempt(add_extra)
            self.boards[name] = {"paths": paths, "settings": settings, "extras": extras}
        def check_review():
            path = relative_path(self.root, manifest["review_evidence"])
            if not path.is_file() or not path.stat().st_size:
                raise Blocked("Recorded engineering design review is missing.")
            evidence = load_json(path)
            self.review_path, self.review_sha256 = path, sha256(path)
            self.errors.extend(review_errors(evidence, self.release_id, self.inputs))
        self.attempt(check_review)
        return self

    def report(self):
        return {
            "release_id": self.release_id, "blocked": bool(self.errors),
            "blockers": self.errors, "input_sha256": dict(sorted(self.inputs.items())),
            "review_sha256": self.review_sha256,
            "scope": "Design/file preflight only; hardware behavior remains unproven.",
        }


def run_cli(cli, arguments, root, log):
    command = [cli, *map(str, arguments)]
    result = subprocess.run(command, cwd=root, text=True, capture_output=True,
                            timeout=600, check=False)
    log.write_text(json.dumps(command) + "\n" + result.stdout + result.stderr,
                   encoding="utf-8")
    if result.returncode:
        raise Blocked(f"KiCad failed ({result.returncode}): {log.name}\n"
                      + (result.stdout + result.stderr)[-3000:])
    return result.stdout.strip()


def export(audit, output, cli_name):
    if output.exists() or output.is_symlink():
        raise Blocked("Output already exists; choose a new release directory.")
    cli = shutil.which(cli_name)
    if cli is None:
        raise Blocked("KiCad 10 kicad-cli is required for export.")
    version = subprocess.run([cli, "version"], text=True, capture_output=True,
                             check=True, timeout=30).stdout.strip()
    if not re.match(r"^10\.", version):
        raise Blocked(f"KiCad 10 is required; found {version!r}.")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir()  # Exclusive reservation; never overwrite a prior release.
    staging = None
    success = False
    try:
        staging = Path(tempfile.mkdtemp(prefix=".g1-jlc-stage-", dir=output.parent))
        for name, board in audit.boards.items():
            folder = staging / name
            reports = folder / "checks"
            gerbers = folder / "gerbers"
            reports.mkdir(parents=True)
            gerbers.mkdir()
            paths, settings = board["paths"], board["settings"]
            run_cli(cli, ["sch", "erc", "--severity-all", "--exit-code-violations",
                          "--format", "json", "--output", reports / "erc.json",
                          paths["schematic"]], audit.root, reports / "erc.log")
            run_cli(cli, ["pcb", "drc", "--severity-all", "--exit-code-violations",
                          "--schematic-parity", "--all-track-errors", "--refill-zones",
                          "--format", "json", "--output", reports / "drc.json",
                          paths["pcb"]], audit.root, reports / "drc.log")
            # One isolated export per layer avoids filename assumptions and
            # guarantees an empty/missing requested layer cannot hide behind
            # another layer's valid artwork. Partial .gbrjob files are omitted.
            for index, layer in enumerate(settings["gerber_layers"]):
                layer_folder = folder / f"layer-{index:02d}"
                layer_folder.mkdir()
                run_cli(cli, ["pcb", "export", "gerbers", "--layers", layer,
                              "--no-protel-ext", "--check-zones",
                              "--use-drill-file-origin", "--output",
                              str(layer_folder) + "/", paths["pcb"]], audit.root,
                        reports / f"gerber-{index:02d}.log")
                artwork = list(layer_folder.glob("*.gbr"))
                if (len(artwork) != 1 or not artwork[0].is_file()
                        or artwork[0].stat().st_size == 0):
                    raise Blocked(f"{name}: requested layer {layer} did not export "
                                  "exactly one nonempty Gerber.")
                destination = gerbers / artwork[0].name
                if destination.exists():
                    raise Blocked(f"{name}: native layer Gerber basename collision.")
                shutil.copyfile(artwork[0], destination)
                shutil.rmtree(layer_folder)
            run_cli(cli, ["pcb", "export", "drill", "--format", "excellon",
                          "--drill-origin", "plot", "--excellon-units", "mm",
                          "--excellon-separate-th", "--output", str(gerbers) + "/",
                          paths["pcb"]], audit.root, reports / "drill.log")
            drill_files = list(gerbers.glob("*.drl"))
            if any(not path.is_file() or not path.stat().st_size for path in drill_files):
                raise Blocked(f"{name}: exported an empty drill file.")
            if settings["drill_files_required"] and not drill_files:
                raise Blocked(f"{name}: required nonempty drill files were not exported.")
            for source, destination in board["extras"]:
                if (gerbers / destination).exists():
                    raise Blocked(f"{name}: fabrication file would overwrite native export.")
                shutil.copyfile(source, gerbers / destination)
            with zipfile.ZipFile(folder / f"{name}-gerbers.zip", "w",
                                 compression=zipfile.ZIP_DEFLATED) as archive:
                for path in sorted(gerbers.iterdir()):
                    if path.is_file():
                        archive.write(path, path.name)
            shutil.rmtree(gerbers)
            shutil.copyfile(paths["order_settings"], folder / "order-settings.json")
            if name == "rigid":
                shutil.copyfile(paths["bom_csv"], folder / "jlcpcb-bom.csv")
                shutil.copyfile(paths["cpl_csv"], folder / "jlcpcb-cpl.csv")
        final_audit = Audit(audit.root, audit.manifest_path).run()
        if (final_audit.errors or final_audit.inputs != audit.inputs
                or final_audit.review_sha256 != audit.review_sha256):
            raise Blocked("Manufacturing inputs/reviews changed during export; rerun.")
        shutil.copyfile(audit.manifest_path, staging / "release-manifest.json")
        shutil.copyfile(audit.review_path, staging / "engineering-review.json")
        provenance = audit.report() | {"kicad_version": version}
        (staging / "release-provenance.json").write_text(
            json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        checksums = [f"{sha256(path)}  {path.relative_to(staging).as_posix()}"
                     for path in sorted(staging.rglob("*")) if path.is_file()]
        (staging / "SHA256SUMS").write_text("\n".join(checksums) + "\n", encoding="ascii")
        for path in staging.iterdir():
            shutil.move(str(path), output / path.name)
        success = True
    finally:
        if staging is not None:
            shutil.rmtree(staging, ignore_errors=True)
        if not success:
            shutil.rmtree(output, ignore_errors=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--audit", action="store_true", help="Preflight without KiCad (default)")
    modes.add_argument("--export", action="store_true", help="Run KiCad 10 and package fabrication")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--manifest", default="hardware/g1-sd-wifi/manufacturing/release-manifest.json")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--kicad-cli", default="kicad-cli")
    parser.add_argument("--json", action="store_true", help="Print machine-readable preflight")
    args = parser.parse_args(argv)
    try:
        root = args.root.resolve()
        audit = Audit(root, relative_path(root, args.manifest)).run()
        if args.json:
            print(json.dumps(audit.report(), indent=2, sort_keys=True))
        elif audit.errors:
            print("BLOCKED: manufacturing files cannot be released.", file=sys.stderr)
            for error in audit.errors:
                print(f"- {error}", file=sys.stderr)
        else:
            print("Design/file preflight passed; hardware behavior remains unproven.")
        if audit.errors:
            return 2
        if args.export:
            if args.output is None:
                raise Blocked("--export requires an explicit --output directory.")
            export(audit, args.output.absolute(), args.kicad_cli)
            print(f"Created separate rigid/flex prototype fabrication packages: {args.output}")
        return 0
    except (Blocked, OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"BLOCKED: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
