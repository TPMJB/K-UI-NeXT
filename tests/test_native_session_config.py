#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A reused build directory must never mix session and ordinary reader ABIs."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SessionConfig(unittest.TestCase):
    def test_every_object_family_rebuilds_in_both_directions(self):
        with tempfile.TemporaryDirectory(prefix='native-session-config-') as temporary:
            directory = Path(temporary)
            build = directory / 'build'
            compiler = directory / 'compiler.py'
            log = directory / 'compiles.jsonl'
            compiler.write_text('''import json,pathlib,sys
arguments=sys.argv[1:]
output=pathlib.Path(arguments[arguments.index('-o')+1])
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps(arguments))
with pathlib.Path(__file__).with_name('compiles.jsonl').open('a') as stream:
    stream.write(json.dumps(arguments)+'\\n')
''')
            objects = [build / name for name in (
                'native/src/core/vmu_native_heap.o',
                'resident/src/loader/sci_sd_bus.o',
                'legacy/src/core/retail_gd.o',
                'stage/src/core/hash.o',
                'worker/src/loader/vmu_maple_observe_worker.o',
                'ra/src/core/native_readahead_cache.o')]

            def run(mode):
                makefile = {
                    'ordinary': 'Makefile.vmu-native-split-controller-stream-guard',
                    'a2': 'Makefile.vmu-native-session-controls',
                    'a1': 'Makefile.vmu-native-session-controls-a1',
                }[mode]
                command = ['make', '--no-print-directory', '-f', makefile,
                           f'BUILD={build}', f'CC={sys.executable} {compiler}']
                for ignored in ('vmu_maple_observe_symbols.h', 'worker.bin',
                                'native-ra-aux.bin', 'native-ra-aux.reloc',
                                'vmu_maple_observe_legacy_symbols.h',
                                'resident-legacy.bin'):
                    command += ['-o', str(build / ignored)]
                result = subprocess.run([*command, *map(str, objects)], cwd=ROOT,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return [json.loads(line) for line in log.read_text().splitlines()]

            count = 0
            for mode in ('ordinary', 'a2', 'ordinary', 'a1', 'a2', 'a1', 'ordinary'):
                with self.subTest(mode=mode):
                    rows = run(mode)
                    self.assertEqual(len(rows), count + len(objects),
                                     'Profile switch retained stale ABI objects')
                    for row in rows[count:]:
                        legacy = '/legacy/' in row[row.index('-o') + 1]
                        self.assertEqual('-DKUI_NATIVE_SESSION_CONTROLS=1' in row,
                                         mode != 'ordinary' and not legacy)
                        self.assertEqual('-DKUI_VMU_VIRTUAL_A1=1' in row, mode == 'a1')
                        if mode != 'ordinary' and not legacy:
                            self.assertIn('-DKUI_NATIVE_REQUEST_TIMING=0', row)
                            self.assertNotIn('-DKUI_NATIVE_REQUEST_TIMING=1', row)
                    count = len(rows)
                    self.assertEqual(len(run(mode)), count,
                                     'Unchanged profile rebuilt an object')


if __name__ == '__main__':
    unittest.main()
