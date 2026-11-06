#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit exact VC2 NK/Maple geometry without executing or changing owner code.

Only the existing exact-kernel identity gate admits parsing. Output contains
reviewed addresses, sizes, hashes and scalar relationships. It does not admit
a runtime hook, padding patch, live mapping, DMA lease or VMU publication.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from armada_ce_rom_audit import ROMError
from vc2_ce_vmu_owner_audit import VC2SourceROM

WINDOWS = (
    ('process_switch', 0x2118, 56, 'ad46610b7e78c2d9a5b94e692fdf066aad2a2db694279fd12fd227eef412d6cc'),
    ('current_thread_API_return', 0x2256, 102, 'f76f6dde4a440acdc66a64543e9af75285ca9577313f9d70bd64b96b2c50a217'),
    ('ordinary_page_walk', 0x259c, 160, '74ad1367042763b70c717d2affe46321fa592ef0584de2c9e02db47afe830d6d'),
    ('process_geometry', 0x5c5e, 58, 'd2cd54975cf371b99d0eede13132f2ad8d044dd7e4896d753bc87efe85579fec'),
    ('MapPtrToProcess', 0x5c98, 144, 'd73fbdb1ab9d7c2e5eeead5c98374420513ea353355b6bc5ab70581a467a7701'),
    ('SetKMode', 0xdcd0, 36, '54ca089e7faffd4b04be8d1da41b352269b14ccaab76ee8ed88fda96ffc63f41'),
)
MAPLE_WINDOWS = (
    ('root_allocation', 0x16d8, 276, 'c69b56996c1d233f4e1210998e94bcbfc949ac625071b857c4263a1643035a68'),
    ('root_initialization', 0x1a38, 204, '6e77973703f7f0bf6cccac4449d8eaa2e267c5e8048041d5f65778b20439b543'),
    ('DMA_initialization', 0x1dd0, 608, '31b5fd112494d4e225a2cd7355c94e7d905b4cbf8a3feb942b8f8afd8f8f13d1'),
    ('list_builder', 0x6c84, 512, '7d036c7f177199949a8d8130d7d1cef5e9d98f9af01a6dcdee94a0d31cfe7b99'),
    ('RX_allocator', 0x72f4, 100, '3b33c67b9abf1c59ea5a34bb3bffced91bad62612b4c5350df09b0ddebd895a9'),
    ('completion', 0x6f64, 616, '029e36a7dd7e72eda9c9b216e62c684c52f70c2b30028b275161d7965ba8be93'),
    ('raw_active_request', 0x2f18, 96, 'c5565d319fcec40d12bdbb741aa1bc06530aaf93f06086309b629a4f0069ad15'),
    ('raw_request_queue', 0x3198, 144, '2138bc53a8f941432a87130a3765620f5e1f5db29d4d3855e2e232c72dabb023'),
    ('generic_completion_and_queue_advance', 0x2e64, 180, '29b4a8d31a0ecfa341ff3a445f973c2459eb780bf19d9901d4de88e77f010c3d'),
    ('unit_function_factory', 0x38d8, 372, 'ade481ca21f753be126146472e658030de7790c3254597566a1a9d958c5da783'),
    ('device_info_transition', 0x3d2c, 384, 'cd4cb53f650ca3bcb52af4d545bbb921717b8382b064eb9667a8379ddabe1617'),
    ('attached_unit_discovery', 0x6e84, 224, '355fccc07bf033682e42895e088f0a6a49fbd0795a52c40c3caf9fcf090dac19'),
    ('RX_buckets', 0x740c, 496, 'dac9202672e86dd78c372c325f561812c039cda3598284bb613372d0a791acba'),
    ('RX_free', 0x75fc, 268, '4d832860fb1f70505051a87530cb032e1dda6e10e50fe51306e36c4ec518a0d0'),
    ('RX_pages', 0x7708, 64, 'ac945cba310f4fee582169eb05782efdf017306c6ccd0847196bd1d3609f92e2'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, label='Reviewed source scalar'):
    if not condition:
        raise ROMError(label + ' differs from exact VC2 source')


def audit(kernel):
    rom = VC2SourceROM(kernel)
    data = rom.data
    nk = rom.modules['nk.exe']

    def word(rva):
        return struct.unpack('<H', rom.rva_read(nk, rva, 2))[0]

    def literal(rva, register):
        op = word(rva)
        require(op >> 12 == 13 and (op >> 8) & 15 == register)
        return rom.rva_u32(nk, ((rva + 4) & ~3) + 4 * (op & 255))

    def immediate(rva, register, family):
        op = word(rva)
        require(op >> 12 == family and (op >> 8) & 15 == register)
        value = op & 255
        return value - 256 if value & 128 else value

    def load_offset(rva, destination, base):
        op = word(rva)
        require(op >> 12 == 5 and (op >> 8) & 15 == destination and (op >> 4) & 15 == base)
        return 4 * (op & 15)

    kdata = literal(0x211a, 0) - 12
    require(kdata == 0x8c1d5800)
    require(immediate(0x2122, 0, 7) == 120)
    publish = word(0x2128)
    require(publish >> 12 == 1 and (publish >> 8) & 15 == 0 and (publish >> 4) & 15 == 1)
    current_process = kdata + 12 + 120 + 4 * (publish & 15)
    require(current_process == 0x8c1d5890)
    current_thread = literal(0x2260, 0)
    require(current_thread == kdata + 0x94 == 0x8c1d5894)
    require(literal(0xdcd0, 1) == kdata)
    sm = word(0xdcd2)
    require(sm >> 12 == 9 and (sm >> 8) & 15 == 0)
    require(word(0xdcd2 + 4 + 2 * (sm & 255)) == 0x94)
    require(literal(0x5c6c, 4) == 0x8c1de6e0)
    require(immediate(0x5c66, 1, 14) == 32)
    require(immediate(0x5c6e, 3, 14) == 78)
    shift = word(0x5c70)
    require(shift >> 12 == 4 and (shift >> 8) & 15 == 3 and shift & 255 == 0)
    require(load_offset(0x5c78, 1, 4) == 12)
    require(load_offset(0x5c82, 0, 4) == 8)
    require(literal(0x259c, 0) + load_offset(0x259e, 0, 0) == 0xff000008)
    require(immediate(0x25c6, 1, 7) == 12)

    contracts = []
    for name, rva, size, expected in WINDOWS:
        content = rom.rva_read(nk, rva, size)
        require(digest(content) == expected)
        contracts.append(dict(name=name, module='nk.exe', rva=hex(rva), bytes=size,
                              rom_storage_address=hex(rom.rva_address(nk, rva, size)),
                              sha256=expected, crc32=f'{zlib.crc32(content):08x}'))
    maple = rom.modules['maple.dll']
    for name, rva, size, expected in MAPLE_WINDOWS:
        content = rom.rva_read(maple, rva, size)
        require(digest(content) == expected)
        contracts.append(dict(name=name, module='maple.dll', rva=hex(rva), bytes=size,
                              rom_storage_address=hex(rom.rva_address(maple, rva, size)),
                              sha256=expected, crc32=f'{zlib.crc32(content):08x}'))
    alloc = struct.unpack('<H', rom.rva_read(maple, 0x16e6, 2))[0]
    require(alloc >> 12 == 9 and (alloc >> 8) & 15 == 4)
    size_rva = 0x16e6 + 4 + 2 * (alloc & 255)
    require(struct.unpack('<H', rom.rva_read(maple, size_rva, 2))[0] == 0x1a0)
    require(rom.rva_u32(maple, 0x17e0) == 0x01ce82b8)
    require(rom.rva_u32(maple, 0x7750) == 0x01ce8004)
    imports = {s['iat_rva']: s for d in rom.imports(maple)
               if d['dll'].lower() == 'coredll.dll' for s in d['symbols']}
    require(imports[0x8004]['ordinal'] == 1095)
    exports = {e['ordinal']: e for e in rom.exports(rom.modules['coredll.dll'])}
    require(exports[1095]['name'] == '??2@YAPAXI@Z')

    # These decode generic instruction operand forms, recording scalar
    # structures only. The complete windows above bind their reviewed roles.
    def mw(rva):
        return struct.unpack('<H', rom.rva_read(maple, rva, 2))[0]

    def mi(rva, register, expected):
        op = mw(rva)
        require(op >> 12 == 14 and (op >> 8) & 15 == register)
        require(op & 255 == expected)

    def ms(rva, register, expected):
        op = mw(rva)
        require(op >> 12 == 9 and (op >> 8) & 15 == register)
        require(struct.unpack('<H', rom.rva_read(maple, rva + 4 + 2 * (op & 255), 2))[0] == expected)

    def ml(rva, destination, base, expected):
        op = mw(rva)
        require(op >> 12 == 5 and (op >> 8) & 15 == destination and (op >> 4) & 15 == base)
        require(4 * (op & 15) == expected)

    def mt(rva, source, base, expected):
        op = mw(rva)
        require(op >> 12 == 1 and (op >> 8) & 15 == base and (op >> 4) & 15 == source)
        require(4 * (op & 15) == expected)

    def mg(rva, store, expected):
        op = mw(rva)
        require(op >> 8 == (192 if store else 196) and op & 255 == expected)

    mi(0x3920, 4, 28)
    mi(0x39b0, 2, 3)
    ms(0x39c0, 4, 0x150)
    for at, offset in ((0x39d4, 12), (0x39d6, 16), (0x3a08, 28), (0x3a0a, 32)):
        mt(at, 0, 9, offset)
    for at, offset in ((0x39da, 20), (0x39dc, 21), (0x39de, 22), (0x39e0, 23)):
        mg(at, True, offset)
    mt(0x2f2c, 6, 8, 28)
    mt(0x2f3c, 5, 8, 16)
    mg(0x2f30, True, 21)
    mg(0x2f38, True, 22)
    mt(0x2f5c, 5, 8, 32)
    mt(0x2f5e, 2, 8, 12)
    mi(0x2f64, 0, 20)
    mg(0x6d7e, True, 23)
    ml(0x6dba, 6, 8, 16)
    ml(0x6dbe, 4, 8, 12)
    mi(0x7030, 0, 23)
    ml(0x703a, 4, 8, 12)
    mi(0x70d4, 0, 21)
    ml(0x2e7c, 10, 9, 28)
    ml(0x2ea6, 5, 9, 12)
    ml(0x2ea8, 7, 9, 16)
    mt(0x2eb0, 2, 9, 12)
    for at, offset in ((0x2ec0, 16), (0x2ec4, 32), (0x2ec6, 28)):
        mt(at, 1, 9, offset)
    ms(0x2ed2, 3, 0x130)
    out = dict(schema='kui.vc2.runtime.geometry.v1', status='reviewed-source-metadata-matched',
               runtime_admitted=False, patching_admitted=False,
               source=dict(bytes=len(data), sha256=digest(data)),
               geometry=dict(KData=hex(kdata), current_process=hex(current_process),
                             current_thread=hex(current_thread), process_table='0x8c1de6e0',
                             process_count=32, process_stride=0x9c, process_slot_offset=12,
                             process_handle_offset=8, TTB_register='0xff000008',
                             slot_shift=25, block_shift=16, block_mask=511,
                             page_shift=12, page_mask=15, MEMBLOCK_page_offset=12,
                             committed_page_mask=1), contracts=contracts,
               maple_static_geometry=dict(extension_bytes=0x1a0,
                   extension_allocation_import='COREDLL ordinal1095 operator new(unsigned int)',
                   extension_allocation_size_load_rva='0x16e6', root_holder_rva='0x82b8',
                   shutdown_word=0, TX_physical=0x84, TX_virtual=0x88,
                   final_descriptor=0x8c, descriptor_end=0x90, mapped_Maple_base=0x100,
                   RX_allocator_context=0xa8, RX_physical_low=0xc0, RX_physical_high=0xc4,
                   RX_virtual=0xcc, RX_physical_minus_virtual=0xd8,
                   RX_pool_bytes=0x2000, TX_pool_bytes=0x1000,
                   completion_notification=0x15c, completion_event=0x160,
                   unit_function_static_contract_review_complete=True),
               maple_RX_ownership=dict(unit_bytes=28, function_bytes=0x150,
                   unit_pointer_offset='4+port*32+unit_index*4', function_count_byte=8,
                   function_count_max=3, function_pointer_offset='12+function_index*4',
                   function_fields=dict(dispatch=0, RX_virtual=12, requested_capacity=16,
                       command=20, expected_response=21, payload_words=22, included=23,
                       pending_IRP=28, explicit_RX_physical=32, queued_requests=0x130),
                   bucket_sizes=[32, 64, 256, 512, 1024], strict_greater_than_thresholds=[32, 64, 256, 512],
                   initial_discovery_capacity=116, initial_discovery_excluded=True,
                   generic_request_capacity='zero_extend(request_byte7)*4',
                   active_raw_request_RVA='0x2f18', completion_RVA='0x2e64',
                   completion_can_advance_queued_request=True,
                   requires_publication_before_original_completion=True,
                   pointer_may_survive_original_completion=False,
                   runtime_lifetime_admitted=False),
               limits=['Exact-owner static geometry only; no live mappings or source memory is changed.',
                       'A translated physical address does not prove current owner, lifetime, native permissions, coherent cache data or a DMA lease.',
                       'No retail bytes or disassembly are emitted. No VMU card is advertised or read.'])
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kernel', type=Path, metavar='0WINCEOS.BIN')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check', type=Path)
    args = parser.parse_args()
    try:
        if args.output and args.output.resolve() == args.kernel.resolve():
            raise ROMError('Metadata output must not overwrite the owner input')
        result = audit(args.kernel)
        if args.check and json.loads(args.check.read_text()) != result:
            raise ROMError('Checked-in VC2 runtime geometry differs from owner audit')
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + '\n')
        elif args.check:
            print(json.dumps(dict(matched=True, source_contracts=len(result['contracts']),
                                  runtime_admitted=False, owner_bytes_exported=False)))
        else:
            print(json.dumps(result, indent=2))
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
