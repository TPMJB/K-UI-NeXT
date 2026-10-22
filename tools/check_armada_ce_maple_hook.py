#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pin authored CE-mode bridge/P2 context leaf and audit their small ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

BRIDGE = 0x01DD67A4
HOOK = 0xACFCF000
LOG = 0xACFCFE00
BRIDGE_BYTES = bytes.fromhex(
    '062f162f262f362f462f562f662f762f224f024f124f134f01e4d9bd0900062ff854'
    'f3653475f456f2670bd00b400900f664cdbd0900174f164f064f264ff667f666f665'
    'f664f663f662f661f660862f962f8ba90900090000f0fcac')
HOOK_BYTES = bytes.fromhex(
    '02033360f0cb0e4019d00151ffe22031018901711110025120e22231248901711210'
    '14d2222020e22310741001e2203102893c7002a001e2147002e2422051106210331074'
    '100cd1126115100bd1126116100bd1126117100ad11261181000e1191028701042e98b'
    '0e430b000900090000fefcac4d484b31080000ff000000ff1c0000ff9058148c')
EH = struct.Struct('<16sHHIIIIIHHHHHH')
SH = struct.Struct('<10I')
PH = struct.Struct('<8I')
SYM = struct.Struct('<IIIBBH')


def require(test, message):
    if not test:
        raise ValueError(message)


def read_section_elf(data):
    require(len(data) >= EH.size, 'Hook ELF header truncated')
    ident, kind, machine, version, entry, phoff, shoff, _, ehsize, phsize, phnum, shsize, shnum, shstr = EH.unpack_from(data)
    require(ident[:7] == b'\x7fELF\x01\x01\x01' and kind == 2 and machine == 42 and version == 1,
            'Hook requires static little-endian SH ELF')
    require(entry == HOOK and ehsize == EH.size and phsize == PH.size and phnum == 2 and
            shsize == SH.size and 1 <= shnum <= 32 and shstr < shnum, 'Hook ELF geometry changed')
    require(phoff + phnum * PH.size <= len(data) and shoff + shnum * SH.size <= len(data), 'Hook ELF tables truncated')
    raw = [SH.unpack_from(data, shoff + i * SH.size) for i in range(shnum)]
    name_table = raw[shstr]
    require(name_table[4] + name_table[5] <= len(data), 'Hook section names truncated')
    strings = data[name_table[4]:name_table[4]+name_table[5]]
    sections = {}
    for row in raw:
        name, typ, flags, address, offset, size, link, info, alignment, entsize = row
        require(name < len(strings), 'Hook section name invalid')
        end = strings.find(b'\0', name)
        require(end >= 0, 'Hook section name unterminated')
        name = strings[name:end].decode('ascii')
        require(name not in sections, 'Duplicate hook section')
        require(typ not in (4, 6, 9), 'Relocation/dynamic hook section is forbidden')
        require(typ == 8 or offset + size <= len(data), 'Hook section leaves file')
        sections[name] = dict(type=typ, flags=flags, address=address, size=size, link=link,
                              info=info, alignment=alignment, entsize=entsize,
                              offset=offset, data=b'' if typ == 8 else data[offset:offset+size])
    allocated = {n: s for n,s in sections.items() if s['flags'] & 2}
    require(set(allocated) == {'.bridge', '.hook', '.log'}, 'Unexpected allocated hook section')
    for name, address, size, typ, flags in [('.bridge',BRIDGE,92,1,6),('.hook',HOOK,136,1,6),('.log',LOG,120,8,3)]:
        s = allocated[name]
        require((s['address'],s['size'],s['type'],s['flags']) == (address,size,typ,flags), name+' layout changed')
    require(sections['.bridge']['data'] == BRIDGE_BYTES, 'Complete bridge bytes differ')
    require(sections['.hook']['data'] == HOOK_BYTES, 'Complete stackless P2 leaf bytes differ')
    segments = [PH.unpack_from(data, phoff + i * PH.size) for i in range(phnum)]
    require([(p[0],p[2],p[3],p[4],p[5],p[6]) for p in segments] ==
            [(1,BRIDGE,BRIDGE,92,92,5),(1,HOOK,HOOK,136,LOG+120-HOOK,7)], 'Hook load segments changed')
    for p in segments:
        require(p[1]+p[4] <= len(data), 'Hook load segment leaves file')
    require(data[segments[0][1]:segments[0][1]+92] == BRIDGE_BYTES and
            data[segments[1][1]:segments[1][1]+136] == HOOK_BYTES, 'Hook segment bytes differ from sections')
    symtab = sections.get('.symtab')
    require(symtab is not None and symtab['entsize'] == SYM.size and symtab['size'] % SYM.size == 0,
            'Hook symbol table missing')
    require(0 <= symtab['link'] < len(raw), 'Hook symbol string index invalid')
    names = raw[symtab['link']]
    require(names[1] == 3 and names[4]+names[5] <= len(data), 'Hook symbol strings invalid')
    name_bytes = data[names[4]:names[4]+names[5]]
    symbols = {}
    for at in range(0,symtab['size'],SYM.size):
        name, value, size, info, other, index = SYM.unpack_from(symtab['data'],at)
        require(at == 0 or index != 0, 'Unresolved hook symbol')
        if name:
            end = name_bytes.find(b'\0',name)
            require(end>=0,'Hook symbol name invalid')
            symbols[name_bytes[name:end].decode('ascii')] = value
    required = {'_kui_armada_ce_maple_bridge':BRIDGE,'__kui_maple_bridge_end':BRIDGE+92,
                '_kui_armada_ce_maple_hook':HOOK,'__kui_maple_hook_binary_end':HOOK+136,
                '_kui_armada_ce_maple_hook_log':LOG,'__kui_maple_set_kmode':0x01DD6374,
                '__kui_maple_resume':0x01DD5B10}
    require(all(symbols.get(k)==v for k,v in required.items()), 'Hook linked symbol contract changed')
    return sections


