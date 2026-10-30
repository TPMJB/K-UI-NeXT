#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the actual SCI Standard bus against delayed receive-DMA arbitration."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMMON = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
          '-ffunction-sections', '-fdata-sections', '-fno-pie', '-no-pie',
          '-fsanitize=address,undefined', '-Iinclude', '-Isrc/loader',
          '-DKUI_SCI_SD_TEST=1', '-DKUI_NATIVE_VMU_ONLY=1',
          '-DKUI_NATIVE_VMU_LITE=1', '-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1']


class SCIReceivePacing(unittest.TestCase):
    def test_low_display_retains_build_and_diagnostic_labels(self):
        for low in (False, True):
            flags = ['-DKUI_NATIVE_VMU_ONLY=1', '-DKUI_BUILD_ID="RX-PROOF"']
            if low:
                flags += ['-DKUI_RETAIL_TRANSPORT=1']
            result = subprocess.run(['cc', '-E', '-P', '-Iinclude', '-Isrc/loader',
                *flags, 'src/loader/retail_display.c'], cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            body = result.stdout.split('void retail_display_restore(', 1)[1]
            self.assertIn('"BUILD " "RX-PROOF"', body)
            self.assertIn('"NATIVE GD IMAGE"' if not low else 'row=108', body)
            if low:
                self.assertNotIn('"NATIVE GD IMAGE"', body)
        resident = (ROOT/'src/loader/retail_resident.c').read_text()
        for label in ('NATIVE GD FAILURE', 'LBA CNT DST DONE', 'FN CMD SD IMG STATE'):
            self.assertIn('"'+label+'"', resident)

    def test_delayed_arbitration_and_existing_failure_contracts(self):
        with tempfile.TemporaryDirectory(prefix='native-sci-rx-') as temporary:
            directory = Path(temporary)
            environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
            for variant, paced, pio in [('unpaced', 0, 0), ('paced', 1, 0), ('pio', 0, 1)]:
                with self.subTest(variant=variant):
                    bus, fixture, executable = (directory/(variant+suffix)
                                                for suffix in ('.bus.o', '.fixture.o', '.test'))
                    flags = [f'-DKUI_NATIVE_VMU_SCI_RX_PACED={paced}',
                             f'-DKUI_SCI_SD_PIO_ONLY={pio}']
                    # The fixture retains the full runtime's register model;
                    # the production bus uses the actual read-only low flags.
                    commands = [
                        ['cc', *COMMON, *flags, '-DKUI_RETAIL_TRANSPORT=1',
                         '-DKUI_RETAIL_LOW_RESIDENT=1', '-c', 'src/loader/sci_sd_bus.c', '-o', str(bus)],
                        ['cc', *COMMON, *flags, f'-DEXPECT_NATIVE_PACED={paced}',
                         f'-DEXPECT_NATIVE_PIO={pio}', '-c', 'tests/test_native_vmu_sci_rx_pacing.c',
                         '-o', str(fixture)],
                        ['cc', *COMMON, str(bus), str(fixture), '-Wl,--gc-sections', '-o', str(executable)],
                        [str(executable)],
                    ]
                    for command in commands:
                        result = subprocess.run(command, cwd=ROOT, text=True,
                                                capture_output=True, env=environment)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        if command == [str(executable)]:
                            print(result.stdout, end='')
                    if paced:
                        # Removing only the post-credit TDRE wait must be
                        # observed by the fixture's sampled-1-before-clear
                        # assertion; success on the early-TDRE model alone
                        # cannot justify this hardware access contract.
                        source = (ROOT/'src/loader/sci_sd_bus.c').read_text()
                        statement = '        if(!wait_flag(TDRE)) return false;\n'
                        self.assertEqual(source.count(statement), 1)
                        mutant = directory/'without-tdre-wait.c'
                        mutant.write_text(source.replace(statement, '', 1))
                        mutant_bus, mutant_executable = directory/'mutant.o', directory/'mutant.test'
                        for command in (
                            ['cc', *COMMON, *flags, '-DKUI_RETAIL_TRANSPORT=1',
                             '-DKUI_RETAIL_LOW_RESIDENT=1', '-c', str(mutant), '-o', str(mutant_bus)],
                            ['cc', *COMMON, str(mutant_bus), str(fixture), '-Wl,--gc-sections',
                             '-o', str(mutant_executable)],
                        ):
                            result = subprocess.run(command, cwd=ROOT, text=True,
                                                    capture_output=True, env=environment)
                            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        result = subprocess.run([str(mutant_executable)], cwd=ROOT,
                            text=True, capture_output=True, env=environment)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn('arbitration.tdre_witness', result.stderr)
                        print('TDRE wait-removal mutation rejected by the actual write-before-witness assertion')


if __name__ == '__main__':
    unittest.main()
