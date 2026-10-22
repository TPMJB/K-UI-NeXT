#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit exact owner-supplied Armada CE Maple files; emit metadata/hashes only.

Never extracts or patches executables. ROM storage, nominal virtual and live
addresses are distinct; this audit does not establish a runtime hook or lease.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

from armada_ce_rom_audit import KERNEL_BYTES, KERNEL_SHA256, ROM
from armada_pe_audio_audit import PE

IDENTITIES = {
    '0WINCEOS.BIN': (KERNEL_BYTES, KERNEL_SHA256),
    'WINCE/MAPLE.DLL': (26191, 'b8eb7afc2b13fcec8551af4fa75288fca1881f671181b88ea042aa9d77524e6e'),
    'WINCE/MAPLEDEV.DLL': (55381, '16dfc7b3bb6519af633e09e7886e36cbaf73c74ffc387c5c6d6369e26862ac28'),
}
CONTRACTS = (
    ('nk.exe', 'install_ce_vbr', 0x2150, 0x21a8, 'Installs VBR0x8c0120f0 at RVA0x2166.'),
    ('nk.exe', 'interrupt_dispatch', 0x26f0, 0x2790, 'CE handler-table dispatch and pending SYSINTR ring.'),
    ('nk.exe', 'asic_lifecycle', 0x1c564, 0x1c5ec, 'CE ASIC enable, disable and InterruptDone callbacks.'),
    ('nk.exe', 'interrupt_registration', 0x1c778, 0x1c8a0, 'Registers normal ASIC handler for INTEVT0x360.'),
    ('nk.exe', 'normal_asic_dispatch', 0x1d158, 0x1d1f0, 'Masks normal bits12/13 and returns SYSINTR18.'),
    ('maple.dll', 'map_registers', 0x1cae, 0x1cf2, 'Maps physical0x005f6c00 bytes0x100 into extension+0x100.'),
    ('maple.dll', 'hardware_initialize', 0x1e24, 0x1ea8, 'Sets MDTSEL1, MDSTAR common-buffer bus address and MDEN1.'),
    ('maple.dll', 'descriptor_builder', 0x582c, 0x5a22, 'Ordinary shared descriptor-list builder.'),
    ('maple.dll', 'completion_preamble', 0x5b0c, 0x5b18, 'Six register pushes; no branch or PC-relative instruction.'),
    ('maple.dll', 'completion_walker', 0x5b0c, 0x5d28, 'Processes physical response buffers before ordinary consumers.'),
    ('maple.dll', 'main_loop', 0x5d4c, 0x5e08, 'Writes mapped+0x88, waits SYSINTR18, completes and calls InterruptDone18.'),
    ('maple.dll', 'list_ready', 0x5d88, 0x5d94, 'Writes mapped+0x88 value1; includes PC-relative loads.'),
    ('maple.dll', 'special_descriptor_path', 0x5684, 0x582c, 'Separate special-descriptor path also writes mapped+0x88.'),
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def identity(path, relative):
    size, digest = IDENTITIES[relative]
    if path.stat().st_size != size:
        raise ValueError('Unsupported exact-owner file size: ' + relative)
    with path.open('rb') as source:
        data = source.read(size + 1)
    if len(data) != size or sha(data) != digest:
        raise ValueError('Unsupported exact-owner SHA256: ' + relative)
    return dict(file=relative, bytes=size, sha256=digest)


def pe_highlow_targets(pe, name):
    """Return bounded, nonoverlapping HIGHLOW targets, without changing bytes."""
    if len(pe.directories) <= 5:
        raise ValueError('Missing PE relocation directory: ' + name)
    rva, count = pe.directories[5]
    if not rva or not count or count > len(pe.data) or rva > pe.image_bytes - count:
        raise ValueError('Invalid PE relocation directory bounds: ' + name)
    reloc = pe.read(pe.rva_offset(rva, count), count)
    cursor = 0
    targets = set()
    while cursor < count:
        if count - cursor < 8:
            raise ValueError('Truncated PE relocation block: ' + name)
        page, block = struct.unpack_from('<II', reloc, cursor)
        if page & 0xfff or page >= pe.image_bytes or block < 8 or block & 3 or block > count - cursor:
            raise ValueError('Invalid PE relocation block: ' + name)
        for at in range(cursor + 8, cursor + block, 2):
            entry = struct.unpack_from('<H', reloc, at)[0]
            kind, target = entry >> 12, page + (entry & 0xfff)
            if kind == 0:
                continue
            if kind != 3 or target > pe.image_bytes - 4 or any(target + d in targets for d in range(-3, 4)):
                raise ValueError('Invalid, duplicate or unsupported PE relocation: ' + name)
            pe.rva_offset(target, 4)
            targets.add(target)
        cursor += block
    return targets


def relocated_text(rom, name, pe):
    module = rom.modules[name]
    sections = [s for s in module['sections'] if s['flags'] & 0x20000000]
    if len(sections) != 1:
        raise ValueError('Expected exactly one ROM executable section: ' + name)
    section = sections[0]
    start, size = section['rva'], section['stored_bytes']
    if section['compressed'] or not 0 < section['virtual_bytes'] <= size:
        raise ValueError('Expected complete uncompressed ROM text: ' + name)
    original = rom.rva_read(module, start, size)
    normalized = bytearray(pe.read(pe.rva_offset(start, size), size))
    targets = pe_highlow_targets(pe, name)
    text_fixups = 0
    for target in targets:
        if target < start + size and target + 4 > start:
            if target < start or target + 4 > start + size:
                raise ValueError('PE relocation straddles text bounds: ' + name)
            offset = target - start
            value = struct.unpack_from('<I', normalized, offset)[0]
            value = (value + module['nominal_virtual_base'] - pe.image_base) & 0xffffffff
            struct.pack_into('<I', normalized, offset, value)
            text_fixups += 1
    if normalized != original:
        raise ValueError('Relocated external text differs from complete ROM text: ' + name)
    return dict(module=name, external_image_base=pe.image_base,
                rom_nominal_base=module['nominal_virtual_base'], relocation_kind='HIGHLOW',
                fixups=len(targets), text_fixups=text_fixups, compared_bytes=size,
                virtual_text_bytes=section['virtual_bytes'], text_sha256=sha(original),
                complete_stored_text_equal=True)


def copied_fields(rom, address, names, expected):
    size = len(names) * 4
    matches = [c for c in rom.copies if c['destination'] <= address and
               address + size <= c['destination'] + c['copied_bytes']]
    if len(matches) != 1:
        raise ValueError('IRQ fields do not identify one initialized ROM copy')
    source = matches[0]['source'] + address - matches[0]['destination']
    data = rom.address_read(source, size)
    values = struct.unpack('<' + 'I' * len(names), data)
    if values != expected:
        raise ValueError('Exact SYSINTR18 initialized fields changed')
    return dict(destination=address, source=source, file_offset=rom.address_offset(source, size),
                bytes=size, sha256=sha(data), fields=dict(zip(names, values)))


def check_probe_profile(modules, header=None):
    """Cross-check production constants against independently parsed ROM data."""
    if header is None:
        header = Path(__file__).resolve().parents[1] / 'include/kui/armada_ce_maple_observe.h'
    text = Path(header).read_text()
    expected = {}
    for module, prefix in zip(modules, ('KUI_ARMADA_CE_MAPLE', 'KUI_ARMADA_CE_MAPLEDEV')):
        expected[prefix + '_ROM_TOC'] = module['toc_address']
        expected[prefix + '_NOMINAL'] = module['nominal_virtual_base']
        expected[prefix + '_SPAN'] = module['virtual_bytes']
        if module['section_count'] != 3:
            raise ValueError('Probe section-count contract differs from owner ROM')
    for name, value in expected.items():
        matches = re.findall(r'^#define\s+' + re.escape(name) + r'\s+UINT32_C\((0x[0-9a-fA-F]+)\)\s*$', text, re.M)
        if len(matches) != 1 or int(matches[0], 16) != value:
            raise ValueError('Probe constant differs from independently parsed ROM: ' + name)
    return dict(constants=expected, matched=True)


def check_proof_profile(rom, pe, header=None):
    """Verify the reviewed proof window and root against owner source geometry.

    Expected RVAs and length come from the independently reviewed instruction
    paths, never from the production header. The root's reviewed PC-relative
    literal is checked in both sources. Its zero-filled .data tail is virtual
    storage: no file-backed or live root-value read is attempted here.
    """
    module = rom.modules['maple.dll']
    code_rva, code_bytes, expected_crc = 0x5b0c, 32, 0x7bd7ff55
    root_rva, root_literal_rva = 0x72b8, 0x1604
    section = rom.rva_section(module, code_rva, code_bytes)
    pe_sections = [s for s in pe.sections if s['characteristics'] & 0x20000000 and
                   s['rva'] <= code_rva and
                   code_rva + code_bytes <= s['rva'] + s['virtual_bytes']]
    if not section['flags'] & 0x20000000 or \
            code_rva + code_bytes > section['rva'] + section['virtual_bytes'] or len(pe_sections) != 1:
        raise ValueError('Proof code is outside one executable virtual section')
    targets = pe_highlow_targets(pe, 'maple.dll')
    overlaps = [target for target in targets
                if target < code_rva + code_bytes and target + 4 > code_rva]
    if overlaps:
        raise ValueError('Proof code overlaps PE HIGHLOW relocation')
    original = rom.rva_read(module, code_rva, code_bytes)
    external_offset = pe.rva_offset(code_rva, code_bytes)
    external = pe.read(external_offset, code_bytes)
    if external != original:
        raise ValueError('Proof fingerprint bytes differ between owner ROM and PE')
    crc = zlib.crc32(original) & 0xffffffff
    if crc != expected_crc:
        raise ValueError('Proof fingerprint CRC differs from reviewed owner source')
    if rom.rva_u32(module, root_literal_rva) != module['nominal_virtual_base'] + root_rva or \
            pe.rva_u32(root_literal_rva) != pe.image_base + root_rva:
        raise ValueError('Proof root literal differs from reviewed owner source')
    rom_data = [s for s in module['sections'] if s['flags'] & 0x80000000 and
                s['rva'] <= root_rva and root_rva + 4 <= s['rva'] + s['virtual_bytes']]
    pe_data = [s for s in pe.sections if s['name'] == '.data' and
               s['characteristics'] & 0x80000000 and s['rva'] <= root_rva and
               root_rva + 4 <= s['rva'] + s['virtual_bytes']]
    if len(rom_data) != 1 or len(pe_data) != 1 or \
            (rom_data[0]['rva'], rom_data[0]['virtual_bytes']) != \
            (pe_data[0]['rva'], pe_data[0]['virtual_bytes']):
        raise ValueError('Proof root is outside matching writable virtual data')
    if header is None:
        header = Path(__file__).resolve().parents[1] / 'include/kui/armada_ce_maple_proof.h'
    text = Path(header).read_text()
    expected = dict(KUI_ARMADA_CE_MAPLE_PROOF_CODE_RVA=code_rva,
                    KUI_ARMADA_CE_MAPLE_PROOF_CODE_BYTES=code_bytes,
                    KUI_ARMADA_CE_MAPLE_PROOF_CODE_CRC=crc,
                    KUI_ARMADA_CE_MAPLE_PROOF_ROOT_RVA=root_rva)
    for name, value in expected.items():
        number = r'(0x[0-9a-fA-F]+|[0-9]+)'
        matches = re.findall(r'^#define\s+' + re.escape(name) + r'\s+(?:UINT32_C\(' +
                             number + r'\)|' + number + r'[uUlL]*)[ \t]*$', text, re.M)
        if len(matches) != 1 or int(matches[0][0] or matches[0][1], 0) != value:
            raise ValueError('Proof constant differs from independently checked owner source: ' + name)
    storage = rom.rva_address(module, code_rva, code_bytes)
    return dict(constants=expected, matched=True,
        code=dict(rva=code_rva, bytes=code_bytes, crc32=crc, sha256=sha(original),
            rom_storage_address=storage, kernel_file_offset=rom.address_offset(storage, code_bytes),
            external_file_offset=external_offset, highlow_overlap_count=len(overlaps),
            exact_rom_external_bytes_equal=True),
        root=dict(rva=root_rva, bytes=4, reviewed_literal_rva=root_literal_rva,
            writable_data_rva=rom_data[0]['rva'], writable_data_virtual_bytes=rom_data[0]['virtual_bytes'],
            pe_file_backed=root_rva + 4 <= pe_data[0]['rva'] + pe_data[0]['raw_bytes'],
            scope='Original writable virtual data geometry, not a live mapping or root value'))


def audit(startup_dir):
    root = Path(startup_dir)
    inputs = [identity(root / name, name) for name in IDENTITIES]
    rom = ROM(root / '0WINCEOS.BIN')
    modules, equivalence, proof_profile = [], [], None
    for name in ('maple.dll', 'mapledev.dll'):
        relative = 'WINCE/' + name.upper()
        pe = PE(root / relative)
        if sha(pe.data) != IDENTITIES[relative][1]:
            raise ValueError('Owner file changed during audit: ' + relative)
        module = dict(rom.modules[name])
        module['section_count'] = len(module['sections'])
        modules.append(module)
        equivalence.append(relocated_text(rom, name, pe))
        if name == 'maple.dll':
            proof_profile = check_proof_profile(rom, pe)
    contracts = []
    for name, label, begin, end, description in CONTRACTS:
        row = rom.contract(name, label, begin, end, description)
        row['file_offset'] = rom.address_offset(row['rom_storage_address'], row['bytes'])
        contracts.append(row)
    first = rom.header['driver_globals_address']
    last = first + rom.header['driver_globals_bytes']
    return dict(format='armada-ce-maple-audit-v1', inputs=inputs,
        rom_header_address=rom.header_address, rom_header=rom.header,
        copy_records=rom.copies, modules=modules, probe_profile=check_probe_profile(modules),
        proof_profile=proof_profile,
        external_text_equivalence=equivalence,
        sysintr18_descriptor=copied_fields(rom, 0x8c145ed8,
            ('status_register', 'mask_register', 'all_bits', 'software_mask', 'latched', 'desired'),
            (0xa05f6900, 0xa05f6920, 0x3000, 0, 0, 0x3000)),
        sysintr18_callbacks=copied_fields(rom, 0x8c145fec,
            ('enable', 'disable', 'done'), (0x8c02c564, 0x8c02c594, 0x8c02c5c8)),
        contracts=contracts,
        interrupt_ownership=dict(vbr=0x8c0120f0, dispatch=0x8c0126f0,
            normal_intevt=0x360, handler_slot=0x8c145c70, handler=0x8c02d158,
            maple_sysintr=18, normal_bits=0x3000,
            conclusion='CE masks and rearms Maple completion through its own SYSINTR lifecycle.'),
        transport=dict(trigger_select=1, trigger_select_write_rva=0x1e62,
            main_list_ready_rva=0x5d88, main_write_call_rva=0x5d90,
            main_write_register=0x005f6c88, main_write_value=1, mdst_register=0x005f6c18,
            conclusion='VBlank-triggered DMA is configured; mapped+0x88 is not MDST+0x18. The native5 pre-MDST software-start contract does not apply.'),
        memory_conflict=dict(driver_globals_begin=first, driver_globals_end=last,
            native5_worker_begin=0x8cfd0000, native5_worker_end=0x8d000000,
            overlap_bytes=max(0, min(last, 0x8d000000) - max(first, 0x8cfd0000)),
            conclusion='The native5 top-RAM worker range overlaps CE driver globals; no CE VMU lease is established.'),
        limitations=['Storage addresses, TOC load addresses and nominal VAs are not observed live mappings.',
            'Text equivalence proves contracts by RVA, not which copy CE executes.',
            'Completion RVA0x5b0c is a static passive candidate; live backing, legal trampoline entry and caller state remain unproven.',
            'An idle MDST sample does not exclude a VBlank transfer during or after the sample.',
            'No executable bytes are exported; no runtime patch, register write or virtual reply is performed.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup-dir', required=True, type=Path)
    parser.add_argument('--output', type=Path, help='Metadata JSON destination; stdout by default')
    args = parser.parse_args()
    try:
        if args.output and any(args.output.resolve() == (args.startup_dir / name).resolve()
                               for name in IDENTITIES):
            raise ValueError('Metadata output must not replace an owner input')
        encoded = json.dumps(audit(args.startup_dir), indent=2) + '\n'
        if args.output:
            args.output.write_text(encoded, encoding='utf-8')
        else:
            print(encoded, end='')
    except (OSError, ValueError, UnicodeError, struct.error) as error:
        parser.exit(1, 'Audit failed: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
