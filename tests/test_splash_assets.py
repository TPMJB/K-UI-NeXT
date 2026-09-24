# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import importlib.util
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("build_splash",ROOT/"tools/build_splash.py")
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class StartupAssets(unittest.TestCase):
    def test_dainsleif_asset_and_bounds(self):
        data=(ROOT/"resources/branding/startup.png").read_bytes()
        self.assertEqual(hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest(),module.PNG_BLOB)
        pixels=module.decode_png(data)
        self.assertEqual(len(pixels),640*480)
        self.assertTrue(all(0<=v<=65535 for v in pixels))
        bad=bytearray(data);bad[len(bad)//2]^=1
        with self.assertRaises(ValueError):module.decode_png(bad)
        with self.assertRaises(ValueError):module.decode_png(data[:32])
    def test_shortened_original_chime(self):
        samples=list(module.startup_samples())
        self.assertEqual(len(samples),116865)
        self.assertLessEqual(len(samples)/44100,2.65)
        self.assertEqual(samples[0],0)
        self.assertEqual(samples[-1],0)
        self.assertLessEqual(max(map(abs,samples)),26000)
        self.assertTrue(any(samples[10000:20000]))
        # Fade reaches silence before the deadline, without the old long tail.
        self.assertTrue(any(samples[2*44100:]))
        self.assertFalse(any(samples[round(2.62*44100):]))
if __name__=="__main__":
    unittest.main()
