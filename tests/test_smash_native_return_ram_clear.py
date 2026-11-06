#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind complete native dead-RAM retirement to its actual terminal call order.

Every store in the linked leaf is independently executed. The containing
return test then substitutes only that proven bulk effect. The actual075e
negative control runs the original return path and retains authored FD poison.
This does not establish the cause of the console's later VC2 freeze.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tools')]
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from test_smash_menu_return_runtime import (exercise, native_ram_clear_proof,
    native_ram_clear_flow, NATIVE_POISON, NATIVE_POISON_BYTES)

OLD_ELF_SHA256 = '06734275bf5f88a46b2d9ae812dbd6375bcf2a1b89f28fc2a366aaf0d456f0da'
MDE_SHA256 = '04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'


def sha(data):
    return hashlib.sha256(data).hexdigest()



def resident_closure(path):
    image = read_elf(path)
    sy = image[4]
    one = (1, 0, 0, 0xfff1)
    assert sy.get('__kui_native_vmu_only_transport_resident') == one
    assert sy.get('__kui_smash_native_menu_resident') == one
    assert sy['_start'][0] == 0x8c004000
    assert not any(name in sy for name in ('_native_background_manifest',
        '_kui_retail_async_region_storage', '_kui_retail_async_region'))
    begin, end = sy['__retail_resident_bss_begin'][0], sy['__retail_resident_bss_end'][0]
    stack_begin, stack_end = sy['__retail_hook_stack_bottom'][0], sy['__retail_hook_stack'][0]
    assert 0x8c004000 < begin < end <= stack_begin < stack_end < 0x8c010000
    objects = {}
    for name in ('_manifest', '_service', '_card', '_image', '_port'):
        address, size, kind, _ = sy[name]
        assert kind == 1 and size and begin <= address < address + size <= end
        objects[name] = dict(address=hex(address), bytes=size)
    table, bus, _ = symbol_bytes(image, '_bus', 1)
    assert 0x8c004000 <= table < table + len(bus) <= begin and len(bus) == 28
    callbacks = struct.unpack('<7I', bus)
    assert not callbacks[0], 'Raw SCI bus acquired a high callback context'
    admitted = []
    for target in callbacks[1:]:
        matches = [name for name, row in sy.items() if row[0] == target and row[2] == 2]
        assert len(matches) == 1 and not target & 1 and 0x8c004000 <= target < begin
        address, body, _ = symbol_bytes(image, matches[0], 2)
        assert address + len(body) <= begin
        admitted.append(dict(symbol=matches[0], address=hex(target), bytes=len(body)))
    raw_address, raw, _ = symbol_bytes(image, '_kui_smash_menu_return_read_card', 2)
    assert raw_address == 0xac008100 and 0 < len(raw) <= 512
    for name in ('_read_block', '_read_run'):
        address, body, _ = symbol_bytes(image, name, 2)
        assert 0x8c004000 <= address < address + len(body) <= begin
    return dict(status='PASS', resident_elf_sha256=sha(image[0]),
        resident_BSS=[hex(begin), hex(end)], resident_stack=[hex(stack_begin), hex(stack_end)],
        low_owned_objects=objects, raw_address=hex(raw_address), raw_code_bytes=len(raw),
        bus_table=hex(table), bus_callback_context=0, bus_callbacks=admitted,
        no_background_region_or_high_manifest_pointer=True,
        limits='Actual linked synchronous SCI objects and callback table; runtime prepared-state publication and raw SD behavior have separate tests.')

