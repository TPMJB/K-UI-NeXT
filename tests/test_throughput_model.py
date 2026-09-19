#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/throughput_model.py must reproduce the measurement it is built from and
never contradict basic arithmetic, or its predictions cannot be trusted."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import throughput_model as model  # noqa: E402


class ThroughputModelTests(unittest.TestCase):
    def test_reproduces_todays_capture(self):
        # Today's captures ran at 369-378 KiB/s whatever the split, because the
        # measured stage rates already include the UI's share.
        for share in (0.0, 0.2, 0.28, 0.35):
            for opt_cpu in (0.1, 0.35, 0.6):
                rate = model.predict(share, opt_cpu, ui_quiet=False)
                self.assertTrue(360 < rate < 385, (share, opt_cpu, rate))

    def test_quieting_the_ui_helps_exactly_as_much_as_it_cost(self):
        for opt_cpu in (0.1, 0.35, 0.6):
            self.assertAlmostEqual(model.predict(0.0, opt_cpu, ui_quiet=True),
                                   model.predict(0.0, opt_cpu, ui_quiet=False), places=6)
            gains = [model.predict(u, opt_cpu, ui_quiet=True) / model.predict(u, opt_cpu, ui_quiet=False)
                     for u in (0.1, 0.2, 0.3)]
            self.assertEqual(gains, sorted(gains))         # a busier UI has more to give back
            self.assertTrue(all(g >= 1.0 for g in gains))

    def test_each_change_only_helps(self):
        for share in (0.0, 0.28):
            base = model.predict(share, 0.35, ui_quiet=True)
            self.assertGreater(model.predict(share, 0.35, ui_quiet=True, chunk128=True), base)
            self.assertGreater(model.predict(share, 0.35, ui_quiet=True, sha_in_loop=False), base)
            serial = model.predict(share, 0.35, ui_quiet=True, chunk128=True, sha_in_loop=False)
            thread = model.predict(share, 0.35, ui_quiet=True, chunk128=True, sha_in_loop=False, overlap="pio")
            dma = model.predict(share, 0.35, ui_quiet=True, chunk128=True, sha_in_loop=False, overlap="dma")
            self.assertLess(serial, thread)
            self.assertLessEqual(thread, dma)               # DMA also hides the transfer

    def test_audio_regime_is_drive_bound(self):
        # An audio track reads at ~134 KiB/s with almost no CPU in the read
        # (poll time 4% of command time in the evidence). Overlap can add at
        # most the CPU stages' share, and quieting the UI barely matters.
        opt_cpu = 0.05
        audio = dict(model.MEASURED, optical=134.0)
        today = model.predict(0.28, opt_cpu, audio, ui_quiet=False)
        quiet = model.predict(0.28, opt_cpu, audio, ui_quiet=True)
        best = model.predict(0.28, opt_cpu, audio, ui_quiet=True, chunk128=True,
                             sha_in_loop=False, overlap="pio")
        self.assertLess(quiet / today, 1.10)                # the UI barely matters here
        # Nothing beats the drive itself: the measured rate with its CPU share
        # taken out is the ceiling, however much else is hidden or removed.
        self.assertLessEqual(best, 134.0 / (1 - opt_cpu) + 1e-9)
        self.assertLess(best / today, 1.5)

    def test_unknown_overlap_is_rejected(self):
        with self.assertRaises(ValueError):
            model.predict(0.0, 0.35, overlap="threads")


if __name__ == "__main__":
    unittest.main()
