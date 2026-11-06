#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute production cold-stage return preparation/publication/worker bind."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tools')]
from check_vmu_native_worker_gateway import read_elf
from check_vmu_native_readonly import relocated
from test_smash_menu_return_runtime import prepare, FIRMWARE, FIRMWARE_ADDRESSES


def call(cpu, symbols, name, args=()):
    cpu.pc, cpu.pr, cpu.r[15] = symbols[name][0], 0x8c003000, 0x8cff0000
    cpu.sr = 0x700000f0
    for i, value in enumerate(args):
        cpu.r[4+i] = value
    before = cpu.instructions
    stops = {value[0] for key, value in symbols.items() if key.startswith(('_stopped', '_relay_stopped', '_native_report'))}
    for _ in range(5000000):
        if cpu.pc == 0x8c003000:
            return cpu.instructions-before
        assert cpu.pc not in stops, 'Cold stage rejected the publication fixture'
        cpu.step()
    raise AssertionError('Stage publication exceeded its instruction bound')


def audit(build, mde_path):
    """Run all three production functions against the exact current artifacts."""
    build = Path(build)
    gate = read_elf(build/'return-loader.elf')
    stage = read_elf(build/'stage.elf')
    worker = read_elf(build/'native-worker.elf')
    mde = Path(mde_path).read_bytes()
    assert hashlib.sha256(mde).hexdigest() == '04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
    cpu, _, _, _, _, _ = prepare(gate[0], gate[2], gate[4], mde, 'sf_return')
    seed = cpu.block(gate[4]['_kui_smash_menu_return_runtime'][0], 368)
    # Restore the source reset entry and replace all old BOOT1 contents with
    # a canary. Only the actual stage may publish the raw reader and gate.
    cpu.store_block(0x8c010000, mde)
    cpu.store_block(0xac008100, bytes([0x6d])*0x3f00)
    for part in stage[2]:
        if part[0] == 1:
            cpu.store_block(part[2], stage[0][part[1]:part[1]+part[4]])
            if part[5] > part[4]:
                cpu.store_block(part[2]+part[4], bytes(part[5]-part[4]))
    s = stage[4]
    assert s['__smash_menu_return_seed'][0] == s['__smash_native_menu_seed'][0]+304
    cpu.store_block(s['__smash_menu_return_seed'][0], seed)
    manifest = bytearray(s['_manifest'][1])
    struct.pack_into('<QQQ', manifest, 0, 1024, 1, 1024)
    struct.pack_into('<I', manifest, 40, len(mde))
    struct.pack_into('<I', manifest, 60, 0x18efc93a)
    cpu.store_block(s['_manifest'][0], manifest)
    cpu.store_block(s['_boot_crc'][0], struct.pack('<I', zlib.crc32(mde)))
    for address, value in zip(FIRMWARE_ADDRESSES, FIRMWARE):
        cpu.store_block(address, struct.pack('<I', value))
    prep = call(cpu, s, '_kui_smash_native_return_stage_prepare')
    prepared = cpu.block(s['_smash_return_prepared'][0], 420)
    assert prepared[:368] == seed
    assert list(struct.unpack_from('<9I', prepared, 368)) == FIRMWARE
    assert cpu.block(0x8c0117f0, 42) == mde[0x17f0:0x181a]
    publication = call(cpu, s, '_kui_smash_native_return_stage_publish')
    raw = (build/'smash-return-raw.bin').read_bytes()
    code = (build/'return-loader.bin').read_bytes()
    runtime = gate[4]['_kui_smash_menu_return_runtime'][0]
    assert cpu.block(0xac008100, 512) == raw
    copied = bytearray(cpu.block(0xac008300, len(code)))
    copied[runtime-0xac008300:runtime-0xac008300+420] = bytes(420)
    assert copied == code and cpu.block(runtime, 420) == prepared
    assert cpu.block(0x8c0117f0, 12) == struct.pack('<4HI', 0xd001, 0x402b, 9, 9, 0xac008300)
    assert cpu.block(0x8c0117fc, 30) == mde[0x17fc:0x181a]
    assert cpu.block(0xac00b800, 2048) == bytes(2048)
    destination, span = 0xacbe0000, 0x12000
    binary = (build/'native-worker.bin').read_bytes()
    wire = (build/'native-worker.reloc').read_bytes()
    body = relocated(binary, wire, destination)
    cpu.store_block(destination, body+bytes(span-len(body)))
    binding = call(cpu, s, '_kui_smash_native_return_stage_bind', (destination, len(body), span))
    record = bytearray(cpu.block(runtime, 420))
    target = struct.unpack_from('<I', record, 408)[0]
    expected = worker[4]['_kui_smash_native_return_entry'][0]+destination-0xac100000
    assert target == expected
    authority = struct.unpack_from('<I', record, 412)[0]
    record[412:416] = bytes(4)
    assert zlib.crc32(record, zlib.crc32(code)) & 0xffffffff == authority
    assert record[:408] == prepared[:408] and record[416:] == prepared[416:]
    assert cpu.block(destination, len(body)) == body
    report = dict(schema='kui.smash-menu.return-publication.v1', passed=True,
        stage_elf_sha256=hashlib.sha256(stage[0]).hexdigest(),
        gate_elf_sha256=hashlib.sha256(gate[0]).hexdigest(),
        worker_elf_sha256=hashlib.sha256(worker[0]).hexdigest(),
        prepare_instructions=prep, publication_instructions=publication, binding_instructions=binding,
        original_firmware_captured_before_patch=True, raw_and_code_exact=True,
        initialized_runtime_exact=True, private_stack_zeroed=True,
        reset_patch_before_cleanup='0x8c0117f0', native_return_seed_delta=304,
        actual_relocated_worker_gateway=hex(target), metadata_resealed_after_worker_bind=True,
        limits=['The actual three cold-stage functions execute; normal bootstrap/GD loading and the call-site ordering are independently covered by the native route audit.',
                'OCBP and CCR accesses are modeled without cache timing. The worker is relocated from its exact artifact; retirement, game and physical I/O are not invoked.'])
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', required=True, type=Path)
    p.add_argument('--mde', required=True, type=Path)
    p.add_argument('--report', type=Path)
    a = p.parse_args()
    text = json.dumps(audit(a.build, a.mde), indent=2)+'\n'
    if a.report:
        a.report.parent.mkdir(parents=True, exist_ok=True);a.report.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
