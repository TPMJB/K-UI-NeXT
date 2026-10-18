#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain complete embedded notices for the independently pinned CHD codecs."""
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "third_party/libchdr"
NOTICE = re.compile(r"copyright|redistribution|permission is hereby|unencumbered|"
                    r"public domain|licen[cs]e|spdx", re.IGNORECASE)


def write_chd_codec_notices(directory):
    """Write explicit licenses and verbatim C/header notice blocks to directory.

    Sources include the C decoder, all bundled C dependencies and their headers.
    This deliberately retains complete alternative-license blocks (including
    dr_flac's final MIT-0 text), rather than extracting a copyright line alone.
    Duplicate comment text is written once with all source paths identified.
    """
    output = Path(directory) / "CHD"
    output.mkdir(parents=True, exist_ok=True)
    for source, name in (
        (ROOT / "third_party/chd-codecs.md", "chd-codecs.md"),
        (VENDOR / "LICENSE.txt", "libchdr-LICENSE.txt"),
        (VENDOR / "deps/lzma-26.02/LICENSE", "LZMA-LICENSE.txt"),
        (VENDOR / "deps/zstd-1.5.7/LICENSE", "Zstandard-LICENSE.txt"),
    ):
        shutil.copyfile(source, output / name)
    blocks = {}
    for source in sorted(VENDOR.rglob("*")):
        if source.suffix not in (".c", ".h") or source.name == "kui_config.h":
            continue
        text = source.read_text(encoding="utf-8")
        for block in re.findall(r"/\*[\s\S]*?\*/", text):
            if NOTICE.search(block):
                blocks.setdefault(block, []).append(source.relative_to(ROOT).as_posix())
        # Some public-domain SDK notices use line comments instead of C blocks.
        for block in re.findall(r"(?:^[ \t]*//[^\n]*\n)+", text, re.MULTILINE):
            if NOTICE.search(block):
                blocks.setdefault(block.rstrip(), []).append(source.relative_to(ROOT).as_posix())
    content = "\n\n".join("Sources: " + ", ".join(dict.fromkeys(paths)) + "\n" + block
                           for block, paths in blocks.items()) + "\n"
    for required in ("Aaron Giles", "David Reid", "Martin Raiber", "Meta Platforms",
                     "MIT No Attribution", "free and unencumbered"):
        if required not in content:
            raise ValueError("Incomplete CHD dependency notices: " + required)
    (output / "UPSTREAM-NOTICES.txt").write_text(content, encoding="utf-8")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    write_chd_codec_notices(parser.parse_args().directory)
