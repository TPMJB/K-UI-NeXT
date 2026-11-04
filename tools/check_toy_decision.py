#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check stripped Toy B/C layout, loaded bytes and conservative stack fit."""
import argparse
import json
from pathlib import Path
import struct

from check_loader_layout import inspect_elf
from check_mdk2_cdda import stack_records
from check_native_cdda_compat import config


def check(disabled, observed):
    images, normalized, records = [], [], []
    for profile, directory in ((1, Path(disabled)), (2, Path(observed))):
        settings = config(directory)
        if settings.get('TOY_DECISION_PROFILE') != str(profile):
            raise ValueError('Wrong decision profile in ' + str(directory))
        if any(settings.get(k) != v for k, v in {
            'DATA_SCHEDULE_MODE': '2', 'DATA_PIPELINE_PROFILE': '2',
            'DATA_BUDGET_PROFILE': '4', 'FMV_TEST': '1', 'FMV_IDLE_REARM': '0',
            'TOY_DEMAND_TEST_PROFILE': '0'}.items()):
            raise ValueError('Decision build does not retain its AP contract')
        image = inspect_elf((directory / 'worker.elf').read_bytes(), 0x8cfd0000,
            0x8cfe0000, private_p2=True, entry_at_base=False,
            entry_symbol='_kui_toy_pilot_initialize')
        if image['payload'] != (directory / 'worker.bin').read_bytes():
            raise ValueError('Worker bin differs from ELF')
        symbols = image['symbols']
        control = symbols['_decision_enabled'] - 0x20000000 - 0x8cfd0000
        payload = bytearray(image['payload'])
        if struct.unpack_from('<I', payload, control)[0] != profile:
            raise ValueError('Missing initialized runtime decision selector')
        payload[control:control+4] = bytes(4)
        text=settings.get('TOY_TEXT_REPORT','0')=='1'
        if text:
            from check_toy_report_export import check as check_export
            export=check_export(directory)
            binding=symbols['_kui_toy_report_binding']-0x20000000-0x8cfd0000
            if struct.unpack_from('<I',payload,binding+12)[0]!=profile:
                raise ValueError('Wrong initialized report binding profile')
            payload[binding+12:binding+16]=bytes(4)
        build = settings['BUILD'].encode()
        if len(build) != 12 or not payload.count(build+b'\0'):
            raise ValueError('Missing fixed-width displayed build identifier')
        payload = payload.replace(build+b'\0', bytes(13))
        for name in ('_kui_toy_loader_trace_begin', '_kui_toy_loader_trace_end',
                     '_kui_toy_loader_data_probe_begin', '_kui_toy_loader_schedule_report_begin'):
            if name in symbols:
                raise ValueError('Legacy hot observer still linked: ' + name)
        frames = stack_records(directory.glob('resident-sci.elf.ltrans*.su'))
        frames += stack_records((directory / 'worker').rglob('*.su'))
        frames += stack_records(directory.glob('worker.elf.ltrans*.su'))
        bound = 256 + sum(frame for _, frame in frames)
        if bound > 8192-32:
            raise ValueError('Conservative worker stack bound exceeded')
        if symbols['__toy_pilot_stack_top']-symbols['__toy_pilot_stack_bottom'] != 8192:
            raise ValueError('Private worker stack changed size')
        records.append({'profile': profile, 'directory': str(directory),
            'worker_end': hex(symbols['__toy_pilot_worker_end']),
            'spare_bytes': 0x8cfe0000-symbols['__toy_pilot_worker_end'],
            'stack_upper_bound': bound, 'stack_capacity': 8192,
            'legacy_hot_observers_absent': True,
            'text_report':export if text else False})
        images.append(image);normalized.append(payload)
    if images[0]['symbols'] != images[1]['symbols']:
        raise ValueError('B/C linked symbols or memory geometry differ')
    if normalized[0] != normalized[1]:
        raise ValueError('B/C worker bytes differ beyond initialized selectors and build ID')
    low = []
    for directory in (Path(disabled), Path(observed)):
        data=(directory/'resident-sci.bin').read_bytes()
        low.append(data.replace(config(directory)['BUILD'].encode()+b'\0',bytes(13)))
    if low[0] != low[1]:
        raise ValueError('B/C low reader differs beyond build ID')
    return {'profiles': records, 'same_geometry': True,
            'worker_differences_limited_to_initialized_selector_and_build_id': True,
            'report_binding_profile_is_only_additional_allowed_word': bool(records[0]['text_report']),
            'low_reader_unchanged_except_build_id': True, 'console_tested': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('disabled', type=Path)
    parser.add_argument('observed', type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.disabled, args.observed), indent=2))