def check(directory):
    directory = Path(directory)
    sections = read_section_elf((directory/'hook.elf').read_bytes())
    require((directory/'bridge.bin').read_bytes() == sections['.bridge']['data'], 'Bridge binary differs from ELF')
    require((directory/'hook.bin').read_bytes() == sections['.hook']['data'], 'Hook binary differs from ELF')
    return dict(bridge_nominal_va=hex(BRIDGE),bridge_rva='0x67a4',bridge_bytes=len(BRIDGE_BYTES),
                bridge_sha256=hashlib.sha256(BRIDGE_BYTES).hexdigest(),helper_p2_entry=hex(HOOK),
                helper_bytes=len(HOOK_BYTES),helper_sha256=hashlib.sha256(HOOK_BYTES).hexdigest(),
                log_p2=hex(LOG),log_bytes=120,reserved_page_p1=['0x8cfcf000','0x8cfd0000'],
                complete_authored_code_pinned=True,unresolved_symbols=0,helper_stack_bytes=0,
                bridge_owned_stack_bytes=52,bridge_and_setkmode_wrapper_stack_bytes=68,
                sample_limit=32,visit_counter_saturates=True,
                guest_data_pointers_followed=False,maple_or_sd_io=False,virtual_vmu_enabled=False,
                api_calls=['exact MAPLE SetKMode thunk at module RVA0x6374; true then original mode'],
                mutable_write_domain='fixed owned stack slots and fixed 120-byte private log only',
                incoming_t='dead before original first TST/BT at RVA0x5b24/0x5b26',
                caveat='Source-specific API-mode/calling conventions and pre-NK reservation/patch audit are also required')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    try:
        print(json.dumps(check(args.directory),indent=2))
    except (OSError,ValueError,struct.error,UnicodeError) as error:
        parser.exit(1,'Maple hook audit failed: '+str(error)+'\n')

if __name__=='__main__':
    main()
