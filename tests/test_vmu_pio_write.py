#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute independently authored SH PIO adapter bytes and harmful mutations."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_vmu_pio_write import check_pio_entry

BASE = 0x8cfd0000
ADAPTER = BASE + 64
WRITER = BASE + 128


def fixture():
    payload = bytearray(160)
    struct.pack_into("<6H", payload, 64,
                     0xe000, 0x1406, 0x6763, 0xd102, 0x412b, 0xe601)
    struct.pack_into("<I", payload, 80, WRITER)
    return {"payload": bytes(payload)}


class PioEntryTests(unittest.TestCase):
    def test_actual_native_zero_callback_and_exact_writer_arguments(self):
        report = check_pio_entry(fixture(), ADAPTER, 12, WRITER, base=BASE)
        self.assertTrue(report["callback_null_at_writer_entry"])
        self.assertEqual(report["write_blocks"], 1)
        self.assertEqual(report["native_cases"], 16)

    def test_rejects_dma_callback_wrong_sector_count_pointer_and_unsupported_code(self):
        for at, value in ((64, 0xe001), (66, 0x1405), (68, 0x6753),
                          (74, 0xe602), (64, 0x000b)):
            with self.subTest(at=at, value=value):
                image = fixture()
                payload = bytearray(image["payload"])
                struct.pack_into("<H", payload, at, value)
                image["payload"] = bytes(payload)
                with self.assertRaises(ValueError):
                    check_pio_entry(image, ADAPTER, 12, WRITER, base=BASE)

    def test_rejects_unadmitted_callee_and_escaped_delay_slot(self):
        image = fixture()
        payload = bytearray(image["payload"])
        struct.pack_into("<I", payload, 80, WRITER + 4)
        image["payload"] = bytes(payload)
        with self.assertRaises(ValueError):
            check_pio_entry(image, ADAPTER, 12, WRITER, base=BASE)
        with self.assertRaises(ValueError):
            check_pio_entry(fixture(), ADAPTER, 10, WRITER, base=BASE)


if __name__ == "__main__":
    unittest.main()
