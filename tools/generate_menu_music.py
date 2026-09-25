#!/usr/bin/env python3
"""Generate the menu rotation's PCM recordings.

These are the five independently authored original K-UI loops and Harbor
Lights, K-UI NeXT's own one-minute piece. --ogg also encodes each recording as
the Ogg Vorbis file that ships on the card.
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


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=ROOT / 'resources/music')
    parser.add_argument('--ogg', action='store_true',
                        help='also write the shipped .ogg beside each WAV (needs ffmpeg/libvorbis)')
    args = parser.parse_args()
    module = load('original_kui_music', ROOT / 'resources/music/original_generator.py')
    demo = load('kui_music_demo', ROOT / 'tools/generate_music_demo.py')
    paths = []
    for index, name in enumerate(module.TRACKS):
        pcm = module.compose() if index == 0 else module.synthwave(index - 1)
        module.write_track(args.directory / name, pcm)
        paths.append(args.directory / name)
    # Harbor Lights joined the rotation after 1.5, as the sixth song.
    paths.append(demo.write_wav(args.directory / demo.FILENAME))
    print(f'{paths[-1]}: {demo.SECONDS}s, mono PCM16, {demo.RATE}Hz, {paths[-1].stat().st_size:,} bytes')
    if args.ogg:
        for path in paths:
            encode_ogg(path, path.with_suffix('.ogg'))

if __name__ == '__main__':
    main()
