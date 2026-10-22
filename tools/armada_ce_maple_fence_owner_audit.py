#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce metadata-only exact-owner evidence for a CE completion wrapper.

No owner executable bytes or disassembly are written. A reviewed source
contract is not proof of a live DMA fence, unload recovery, or save emulation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

from armada_ce_maple_hook_owner_audit import audit as audit_mode_tail, hx, region, require, window
from armada_ce_rom_audit import ROM
from armada_pe_audio_audit import PE
from armada_ce_maple_audit import pe_highlow_targets

WINDOWS = (
    ('maple.dll','root_lifecycle_wiring',0x1514,0x15e4,
     'DriverEntry creates one extension, initializes transport, publishes it and registers the root cleanup callback.'),
    ('maple.dll','root_shutdown_lifecycle',0x1934,0x1abe,
     'Cleanup sets extension shutdown word0 to1, signals the worker event, waits up to1000ms and may terminate an unresponsive worker before freeing transport state. A PRE/POST wrapper must not claim arbitrary concurrent root-unload recovery.'),
    ('maple.dll','initialize_before_worker_creation',0x1af8,0x1bb4,
     'Transport calls hardware initialization0x1e24 before creating its ordinary workers. Normal steady-state completion does not call this initializer.'),
    ('maple.dll','map_register_block',0x1cae,0x1cf2,
     'Hardware mapping starts from physical0x005f6c00, translates bus address and maps0x100 bytes into extension+0x100. A runtime fence must independently admit the actual live mapping before MMIO access.'),
    ('maple.dll','hardware_initialization_port_writes',0x1e24,0x1ea8,
     'Six WRITE_PORT_ULONG calls initialize mapped offsets14=0,8c=6155007f,80=c3501000,10=1,04=TX bus,14=1. The only two MDEN writes in this reviewed module call family are in initialization.'),
    ('maple.dll','generic_request_enqueue',0x2e60,0x2f1c,
     'Generic requests update function-object fields and extension rebuild flag11c. They do not enable DMA or build a new hardware list directly.'),
    ('maple.dll','generic_response_completion',0x2bd4,0x2c4c,
     'Ordinary completion copies received header/payload into the IRP output, frees the receive suballocation, clears request state and completes the IRP. A fence-only test must not modify this payload.'),
    ('maple.dll','completion_software_interrupt_signal',0x3930,0x3968,
     'The completion helper temporarily enters kernel mode and writes extension[118] OR01000000 to PVR/ASIC mask addressa05f8084. It does not access Maple MDEN or MDST.'),
    ('maple.dll','special_operation_port_write',0x5684,0x582c,
     'Special light-gun operation writes mapped+88. It is selected before ordinary completion by this same main worker, not concurrently inside the wrapped completion body.'),
    ('maple.dll','unmodified_completion_body',0x5b0c,0x5d28,
     'Ordinary response walker remains wholly unchanged. It pushes r8..r13/PR, handles responses and returns normally. The authored call wrapper must restore the previous CPU mode and ordinary CE stack before calling it.'),
    ('maple.dll','main_worker_sequence',0x5d4c,0x5dd6,
     'The worker handles special/rebuild, writes mapped+88, waits and checks normal interrupt bit1000. Only then it calls completion. InterruptDone18 and mapped+18 read follow the completion return.'),
    ('maple.dll','worker_call_and_interruptdone',0x5da8,0x5dc6,
     'The bit1000 branch skips directly to5db6 when no completion is needed. The original call uses extension r8 as r4 and PR5db6; code5db6 overwrites r0 then calls InterruptDone18, so the completion r0 result is not consumed.'),
    ('maple.dll','secondary_interrupt_worker',0x5e08,0x5e60,
     'The second worker handles SYSINTR19 and only sets extension rebuild flag11c when its status bit800 is seen. This window contains no Maple-port write.'),
    ('maple.dll','write_port_import_stub',0x6488,0x6494,
     'Tail-jump stub uses the WDMLIB WRITE_PORT_ULONG IAT at RVA70ac. All complete-text literal references to this stub and their direct r0 callsites are enumerated below.'),
)


