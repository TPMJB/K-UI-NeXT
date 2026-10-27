#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and check a NON-LAUNCHABLE standalone SDK-SCI reader fit experiment.

This produces an ELF and size/stack receipt only. It does not build a stage,
entry image, KUI module or installer, and never publishes SDK callbacks.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from check_retail_instructions import audit as instruction_audit
import check_retail_stack as graph
from check_vmu_native_worker_gateway import read_elf

ROOT = Path(__file__).resolve().parents[1]
RUNTIME_CHANGES = (
    'include/kui/native_sdk_sci_runtime.h', 'src/loader/retail_async.h',
    'src/loader/retail_async.c', 'src/loader/retail_resident.c')
SOURCES = (
    'src/loader/retail_resident.c', 'src/core/retail_gd.c', 'src/loader/minic.c',
    'src/core/retail_image.c', 'src/loader/retail_display.c',
    'src/loader/sci_sd_bus.c', 'src/core/retail_cursor.c',
    'src/loader/sci_stream.c', 'src/loader/retail_async.c',
    'src/loader/retail_resident.S')
FLAGS = [
    '-std=c11', '-Os', '-g', '-Wall', '-Wextra', '-Werror', '-m4-single',
    '-ml', '-mdiv=call-div1', '-mfixed-range=fr0-fr15,fpul-fpul',
    '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fno-pic',
    '-ffunction-sections', '-fdata-sections', '-fno-jump-tables',
    '-fstack-usage', '-fcallgraph-info=su', '-flto', '-mrelax',
    '-Iinclude', '-Isrc/loader', '-DKUI_ON_CONSOLE=1',
    '-DKUI_RETAIL_LOW_RESIDENT=1', '-DKUI_RETAIL_FAST_IO=1',
    '-DKUI_GENERAL_SCI_RESUME_FIX=1', '-DKUI_RETAIL_NATIVE_DISC_CHECK=1',
    '-DKUI_RETAIL_NATIVE_DRIVE_STATE=1', '-DKUI_RETAIL_TRANSPORT=1',
    '-DKUI_RETAIL_ASYNC=1', '-DKUI_RETAIL_GD_ASYNC=1',
    '-DKUI_SCI_SD_NO_BLOCK=1', '-DKUI_RETAIL_MANIFEST_SLOTS=64',
    '-DKUI_RETAIL_NATIVE_HYBRID=1', '-DKUI_NATIVE_READER_DIAGNOSTIC=1',
    '-DKUI_NATIVE_REQUEST_TIMING=1', '-DKUI_NATIVE_COOP_PUMP=1',
    '-DKUI_NATIVE_IRQ_REARM=1', '-DKUI_NATIVE_LOADER_TIMELINE=1',
    '-DKUI_NATIVE_SDK_SCI_ROUTE=1', '-DKUI_NATIVE_SDK_SCI_STRUCTURAL=1',
    '-DKUI_NATIVE_STANDALONE_READER=1', '-DKUI_BUILD_ID="fit-only"']
