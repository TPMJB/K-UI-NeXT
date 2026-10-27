#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile actual exception assembly and execute it in a bounded SH model."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from check_vmu_native_fault_capture import audit
from check_vmu_native_worker_gateway import read_elf


class FaultCaptureTests(unittest.TestCase):
    def test_actual_linked_gateway_and_publication(self):
        compiler = ROOT / '.deps/sh-elf/bin/sh-elf-gcc'
        if not compiler.exists():
            self.skipTest('Pinned SH compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='kui-fault-capture-sh-') as temporary:
            directory = Path(temporary)
            (directory / 'state.S').write_text('''
    .section .text.fault_terminal,"ax"
    .align 2
    .global _kui_vmu_native_runtime_fault_hook
    .type _kui_vmu_native_runtime_fault_hook,@function
_kui_vmu_native_runtime_fault_hook:
    nop
    .size _kui_vmu_native_runtime_fault_hook,.-_kui_vmu_native_runtime_fault_hook
    .section .bss,"aw",@nobits
    .align 2
    .global _kui_vmu_native_fault_capture_state
    .type _kui_vmu_native_fault_capture_state,@object
_kui_vmu_native_fault_capture_state:
    .space 32
    .size _kui_vmu_native_fault_capture_state,32
''')
            (directory / 'layout.ld').write_text('''
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_vmu_native_fault_capture_gateway)
SECTIONS {
    . = 0x8c100000;
    .text : { *(.text .text.*) }
    . = 0xac110000;
    .bss (NOLOAD) : { *(.bss .bss.*) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
''')
            for source, output in ((ROOT / 'src/loader/vmu_native_fault_capture.S', directory / 'gateway.o'),
                                   (directory / 'state.S', directory / 'state.o')):
                subprocess.run([str(compiler), '-ml', '-m4-single-only', '-DKUI_VMU_NATIVE_CACHED_CODE=1',
                                '-c', str(source), '-o', str(output)], check=True)
            worker = directory / 'worker.elf'
            subprocess.run([str(compiler), '-ml', '-m4-single-only', '-nostdlib', '-Wl,--build-id=none',
                            '-T' + str(directory / 'layout.ld'), str(directory / 'gateway.o'),
                            str(directory / 'state.o'), '-o', str(worker)], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            result = audit(read_elf(worker), self_test=True)
            self.assertEqual(result['instruction_cases'], 432)
            self.assertGreater(result['mutation_rejections'], 30)
            self.assertEqual(result['publication_cases'], 5)


if __name__ == '__main__':
    unittest.main()
