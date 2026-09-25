#!/usr/bin/env python3
"""Generate the independently authored original K-UI PCM loops.

--ogg also encodes each loop as the Ogg Vorbis file that ships on the card.
"""
# SPDX-License-Identifier: GPL-3.0-only
import argparse
import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
# Pinned encoder settings for the committed resources/music/*.ogg. Bitexact
# output omits version strings and uses a fixed stream serial, so the same
# FFmpeg/libvorbis build reproduces the recorded bytes.
OGG_QUALITY = 5
OGG_OPTIONS = ("-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
               "-c:a", "libvorbis", "-q:a", str(OGG_QUALITY))


def encode_ogg(wav, ogg):
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("--ogg requires ffmpeg built with libvorbis")
    subprocess.run([ffmpeg, "-v", "error", "-y", "-i", str(wav), *OGG_OPTIONS, str(ogg)], check=True)
    data = ogg.read_bytes()
    print(f"{ogg}: Ogg Vorbis q{OGG_QUALITY}, {len(data):,} bytes, sha256 {hashlib.sha256(data).hexdigest()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=ROOT / 'resources/music')
    parser.add_argument('--ogg', action='store_true',
                        help='also write the shipped .ogg beside each WAV (needs ffmpeg/libvorbis)')
    args = parser.parse_args()
    source = ROOT / 'resources/music/original_generator.py'
    spec = importlib.util.spec_from_file_location('original_kui_music', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for index, name in enumerate(module.TRACKS):
        pcm = module.compose() if index == 0 else module.synthwave(index - 1)
        module.write_track(args.directory / name, pcm)
        if args.ogg:
            encode_ogg(args.directory / name, (args.directory / name).with_suffix('.ogg'))

if __name__ == '__main__':
    main()