def source_audit(startup_dir):
    baseline=audit_mode_tail(startup_dir)
    root=Path(startup_dir)
    rom=ROM(root/'0WINCEOS.BIN')
    pe=PE(root/'WINCE/MAPLE.DLL')
    module=rom.modules['maple.dll']
    nominal=module['nominal_virtual_base']
    require(nominal,0x01dd0000,'MAPLE nominal source base')
    imports=[s for d in pe.imports() if d['dll'].lower()=='wdmlib.dll' for s in d['symbols']]
    require([(s['iat_rva'],s.get('name')) for s in imports if s.get('name')=='WRITE_PORT_ULONG'],
            [(0x70ac,'WRITE_PORT_ULONG')],'WRITE_PORT_ULONG import')
    text=module['sections'][0]
    content=rom.rva_read(module,text['rva'],text['stored_bytes'])
    def word16(rva):
        return struct.unpack_from('<H',content,rva-text['rva'])[0]
    def word32(rva):
        return struct.unpack_from('<I',content,rva-text['rva'])[0]
    references=[text['rva']+i for i in range(len(content)-3)
                if struct.unpack_from('<I',content,i)[0]==nominal+0x6488]
    require(references,[0x1ea0,0x5808,0x5de4],'Complete-text WRITE_PORT thunk references')
    iat_references=[text['rva']+i for i in range(len(content)-3)
                    if struct.unpack_from('<I',content,i)[0]==nominal+0x70ac]
    require(iat_references,[0x6490],'Complete-text direct WRITE_PORT IAT references')
    loads=[]
    for offset in range(0,len(content)-1,2):
        word=struct.unpack_from('<H',content,offset)[0]
        if word&0xff00!=0xd000:
            continue
        pc=text['rva']+offset
        literal=(pc&~3)+4+4*(word&255)
        if text['rva']<=literal<=text['rva']+len(content)-4 and word32(literal)==nominal+0x6488:
            loads.append((pc,literal))
    expected=((0x1e38,0x1ea0,0x1e3c,0x14),(0x1e46,0x1ea0,0x1e4a,0x8c),
              (0x1e54,0x1ea0,0x1e58,0x80),(0x1e60,0x1ea0,0x1e62,0x10),
              (0x1e72,0x1ea0,0x1e74,0x04),(0x1e78,0x1ea0,0x1e7c,0x14),
              (0x56d4,0x5808,0x56d6,0x88),(0x5d8e,0x5de4,0x5d90,0x88))
    require(loads,[(a,b) for a,b,_,_ in expected],'All reviewed port-write literal loads')
    for load,literal,call,_ in expected:
        require(word16(call),0x400b,'WRITE_PORT r0 callsite'+hx(call))
        require(call-load in (2,4),True,'WRITE_PORT bounded load-to-call span')
    call_rva=0x5db0
    original=rom.rva_read(module,call_rva,32)
    original_word=struct.unpack_from('<I',original)[0]
    require(original_word,0x400bd013,'Original ordinary worker call word')
    require(word16(0x5db4),0x6483,'Original extension argument delay instruction')
    require(word32(0x5e00),nominal+0x5b0c,'Original completion target literal')
    require(word16(0x5dae),0x8902,'Original skip-completion BT')
    require(word16(0x5db6),0xd00c,'Original following InterruptDone target load')
    displacement=(0x67a4-(0x5db2+4))//2
    require(displacement,0x4f7,'Worker BSR displacement')
    patched_word=0x0009|((0xb000|displacement)<<16)
    require(patched_word,0xb4f70009,'Authored worker NOP/BSR word')
    patched=bytearray(original)
    struct.pack_into('<I',patched,0,patched_word)
    require(zlib.crc32(original),0xd48fad4a,'Original worker32 CRC')
    require(zlib.crc32(patched),0x004742ae,'Authored worker32 CRC')
    relocations=pe_highlow_targets(pe,'maple.dll')
    require(sorted(t for t in relocations if t<call_rva+4 and t+4>call_rva),[],
            'Worker instruction patch HIGHLOW overlap')
    entry=rom.rva_read(module,0x5b0c,32)
    require(zlib.crc32(entry),0x7bd7ff55,'Unmodified completion entry CRC')
    mode_digest=hashlib.sha256(json.dumps(baseline,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    return dict(format='armada-ce-maple-fence-owner-contracts-v1',license='GPL-3.0-only',
        scope='Metadata-only exact-source evidence for a pre-NK worker call wrapper. No owner executable bytes or disassembly are exported. This is not a runtime fence, DMA ownership, unwind recovery or save-emulation result.',
        source_identities=baseline['source_identities'],external_text_equivalence=baseline['external_text_equivalence'],
        reused_mode_and_tail_contract=dict(tool='armada_ce_maple_hook_owner_audit.py',
            canonical_audit_json_sha256=mode_digest,source_contracts=len(baseline['source_contracts']),
            regular_worker_MD=0,mapped_restore_required=True),
        source_contracts=[window(rom,*spec) for spec in WINDOWS],
        worker_patch=dict(rom_storage_address=hx(rom.rva_address(module,call_rva,32)),
            rva=hx(call_rva),bytes=4,original_word=hx(original_word),authored_word=hx(patched_word),
            check_window_bytes=32,original_crc32_ieee=f'{zlib.crc32(original):08x}',
            authored_crc32_ieee=f'{zlib.crc32(patched):08x}',
            BSR_instruction_rva=hx(0x5db2),BSR_target_rva=hx(0x67a4),BSR_displacement_halfwords=hx(displacement),
            retained_delay_instruction_rva=hx(0x5db4),retained_return_rva=hx(0x5db6),
            skipped_completion_target_rva=hx(0x5db6),HIGHLOW_overlap_rvas=[],
            publication_rule='Validate original worker32 and wholly original completion entry, admit bridge and helper, publish this aligned four-byte callsite word last. Do not also install the earlier completion-entry detour.'),
        completion_entry=dict(rom_storage_address=hx(rom.rva_address(module,0x5b0c,32)),
            rva=hx(0x5b0c),bytes=32,expected_crc32_ieee=f'{zlib.crc32(entry):08x}',
            original_body_remains_unchanged=True),
        port_write_scan=dict(complete_stored_text_bytes=len(content),
            thunk_rva=hx(0x6488),IAT_rva=hx(0x70ac),
            unaligned_absolute_thunk_references=[hx(v) for v in references],
            unaligned_absolute_IAT_references=[hx(v) for v in iat_references],
            reviewed_calls=[dict(load_rva=hx(a),literal_rva=hx(b),call_rva=hx(c),mapped_register_offset=hx(d),
                role='initialization' if a<0x2000 else 'same-worker-special-operation' if a<0x5800 else 'same-worker-cycle')
                for a,b,c,d in expected],
            MDEN_write_calls=[hx(c) for a,b,c,d in expected if d==0x14],
            ordinary_completion_port_write_calls=[],
            limitation='Exact-source literal/IAT and direct-r0-call enumeration plus reviewed call windows; not a proof that unrelated modules or a concurrent root lifecycle cannot access this hardware.'),
        wrapper_contract=dict(mapped_bridge_rva=hx(0x67a4),mapped_bridge_bytes=92,
            original_body_rva=hx(0x5b0c),worker_return_rva=hx(0x5db6),
            helper_p2_entry=hx(0xacfcd000),pre_phase=0,post_phase=1,
            CE_owned_frame_bytes=28,SetKMode_wrapper_bytes=20,combined_mode_call_frame_bytes=48,
            saved_registers=['r8','r9','r10','PR','MACH','MACL','GBR'],
            body_runs_with_previous_mode=True,body_runs_on_ordinary_CE_stack=True,
            body_runs_before_original_InterruptDone18=True,
            ordinary_worker_result_r0_consumed=False,incoming_T_live=False,
            C_helper_ABI='r4 extension, r5 phase; PRE r6 original KMode Boolean, POST r6 KMode Boolean observed before its second explicit kernel entry; r8..r14 must be preserved by the helper and original body.'),
        stage_guard_regions=baseline['stage_guard_regions']+[region(rom,'ordinary_worker_call32',rom.rva_address(module,call_rva,32),32)],
        limitations=[
            'A live lease must validate the exact mapped MMIO instance, MDEN/MDTSEL/MDSTAR values and bounded MDST-idle transition; this source metadata does not provide those observations.',
            'The fence-only test must not alter a TX/RX descriptor, response, payload, virtual card or SD file.',
            'Ordinary root-driver initialization and unload are excluded during the test. Cleanup may terminate a stalled worker after1000ms, which could bypass an authored POST hook.',
            'The padding wrapper has no added CE pdata unwind record. This design specifies ordinary ABI returns, not recovery from an exception or asynchronous thread termination.',
            'Disabling DMA over the original walker can delay physical input/device polls. Hardware tests must verify controls and normal completion after restoration.',
            'Physical VMU enumeration is an ordinary device-list path; it does not justify arbitrary root-controller or driver hot-unload.',
            'No source or metadata here is borrowed from DreamShell or SWAT.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup-dir',type=Path,required=True)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--check',type=Path)
    args=parser.parse_args()
    try:
        inputs=[(args.startup_dir/n).resolve() for n in ('0WINCEOS.BIN','WINCE/MAPLE.DLL')]
        if args.output and args.output.resolve() in inputs:
            raise ValueError('Metadata output must not replace an owner input')
        result=source_audit(args.startup_dir)
        if args.check and json.loads(args.check.read_text())!=result:
            raise ValueError('Checked-in metadata differs from exact owner audit')
        if args.output:
            args.output.write_text(json.dumps(result,indent=2)+'\n')
        elif args.check:
            print(json.dumps(dict(matched=True,source_contracts=len(result['source_contracts']),
                port_write_calls=len(result['port_write_scan']['reviewed_calls']),executable_bytes_exported=False)))
        else:
            print(json.dumps(result,indent=2))
    except (OSError,ValueError,UnicodeError,KeyError,struct.error) as error:
        parser.exit(1,'Fence owner audit failed: '+str(error)+'\n')


if __name__=='__main__':
    main()
