#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Named destinations through the real capture engine and FAT32/exFAT.

The default capture fixture remains unchanged. Environment settings here select
the new output policy, a recognizable title and distinct content with that same
title. Independent hashes check that naming never changes saved track bytes.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import verify_dump
from test_images import run as command
from test_known_images import catalogue

BINARY = str(ROOT / "build/capture-image")


def run(image, *args, root="/Games", title="MDK2", variant=0, job=None,
        opts="crc32,noend", expected=0):
    env = dict(os.environ, KUI_TEST_OPTS=opts, KUI_TEST_TITLE=title,
               KUI_TEST_DISC_VARIANT=str(variant))
    for key in ("KUI_TEST_OUTPUT_ROOT", "KUI_TEST_GAME_NAMES", "KUI_TEST_JOB"):
        env.pop(key, None)
    if root is not None:
        env.update(KUI_TEST_OUTPUT_ROOT=root, KUI_TEST_GAME_NAMES="1")
    if job is not None:
        env["KUI_TEST_JOB"] = job
    result = subprocess.run([BINARY, str(image), *args], env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode != expected:
        raise AssertionError(f"{args}, root={root!r}: exit {result.returncode}, expected {expected}\n{result.stdout}")
    return result.stdout


def field(output, label):
    match = re.search(r"^" + re.escape(label) + r" (.*)$", output, re.M)
    assert match, (label, output)
    return match[1]


def job_path(output):
    return re.search(r"^STATS .* job=(.*)$", output, re.M)[1]


def reference(output):
    match = re.fullmatch(r"checked=(\d+) result=(-?\d+) catalog=(.*)", field(output, "REFERENCE"))
    assert match
    return int(match[1]), int(match[2]), match[3]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def files(folder):
    return {str(p.relative_to(folder)): digest(p) for p in folder.rglob("*") if p.is_file()}


def export(image, destination, root="/Games"):
    destination.mkdir()
    run(image, "export", str(destination), root=root)
    return destination


def named_manifest(folder, expected_name="MDK2.gdi"):
    manifest = json.loads((folder / "manifest.json").read_text())
    assert manifest["gdi_file"] == expected_name
    assert (folder / expected_name).is_file()
    assert not (folder / "disc.gdi").exists()
    assert verify_dump.verify(folder)
    return manifest


def main():
    with tempfile.TemporaryDirectory(prefix="kui-named-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            command("mkfs.fat", "-F", "32", str(clean)) if kind == "fat32" else command("mkfs.exfat", str(clean))
            run(clean, "seed")
            count = 0

            def copy_image(label, source=clean):
                nonlocal count
                count += 1
                target = base / f"{kind}-{count}-{label}.img"
                shutil.copyfile(source, target)
                return target

            # NULL output retains the old directory and metadata contract.
            legacy = copy_image("legacy")
            legacy_output = run(legacy, "new", root=None)
            legacy_job = job_path(legacy_output)
            assert legacy_job.startswith("0:/KUI/dumps/d")
            legacy_export = export(legacy, base / f"{kind}-legacy", root=None)
            legacy_folder = next(legacy_export.iterdir())
            assert "gdi_file" not in json.loads((legacy_folder / "manifest.json").read_text())
            assert (legacy_folder / "disc.gdi").is_file()
            tracks = verify_dump.verify(legacy_folder)
            expected_hashes = {p.name: digest(p) for p in legacy_folder.glob("track*")}

            image = copy_image("named")
            output = run(image, "new")
            assert job_path(output) == "0:/Games/MDK2"
            assert field(output, "DISC_TITLE") == "MDK2" and field(output, "GDI_NAME") == "MDK2.gdi"
            assert reference(output)[:2] == (1, 1)  # lookup attempted, no database
            first = export(image, base / f"{kind}-first") / "MDK2"
            named_manifest(first)
            assert {p.name: digest(p) for p in first.glob("track*")} == expected_hashes
            first_files = files(first)
            complete_one = copy_image("complete-one", image)

            # A second New never overwrites the first. Its interrupted state
            # resumes in its own title-numbered folder and ends byte-identical.
            stopped = run(image, "new", "stop-middle", expected=3)
            assert job_path(stopped) == "0:/Games/MDK2 (2)"
            assert reference(stopped)[0] == 0
            output = run(image, "resume")
            assert job_path(output) == "0:/Games/MDK2 (2)"
            twice = export(image, base / f"{kind}-twice")
            assert files(twice / "MDK2") == first_files
            named_manifest(twice / "MDK2 (2)")
            assert {p.name: digest(p) for p in (twice / "MDK2 (2)").glob("track*")} == expected_hashes

            # The newest title match may be a different disc/revision. Both
            # Resume and Verify must use checkpoint identity, not its name.
            other = run(image, "new", variant=1)
            assert job_path(other) == "0:/Games/MDK2 (3)"
            for mode in ("resume", "verify"):
                before = digest(image)
                selected = run(image, mode)
                assert job_path(selected) == "0:/Games/MDK2 (2)"
                assert "WRITES 0" in selected and digest(image) == before
            run(image, "mutate", "both-checkpoints", job="0:/Games/MDK2 (2)")
            before = digest(image)
            selected = run(image, "verify")
            assert job_path(selected) == "0:/Games/MDK2"
            assert "WRITES 0" in selected and digest(image) == before
            print(f"PASS {kind} named: title/GDI, no overwrite, interrupted resume, full-identity selection", flush=True)

            # Existing files count as collisions too, with FAT case folding.
            collision = copy_image("file-collision")
            run(collision, "mkdir", "0:/Games")
            marker = base / f"{kind}-keep-marker.txt"
            marker.write_text("EXISTING FILE MUST SURVIVE\n")
            run(collision, "put", str(marker), "0:/Games/mdk2")
            output = run(collision, "new")
            assert job_path(output) == "0:/Games/MDK2 (2)"
            collided = export(collision, base / f"{kind}-collision")
            preserved = [p for p in collided.iterdir() if p.is_file()]
            assert len(preserved) == 1 and preserved[0].read_bytes() == marker.read_bytes()
            named_manifest(collided / "MDK2 (2)")

            # The UI default can still find a legacy partial job when there is
            # no matching named checkpoint in the selected parent directory.
            fallback = copy_image("fallback", legacy)
            before = digest(fallback)
            output = run(fallback, "verify", root="/Different/Parent")
            assert job_path(output) == legacy_job
            assert field(output, "GDI_NAME") == "disc.gdi"
            assert "WRITES 0" in output and digest(fallback) == before

            # A longest valid root and title exercise complete path capacity,
            # nested mkdirs and the GDI filename without silent truncation.
            long_root = "/" + "a" * 62 + "/" + "b" * 63
            title = "T" * 128
            long_image = copy_image("long-path")
            output = run(long_image, "new", root=long_root, title=title)
            actual = job_path(output)
            assert actual.startswith("0:" + long_root + "/") and len(actual) < 256
            gdi = field(output, "GDI_NAME")
            assert gdi.endswith(".gdi") and len(gdi) <= 100
            long_files = export(long_image, base / f"{kind}-long", root=long_root)
            long_job = next(long_files.iterdir())
            named_manifest(long_job, gdi)

            hidden = copy_image("dot-title")
            output = run(hidden, "new", title="...Hidden")
            hidden_gdi = field(output, "GDI_NAME")
            assert not hidden_gdi.startswith(".")
            hidden_files = export(hidden, base / f"{kind}-hidden")
            named_manifest(next(hidden_files.iterdir()), hidden_gdi)

            invalid = copy_image("invalid")
            before = digest(invalid)
            for parent in ("", "relative", "/../escape", "/Games/../Else", "0:/Games", "/Games/CON", "/" + "x" * 128):
                output = run(invalid, "new", root=parent, expected=1)
                assert "WRITES 0" in output and digest(invalid) == before, parent
            print(f"PASS {kind} named: file collisions, legacy fallback, maximum/invalid destinations", flush=True)

            # Failed manifest publication must not report a completed capture.
            # A later resume can use the committed tracks and regenerate it.
            publication = copy_image("publish-failure")
            failed = run(publication, "new", "manifest-write-fail", expected=1)
            assert "CAPTURE/VERIFY FAILED" in failed and reference(failed)[0] == 0
            partial = export(publication, base / f"{kind}-unpublished") / "MDK2"
            assert not (partial / "manifest.json").exists()
            resumed = run(publication, "resume")
            assert job_path(resumed) == "0:/Games/MDK2"
            named_manifest(export(publication, base / f"{kind}-published") / "MDK2")

            # Structured reference status is distinct from successful capture
            # or read-back; test every UI-relevant grade through the real lookup.
            grades = [
                ("full", catalogue(tracks), 6),
                ("data", catalogue(tracks, flip={2}), 5),
                ("partial", catalogue(tracks, flip={3}), 3),
                ("identified", catalogue(tracks, only={3}), 4),
                ("none", catalogue(tracks, flip={1, 3, 6}), 2),
                ("unreadable", "NOT A CATALOGUE\n", -1),
            ]
            for label, text, grade in grades:
                referenced = copy_image(f"reference-{label}", complete_one)
                database = base / f"{kind}-{label}.db"
                database.write_text(text)
                run(referenced, "put", str(database), "0:/KUI/tosec.db")
                before = digest(referenced)
                output = run(referenced, "verify")
                checked, result, catalog = reference(output)
                assert checked == 1 and result == grade, (label, reference(output), output)
                assert "verified=1" in output and "WRITES 0" in output and digest(referenced) == before
                if grade >= 3:
                    assert catalog == "TOSEC" and field(output, "REFERENCE_NAME") == "Fake Test Disc"
            for checked_image in (image, collision, fallback, long_image, hidden, publication):
                command("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(checked_image))
            print(f"PASS {kind} named: interrupted publication, exact reference grades, read-only verification", flush=True)


if __name__ == "__main__":
    main()
