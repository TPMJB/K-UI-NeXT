#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the selected low-reader bootstrap callbacks in the linked stage.

The expected entries come independently from each reader's typed ELF symbols.
Selecting the right reader blob is insufficient: the stage must arm and later
publish that same reader's heap, return, binding and storage-gateway entries.
This audit proves address selection and writes, not console compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from check_vmu_native_sdk_sci_route import SDKCPU, RETURN, assert_c_abi
from check_native_dual_readers import ScalarOps

SCHEMA = 'kui.native.dual-low-callbacks.audit.v1'
MARKER = '__kui_native_dual_low_callbacks_stage'
FIELDS = ('heap_hook', 'return_hook', 'control', 'heap_original',
          'sd_lease', 'background_bind')
SYMBOLS = ('_kui_vmu_observe_heap_hook', '_kui_vmu_observe_return_hook',
           '_kui_vmu_observe_boot_control', '_kui_vmu_native_heap_original',
           '_kui_vmu_observe_sd_lease', '_kui_vmu_native_background_manifest_bind')
SELECTOR = '_native_low_bindings'
TABLE = '_native_low_bindings_table'
ARM = '_native_arm_heap_hook'
CONFIGURE = '_native_low_configure'
BIND_MAP = '_native_low_bind_map'
MASK = 0xffffffff
# Only these decoded MOV.L operands may move with the shared stage's data
# layout. Every literal must still equal its named, typed ELF symbol role.
# Offset entries are (literal offset, referencing instruction offsets,
# actual symbol, exact bias). No RAM-looking word is treated as a wildcard.
LITERAL_ROLES = {
    SELECTOR: ((0x30,(0x00,),'_manifest',0),
               (0x34,(0x1e,),TABLE,0),
               (0x38,(0x28,),'_stopped',0),
               (0x3c,(0x2a,),'_native_low_bindings_refused',0)),
    ARM: ((0xa0,(0x04,),SELECTOR,0),
          (0xa8,(0x24,),'_native_report',0),
          (0xac,(0x4a,),'_native_heap',0),
          (0xb0,(0x58,),'_native_call',0),
          (0xb8,(0x6a,),'_kui_vmu_observe_stage_install',0),
          (0xbc,(0x6e,),'_native_installer_stack',0xffe0)),
    CONFIGURE: ((0x28,(0x02,),SELECTOR,0),),
    BIND_MAP: ((0x18,(0x02,),SELECTOR,0),),
}
# Complete instruction bodies plus all constant words, after the above exact
# named-role checks. These four cold functions were reviewed independently
# with objdump and executed below; earlier stage pin profiles are unchanged.
PINS = {
    SELECTOR: (64,'24f3327ef9c6334692b25d551b56a0b6385f70c829898a3d926839ce91127c7b'),
    ARM: (192,'72f36965fc0c6048bb377b9177c37f311f9b8ee654c89f395ed0323169b42694'),
    CONFIGURE: (44,'1be99e2c822f827c1ade1c4000b30afb3b13fd306b5326009d03b9f431570ff1'),
    BIND_MAP: (28,'f6c3746a5cf57320d243a67b146e8af988e5da8e1d21e0cfa79b589a7d6db5c0'),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def expected_entries(image):
    """Read roles from real bounded ELF symbols, independent of generated C."""
    result = []
    for index, name in enumerate(SYMBOLS):
        address, size, kind, owner = image[4][name]
        require(owner < len(image[3]), 'Unallocated low role: '+name)
        section = image[3][owner]
        if index == 4:
            # SH relaxation leaves ST_SIZE eight bytes beyond this isolated
            # section in the admitted low reader. The owned linker range,
            # not a stale declared size, is the gateway's complete span.
            require(image[4].get('__vmu_sd_lease_begin') == (address,0,0,owner) and
                    image[4].get('__vmu_sd_lease_end') == (address+section[5],0,0,owner) and
                    section[3] == address and size >= section[5] > 0,
                    'SD gateway does not own its complete isolated linker range')
            size = section[5]
        require(section[3] <= address < address+size <= section[3]+section[5],
                'Unbounded low role: '+name)
        if index in (2, 3):
            require(kind == 1 and section[1] == 8 and section[2]&7 == 3 and
                    size == (56 if index == 2 else 32) and not address&31,
                    'Low role is not the aligned typed control object: '+name)
        else:
            require(kind == 2 and section[1] == 1 and section[2]&7 == 6 and
                    size > 0 and not (address|size)&1,
                    'Low role is not a typed executable entry: '+name)
        require(0x8c004000 <= address < address+size <= 0x8c007ba0,
                'Low role escapes the retained reader: '+name)
        result.append(address)
    return tuple(result)


def linked_table(stage, expected):
    require(stage[4].get(MARKER) == (1, 0, 0, 0xfff1),
            'Stage lacks the selected low-callback capability')
    address, body, section = symbol_bytes(stage, TABLE, 1)
    require(len(body) == 48 and not address&3 and section[2]&7 == 2,
            'Low callback table must be exactly two immutable six-word rows')
    rows = struct.unpack('<12I', body)
    require(rows[:6] == expected[0] and rows[6:] == expected[1],
            'A selected callback role differs from its own linked low ELF')
    return address


def canonical_helper(stage,name):
    address,body,section=symbol_bytes(stage,name,2)
    require(section[2]&7 == 6,'Callback helper is not immutable executable code: '+name)
    normalized=bytearray(body)
    for offset,refs,symbol,bias in LITERAL_ROLES[name]:
        require(not (address+offset)&3 and offset+4 <= len(body),
                'Named callback literal escapes its complete function')
        record=stage[4][symbol]
        value,size,kind,owner=record
        require(owner<len(stage[3]) and kind in (1,2) and size>0,
                'Callback literal role lacks a typed allocated symbol: '+symbol)
        owner_section=stage[3][owner]
        require(owner_section[3] <= value < value+size <= owner_section[3]+owner_section[5],
                'Callback literal role escapes its actual allocation: '+symbol)
        require(struct.unpack_from('<I',body,offset)[0] == (value+bias)&MASK,
                'Callback literal does not name its exact linked role: '+name+'/'+symbol)
        for ref in refs:
            require(ref+2 <= len(body),'Callback literal instruction escapes its function')
            op=struct.unpack_from('<H',body,ref)[0]
            decoded=((address+ref+4)&~3)+4*(op&255)
            require(op&0xf000 == 0xd000 and decoded == address+offset,
                    'Callback role is no longer the named decoded MOV.L operand')
        struct.pack_into('<I',normalized,offset,0)
    return body,bytes(normalized)


def pin_helpers(stage):
    require(set(PINS) == set(LITERAL_ROLES),'Callback helper pins have not been independently reviewed')
    _,message,section=symbol_bytes(stage,'_native_low_bindings_refused',1)
    require(message == b'DUAL LOW CALLBACK READER REFUSED\0' and section[2]&7 == 2,
            'Callback refusal is not its exact immutable named message')
    result={}
    for name,(count,digest) in PINS.items():
        body,normalized=canonical_helper(stage,name)
        require(len(body) == count and hashlib.sha256(normalized).hexdigest() == digest,
                'Reviewed callback helper instruction/constant pin changed: '+name)
        result[name]=dict(bytes=count,actual_sha256=hashlib.sha256(body).hexdigest(),
                          named_role_canonical_sha256=digest,
                          exact_named_literal_words=len(LITERAL_ROLES[name]))
    return result


class Refused(Exception):
    pass


class CallbackCPU(ScalarOps, SDKCPU):
    """The actual compiled scalar stage, with no translated selector logic."""
    def __init__(self, resident, stage, reader, transport, *, mask=15, bank=1, seed=0):
        super().__init__(resident, seed=seed, mask=mask, bank=bank)
        self.memory = bytearray(self.memory)
        for kind, offset, address, _, count, _, _, _ in stage[2]:
            if kind == 1 and count:
                self.store_block(address, stage[0][offset:offset+count])
        self.stage = stage
        self.symbols = stage[4]
        self.manifest = self.symbols['_manifest'][0]
        self.write(self.manifest+48, reader)
        self.write(self.manifest+44, transport)
        self.r[15] = 0x8cfef000
        self.pr = RETURN
        self.stack_top = self.r[15]
        self.targets = {self.symbols['_native_report'][0],self.symbols['_stopped'][0]}
        self.executed_addresses = set()
        self.code_bounds = []
        for name in (SELECTOR, ARM, CONFIGURE, BIND_MAP, '_kui_native_dual_u32'):
            if name in self.symbols:
                address, body, _ = symbol_bytes(stage, name, 2)
                self.code_bounds.append((address, address+len(body)))
        self.refusal = None
        self.forbid_control_read = False
        self.low_control = resident[4]['_kui_vmu_observe_boot_control'][0]&0x1fffffff
        self.flushes = []
        self.map_calls = []
        self.selected_map_target = None
        self.map_arguments = None
        self.map_result = 1
        self.initializer_slot = None
        self.published_readback_bad = False
        self.writes = []

    def read(self, address, count=4):
        if getattr(self,'forbid_control_read',False) and 0x0c004000 <= (address&0x1fffffff) < 0x0c007ba0:
            raise ValueError('Callback selection dereferenced low ownership before valid selection')
        if count == 2 and address == self.pc and hasattr(self, 'code_bounds'):
            require(any(begin <= address < end for begin, end in self.code_bounds),
                    'Callback replay escaped the reviewed stage functions: '+hex(address))
            self.executed_addresses.add(address)
        if getattr(self,'initializer_slot',None) is not None and address == self.initializer_slot|0x20000000:
            require(self.flushes == [self.initializer_slot&~31],
                    'Initializer P2 readback occurred before its exact cache-line publication')
            if self.published_readback_bad:
                return super().read(address,count)^4
        return super().read(address, count)

    def plain(self, op, pc):
        if op&0xf0ff == 0x4010:  # DT Rn: unsigned wrap, T iff new value is zero.
            n=op>>8&15
            self.r[n]=(self.r[n]-1)&MASK
            self.sr=(self.sr&~1)|int(self.r[n] == 0)
            return
        if op&0xf0ff == 0x00a3:  # OCBP @Rn; no cache-line contents are fabricated.
            address = self.r[op>>8&15]
            require(self.initializer_slot is not None and address == self.initializer_slot&~31,
                    'Arming published an unrelated cache line')
            self.flushes.append(address)
            return
        super().plain(op,pc)

    def callee(self):
        if self.selected_map_target is not None and self.pc == self.selected_map_target:
            require(self.r[4:6] == self.map_arguments,
                    'Low manifest binder arguments changed')
            self.map_calls.append(self.pc)
            continuation = self.pr
            for i in range(8):
                self.r[i] = (0x92345671+i*0x13579bd+len(self.map_calls))&MASK
            self.r[0] = self.map_result&MASK
            self.mach ^= 0xaaaaaaaa
            self.macl ^= 0x55555555
            self.sr ^= 1
            # The wrapper is a tail JMP after restoring its caller PR. The
            # actual selected low binder returns through its reviewed RTS
            # epilogue; its return register is not an arbitrary callee output.
            self.pc = continuation
            return
        if self.pc == self.symbols['_stopped'][0]:
            require(self.r[4] == self.symbols['_native_low_bindings_refused'][0],
                    'Invalid low selection did not report the exact selection refusal')
            self.refusal = [0,self.r[5]]
            raise Refused()
        require(self.pc == self.symbols['_native_report'][0],
                'Unexpected callback replay callee: '+hex(self.pc))
        self.refusal = self.r[4:6]
        raise Refused()


def selector_case(resident, stage, expected, table, reader, transport, *, mask=15, bank=1, seed=0):
    cpu = CallbackCPU(resident, stage, reader, transport, mask=mask, bank=bank, seed=seed)
    before = cpu.snapshot()
    valid = transport == 1 and reader in (1, 2)
    cpu.forbid_control_read = True
    try:
        cpu.run_symbol(SELECTOR)
    except Refused:
        require(not valid and cpu.refusal == [0, reader],
                'Unsupported reader was not refused with its actual mode')
    else:
        require(valid, 'Unsupported reader selected low callbacks')
        assert_c_abi(cpu, before)
        row = 0 if reader == 1 else 1
        require(cpu.r[0] == table+24*row,
                'Compiled selector returned a different reader callback row')
        actual = tuple(cpu.read(cpu.r[0]+4*i) for i in range(6))
        require(actual == expected[row],
                'Compiled selection used a callback from the other low reader')
    require(not cpu.iprb_writes and not cpu.vbr_writes,
            'Cold callback selection changed interrupt ownership')
    require(all(cpu.stack_top-256 <= address < cpu.stack_top
                for _, address, _, _ in cpu.writes),
            'Cold callback selector wrote outside its caller stack')
    return valid


def selector_cases(resident, stage, expected, table):
    cases = refused = 0
    for mask in (0, 6, 15):
        for bank in (0, 1):
            for reader in (0, 1, 2, 3, 255, 256, MASK):
                for transport in (0, 1, 2, 3, MASK):
                    refused += not selector_case(resident, stage, expected, table, reader, transport,
                                                  mask=mask, bank=bank, seed=cases)
                    cases += 1
    return dict(actual_linked_selector_cases=cases, unsupported_mode_refusals=refused,
                all_six_roles_from_selected_actual_elf=True,
                exact_immutable_row_pointer=True, c_abi_preserved=True,
                no_interrupt_register_or_nonstack_writes=True)


def console_config_layout():
    """Compile real SH offsetof values; host pointer widths are not assumed."""
    from test_native_dual_readers import LAYOUT_FIELDS, console_layouts
    extra = {
        'config_bytes':'sizeof(struct kui_vmu_native_passive_config)',
        'config_sd_lease':'offsetof(struct kui_vmu_native_passive_config,sd_lease)',
        'config_low_terminal':'offsetof(struct kui_vmu_native_passive_config,low_terminal)',
        'config_low_heap_hook':'offsetof(struct kui_vmu_native_passive_config,low_heap_hook)',
    }
    saved = dict(LAYOUT_FIELDS)
    try:
        LAYOUT_FIELDS.update(extra)
        result = console_layouts()
    finally:
        LAYOUT_FIELDS.clear()
        LAYOUT_FIELDS.update(saved)
    return {name:result['layouts']['x'][name] for name in extra}


def configure_case(resident,stage,expected,layout,reader,*,seed=0,bank=1):
    cpu = CallbackCPU(resident,stage,reader,1,seed=seed,bank=bank)
    address = 0x8ce00000
    pattern = bytes((0x5a+i*19+seed)&255 for i in range(layout['config_bytes']))
    for i,value in enumerate(pattern):
        cpu.write(address+i,value,1)
    cpu.r[4] = address
    before = cpu.snapshot()
    cpu.writes = []
    cpu.run_symbol(CONFIGURE)
    assert_c_abi(cpu,before)
    row = expected[reader-1]
    wanted = bytearray(pattern)
    field_values = {layout['config_sd_lease']:row[4],
                    layout['config_low_terminal']:row[1],
                    layout['config_low_heap_hook']:row[0]}
    for offset,value in field_values.items():
        struct.pack_into('<I',wanted,offset,value)
    actual = bytes(cpu.read(address+i,1) for i in range(len(wanted)))
    require(actual == wanted,'Worker configuration did not publish the selected low callbacks exactly')
    require(all((at-address in field_values and count == 4) or
                cpu.stack_top-256 <= at < cpu.stack_top for _,at,_,count in cpu.writes),
            'Low configuration wrote outside its three callback fields and caller stack')
    require(not cpu.vbr_writes and not cpu.iprb_writes,'Low configuration changed interrupt ownership')
    return cpu


def bind_map_case(resident,stage,expected,reader,*,seed=0,bank=1,result=1):
    cpu = CallbackCPU(resident,stage,reader,1,seed=seed,bank=bank)
    cpu.selected_map_target = expected[reader-1][5]
    cpu.targets.add(cpu.selected_map_target)
    cpu.map_arguments = [cpu.manifest,0x8c420000]
    cpu.map_result = result
    cpu.r[4:6] = cpu.map_arguments
    before = cpu.snapshot()
    cpu.writes = []
    cpu.run_symbol(BIND_MAP)
    assert_c_abi(cpu,before)
    require(cpu.map_calls == [expected[reader-1][5]] and cpu.r[0] == result&MASK,
            'Cold binder did not call and return the selected low reader entry')
    require(all(cpu.stack_top-256 <= at < cpu.stack_top for _,at,_,_ in cpu.writes),
            'Low binder wrapper wrote outside its caller stack')
    require(not cpu.vbr_writes and not cpu.iprb_writes,'Low binder wrapper changed interrupt ownership')
    return cpu


def arm_case(resident,stage,expected,reader,*,seed=0,bank=1,variant='ok'):
    cpu = CallbackCPU(resident,stage,reader,1,seed=seed,bank=bank)
    low = expected[reader-1]
    control,original = low[2]|0x20000000,low[3]|0x20000000
    for i in range(14):
        cpu.write(control+4*i,0)
    for i in range(8):
        cpu.write(original+4*i,0)
    heap = stage[4]['_native_heap'][0]
    call = stage[4]['_native_call'][0]
    slot,target = 0x8c020020,0x8c020040
    frame = (0x0c00e400,0x8c00e400,0xac00e400)[seed%3]
    frame_pattern = [0x13579bc0+i for i in range(8)]
    for i,value in enumerate(frame_pattern):
        cpu.write(frame+4*i,value)
    cpu.write(heap+28,slot)
    cpu.write(heap+32,target)
    cpu.write(call+8,0xac020052)
    cpu.write(call+12,0x0c400000)
    cpu.write(call+16,0x00600000)
    cpu.write(slot,target)
    failure = None
    if isinstance(variant,tuple) and variant[0] == 'control':
        at=control+4*variant[1]
        cpu.write(at,0x12345678)
        failure=low[2]+4*variant[1]
    elif isinstance(variant,tuple) and variant[0] == 'original':
        at=original+4*variant[1]
        cpu.write(at,0x12345678)
        failure=low[3]+4*variant[1]
    elif variant == 'slot':
        cpu.write(slot,target^4)
        failure=slot
    elif variant == 'readback':
        cpu.published_readback_bad=True
        failure=slot
    cpu.initializer_slot=slot
    cpu.r[4]=frame
    before=cpu.snapshot()
    cpu.writes=[]
    try:
        cpu.run_symbol(ARM)
    except Refused:
        require(failure is not None and cpu.refusal == [2,failure],
                'Arming refused with the wrong ownership witness')
        require([cpu.read(frame+4*i) for i in range(8)] == frame_pattern,
                'Arming published an owner-return hook after refusal')
        if variant != 'readback':
            require(not cpu.flushes and all(cpu.stack_top-256 <= at < cpu.stack_top
                    for _,at,_,_ in cpu.writes),'Arming changed ownership after a precondition refusal')
    else:
        require(failure is None,'Arming accepted a changed control, original target or readback')
        assert_c_abi(cpu,before)
        installer=stage[4]['_kui_vmu_observe_stage_install'][0]
        stack=stage[4]['_native_installer_stack'][0]+0x10000-32
        require(tuple(cpu.read(control+4*i) for i in range(3)) == (installer,stack,1),
                'Arming did not publish the selected control object and installer')
        require(tuple(cpu.read(original+4*i) for i in range(8)) ==
                (target,0x0c400000,0x00600000,0x0c020052,0,0,0,0),
                'Arming did not retain the exact initializer/caller in the selected original object')
        wanted=frame_pattern.copy();wanted[5]=low[1]
        require(cpu.read(slot) == low[0] and
                [cpu.read(frame+4*i) for i in range(8)] == wanted,
                'Arming used another reader heap entry or owner-return target')
        require(cpu.flushes == [slot&~31],'Initializer slot was not published once')
    allowed={control,control+4,control+8,original,original+4,original+8,original+12,slot,frame+20}
    require(all((at in allowed and count == 4) or cpu.stack_top-256 <= at < cpu.stack_top
                for _,at,_,count in cpu.writes),'Arming wrote outside exact owned objects/slot/frame')
    require(not cpu.vbr_writes and not cpu.iprb_writes,'Arming changed interrupt ownership')
    return cpu


def consumer_cases(resident,stage,expected,layout):
    arms=configurations=binders=refusals=0
    for reader in (1,2):
        for bank in (0,1):
            for seed in range(6):
                arm_case(resident,stage,expected,reader,seed=seed,bank=bank);arms+=1
                configure_case(resident,stage,expected,layout,reader,seed=seed,bank=bank);configurations+=1
                for result in (0,1,2,MASK):
                    bind_map_case(resident,stage,expected,reader,seed=seed,bank=bank,result=result);binders+=1
        for variant in [*(('control',i) for i in range(14)),
                        *(('original',i) for i in range(8)),'slot','readback']:
            arm_case(resident,stage,expected,reader,variant=variant);refusals+=1
    invalid_refusals=0
    for reader,transport in ((0,1),(3,1),(255,1),(MASK,1),(1,0),(2,0),(1,2),(2,3)):
        for name in (ARM,CONFIGURE,BIND_MAP):
            cpu=CallbackCPU(resident,stage,reader,transport)
            cpu.forbid_control_read=True
            cpu.r[4:6]=[0x8ce00000,0x8c420000]
            cpu.writes=[]
            try:cpu.run_symbol(name)
            except Refused:
                require(cpu.refusal == [0,reader],
                        'Cold callback consumer did not stop before invalid low selection')
                require(not cpu.flushes and not cpu.map_calls and
                        all(cpu.stack_top-256 <= at < cpu.stack_top for _,at,_,_ in cpu.writes),
                        'Invalid callback consumer touched ownership or configured an entry')
                invalid_refusals+=1
            else:raise ValueError('Cold callback consumer accepted an unsupported reader/transport')
    return dict(actual_linked_arming_cases=arms,actual_linked_configuration_cases=configurations,
                actual_linked_manifest_binding_cases=binders,arming_ownership_refusals=refusals,
                invalid_consumer_mode_refusals=invalid_refusals,
                initializer_slot_and_owner_return_use_selected_reader=True,
                three_worker_callbacks_use_selected_reader=True,
                indirect_manifest_binding_uses_selected_reader=True,
                source_and_owned_manifest_arguments_and_return_preserved=True,
                original_control_and_frame_neighbours_preserved=True,
                exact_initializer_cache_line_published_before_readback=True,
                typed_manifest_binder_clobbers_caller_saved_registers=True,
                tail_binder_retains_actual_rts_pr_contract=True,
                unsupported_modes_stop_before_control_or_callback_writes=True,
                no_interrupt_register_writes=True)


def changed_table_word(stage, address, value):
    """Mutate actual linked data while preserving symbol metadata."""
    data = bytearray(stage[0])
    section = next(s for s in stage[3] if s[1] == 1 and
                   s[3] <= address < address+4 <= s[3]+s[5])
    struct.pack_into('<I', data, section[4]+address-section[3], value)
    return (bytes(data), *stage[1:])


def table_mutants(resident, stage, expected, table, layout):
    rejected = 0
    # The original defect: every differing Y code entry was borrowed from X.
    # Each single-role mutation must fail independently; no aggregate checksum
    # or source-generator agreement can hide the wrong entry's role.
    for row in (0, 1):
        for index in range(6):
            replacement = expected[1-row][index]
            if replacement == expected[row][index]:
                replacement ^= 4
            changed = changed_table_word(stage, table+24*row+4*index, replacement)
            try:
                selector_case(resident, changed, expected, table, row+1, 1)
            except (ValueError, AssertionError):
                rejected += 1
            else:
                raise ValueError('Actual selector accepted a mixed low-callback role')
    changed = stage
    for index in range(6):
        changed = changed_table_word(changed, table+24+4*index, expected[0][index])
    try:
        selector_case(resident, changed, expected, table, 2, 1)
    except (ValueError, AssertionError):
        rejected += 1
    else:
        raise ValueError('Old SCI-only callback behavior remains accepted for Legacy')
    consumer_rejections=0
    for call in (lambda:arm_case(resident,changed,expected,2),
                 lambda:configure_case(resident,changed,expected,layout,2),
                 lambda:bind_map_case(resident,changed,expected,2)):
        try:
            call()
        except (ValueError,AssertionError):
            consumer_rejections+=1
        else:
            raise ValueError('An actual cold consumer accepted the old SCI-only Y callbacks')
    return dict(individual_wrong_role_mutants=rejected-1, old_sci_only_y_regression_rejected=True,
                actual_old_sci_only_consumer_mutants_rejected=consumer_rejections)


def instruction_truth():
    cases=0
    for n in range(16):
        for value in (0,1,2,0x7fffffff,0x80000000,MASK):
            for t in (0,1):
                cpu=CallbackCPU.__new__(CallbackCPU)
                cpu.r=[0x12345670+i for i in range(16)]
                cpu.r[n]=value
                before=cpu.r.copy()
                cpu.sr=0x60000bf0|t
                cpu.plain(0x4010|(n<<8),0)
                before[n]=(value-1)&MASK
                require(cpu.r == before and
                        cpu.sr == (0x60000bf0|int(value==1)),
                        'DT instruction model failed wrap/T/register preservation truth test')
                cases+=1
    for n in range(16):
        cpu=CallbackCPU.__new__(CallbackCPU)
        cpu.r=[0x12345670+i for i in range(16)]
        cpu.initializer_slot=0x8c02002c
        cpu.r[n]=cpu.initializer_slot&~31
        cpu.flushes=[]
        cpu.plain(0x00a3|(n<<8),0)
        require(cpu.flushes == [0x8c020020],'OCBP model failed exact-line observation')
        cpu.r[n]^=32
        try:cpu.plain(0x00a3|(n<<8),0)
        except ValueError:pass
        else:raise ValueError('OCBP model accepted an unrelated cache line')
        cases+=2
    return cases


def pin_mutants(stage,expected):
    rejected=0
    for name in PINS:
        address,body,section=symbol_bytes(stage,name,2)
        begin=section[4]+address-section[3]
        for at in range(len(body)):
            data=bytearray(stage[0]);data[begin+at]^=1
            changed=(bytes(data),*stage[1:])
            try:pin_helpers(changed)
            except ValueError:rejected+=1
            else:raise ValueError('Callback pin accepted a changed instruction/constant/literal byte')
    address,body,section=symbol_bytes(stage,'_native_low_bindings_refused',1)
    begin=section[4]+address-section[3]
    for at in range(len(body)):
        data=bytearray(stage[0]);data[begin+at]^=1
        try:pin_helpers((bytes(data),*stage[1:]))
        except ValueError:rejected+=1
        else:raise ValueError('Callback refusal accepted a changed immutable message byte')
    table,body,section=symbol_bytes(stage,TABLE,1)
    begin=section[4]+table-section[3]
    for at in range(len(body)):
        data=bytearray(stage[0]);data[begin+at]^=1
        try:linked_table((bytes(data),*stage[1:]),expected)
        except ValueError:rejected+=1
        else:raise ValueError('Callback table accepted a changed selected role byte')
    for value in (None,(0,0,0,0xfff1),(2,0,0,0xfff1),(1,4,1,1)):
        symbols=dict(stage[4])
        if value is None:symbols.pop(MARKER)
        else:symbols[MARKER]=value
        try:linked_table((*stage[:4],symbols),expected)
        except ValueError:rejected+=1
        else:raise ValueError('Callback table accepted an absent or changed capability marker')
    return rejected


def hardcoded_selector_mutant(resident,stage,expected,table):
    address,body,section=symbol_bytes(stage,SELECTOR,2)
    offsets=[at for at in range(0,len(body)-1,2) if struct.unpack_from('<H',body,at)[0]==0xe018]
    require(len(offsets)==1,'Reviewed selector lacks its single Legacy row displacement instruction')
    data=bytearray(stage[0]);offset=section[4]+address-section[3]+offsets[0]
    struct.pack_into('<H',data,offset,0xe000)  # MOV #0,R0: old always-X row behavior.
    try:selector_case(resident,(bytes(data),*stage[1:]),expected,table,2,1)
    except (ValueError,AssertionError):return 1
    raise ValueError('Actual selector accepted the old hardcoded SCI row behavior')


def check(directory, *, self_test=False):
    directory = Path(directory)
    resident = read_elf(directory/'resident-sci.elf')
    legacy = read_elf(directory/'resident-legacy.elf')
    stage = read_elf(directory/'stage.elf')
    expected = (expected_entries(resident), expected_entries(legacy))
    table = linked_table(stage, expected)
    pins = pin_helpers(stage)
    layout = console_config_layout()
    proof = dict(schema=SCHEMA, enabled=True,
                 artifact_sha256={name:hashlib.sha256(image[0]).hexdigest()
                                  for name,image in (('stage',stage),('resident',resident),('legacy',legacy))},
                 actual_linked_stage_executed=True,
                 helper_instruction_truth_cases=instruction_truth(),
                 exact_named_role_instruction_pins=pins,
                 expected_callback_roles={name:dict(zip(FIELDS, row))
                                          for name,row in zip(('new','legacy'),expected)},
                 actual_sh_configuration_layout=layout,
                 selector=selector_cases(resident,stage,expected,table),
                 consumers=consumer_cases(resident,stage,expected,layout))
    if self_test:
        proof['self_test'] = table_mutants(resident,stage,expected,table,layout)
        proof['self_test']['pinned_byte_and_capability_mutants_rejected']=pin_mutants(stage,expected)
        proof['self_test']['actual_hardcoded_sci_selector_mutant_rejected']=hardcoded_selector_mutant(
            resident,stage,expected,table)
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    try:
        print(json.dumps(check(args.directory,self_test=args.self_test),indent=2))
    except (OSError, ValueError, KeyError, AssertionError) as error:
        parser.exit(2, 'Native dual callback audit refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
