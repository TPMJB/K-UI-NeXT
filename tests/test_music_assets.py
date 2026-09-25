#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
MUSIC = ROOT / 'resources/music'
CHECKER = ROOT / 'build/music-asset-check'
ORIGINAL = 'resources/music/original_generator.py'
# Harbor Lights joined the rotation after 1.5 from K-UI NeXT's own generator.
DEMO = 'tools/generate_music_demo.py'

class MusicAssets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((MUSIC / 'manifest.json').read_text())
        cls.generated = tempfile.TemporaryDirectory(prefix='kui-original-music-')
        subprocess.run([sys.executable, str(ROOT / 'tools/generate_menu_music.py'),
                        '--directory', cls.generated.name], check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.generated.cleanup()

    def test_original_generator_and_recordings(self):
        manifest = self.manifest
        source = (MUSIC / 'original_generator.py').read_bytes()
        self.assertEqual(hashlib.sha256(source).hexdigest(), manifest['source_sha256'])
        blob = b'blob ' + str(len(source)).encode() + b'\0' + source
        self.assertEqual(hashlib.sha1(blob).hexdigest(), manifest['source_git_blob'])
        self.assertEqual([track['generator'] for track in manifest['tracks']], [ORIGINAL] * 5 + [DEMO])
        for track in manifest['tracks']:
            with self.subTest(track=track['file']):
                path = Path(self.generated.name) / track['file']
                data = path.read_bytes()
                self.assertEqual(len(data), track['bytes'])
                if track['generator'] == ORIGINAL:
                    # 1.5 cards may still hold these WAVs as bundled fallbacks.
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

    def test_shipped_ogg_encodings(self):
        """The card's Oggs are the recorded bytes and decode to these WAVs."""
        tracks = self.manifest['tracks']
        pairs = []
        for track in tracks:
            with self.subTest(track=track['ogg']['file']):
                data = (MUSIC / track['ogg']['file']).read_bytes()
                self.assertEqual(len(data), track['ogg']['bytes'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), track['ogg']['sha256'])
                self.assertLessEqual(len(data), 2 * 1024 * 1024)
                self.assertEqual(Path(track['ogg']['file']).stem, Path(track['file']).stem)
                pairs += [str(MUSIC / track['ogg']['file']), str(Path(self.generated.name) / track['file'])]
        # The runtime requests these names, slot by slot, and falls back to the
        # 1.5 WAVs; Harbor Lights has none (NULL).
        source = (ROOT / 'src/apps/music.c').read_text()
        def names(array):
            body = re.search(r'\b%s\[KUI_MUSIC_TRACKS\]=\{(.*?)\};' % array, source, re.S)
            return [name or None for name in re.findall(r'"([^"]+)"|\bNULL\b', body.group(1))]
        self.assertEqual(names('files'), [track['ogg']['file'] for track in tracks])
        self.assertEqual(names('legacy_files'),
                         [track['file'] if track['generator'] == ORIGINAL else None for track in tracks])
        self.assertIn('#define KUI_MUSIC_TRACKS %du' % len(tracks), (ROOT / 'include/kui/music.h').read_text())
        self.assertTrue(CHECKER.exists(), 'build/music-asset-check is missing; run make test')
        result = subprocess.run([str(CHECKER), *pairs], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

if __name__ == '__main__':
    unittest.main()
