#!/usr/bin/env python3
"""Check the supplied long-song fixture through the independent WAV reader."""
# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("music_demo", ROOT / "tools/generate_music_demo.py")
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)

with tempfile.TemporaryDirectory() as temporary:
    path = demo.write_demo(Path(temporary))
    # A real, audible PCM file that crosses the old 2-MiB whole-file cache
    # boundary, fits the new cache, and keeps playing after crossing it.
    assert 2 * 1024 * 1024 < path.stat().st_size < 6 * 1024 * 1024
    with wave.open(str(path), "rb") as stream:
        assert stream.getcomptype() == "NONE"
        assert (stream.getnchannels(), stream.getsampwidth(), stream.getframerate()) == (1, 2, 22050)
        assert stream.getnframes() == 60 * 22050
        pcm = stream.readframes(stream.getnframes())
        assert not stream.readframes(1)
    samples = struct.unpack("<" + "h" * (len(pcm) // 2), pcm)
    assert samples[0] == samples[-1] == 0
    assert 10000 <= max(abs(value) for value in samples) < 32767
    for second in (1, 25, 50, 58):
        window = samples[second * 22050:(second + 1) * 22050]
        assert sum(value * value for value in window) / len(window) > 1000 * 1000
    manifest = json.loads(path.with_suffix(".json").read_text())
    assert manifest["duration_seconds"] == 60 and manifest["pcm_bytes"] == len(pcm)
print("PASS music demo: one-minute audible PCM WAV, >2 MiB, bounded peak and audible tail")
