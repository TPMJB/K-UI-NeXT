#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""End-of-capture reference check, on real captures to FAT32 and exFAT images.

The capture harness's fake disc is deterministic, so its per-track sizes and
CRC32s can be read from a first run and turned into catalogue files that a second
run must then match. That exercises the real path: FatFs reading the catalogues
from the card, the matcher, and the wording in the report."""
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import verify_dump
from test_images import run

BINARY = str(ROOT / "build/capture-image")
HEADER = "DREAMSHELL_REDUMP_CRC_V1\n"


def catalogue(tracks, name="Fake Test Disc", declared=None, only=None, flip=()):
    lines = [HEADER]
    chosen = [(i + 1, t) for i, t in enumerate(tracks) if only is None or (i + 1) in only]
    lines.append(f"G\t{declared or len(chosen)}\t{name}\n")
    for number, t in chosen:
        crc = int(t["crc32"], 16) ^ (1 if number in flip else 0)
        lines.append(f"T\t{number}\t{t['bytes']}\t{crc:08x}\n")
    lines.append("E\n")
    return "".join(lines)


def main():
    with tempfile.TemporaryDirectory(prefix="kui-known-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(clean)) if kind == "fat32" else run("mkfs.exfat", str(clean))
            run(BINARY, str(clean), "seed")
            checker = "fsck.fat" if kind == "fat32" else "fsck.exfat"

            def capture(name, files=(), mode="new", source=None, expected=0):
                """Fresh image (or a copy of `source`), catalogues put on it, one capture."""
                image = base / f"{kind}-{name}.img"
                shutil.copyfile(source or clean, image)
                for card_name, text in files:
                    host = base / f"{kind}-{name}-{card_name}"
                    host.write_text(text)
                    run(BINARY, str(image), "put", str(host), f"0:/KUI/{card_name}")
                return image, run(BINARY, str(image), mode, expected=expected)

            # No catalogue on the card: the capture is unchanged and says so plainly.
            first, output = capture("none")
            assert "SAVED DATA VERIFIED" in output
            assert "No independent reference compared: KUI/redump.db and KUI/tosec.db not on card" in output
            out_dir = base / f"{kind}-original"
            out_dir.mkdir()
            run(BINARY, str(first), "export", str(out_dir))
            tracks = verify_dump.verify(sorted(out_dir.iterdir())[0])
            assert len(tracks) == 6

            # A catalogue holding exactly this disc: a full match, named.
            _, output = capture("full", [("tosec.db", catalogue(tracks))])
            assert "SAVED DATA VERIFIED" in output
            assert "Reference check (TOSEC): FULL TRACK MATCH" in output
            assert "  Fake Test Disc" in output and "No independent reference compared" not in output

            # Both catalogues: the better grade wins. Redump lists one data track
            # (identifies it), TOSEC lists all six (full match), so TOSEC is reported.
            _, output = capture("both", [("redump.db", catalogue(tracks, "Redump Name", only={3})),
                                         ("tosec.db", catalogue(tracks))])
            assert "Reference check (TOSEC): FULL TRACK MATCH" in output and "Fake Test Disc" in output
            # Redump alone, listing one data track: identified, not a full match.
            _, output = capture("redump", [("redump.db", catalogue(tracks, "Redump Name", only={3}))])
            assert "Reference check (Redump): IDENTIFIED BY DATA TRACK" in output and "  Redump Name" in output

            # One audio track differs: the data tracks still match, and it says so.
            _, output = capture("audio", [("tosec.db", catalogue(tracks, flip={2}))])
            assert "Reference check (TOSEC): DATA TRACKS MATCH" in output
            assert "Some tracks differ from the reference" in output
            # A data track differs: only a partial match.
            _, output = capture("data", [("tosec.db", catalogue(tracks, flip={3}))])
            assert "Reference check (TOSEC): PARTIAL MATCH ONLY" in output
            # Nothing matches: inconclusive, and never a failure.
            _, output = capture("nomatch", [("tosec.db", catalogue(tracks, flip={1, 3, 6}))])
            assert "NO CATALOGUE MATCH (INCONCLUSIVE)" in output and "Inconclusive" in output
            assert "SAVED DATA VERIFIED" in output   # the capture itself still succeeded

            # A catalogue that cannot be read must not change the outcome either.
            for label, text in (("badheader", "NOT A CATALOGUE\n" + catalogue(tracks)),
                                ("empty", ""), ("truncated", catalogue(tracks)[:60])):
                image, output = capture(label, [("tosec.db", text)])
                assert "SAVED DATA VERIFIED" in output, label
                assert "FULL TRACK MATCH" not in output, label
                run(checker, "-n", str(image))

            # Verify mode (a read-back of the saved files) gets the same check.
            done, _ = capture("done", [("tosec.db", catalogue(tracks))])
            image = base / f"{kind}-verified.img"
            shutil.copyfile(done, image)
            output = run(BINARY, str(image), "verify")
            assert "SAVED DATA VERIFIED" in output and "FULL TRACK MATCH" in output
            run(checker, "-n", str(image))
            print(f"PASS {kind} known dumps: none, full, both, redump, audio/data/no-match, unreadable, verify mode", flush=True)


if __name__ == "__main__":
    main()
