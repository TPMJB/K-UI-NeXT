# SPDX-License-Identifier: GPL-3.0-only
"""tools/rip_time.py: the projections must come from the report, and from the right pass.

The first version keyed `BENCH capture total` by settings alone. That line carries no uihz=, so
the totals of every UI pass were averaged together and the tool reported the same finished time
for a full-speed pass and a 2 Hz one, and a finished time that differed from the capture time even
where the end read-back was off (where they are the same run)."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools" / "rip_time.py"
sys.path.insert(0, str(ROOT / "tools"))
import rip_time as r

# 1000 sectors of plan = 2,352,000 bytes; at 1000 KiB/s that is 2352000/1024/1000 = 2.297 s.
REPORT = """T01 FAD [150,1150) data; gap excluded=0
BENCH pass 1/2 uihz=full
BENCH capture uihz=full hash=both end=on sample=0 sectors=256 result=ok bytes=602112 us=1000000 kib_s=100.0
BENCH capture total hash=both end=on sample=0 verify_us=1000000 total_kib_s=50.0 sampled=0 verified=1
BENCH capture uihz=full hash=crc32 end=off sample=0 sectors=256 result=ok bytes=602112 us=500000 kib_s=200.0
BENCH capture total hash=crc32 end=off sample=0 verify_us=0 total_kib_s=200.0 sampled=0 verified=0
BENCH pass 2/2 uihz=2
BENCH capture uihz=2 hash=both end=on sample=0 sectors=256 result=ok bytes=602112 us=250000 kib_s=400.0
BENCH capture total hash=both end=on sample=0 verify_us=250000 total_kib_s=200.0 sampled=0 verified=1
BENCH capture uihz=2 hash=crc32 end=off sample=0 sectors=256 result=ok bytes=602112 us=125000 kib_s=800.0
BENCH capture total hash=crc32 end=off sample=0 verify_us=0 total_kib_s=800.0 sampled=0 verified=0
"""


class RipTime(unittest.TestCase):
    def test_disc_size_comes_from_the_plan_then_the_toc(self):
        size, where = r.disc_bytes("T01 FAD [150,1150) data; gap excluded=0")
        self.assertEqual(size, 1000 * 2352)
        self.assertIn("plan", where)
        size, where = r.disc_bytes("  T01 ctrl=4 start=150 next=1150\n  T01 ctrl=4 start=150 next=1150")
        self.assertEqual(size, 1000 * 2352, "a TOC repeated by a second run must not be counted twice")
        self.assertIn("TOC", where)
        self.assertEqual(r.disc_bytes("nothing here"), (None, None))

    def test_each_total_belongs_to_its_own_pass(self):
        rates = r.settings_rates(REPORT)
        self.assertEqual(rates[("full", "both", "on", "0")], (100.0, 50.0))
        self.assertEqual(rates[("2", "both", "on", "0")], (400.0, 200.0))
        # Where the read-back is off, the finished rate IS the capture rate.
        self.assertEqual(rates[("full", "crc32", "off", "0")], (200.0, 200.0))
        self.assertEqual(rates[("2", "crc32", "off", "0")], (800.0, 800.0))

    def test_repeats_of_one_setting_average(self):
        doubled = REPORT + REPORT.split("BENCH pass 2/2", 1)[0].split("\n", 1)[1]
        rates = r.settings_rates(doubled)
        self.assertAlmostEqual(rates[("full", "both", "on", "0")][0], 100.0)

    def test_minutes_and_the_whole_tool_on_a_synthetic_report(self):
        self.assertAlmostEqual(r.minutes(1000 * 2352, 1000.0), 2352000 / 1024 / 1000 / 60)
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "report.txt"
            path.write_text(REPORT)
            out = subprocess.run([sys.executable, str(TOOL), str(path)], capture_output=True, text=True, check=True).stdout
        self.assertIn("from the capture plan", out)
        # 2,352,000 bytes at 800 KiB/s = 0.05 min; at 100 KiB/s = 0.4 min.
        self.assertRegex(out, r"2\s+crc32\s+off\s+0\s+800\.0\s+0\.0 min")
        self.assertRegex(out, r"full\s+both\s+on\s+0\s+100\.0\s+0\.4 min")

    def test_a_wrapped_log_is_rejoined(self):
        line = "BENCH capture uihz=2 hash=crc32 end=off sample=0 sectors=256 result=ok bytes=602112 us=125000 kib_s=800.0"
        wrapped = "\n".join(line[i:i + 76] for i in range(0, len(line), 76))
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "wrapped.txt"
            path.write_text(wrapped + "\n")
            self.assertIn(line, r.read([str(path)]))


if __name__ == "__main__":
    unittest.main()
