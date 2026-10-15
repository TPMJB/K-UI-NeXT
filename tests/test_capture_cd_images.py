#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FatFs CD capture, independent raw-sector fixtures and acquisition policy."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import verify_dump
from make_recovery_vectors import sector, edc

BINARY = str(ROOT / "build/capture-image")
MANIFEST = str(ROOT / "build/test-recovery-manifest")
LAYOUTS = {
    "cd-data": ((4, 150, 790),),
    "cd-audio": ((0, 150, 470), (0, 470, 790)),
    "cd-mixed": ((4, 150, 390), (0, 390, 550), (4, 550, 790)),
    "cd-offset": ((4, 225, 865),),
}


def run(image, *args, layout="cd-data", disc=None, opts="cue", expected=0, selected=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith("KUI_TEST_")}
    env.update(KUI_TEST_LAYOUT=layout, KUI_TEST_OPTS=opts)
    if disc is not None:
        env["KUI_TEST_DISC_FILE"] = str(disc)
    if selected is not None:
        env["KUI_TEST_SELECTED_JOB"] = selected
    result = subprocess.run([BINARY, str(image), *args], env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert result.returncode == expected, (args, layout, opts, result.returncode, result.stdout[-5000:])
    return result.stdout


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def export(image, destination, **kwargs):
    destination.mkdir()
    run(image, "export", str(destination), **kwargs)
    folders = list(destination.iterdir())
    assert len(folders) == 1, folders
    return folders[0]


def fixture(path, layout, mode=1):
    output = bytearray()
    for control, first, end in LAYOUTS[layout]:
        for fad in range(first, end):
            if control:
                data = sector(fad, fad & 255)
                if mode == 2:
                    data[15] = 2
                    data[16:24] = bytes(8)
                    data[2072:2076] = edc(data[16:2072]).to_bytes(4, "little")
            else:
                # Raw drive-order PCM bytes: no sync, EDC, byte swapping, or
                # manufactured IP metadata. Every sector is independently known.
                data = bytes((fad * 11 + i * 17) & 255 for i in range(2352))
            output.extend(data)
    path.write_bytes(output)
    return bytes(output)


def validate(folder, layout, source):
    m = json.loads((folder / "manifest.json").read_text())
    assert m["profile"] == verify_dump.CD_PROFILE and m["output_format"] == "bin_cue"
    assert m["title"] == {"cd-data": "Data CD", "cd-offset": "Data CD",
                           "cd-audio": "Audio CD", "cd-mixed": "Mixed Mode CD"}[layout]
    assert m["gdi_file"] == ".capture.gdi" and not (folder / "disc.gdi").exists()
    assert len(m["tracks"]) == len(LAYOUTS[layout])
    identity = hashlib.sha256(verify_dump.CD_PROFILE.encode())
    identity.update(struct.pack("<I", len(m["tracks"])))
    cursor = 0
    for number, (t, (control, first, end)) in enumerate(zip(m["tracks"], LAYOUTS[layout]), 1):
        size = (end - first) * 2352
        assert t["number"] == number and t["control"] == control and t["session"] == 0
        assert (t["start_fad"], t["end_fad"], t["toc_end_fad"], t["excluded_tail_sectors"]) == (first, end, end, 0)
        assert (folder / t["file"]).read_bytes() == source[cursor:cursor+size]
        identity.update(struct.pack("<6I", number, control, 0, first, end, end))
        for point in (first, first+(end-first-1)//2, end-1):
            identity.update(struct.pack("<I", point))
            offset = cursor+(point-first)*2352
            identity.update(source[offset:offset+2352])
        cursor += size
    assert cursor == len(source) and identity.hexdigest() == m["identity"]
    results = verify_dump.verify(folder)
    assert verify_dump.compare_reference(results, m, verify_dump.CD_PROFILE) == (True, len(m["tracks"]))
    try:
        verify_dump.compare_reference(results, m)
    except ValueError:
        pass
    else:
        raise AssertionError("GD and CD reference domains must not alias")
    cue = (folder / m["cue_file"]).read_text()
    assert "DENSITY" not in cue and "    INDEX 00" not in cue
    assert cue.count("PREGAP") == (1 if layout == "cd-offset" else 0)
    if layout == "cd-offset":
        assert "PREGAP 00:01:00" in cue
    checked = subprocess.run([MANIFEST, str(folder / "manifest.json")], text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert checked.returncode == 0, checked.stdout
    return results


def main():
    with tempfile.TemporaryDirectory(prefix="kui-cd-capture-") as temporary:
        base = Path(temporary)
        fixtures = {}
        for layout in LAYOUTS:
            path = base / (layout + ".raw")
            fixtures[layout] = (path, fixture(path, layout))
        good, source = fixtures["cd-data"]
        variants = {}
        # The acquisition oracle is independent polynomial-division parity.
        # All these corruptions lie outside EDC coverage or carry a fully
        # recomputed valid sector with the wrong absolute address.
        for name, offset in (("pq", 2076),):
            data = bytearray(source);data[100 * 2352 + offset] ^= 1
            path = base / (name + ".raw");path.write_bytes(data);variants[name] = path
        data = bytearray(source);data[100 * 2352:101 * 2352] = sector(250, 250, reserved=1)
        variants["reserved"] = base / "reserved.raw";variants["reserved"].write_bytes(data)
        data = bytearray(source);data[100 * 2352:101 * 2352] = sector(251, 250)
        variants["address"] = base / "address.raw";variants["address"].write_bytes(data)
        data = bytearray(source);data[:2352] = sector(150, 151)
        variants["other"] = base / "other.raw";variants["other"].write_bytes(data)
        mode2 = base / "mode2.raw";fixture(mode2, "cd-data", 2)
        for filesystem in ("fat32", "exfat"):
            clean = base / f"{filesystem}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            subprocess.run(["mkfs.fat", "-F", "32", str(clean)] if filesystem == "fat32" else
                           ["mkfs.exfat", str(clean)], check=True, stdout=subprocess.PIPE)
            run(clean, "seed")
            number = 0

            def fresh():
                nonlocal number
                number += 1
                image = base / f"{filesystem}-{number}.img"
                shutil.copyfile(clean, image)
                return image

            for layout, (disc, raw) in fixtures.items():
                image = fresh()
                output = run(image, "new", layout=layout, disc=disc, opts="cue,strict,dma")
                assert "SAVED DATA VERIFIED" in output
                folder = export(image, base / f"{filesystem}-{layout}", layout=layout)
                expected = validate(folder, layout, raw)
                before = digest(image)
                output = run(image, "verify", layout=layout, disc=disc, opts="strict")
                assert "WRITES 0" in output and digest(image) == before
                # A controlled stop retains its verified prefix and resumes to
                # the same complete raw bytes and hashes as an uninterrupted job.
                stopped = fresh()
                run(stopped, "new", "stop-early", layout=layout, disc=disc, expected=3)
                run(stopped, "mutate", "checkpoint", layout=layout)
                run(stopped, "resume", layout=layout, disc=disc, opts="cso")
                resumed = export(stopped, base / f"{filesystem}-{layout}-resumed", layout=layout)
                assert validate(resumed, layout, raw) == expected
            print(f"PASS {filesystem}: strict CD data/audio/mixed/offset, complete TOC bytes, CUE, checkpoint fallback/resume, read-only Verify", flush=True)

            complete = fresh();run(complete, "new", disc=good)
            directory = export(complete, base / f"{filesystem}-identity").name
            selected = "/KUI/dumps/" + directory
            before = digest(complete)
            output = run(complete, "resume", disc=variants["other"], selected=selected, expected=1)
            assert "WRITES 0" in output and digest(complete) == before
            for options, fault in (("cue,retry=2", "none"), ("", "none"), ("cso", "none"),
                                   ("chd", "none"), ("cue", "cancel-before")):
                image = fresh();before = digest(image)
                output = run(image, "new", fault, opts=options, disc=good, expected=3 if fault == "cancel-before" else 1)
                assert "WRITES 0" in output and digest(image) == before
                if fault == "none":
                    assert "OPTICAL_READS 0" in output
            print(f"PASS {filesystem}: exact CD job identity, invalid options/format and pre-cancel preserve card", flush=True)

            for limit in (0, 1, 5, 10, 20, 50):
                image = fresh()
                output = run(image, "new", "disc-fail", disc=good, opts=f"cue,retry={limit}", expected=1)
                assert f"BAD_ATTEMPTS {1 + (limit or 10)} " in output, output
                partial = export(image, base / f"{filesystem}-retry-{limit}")
                assert not (partial / "manifest.json").exists()
                run(image, "resume", disc=good)
                restored = export(image, base / f"{filesystem}-retry-{limit}-resumed")
                validate(restored, "cd-data", source)
            print(f"PASS {filesystem}: retry limits 1/5/10/20/50 and historical default, failed read never publishes completion, resume equality", flush=True)

            for name, disc in variants.items():
                if name == "other":
                    continue
                image = fresh()
                output = run(image, "new", disc=disc, opts="cue,strict,retry=1" + (",dma" if name == "pq" else ""), expected=1)
                assert "Strict sync/address/EDC/P/Q" in output
                partial = export(image, base / f"{filesystem}-strict-{name}")
                raw = (partial / "track01.bin").read_bytes()
                assert len(raw) <= 100*2352 and raw == source[:len(raw)]
                assert not (partial / "manifest.json").exists()
                run(image, "resume", disc=good, opts="cue,strict")
                restored = export(image, base / f"{filesystem}-strict-{name}-resumed")
                validate(restored, "cd-data", source)
            # Default acquisition remains EDC-only; enabling strict does not
            # silently change existing output or make audio parity claims.
            image = fresh();run(image, "new", disc=variants["pq"])
            validate(export(image, base / f"{filesystem}-normal-pq"), "cd-data", variants["pq"].read_bytes())
            image = fresh()
            output = run(image, "new", disc=mode2, opts="cue,strict", expected=1)
            assert "supports Mode1 data only; Mode2 refused" in output and "WRITES 0" in output
            run(image, "new", disc=mode2)
            validate(export(image, base / f"{filesystem}-mode2"), "cd-data", mode2.read_bytes())
            subprocess.run(["fsck.fat" if filesystem == "fat32" else "fsck.exfat", "-n", str(image)],
                           check=True, stdout=subprocess.PIPE)
            print(f"PASS {filesystem}: strict independent P/Q/reserved/address failures do not commit bad bytes; audio unaffected; Mode2 explicit strict refusal/default acceptance", flush=True)


if __name__ == "__main__":
    main()
