#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce exact-owner CE Maple mode and text-tail reservation evidence.

Output is metadata: identities, locations, lengths, hashes and independently
reviewed findings. Owner instructions, binaries and disassembly are not output.
A source contract does not authorize an unvalidated runtime hook installation.
"""
import argparse
import json
from pathlib import Path
import struct
import zlib

from armada_ce_maple_audit import identity, pe_highlow_targets, relocated_text, sha
from armada_ce_rom_audit import ROM
from armada_pe_audio_audit import PE

CONTRACTS = (
    ('maple.dll', 'regular_thread_creation', 0x1b80, 0x1bb4,
     'Calls stub0x6344 twice with security0, stack0, start0x183c or0x1854, extension argument, flags0 and threadId0. This is COREDLL CreateThread, not a proven kernel/system thread.'),
    ('maple.dll', 'main_thread_wrapper', 0x183c, 0x1854,
     'The regular thread start wrapper calls worker0x5d4c. It contains no SetKMode call.'),
    ('maple.dll', 'create_thread_import_stub', 0x6344, 0x6350,
     'Tail-jump import stub loads IAT RVA0x7034, COREDLL ordinal492 CreateThread.'),
    ('maple.dll', 'set_kernel_mode_import_stub', 0x6374, 0x6380,
     'Tail-jump import stub loads IAT RVA0x7044, COREDLL ordinal630 SetKMode.'),
    ('maple.dll', 'worker_completion_call', 0x5d4c, 0x5dd6,
     'Ordinary worker calls completion0x5b0c with the extension pointer after status bit0x1000. It does not call SetKMode before that completion. Existing initialization SetKMode calls are not evidence of persistent worker mode.'),
    ('maple.dll', 'completion_entry_t_dead', 0x5b0c, 0x5b28,
     'Entry pushes r8..r13 and PR, moves/adds/loads metadata, then first condition consumer follows TST r2,r2 at0x5b24. Incoming T is dead at this entry; a bridge may replay the displaced pushes and resume the unchanged body without preserving incoming T.'),
    ('maple.dll', 'temporary_initialization_kernel_mode', 0x1ef4, 0x1f08,
     'An initialization path calls SetKMode(1), retaining the returned previous mode in r4 for its later restore path. This is outside the regular thread wrapper.'),
    ('maple.dll', 'initialization_mode_restore', 0x214a, 0x2158,
     'Calls SetKMode with the retained old mode before returning from initialization.'),
    ('coredll.dll', 'CreateThread_syscall', 0x8d20, 0x8d28,
     'CreateThread export492 jumps to syscall0xfffffd95, Win32 method54.'),
    ('coredll.dll', 'SetKMode_syscall', 0x8c44, 0x8c58,
     'SetKMode export630 saves PR and reserves a16-byte argument area, calls syscall0xfffffd29 (Win32 method108), then restores PR and returns the old mode Boolean.'),
    ('coredll.dll', 'ThreadBaseFunc', 0x93e4, 0x9434,
     'ThreadBaseFunc export13 performs DLL/debug notifications and calls the provided start function. No SetKMode is performed by this wrapper.'),
    ('nk.exe', 'CreateThread_method54', 0x7a40, 0x7af0,
     'Regular CreateThread calls thread creation helper0x7850. Its argument at caller stack+24 is explicitly zero, unlike separate internal kernel-thread paths.'),
    ('nk.exe', 'regular_thread_context_call', 0x7850, 0x7970,
     'The regular path uses the global ThreadBaseFunc and forwards mode argument0 into context initializer0x3e40. A separate creation flag0x40000000 selects a different start path; MAPLE passes flags0.'),
    ('nk.exe', 'initial_thread_SR_mode', 0x3e40, 0x3ef0,
     'Initial context SR at thread+0xb4 starts0x8000. Instructions0x3e9c and0x3eca..0x3eda clear its top two bits, then OR0x40000000 only when the forwarded mode argument is nonzero. The regular MAPLE CreateThread path therefore initializes MD0.'),
    ('nk.exe', 'resolve_ThreadBaseFunc_global', 0x89b2, 0x89be,
     'Resolves the UTF16 ThreadBaseFunc export name and publishes it at global0x8c14d4e0, the global used by the ordinary thread creation path.'),
    ('nk.exe', 'SetKMode_method108', 0xdcd0, 0xdcf4,
     'Finds current thread via KData+0x94, then its callstack pointer at+24. Returns whether callstack+8 was64, writes64 for true input or0 for false input. This modifies the mode used by the ordinary API return path.'),
    ('nk.exe', 'API_entry_saved_mode', 0x2210, 0x2242,
     'Captures previous SSR.MD as byte value0 or64 and passes the saved entry frame to the API dispatcher.'),
    ('nk.exe', 'API_frame_mode_publication', 0x5574, 0x5590,
     'Copies the saved entry mode to new callstack+8 and publishes that callstack pointer at current thread+24, matching the fields changed by SetKMode.'),
    ('nk.exe', 'API_return_mode_capture', 0x5950, 0x597c,
     'Loads current thread callstack+8 into the saved return-mode value and captures its return address.'),
    ('nk.exe', 'API_return_mode_publication', 0x5a08, 0x5a28,
     'For ordinary small mode values0/64, writes the return-mode value into the entry return frame and returns the captured CE return address.'),
    ('nk.exe', 'API_return_mode_branch', 0x2286, 0x2294,
     'Nonzero saved mode returns directly by JMP while execution remains in kernel mode. Zero mode branches to the SSR/RTE user-return path.'),
    ('nk.exe', 'API_user_RTE_return', 0x2256, 0x2272,
     'Loads the saved zero mode into SSR (with conditional FPU-disable0x8000), then RTE restores user mode at the CE return address. Thus restoring old mode0 must finish in a CE-mapped bridge, not P2.'),
)


def hx(value):
    return f'0x{value:08x}'


def window(rom, module, name, start, end, finding):
    m = rom.modules[module]
    at = rom.rva_address(m, start, end-start)
    contents = rom.rva_read(m, start, end-start)
    return dict(id=name, module=module, rva=hx(start), end_rva_exclusive=hx(end),
        bytes=len(contents), rom_storage_address=hx(at),
        kernel_file_offset=hx(rom.address_offset(at, len(contents))),
        sha256=sha(contents), crc32_ieee=f'{zlib.crc32(contents):08x}', finding=finding)


def region(rom, name, address, size):
    contents = rom.address_read(address, size)
    return dict(id=name, address=hx(address), kernel_file_offset=hx(rom.address_offset(address,size)),
        bytes=size, sha256=sha(contents), crc32_ieee=f'{zlib.crc32(contents):08x}')


def require(value, expected, label):
    if value != expected:
        raise ValueError(label + ' differs from reviewed source contract')


def audit(startup_dir):
    root = Path(startup_dir)
    identities = [identity(root/name, name) for name in ('0WINCEOS.BIN','WINCE/MAPLE.DLL')]
    rom = ROM(root/'0WINCEOS.BIN')
    pe = PE(root/'WINCE/MAPLE.DLL')
    module = rom.modules['maple.dll']
    equivalence = relocated_text(rom, 'maple.dll', pe)
    imports = {s['iat_rva']:s for d in pe.imports() if d['dll'].lower()=='coredll.dll' for s in d['symbols']}
    require(imports[0x7034]['ordinal'],492,'MAPLE CreateThread import')
    require(imports[0x7044]['ordinal'],630,'MAPLE SetKMode import')
    exports = {e['ordinal']:e for e in rom.exports(rom.modules['coredll.dll'])}
    for ordinal,name,rva in ((492,'CreateThread',0x8d20),(630,'SetKMode',0x8c44),(13,'ThreadBaseFunc',0x93e4)):
        require((exports[ordinal]['name'],exports[ordinal]['rva']),(name,rva),'COREDLL export'+str(ordinal))
    for method,target in ((54,0x8c017a40),(108,0x8c01dcd0)):
        require(struct.unpack('<I',rom.address_read(0x8c0118e0+method*4,4))[0],target,'Win32 method'+str(method))
    require(rom.address_read(0x8c0116f0,30),'ThreadBaseFunc\0'.encode('utf-16le'),'ThreadBaseFunc lookup name')
    text=module['sections'][0]
    require((module['toc_address'],module['e32_address'],module['o32_address']),
        (0x8c141614,0x8c099b70,0x8c099bd4),'MAPLE ROM metadata locations')
    require(tuple(text[k] for k in ('virtual_bytes','rva','stored_bytes','data_address','real_address_field','flags')),
        (0x57a2,0x1000,0x5800,0x8c0af000,0x03dd1000,0x60000020),'MAPLE text O32 record')
    require(module['virtual_bytes'],0xa000,'MAPLE module span')
    tail_start,tail_end=0x67a4,0x6800
    tail=rom.rva_read(module,tail_start,tail_end-tail_start)
    require(any(tail),False,'Aligned text tail zero padding')
    raw_tail=pe.read(pe.rva_offset(tail_start,len(tail)),len(tail))
    require(raw_tail,tail,'ROM/PE tail padding')
    relocation_targets=pe_highlow_targets(pe,'maple.dll')
    overlaps=sorted(t for t in relocation_targets if t<tail_end and t+4>tail_start)
    require(overlaps,[],'Tail padding HIGHLOW overlap')
    reference_begin,reference_end=0x67a2,0x6800
    absolute_references=[]
    for section in pe.sections:
        if section['name']=='.reloc':
            continue
        contents=pe.read(section['raw_offset'],section['raw_bytes'])
        for offset in range(max(0,len(contents)-3)):
            value=struct.unpack_from('<I',contents,offset)[0]
            if pe.image_base+reference_begin<=value<pe.image_base+reference_end:
                absolute_references.append(section['rva']+offset)
    require(absolute_references,[],'Tail absolute-pointer reference scan')
    text_contents=pe.read(pe.rva_offset(text['rva'],text['stored_bytes']),text['stored_bytes'])
    branch_references=[]
    for offset in range(0,len(text_contents)-1,2):
        word=struct.unpack_from('<H',text_contents,offset)[0]
        pc=text['rva']+offset
        if word&0xf000 in (0xa000,0xb000):
            displacement=word&0xfff
            if displacement&0x800: displacement-=0x1000
        elif word&0xff00 in (0x8900,0x8b00,0x8d00,0x8f00):
            displacement=word&0xff
            if displacement&0x80: displacement-=0x100
        else:
            continue
        target=pc+4+displacement*2
        if reference_begin<=target<reference_end:
            branch_references.append(dict(instruction_rva=hx(pc),target_rva=hx(target)))
    require(branch_references,[],'Tail direct-branch reference scan')
    all_tail_relocations=sorted(t for t in relocation_targets if t<reference_end and t+4>reference_begin)
    require(all_tail_relocations,[],'Original text-end padding HIGHLOW overlap')
    contracts=[window(rom,*spec) for spec in CONTRACTS]
    guard_regions=[region(rom,'ROMHDR',rom.header_address,84),
        region(rom,'MAPLE_TOC',module['toc_address'],32),
        region(rom,'MAPLE_E32',module['e32_address'],100),
        region(rom,'MAPLE_text_O32',module['o32_address'],24),
        region(rom,'completion_entry32',rom.rva_address(module,0x5b0c,32),32),
        region(rom,'aligned_text_tail',rom.rva_address(module,tail_start,len(tail)),len(tail))]
    return dict(format='armada-ce-maple-hook-owner-contracts-v1',license='GPL-3.0-only',
        scope='Metadata-only exact-owner source evidence. No owner code bytes or disassembly are emitted. Source findings are not proof of runtime installation, allocation lifetime, IRQ ownership, universal compatibility or successful save emulation.',
        source_identities=identities,external_text_equivalence=equivalence,source_contracts=contracts,
        mode_contract=dict(regular_MAPLE_worker_initial_MD=0,
            regular_thread_creation='MAPLE1b80 -> stub6344/IAT7034 -> COREDLL492/8d20 -> fffffd95 -> NK54/7a40 ->7850 ->3e40',
            worker_start='COREDLL ThreadBaseFunc93e4 -> MAPLE183c ->5d4c ->5b0c',
            mode_bridge='MAPLE6374/IAT7044 -> COREDLL630/8c44 -> fffffd29 -> NK108/dcd0 -> ordinary API return',
            SetKMode_true_returns_MD=1,SetKMode_returns_old_mode_Boolean=True,
            restore_old_zero_returns_MD=0,restore_zero_requires_CE_mapped_return_code=True,
            existing_initialization_calls_prove_worker_MD1=False,
            incoming_completion_T_live=False,
            incoming_T_reason='First T consumer follows TST r2,r2 atMAPLE5b24; replayed entry pushes do not consume incoming T.'),
        source_text_tail=dict(text_o32_record_address=hx(module['o32_address']),virtual_size_word_address=hx(module['o32_address']),
            original_virtual_bytes=hx(text['virtual_bytes']),rva=hx(text['rva']),stored_bytes=hx(text['stored_bytes']),
            storage_address=hx(text['data_address']),real_address_field=hx(text['real_address_field']),flags=hx(text['flags']),
            module_virtual_bytes=hx(module['virtual_bytes']),aligned_padding_start_rva=hx(tail_start),aligned_padding_end_rva_exclusive=hx(tail_end),
            aligned_padding_bytes=len(tail),all_padding_bytes_zero=True,external_PE_padding_equal=True,HIGHLOW_overlap_rvas=[],
            reviewed_padding_reference_span=dict(start_rva=hx(reference_begin),end_rva_exclusive=hx(reference_end)),
            stored_section_unaligned_absolute_pointer_references=[],
            absolute_scan_excluded_sections=['.reloc'],
            text_halfword_direct_branch_references=[],
            original_padding_HIGHLOW_overlap_rvas=[],
            negative_reference_scan_limitation='Conservative raw scans of stored sections and direct branch encodings, not a complete indirect runtime reachability proof.',
            reservation_rule='A bridge placed in this padding must be bounded by the stored text end and its end included in the O32 virtual size before CE copies/maps it. A zero source tail is not an executable reservation until the stage validates the exact original metadata and commits its own bounded reservation.'),
        stage_guard_regions=guard_regions,
        limitations=[
            'Observed GD callback MD1 cannot be transferred to the separately created MAPLE worker thread.',
            'The mode findings describe this exact source. A bridge must validate the runtime module instance, import target and original bytes before using them.',
            'A SetKMode bridge must preserve the required ABI registers, PR and stack argument area and restore old mode while still executing CE-mapped code.',
            'Two matching headers and repeated mappings do not establish DMA idleness or a lifetime lease.',
            'Text-tail source geometry alone does not exclude every possible indirect runtime reference; exact reviewed source, relocation/reference review and stage admission remain required.',
            'These contracts do not connect the native virtual-A2 backend or authorize changing any save, response or physical card.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup-dir',required=True,type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--check',type=Path)
    args=parser.parse_args()
    try:
        inputs=[(args.startup_dir/name).resolve() for name in ('0WINCEOS.BIN','WINCE/MAPLE.DLL')]
        if args.output and args.output.resolve() in inputs:
            raise ValueError('Metadata output must not replace owner input')
        result=audit(args.startup_dir)
        if args.check and json.loads(args.check.read_text())!=result:
            raise ValueError('Checked-in metadata differs from exact owner audit')
        if args.output:
            args.output.write_text(json.dumps(result,indent=2)+'\n')
        elif args.check:
            print(json.dumps(dict(matched=True,source_files=len(result['source_identities']),contracts=len(result['source_contracts']),executable_bytes_exported=False)))
        else:
            print(json.dumps(result,indent=2))
    except (OSError,ValueError,UnicodeError,struct.error,KeyError) as error:
        parser.exit(1,'Hook owner audit failed: '+str(error)+'\n')


if __name__=='__main__':
    main()
