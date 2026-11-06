#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit exact Swirl NK/Maple geometry without executing or changing owner code.

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
from swirl_ce_vmu_owner_audit import SwirlSourceROM

WINDOWS = (('process_switch', 8472, 56, '88b7b2e14f7058663b8c43a8dda6153e5cd848211caeeec929af2d8109bb64c9'),
 ('current_thread_API_return', 8790, 102, 'faee040248e752eb4f9e883c9ee3c9dcd107da7e51d66a98250ff338a5b8c7ec'),
 ('ordinary_page_walk', 9628, 160, '20f57397ea5ceab580e7fd4cd4a84cccd0a7536bad369c57e35763132253ed34'),
 ('process_geometry', 23646, 58, '6f617a33b7f9d871dcb35401abca2d575afdacf10539e25aaf515b55286268a8'),
 ('MapPtrToProcess', 23704, 144, '6053bda6dc01067f0cded72515490fae4dc488e6b9456cc1998304b70b19c06a'),
 ('SetKMode', 56508, 36, '31540e29b7f804439612503b6e63198d13aa7f836a5c1a4fe1ae6e3469fbc0a5'))
MAPLE_WINDOWS = (('root_allocation', 5848, 276, '9c6172aac5511d40d140cdad61529046bac200071c0da24e71bc1d0a80f30b66'),
 ('root_initialization', 6712, 204, '5c5e73c8829c42c30ba4c29d28b218fd019f8fc01606af55021b186b99c670bc'),
 ('DMA_initialization', 7632, 608, '19495da1f2649c0cbee023631cca149f51aae13132322a23061557cc47b3175e'),
 ('list_builder', 27832, 512, '655f7103f0b242f73992424487c56aa707240c12154ca2aa52114fd7b6ad4174'),
 ('RX_allocator', 29480, 100, '6c37a45279c7edae5ba1449dfc37888a74df083d406f0f00429884cfa549e85e'),
 ('completion', 28568, 616, '9b1b6321bf52dcbee740ab3fa0674b12c79c07fd1f83084401fdbd38397d9009'),
 ('raw_active_request', 12056, 96, '4feaf596cf193900df7db1acd27844b9532b8f6996a257ee71f891a4851c4ce0'),
 ('raw_request_queue', 12696, 144, '61cb574e4662baee030df7d2e4f284fce3ba0fce69eea8089b04396ef6855547'),
 ('generic_completion_and_queue_advance',
  11876,
  180,
  '252bf17a43ae5ef9c720240c9153712260991ddaf67bbfed76764a15d5635855'),
 ('unit_function_factory', 14572, 372, '6af241a50fdb53900ec3e3485de15cbae94a20a2b2b67edbb2e9be41510efa8c'),
 ('device_info_transition', 15680, 384, '73a646d27dfd6b74eaefa6c6a86782dc4ca9d2c26b068950dba26772c4272cab'),
 ('attached_unit_discovery', 28344, 224, '9bf940c45d92e3f639c0d9a3a562a21ff5859bf1c90cb71ade4fb8e9eab239aa'),
 ('RX_buckets', 29760, 496, 'b8dda5cb8ac427d6a40f4e36013a1abcd031e501a117522a7b081b15154e686e'),
 ('RX_free', 30256, 268, '50bfd57cb77cbf6b49a173f42b8313ddd2dfd2b5ccdac8053e24e6bed2dc266b'),
 ('RX_pages', 30524, 64, 'ac945cba310f4fee582169eb05782efdf017306c6ccd0847196bd1d3609f92e2'))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, label='Reviewed source scalar'):
    if not condition:
        raise ROMError(label + ' differs from exact Swirl source')


def audit(kernel):
    rom = SwirlSourceROM(kernel)
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
    require(kdata == 0x8c1d1800)
    require(immediate(0x2122, 0, 7) == 120)
    publish = word(0x2128)
    require(publish >> 12 == 1 and (publish >> 8) & 15 == 0 and (publish >> 4) & 15 == 1)
    current_process = kdata + 12 + 120 + 4 * (publish & 15)
    require(current_process == 0x8c1d1890)
    current_thread = literal(0x2260, 0)
    require(current_thread == kdata + 0x94 == 0x8c1d1894)
    require(literal(0xdcbc, 1) == kdata)
    sm = word(0xdcbe)
    require(sm >> 12 == 9 and (sm >> 8) & 15 == 0)
    require(word(0xdcbe + 4 + 2 * (sm & 255)) == 0x94)
    require(literal(0x5c6c, 4) == 0x8c1da6e0)
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
    require(rom.rva_u32(maple, 0x7784) == 0x01ce8004)
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

    mi(0x3934, 4, 28)
    mi(0x39c4, 2, 3)
    ms(0x39d4, 4, 0x150)
    for at, offset in ((0x39e8, 12), (0x39ea, 16), (0x3a1c, 28), (0x3a1e, 32)):
        mt(at, 0, 9, offset)
    for at, offset in ((0x39ee, 20), (0x39f0, 21), (0x39f2, 22), (0x39f4, 23)):
        mg(at, True, offset)
    mt(0x2f2c, 6, 8, 28)
    mt(0x2f3c, 5, 8, 16)
    mg(0x2f30, True, 21)
    mg(0x2f38, True, 22)
    mt(0x2f5c, 5, 8, 32)
    mt(0x2f5e, 2, 8, 12)
    mi(0x2f64, 0, 20)
    mg(0x6db2, True, 23)
    ml(0x6dee, 6, 8, 16)
    ml(0x6df2, 4, 8, 12)
    mi(0x7064, 0, 23)
    ml(0x706e, 4, 8, 12)
    mi(0x7108, 0, 21)
    ml(0x2e7c, 10, 9, 28)
    ml(0x2ea6, 5, 9, 12)
    ml(0x2ea8, 7, 9, 16)
    mt(0x2eb0, 2, 9, 12)
    for at, offset in ((0x2ec0, 16), (0x2ec4, 32), (0x2ec6, 28)):
        mt(at, 1, 9, offset)
    ms(0x2ed2, 3, 0x130)
    out = dict(schema='kui.swirl.runtime.geometry.v1', status='reviewed-source-metadata-matched',
               runtime_admitted=False, patching_admitted=False,
               source=dict(bytes=len(data), sha256=digest(data)),
               geometry=dict(KData=hex(kdata), current_process=hex(current_process),
                             current_thread=hex(current_thread), process_table='0x8c1da6e0',
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
            raise ROMError('Checked-in Swirl runtime geometry differs from owner audit')
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
