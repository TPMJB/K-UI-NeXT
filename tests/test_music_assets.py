#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]

class MusicAssets(unittest.TestCase):
    def test_original_generator_and_recordings(self):
        music = ROOT / 'resources/music'
        manifest = json.loads((music / 'manifest.json').read_text())
        source = (music / 'original_generator.py').read_bytes()
        self.assertEqual(hashlib.sha256(source).hexdigest(), manifest['source_sha256'])
        blob = b'blob ' + str(len(source)).encode() + b'\0' + source
        self.assertEqual(hashlib.sha1(blob).hexdigest(), manifest['source_git_blob'])
        self.assertEqual(len(manifest['tracks']), 5)
        with tempfile.TemporaryDirectory(prefix='kui-original-music-') as generated:
            subprocess.run([sys.executable, str(ROOT / 'tools/generate_menu_music.py'),
                            '--directory', generated], check=True, capture_output=True, text=True)
            for track in manifest['tracks']:
                with self.subTest(track=track['file']):
                    path = Path(generated) / track['file']
                    data = path.read_bytes()
                    self.assertEqual(len(data), track['bytes'])
                    self.assertLessEqual(len(data), 2 * 1024 * 1024)
                    self.assertEqual(hashlib.sha256(data).hexdigest(), track['sha256'])
                    with wave.open(str(path), 'rb') as wav:
                        self.assertEqual(wav.getnchannels(), track['channels'])
                        self.assertEqual(wav.getsampwidth() * 8, track['sample_bits'])
                        self.assertEqual(wav.getframerate(), track['sample_rate'])
                        self.assertEqual(wav.getnframes(), track['frames'])
                        pcm = wav.readframes(wav.getnframes())
                        self.assertTrue(any(pcm))
                        self.assertEqual(len(pcm), track['frames'] * track['channels'] * 2)

if __name__ == '__main__':
    unittest.main()