EXPORTS = (
    'memcpy', 'memset', 'kui_retail_resident_init',
    'kui_retail_resident_dispatch', 'kui_retail_gd_dispatch',
    'kui_retail_async_irq', 'kui_retail_sdk_sci_entry')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command):
    result = subprocess.run(command, cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise ValueError(result.stdout)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, default=ROOT/'.deps/sh-elf/bin')
    parser.add_argument('--build', type=Path, default=Path('build/standalone-fit'))
    args = parser.parse_args()
    build = args.build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    toolchain = args.toolchain.resolve()
    cc = str(toolchain/'sh-elf-gcc')
    objects, commands = [], []
    for source in SOURCES:
        suffix = '_asm' if source.endswith('.S') else ''
        obj = build/(Path(source).stem+suffix+'.o')
        command = [cc, *FLAGS, '-c', source, '-o', str(obj)]
        commands.append(command)
        run(command)
        objects.append(str(obj))
    elf = build/'resident.elf'
    link = [cc, '-fno-tree-scev-cprop',
            *[flag for flag in FLAGS if flag not in
              ('-ffunction-sections', '-fdata-sections')],
            '-nostdlib', '-Wl,--gc-sections', '-Wl,--build-id=none',
            '-save-temps=obj', '-Wl,--relax',
            *['-Wl,-u,_'+symbol for symbol in EXPORTS],
            '-Tsrc/loader/retail_resident_async_low.ld',
            '-Wl,-Map,'+str(build/'resident.map'), '-o', str(elf),
            *objects, '-lgcc']
    commands.append(link)
    (build/'link.log').write_text(run(link))
    image = read_elf(elf)  # Includes undefined/TLS/dynamic-symbol rejection.
    symbols = image[4]
    value = lambda name: symbols[name][0]
    assert value('_start') == 0x8c004000
    assert value('__retail_hook_stack_bottom') == 0x8c007ba0
    assert value('__retail_hook_stack') == 0x8c007d00
    assert value('__retail_resident_bss_end') <= 0x8c007ba0
    assert symbols['_standalone_reader_region'][1] == 0x760
    assert symbols['_manifest'][1] == 1020
    assert not any('vmu_observe' in name or 'native_ra_' in name or
                   'native_async_loan' in name for name in symbols)
    disassembly = run([str(toolchain/'sh-elf-objdump'), '-d', str(elf)])
    (build/'resident.dis').write_text(disassembly)
    instructions = instruction_audit('standalone-fit-resident', disassembly)
    frames, calls = graph.load(sorted(build.glob('resident.elf*.ci')))
    paths = graph.worst(frames, calls, roots_override={
        'kui_retail_resident_dispatch': 0, 'kui_retail_sdk_sci_dispatch': 0,
        'kui_retail_menu_return': 0})
    worst = max(path['bytes'] for path in paths.values())
    assert worst+graph.MARGIN <= 352-48
    selected = ['_start', '__retail_resident_binary_end',
                '__retail_resident_bss_begin', '__retail_resident_bss_end',
                '__retail_hook_stack_bottom', '__retail_hook_stack',
                '_standalone_reader_region', '_manifest',
                '_kui_retail_async_region_storage', '_kui_retail_sdk_sci_entry',
                '_kui_native_sdk_sci_route_exports']
    receipt = dict(
        schema='kui.native.standalone-reader-fit.v1', launchable=False,
        result='LOW RESIDENT FIT PASS ONLY', runtime_admission=False,
        stage_built=False, module_built=False, callback_binding_implemented=False,
        console_compatibility_proved=False, sd_hardware_executed=False,
        vm_enabled=False, read_ahead_enabled=False, heap_reservation_bytes=0,
        compiler=run([cc, '--version']).splitlines()[0], flags=FLAGS,
        source_sha256={str(path): digest(ROOT/path) for path in
                       (*RUNTIME_CHANGES, *SOURCES,
                        'src/loader/retail_resident_async_low.ld',
                        'tools/probe_native_standalone_reader_fit.py')},
        symbols={name: dict(address=f'{value(name):08x}',
                            bytes=symbols[name][1]) for name in selected},
        bss_to_stack_margin_bytes=0x8c007ba0-value('__retail_resident_bss_end'),
        decoded_integer_instructions=instructions,
        undefined_symbols=0, fpu_instructions=0, illegal_delay_slots=0,
        private_stack=dict(worst_c_bytes=worst, margin_bytes=graph.MARGIN,
                           available_bytes=352-48, paths=paths),
        artifacts={str(path.relative_to(ROOT)): digest(path) for path in
                   (elf, build/'resident.map', build/'resident.dis')},
        missing=['Late SDK callback-table binding after original initialization',
                 'Route lifetime/reset ownership and live context proof',
                 'Complete stage, entry, module and guarded installer',
                 'Standalone SD/IRQ execution tests and console confirmation'],
        commands=commands)
    (build/'fit-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print('FIT ONLY PASS; NOT LAUNCHABLE; BSS/stack margin',
          receipt['bss_to_stack_margin_bytes'], 'bytes')


if __name__ == '__main__':
    main()
