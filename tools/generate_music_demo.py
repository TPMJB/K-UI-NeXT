#!/usr/bin/env python3
"""Synthesize Harbor Lights, an original one-minute K-UI playback test song.

No recordings, sound fonts, samples or third-party musical material are inputs.
The generator is GPL-3.0-only. The generated composition and recording may be
used, modified and redistributed without additional restrictions to the extent
rights exist in that generated material, as with K-UI's original menu music.
"""
# SPDX-License-Identifier: GPL-3.0-only
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import wave

ROOT = Path(__file__).resolve().parents[1]
RATE = 22050
SECONDS = 60
FRAMES = RATE * SECONDS
TITLE = "Harbor Lights"
FILENAME = "harbor-lights.wav"
TAU = 2.0 * math.pi


def frequency(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def compose():
    mix = array("d", [0.0]) * FRAMES
    beat = 60.0 / 96.0

    def voice(start, duration, note, level, kind):
        first = round(start * RATE)
        length = round(duration * RATE)
        freq = frequency(note)
        for i in range(min(length, FRAMES - first)):
            t = i / RATE
            phase = TAU * freq * t
            if kind == "pad":
                envelope = min(t / 0.18, 1.0, (duration - t) / 0.32)
                signal = (math.sin(phase) + 0.25 * math.sin(phase * 1.003)
                          + 0.08 * math.sin(phase * 2.0))
            elif kind == "bass":
                envelope = min(t / 0.015, 1.0, (duration - t) / 0.045) * math.exp(-t * 2.0)
                signal = math.sin(phase) + 0.22 * math.sin(phase * 2.0)
            else:
                envelope = min(t / 0.008, 1.0, (duration - t) / 0.07) * math.exp(-t * 5.5)
                signal = math.sin(phase) + 0.3 * math.sin(phase * 2.0) + 0.1 * math.sin(phase * 3.0)
            mix[first + i] += level * envelope * signal

    # 24 bars at 96 BPM = exactly one minute. Cmaj7, Am7, Fmaj7 and G6,
    # arranged as a quiet intro, warm arpeggio section and a gentle outro.
    chords = ((48, 52, 55, 59), (45, 48, 52, 55),
              (41, 45, 48, 52), (43, 47, 50, 52))
    arp = (0, 2, 1, 3, 2, 1, 3, 2)
    for bar in range(24):
        start = bar * 4.0 * beat
        notes = chords[bar % len(chords)]
        for note in notes:
            voice(start, 4.0 * beat, note + 12, 0.058, "pad")
        for step in range(4):
            voice(start + step * beat, beat * 0.9, notes[0] - 12,
                  0.19 if bar >= 4 else 0.10, "bass")
        for step, chord_index in enumerate(arp):
            note = notes[chord_index] + 24
            at = start + step * beat / 2.0
            level = 0.10 if 4 <= bar < 20 else 0.065
            voice(at, beat * 0.72, note, level, "bell")
            if bar >= 4:
                voice(at + 0.75 * beat, beat * 0.56, note, level * 0.27, "bell")

    # Subtle synthesized kick and brushed noise. The fixed local LCG makes the
    # noise reproducible without depending on Python's global random state.
    noise_state = 0x4B55494E
    for quarter in range(16, 80):
        first = round(quarter * beat * RATE)
        for i in range(round(0.19 * RATE)):
            t = i / RATE
            phase = TAU * (48.0 * t + 3.0 * (1.0 - math.exp(-24.0 * t)))
            mix[first + i] += 0.20 * math.sin(phase) * math.exp(-21.0 * t)
        first = round((quarter + 0.5) * beat * RATE)
        previous = 0.0
        for i in range(round(0.065 * RATE)):
            noise_state = (1664525 * noise_state + 1013904223) & 0xffffffff
            noise = ((noise_state >> 8) / 8388608.0) - 1.0
            high = noise - previous
            previous = noise
            mix[first + i] += 0.014 * high * math.exp(-i / (RATE * 0.018))

    peak = max(abs(value) for value in mix)
    gain = 23000.0 / peak
    pcm = bytearray(FRAMES * 2)
    for i, value in enumerate(mix):
        fade = min(i / (RATE * 0.3), (FRAMES - 1 - i) / (RATE * 1.5), 1.0)
        struct.pack_into("<h", pcm, i * 2, round(value * gain * fade))
    return pcm


def write_demo(directory):
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / FILENAME
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, RATE, FRAMES, "NONE", "not compressed"))
        output.writeframes(compose())
    data = path.read_bytes()
    manifest = {
        "title": TITLE,
        "file": FILENAME,
        "generator": "tools/generate_music_demo.py",
        "generator_license": "GPL-3.0-only",
        "provenance": "Original synthesis for TPMJB/K-UI-NeXT; no external music or samples.",
        "audio_permissions": "Use, modify and redistribute without additional restrictions to the extent rights exist in the generated material.",
        "format": "PCM16 little-endian WAV",
        "channels": 1,
        "sample_rate": RATE,
        "frames": FRAMES,
        "duration_seconds": SECONDS,
        "pcm_bytes": FRAMES * 2,
        "file_bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }
    (directory / "harbor-lights.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "build/music-demo")
    parser.add_argument("--ogg", action="store_true",
                        help="also make an optional PC-listening Ogg with ffmpeg; not a console format")
    args = parser.parse_args()
    path = write_demo(args.directory)
    if args.ogg:
        ffmpeg = shutil.which("ffmpeg")
        if not ffmpeg:
            raise SystemExit("WAV generated; --ogg additionally requires ffmpeg/libvorbis")
        subprocess.run([ffmpeg, "-v", "error", "-y", "-i", str(path),
                        "-map_metadata", "-1", "-c:a", "libvorbis", "-q:a", "3",
                        str(path.with_suffix(".ogg"))], check=True)
    print(f"{path}: {SECONDS}s, mono PCM16/{RATE} Hz, {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