def audit(path, old_path, mde_path, resident_path):
    paths = dict(elf=path, old_elf=old_path, resident_elf=resident_path, mde=mde_path)
    before = {name: file.read_bytes() for name, file in paths.items()}
    sources = {ROOT / rel for rel in (
        'tests/test_smash_native_return_ram_clear.py', 'tests/test_smash_menu_return_runtime.py',
        'tests/test_smash_ce_return_ram_clear.py', 'src/loader/smash_native_return_ram_clear.S',
        'src/loader/smash_ce_return_ram_clear.S', 'src/loader/smash_menu_return_loader.c',
        'src/loader/smash_menu_return_entry.S', 'src/loader/smash_menu_return_loader.ld',
        'src/loader/native_return_hardware.S', 'src/loader/native_return_hardware.c',
        'src/loader/smash_menu_return_raw.inc', 'src/loader/retail_resident.c',
        'src/loader/retail_storage_impl.h', 'src/loader/sci_sd_bus.c',
        'src/core/smash_menu_return.c', 'include/kui/smash_menu_return_loader.h',
        'Makefile.smash-native-menu')}
    for module in tuple(sys.modules.values()):
        name = getattr(module, '__file__', None)
        if name:
            file = Path(name).resolve()
            if file.is_relative_to(ROOT) and file.suffix == '.py':
                sources.add(file)
    source_sha = {str(file.relative_to(ROOT)): sha(file.read_bytes()) for file in sorted(sources)}
    current, old = read_elf(path), read_elf(old_path)
    mde = mde_path.read_bytes()
    assert len(mde) == 1848288 and sha(mde) == MDE_SHA256, 'Original MDE identity differs'
    assert sha(old[0]) == OLD_ELF_SHA256, 'Old negative must be the actual delivered075e native ELF'
    assert not native_ram_clear_flow(old[4]), 'Old negative control unexpectedly clears native RAM'
    # The exact full ELF pin is reported below. Its semantic negative is the
    # real legacy path, not a patch of the new clear or a fabricated return.
    assert old[4]['__kui_smash_card_session_flow'] == (1, 0, 0, 0xfff1)
    closure = resident_closure(resident_path)
    leaf = native_ram_clear_proof(current[0], current[2], current[4])
    assert leaf and leaf['status'] == 'PASS'
    cases = []
    for kind in ('sf_return', 'psii_return', 'busy_irq_progress', 'busy_timeout',
                 'bad_worker', 'bad_return_seed', 'bad_capsule_crc'):
        row = exercise(current[0], current[2], current[4], mde, kind)
        cases.append(row)
    old_row, old_cpu = exercise(old[0], old[2], old[4], mde, 'sf_return', return_cpu=True)
    assert old_row['old_FD_window_retained'] and not old_row['native_ram_clear_calls']
    assert old_cpu.block(NATIVE_POISON, NATIVE_POISON_BYTES) == bytes([0xa5]) * NATIVE_POISON_BYTES
    assert all(file.read_bytes() == before[name] for name, file in paths.items()), 'Input changed during native retirement proof'
    assert source_sha == {str(file.relative_to(ROOT)): sha(file.read_bytes()) for file in sources}, 'Proof source changed during execution'
    return dict(schema='kui.smash.native.return.ram-clear.v1', status='PASS',
        test_sha256=source_sha['tests/test_smash_native_return_ram_clear.py'],
        source_sha256=source_sha,
        inputs={name: dict(path=str(file.resolve()), bytes=len(before[name]), sha256=sha(before[name]))
                for name, file in paths.items()}, inputs_immutable_during_execution=True,
        elf_sha256=sha(current[0]), old_elf_sha256=sha(old[0]),
        original_mde_sha256=MDE_SHA256, linked_leaf=leaf, low_resident_closure=closure, cases=cases,
        old075e_negative=dict(case=old_row, old_FD_poison_retained=True,
            poisoned_begin=hex(NATIVE_POISON), poisoned_bytes=NATIVE_POISON_BYTES),
        full_leaf_stores_executed=True, native_clear_before_first_raw_read=True,
        no_clear_on_busy_or_preterminal_refusal=True,
        limits=['Cache/SD/retirement are authored containing-test boundaries.',
            'Complete leaf stores, bounds and CPU state execute independently; their bulk memory effect is explicit in each containing case.',
            'Failure of old dead-RAM retirement is reproduced; physical VC2 freeze attribution remains unproved.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--old-elf', type=Path, required=True)
    parser.add_argument('--resident-elf', type=Path, required=True)
    parser.add_argument('--mde', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = audit(args.elf, args.old_elf, args.mde, args.resident_elf)
    text = json.dumps(report, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
