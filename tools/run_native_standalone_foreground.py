#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run explicit foreground-control admission and actual reader host models.

These tests execute production C against an authored card/register model.
They do not establish console compatibility, SH exception behavior or timing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEFINITIONS = (
    'KUI_ON_CONSOLE=1', 'KUI_NATIVE_READER_DIAGNOSTIC=1',
    'KUI_RETAIL_NATIVE_HYBRID=1', 'KUI_SCI_SD_NO_BLOCK=1',
    'KUI_GENERAL_SCI_RESUME_FIX=1', 'KUI_RETAIL_NATIVE_DISC_CHECK=1',
    'KUI_RETAIL_NATIVE_DRIVE_STATE=1', 'KUI_RETAIL_ASYNC_TEST=1',
    'KUI_SCI_STREAM_TEST=1', 'KUI_RETAIL_ASYNC=1',
    'KUI_RETAIL_GD_ASYNC=1', 'KUI_RETAIL_TRANSPORT=1',
    'KUI_RETAIL_LOW_RESIDENT=1', 'KUI_NATIVE_REQUEST_TIMING=1',
    'KUI_NATIVE_COOP_PUMP=1', 'KUI_NATIVE_IRQ_REARM=1',
    'KUI_NATIVE_LOADER_TIMELINE=1', 'KUI_NATIVE_SDK_SCI_ROUTE=1',
    'KUI_NATIVE_SDK_SCI_STRUCTURAL=1', 'KUI_NATIVE_STANDALONE_READER=1',
    'KUI_NATIVE_STANDALONE_STRUCTURAL=1', 'KUI_NATIVE_STANDALONE_FOREGROUND=1',
)
FLAGS = (
    '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
    '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
    '-fno-sanitize-recover=all', '-Iinclude', '-Isrc/loader', '-Itests',
)
TESTS = {
    'admission': {
        'sources': ('tests/test_native_standalone_foreground.c',
                    'src/core/native_standalone_admission.c',
                    'src/core/hash.c', 'src/core/data.c'),
        'link_flags': (), 'prefix': 'FOREGROUND_ADMISSION_JSON ',
    },
    'runtime': {
        'sources': ('tests/test_retail_async_foreground.c',
                    'src/loader/retail_async.c', 'src/loader/sci_stream.c',
                    'src/core/retail_cursor.c', 'src/core/retail_image.c',
                    'src/core/retail_gd.c'),
        'link_flags': ('-Wl,--wrap=kui_sci_stream_stop',),
        'prefix': 'FOREGROUND_READER_JSON ',
    },
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def invoke(command, env=None):
    return subprocess.run(command, cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def source_closure(cc, definitions):
    """Ask the real preprocessor for each test's complete project closure."""
    paths = {Path(__file__).resolve().relative_to(ROOT).as_posix()}
    for test in TESTS.values():
        result = invoke([cc, *FLAGS, *definitions, '-MM', *test['sources']])
        if result.returncode:
            raise RuntimeError('Dependency discovery failed:\n' + result.stdout)
        paths.update(test['sources'])
        for line in result.stdout.replace('\\\n', ' ').splitlines():
            if ':' not in line:
                continue
            for name in shlex.split(line.split(':', 1)[1]):
                candidate = (ROOT / name).resolve()
                if candidate.is_relative_to(ROOT):
                    paths.add(candidate.relative_to(ROOT).as_posix())
    return {name: sha(ROOT / name) for name in sorted(paths)}


def parse_result(label, output, prefix, legacy_stream=False, dma_stream=False, pio_stream=False, vram_dma=False):
    rows = [line[len(prefix):] for line in output.splitlines()
            if line.startswith(prefix)]
    if len(rows) != 1:
        raise ValueError(label + ': missing unique test result')
    result = json.loads(rows[0])
    positive = ('checks', 'accepted', 'refused') if label == 'admission' else (
        'checks', 'byte_exact_requests', 'cancel_cases', 'bus_refusal_cases',
        'stop_failure_cases')
    if any(type(result.get(key)) is not int or result[key] <= 0 for key in positive):
        raise ValueError(label + ': test did not exercise required cases')
    if result.get('hardware_executed') is not False:
        raise ValueError(label + ': unexpected hardware claim')
    if label == 'runtime' and any(result.get(key) != 0 for key in (
            'callback_reads', 'publications', 'vbr_writes')):
        raise ValueError('Foreground runtime touched an SDK callback route')
    if label == 'runtime':
        legacy_keys=('legacy_stream_cases','legacy_piece_transfers','legacy_cancel_cases',
                     'legacy_error_cases','legacy_busy_polls','legacy_idle_polls')
        if any(type(result.get(key)) is not int or
               (result[key]<=0 if legacy_stream else result[key]!=0) for key in legacy_keys):
            raise ValueError('Legacy-stream cases do not match the selected profile')
        dma_keys=('dma_stream_cases','dma_piece_transfers','dma_partial_pieces',
                  'dma_error_cases','dma_cancel_cases','dma_end_events','dma_drive_events')
        if any(type(result.get(key)) is not int or
               (result[key]<=0 if dma_stream else result[key]!=0) for key in dma_keys):
            raise ValueError('DMA-stream cases do not match the selected profile')
        pio_keys=('pio_stream_cases','pio_piece_transfers','pio_partial_pieces',
                  'pio_error_cases','pio_cancel_cases','pio_drive_events',
                  'pio_callbacks_registered','pio_callbacks_cleared','pio_virtual_direct_reads')
        if any(type(result.get(key)) is not int or
               (result[key]<=0 if pio_stream else result[key]!=0) for key in pio_keys):
            raise ValueError('PIO-stream cases do not match the selected profile')
        vram_keys=('vram_dma_cases','vram_dma_bytes','vram_odd_fragments',
                   'vram_crc_cases','vram_abort_cases','vram_bus_refusal_cases',
                   'vram_refusal_cases')
        if any(type(result.get(key)) is not int or
               (result[key]<=0 if vram_dma else result[key]!=0) for key in vram_keys):
            raise ValueError('VRAM DMA cases do not match the selected profile')
        if result.get('vram_same_offset_ram_writes') != 0:
            raise ValueError('VRAM DMA was redirected into same-offset main RAM')
        virtual_reads=result.get('vram_cpu_virtual_reads')
        if type(virtual_reads) is not int or (virtual_reads<=0 if vram_dma and pio_stream else virtual_reads!=0):
            raise ValueError('Numeric04 CPU virtual collision cases do not match the selected profile')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/standalone-foreground-host')
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--legacy-stream',action='store_true',
                        help='Exercise the isolated native CMD28/FN6/FN7 foreground profile')
    parser.add_argument('--mmu-buffers',type=int,choices=(0,1),default=0,
                        help='Select MMU buffer profile; host adapter retains AT0')
    parser.add_argument('--dma-stream',type=int,choices=(0,1),default=0,
                        help='Exercise native command38 physical scatter transfers')
    parser.add_argument('--pio-stream',type=int,choices=(0,1),default=0,
                        help='Exercise command39 CPU-virtual PIO pieces and ordinary PIO16')
    parser.add_argument('--vram-dma',type=int,choices=(0,1),default=0,
                        help='Exercise bounded physical VRAM DMA through the actual reader')
    args = parser.parse_args()
    if args.mmu_buffers and not args.legacy_stream:
        parser.error('--mmu-buffers 1 requires --legacy-stream')
    if args.dma_stream and not (args.legacy_stream and args.mmu_buffers):
        parser.error('--dma-stream 1 requires --legacy-stream --mmu-buffers 1')
    if args.pio_stream and not args.dma_stream:
        parser.error('--pio-stream 1 requires --dma-stream 1')
    if args.vram_dma and not args.dma_stream:
        parser.error('--vram-dma 1 requires --dma-stream 1')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    definitions = ['-D' + value for value in DEFINITIONS]
    if args.legacy_stream:
        definitions.extend(['-DKUI_NATIVE_OCRAM_BOOTSTRAP=1','-DKUI_NATIVE_GD_LEGACY_STREAM=1'])
    definitions.append('-DKUI_NATIVE_GD_MMU_BUFFERS='+str(args.mmu_buffers))
    definitions.append('-DKUI_NATIVE_GD_DMA_STREAM='+str(args.dma_stream))
    definitions.append('-DKUI_NATIVE_GD_PIO_STREAM='+str(args.pio_stream))
    definitions.append('-DKUI_NATIVE_GD_VRAM_DMA='+str(args.vram_dma))
    compiler = shutil.which(args.cc)
    if compiler is None:
        raise RuntimeError('Compiler not found: ' + args.cc)
    compiler = Path(compiler).resolve()
    version = invoke([str(compiler), '--version'])
    if version.returncode:
        raise RuntimeError('Compiler version failed: ' + version.stdout)
    receipt = {
        'schema': 'kui.native.standalone-foreground-host.v1', 'status': 'FAIL',
        'source_root': str(ROOT),
        'legacy_stream_enabled': args.legacy_stream,
        'mmu_buffers_enabled': bool(args.mmu_buffers),
        'dma_stream_enabled': bool(args.dma_stream),
        'pio_stream_enabled': bool(args.pio_stream),
        'vram_dma_enabled': bool(args.vram_dma),
        'compiler': {'path': str(compiler), 'sha256': sha(compiler),
                     'version': version.stdout},
        'modes': {},
        'limits': [
            'Admission tests call the manifest/descriptor helpers; the target stage still independently proves its original SDK image family.',
            'Runtime executes production C with an authored card, SCI/DMA registers, bus ownership and CPU VBR/SR model.',
            'No SH exception/cache ordering, real SD timing, hardware execution or console compatibility is established here.',
            'Failed-stop injection wraps the production stream-stop result at final EXEC completion; a following CHECK observes latched IO.',
            'The host adapter does not model the resident caller terminal report_fault check after async_after.',
            'Native DMA event bits are drained and checked at the service boundary; actual WinCE IRQ queue delivery is not modeled by this harness.',
            'PIO tests use distinct authored U0/P3/0c-process-slot memory banks; no real TLB/page table executes. Callback due and registration are checked, but the resident deferred callback consumer is an explicit boundary.',
            'VRAM tests use distinct authored 64-bit and 32-bit aperture banks, plus a same-offset main-RAM canary. Physical alias/interleave hardware and SH store-width validation are separate linked-model boundaries.',
            'LeakSanitizer is disabled, matching existing host recipes; AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.',
        ],
        'hardware_executed': False, 'console_compatibility_proved': False,
    }
    logs = []
    try:
        before = source_closure(str(compiler), definitions)
        receipt['source_sha256'] = before
        for mode, sanitizer in (('ubsan', 'undefined'),
                                ('asan-ubsan', 'address,undefined')):
            receipt['modes'][mode] = {}
            for label, test in TESTS.items():
                exe = out / ('test-foreground-' + label + '-' + mode)
                command = [str(compiler), *FLAGS, '-fsanitize=' + sanitizer,
                           *definitions, *test['sources'], *test['link_flags'],
                           '-o', str(exe)]
                built = invoke(command)
                logs.append(label + ' ' + mode + ' compile\n' + built.stdout)
                if built.returncode:
                    raise RuntimeError(label + ' compilation failed:\n' + built.stdout)
                env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
                           UBSAN_OPTIONS='halt_on_error=1')
                ran = invoke([str(exe)], env)
                logs.append(label + ' ' + mode + ' execute\n' + ran.stdout)
                if ran.returncode:
                    raise RuntimeError(label + ' execution failed:\n' + ran.stdout)
                result = parse_result(label, ran.stdout, test['prefix'],args.legacy_stream,args.dma_stream,args.pio_stream,args.vram_dma)
                receipt['modes'][mode][label] = {
                    'result': result, 'command': command,
                    'executable': str(exe), 'sha256': sha(exe),
                }
        after = {name: sha(ROOT / name) for name in before}
        receipt['source_hashes_unchanged_after_tests'] = before == after
        if before != after or receipt['compiler']['sha256'] != sha(compiler):
            raise RuntimeError('Source or compiler changed during the test run')
        for label in TESTS:
            if receipt['modes']['ubsan'][label]['result'] != receipt['modes']['asan-ubsan'][label]['result']:
                raise RuntimeError(label + ': sanitizer modes exercised different results')
        receipt['status'] = 'PASS'
    except Exception as error:
        receipt['error'] = str(error)
        raise
    finally:
        (out / 'foreground-host.log').write_text('\n'.join(logs))
        (out / 'foreground-host.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS foreground control host models:', {
        mode: {label: row['result'] for label, row in tests.items()}
        for mode, tests in receipt['modes'].items()})


if __name__ == '__main__':
    main()
