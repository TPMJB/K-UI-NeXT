#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the typed timeline descriptor, owned state and actual low SH bridge.

The bridge model substitutes an ABI-compliant adversary for the worker
callback. The collector has separate host tests; no model establishes game
frame progression or console performance. The cold binder is byte-pinned.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_vmu_native_cache_gateway import CacheGateway
from check_vmu_native_worker_gateway import CPU, MASK, read_elf, symbol_bytes

P1,P2,SPAN=0x8c100000,0xac100000,0x20000
REGION=0x8c420400
SLOT=REGION+0x500+11*4
MARKER=(1,0,0,0xfff1)
PRIVATE_OBJECTS={'_native_loader_timeline':3420,'_native_loader_event':64,'_native_loader_clock':20}
BRIDGE_PIN=('62d84fd8b9c6b54566d1334bc207c06f3d8b7d2355da33e562bdde085c50bc7b',36)
BINDER_PIN=('7caa04ff75dfb56f852276ab71517c1466face73d215dc2c66204d9b49347ecd',180)
SDK_BRIDGE_PIN=('8b006aa6a8fc0f838d068585d20cef48ca2bdbe271a6658cd3f9fd251f758fa6',36)
SDK_BINDER_PIN=('7caa04ff75dfb56f852276ab71517c1466face73d215dc2c66204d9b49347ecd',180)
# The isolated read-ahead profile moves low BSS back to the original timeline
# binding pair. Its bridge instructions remain byte-identical. The cold
# binder differs from the SDK profile only at its exact retained-map helper
# literal; the 384 KiB geometry is separately checked by that profile's binder.
READAHEAD_BRIDGE_PIN=BRIDGE_PIN
READAHEAD_BINDER_PIN=('d58548c610f400992ca2a922c57a0f70caf334591b2d3ab736d93bf0a5eb97e0',180)
# The SDK route changes the two low BSS bindings only. The whole body above
# is still pinned independently; these checks admit no relocation mask.
BRIDGE_INSTRUCTIONS=bytes.fromhex('06d1126709917c311c511821038904d222662b4109000b000900fc04')
BRIDGE_BINDINGS=(0x8c007b34,0x8c007b20)
SDK_BRIDGE_BINDINGS=(0x8c007a34,0x8c007a20)
READAHEAD_BRIDGE_BINDINGS=(0x8c007b34,0x8c007b20)
BIN_AT_NO_DIV_PIN=('37915f9519ac69eb63c1819a0b82ea5a759111693f41f9d9f4cb62e27beb56e7',144)
READAHEAD_BIN_AT_NO_DIV_PIN=('0db32d17b310cf8f96bd164ed8555b1c47810499d2c00ed076948e9410d5c82c',144)
# The diagnostic report moves add32 by0x19c; the first140 arithmetic bytes
# are unchanged and the final word remains bound to that exact typed entry.
READAHEAD_DIAGNOSTIC_BIN_AT_NO_DIV_PIN=('3d98d0322d6bc12a6d480b2c9fadfd57c58d7d3ec64137f19346e9dbb713835b',144)
# The PIO-only report moves add32 by12bytes; the arithmetic body stays the
# same, with its complete new literal binding pinned separately.
READAHEAD_PIO_BIN_AT_NO_DIV_PIN=('7bd6d3b645810ca2266fc51ba96ed166333b070ff85e10ab9e238c02f85b0426',144)
READAHEAD_PIO_PAIR_BIN_AT_NO_DIV_PIN=('bdec557eba8a8c3401f9210e2859cc2f8912a8c1fe93258e70635885b8b98705',144)


def require(test,message):
    if not test:raise ValueError(message)


class Bridge(CacheGateway):
    """Execute the linked nullable tail-call with exact reader/caller inputs."""
    def __init__(self,resident,callback,seed,enabled,exit,function,body=None):
        begin,original,_=symbol_bytes(resident,'_kui_retail_async_trace',2)
        CPU.__init__(self,seed,begin,1)
        self.base,self.blob,self.pc=begin,original if body is None else body,begin
        self.r[4:6]=[exit,function]
        self.r[15]=0x8c007cf0;self.start_sp=self.r[15]
        self.pr=begin+len(original)+0x100
        self.before=self.r.copy(),self.pr,self.sr,self.fpscr,self.fr.copy()
        self.pointer=resident[4]['_kui_retail_async_region_storage'][0]
        self.caller=resident[4]['_kui_retail_hook_sr'][0]
        self.original_sr=(0x40000000^(seed*0x1234567))&MASK
        self.callback=callback;self.enabled=enabled
        self.mem={self.pointer:REGION,SLOT:callback if enabled else 0,self.caller:self.original_sr}
        self.reads,self.calls,self.writes=[],[],[]
        self.targets={callback:'timeline'}

    def read(self,address):
        if address in self.mem:
            self.reads.append(address);return self.mem[address]
        return struct.unpack('<I',self.code(address,4))[0]

    def write(self,address,value):
        assert address%4==0 and self.start_sp-64<=address<self.start_sp,'timeline bridge store escapes its own frame'
        self.mem[address]=value&MASK;self.writes.append(address)

    def plain(self,w,pc):
        n,m=w>>8&15,w>>4&15
        if w&0xf00f==0x300c:self.r[n]=(self.r[n]+self.r[m])&MASK;return
        if w&0xf00f==0x000e:
            address=(self.r[0]+self.r[m])&MASK;self.r[n]=self.read(address);return
        super().plain(w,pc)

    def api(self,target):
        assert self.enabled and target==self.callback and not self.calls
        assert self.r[4:8]==[self.before[0][4],self.before[0][5],self.original_sr,REGION],\
            'timeline callback arguments differ from original exit/function/caller-SR/region'
        assert self.r[8:]==self.before[0][8:] and self.r[15]==self.start_sp,\
            'timeline bridge is not an ABI tail-call on the same GD frame'
        return_pc=self.pr;self.calls.append(target)
        # A tail-called ABI callee may use every volatile GPR and the T bit.
        # It returns directly to the original PR with its own frame consumed.
        for i in range(8):self.r[i]=(0x913579bd+0x13579bd*i+len(self.calls))&MASK
        self.sr^=1;self.pc=return_pc

    def check(self):
        r,pr,sr,fpscr,fr=self.before
        assert self.r[8:]==r[8:] and self.pr==pr and self.sr&~1==sr&~1
        assert (self.fpscr,self.fr)==(fpscr,fr)
        assert self.calls==([self.callback] if self.enabled else [])
        assert self.reads==[self.pointer,SLOT]+([self.caller] if self.enabled else []),\
            'null callback touched caller state or bridge used an unexpected memory view'
        assert not self.writes,'timeline tail-call bridge must not allocate a second GD frame'


def private_state(worker):
    bottom=worker[4]['__vmu_native_worker_stack_bottom'][0]
    bss_begin=worker[4]['__vmu_native_worker_bss_begin'][0]
    bss_end=worker[4]['__vmu_native_worker_bss_end'][0]
    require(P2+80<=bss_begin<=bss_end<=bottom==P2+SPAN-4096,
            'timeline lacks complete private worker BSS/stack bounds')
    ranges=[];report={}
    for name,size in PRIVATE_OBJECTS.items():
        record=worker[4].get(name)
        require(record is not None,'missing typed timeline state: '+name)
        address,count,kind,owner=record
        require(kind==1 and count==size and address%4==0 and owner<len(worker[3]),
                'timeline state size/type/alignment changed: '+name)
        section=worker[3][owner]
        require(section[1]==8 and section[2]&7==3 and
                section[3]<=address<address+size<=section[3]+section[5] and
                bss_begin<=address<address+size<=bss_end,
                'timeline state escapes exact private P2 NOBITS ownership: '+name)
        ranges.append((address,address+size));report[name]={'address':hex(address),'bytes':size}
    require(all(a[1]<=b[0] or b[1]<=a[0] for i,a in enumerate(ranges) for b in ranges[i+1:]),
            'timeline collector/event/profile objects overlap')
    return report


def division_helper(resident,worker,read_ahead=False,diagnostic=False,pio=False,pio_pair=False):
    """Reuse the resident stack proof only for identical linked libgcc code.

    The prefix contains the alternate internal entry used by the helper.
    One reviewed optimized bin_at variant uses reciprocal multiplication
    and has no worker division helper at all; that body is pinned instead.
    """
    name='___udivsi3'
    if name not in worker[4]:
        _,body,_=symbol_bytes(worker,'_bin_at',2)
        require(not diagnostic or read_ahead,'diagnostic timeline arithmetic requires read-ahead')
        require(not pio or diagnostic,'PIO timeline arithmetic requires the diagnostic profile')
        require(not pio_pair or pio,'PIO pair timeline arithmetic requires the PIO profile')
        expected=READAHEAD_PIO_PAIR_BIN_AT_NO_DIV_PIN if pio_pair else READAHEAD_PIO_BIN_AT_NO_DIV_PIN if pio else \
            READAHEAD_DIAGNOSTIC_BIN_AT_NO_DIV_PIN if diagnostic else \
            READAHEAD_BIN_AT_NO_DIV_PIN if read_ahead else BIN_AT_NO_DIV_PIN
        require((hashlib.sha256(body).hexdigest(),len(body))==expected,
                'timeline bin arithmetic changed without a linked division-helper proof')
        if read_ahead:
            # This body has exactly the original arithmetic/instructions;
            # only its final PC-relative add32 target moved with worker code.
            target,target_body,target_section=symbol_bytes(worker,'_add32',2)
            require(struct.unpack_from('<I',body,len(body)-4)[0]==target and
                    target_section[2]&7==6 and P1+80<=target<target+len(target_body)<=P1+SPAN,
                    'read-ahead timeline reciprocal arithmetic lost its exact owned add32 binding')
        return {'linked':False,'reciprocal_bin_body_pinned':True}
    def window(image):
        address,body,section=symbol_bytes(image,name,2)
        require(section[2]&7==6 and address-64>=section[3],
                'division helper internal prefix escapes executable ownership')
        offset=section[4]+address-64-section[3]
        return body,image[0][offset:offset+64+len(body)]
    native,native_window=window(worker)
    low,low_window=window(resident)
    require(native==low and native_window==low_window,
            'worker division helper/internal prefix differs from the resident stack proof')
    return {'linked':True,'identical_resident_body':True,'identical_internal_prefix_bytes':64,
            'body_bytes':len(native),'body_sha256':hashlib.sha256(native).hexdigest()}


def sdk_route_variant(resident,worker,stage):
    markers=(resident[4].get('__kui_native_sdk_sci_route_resident'),
             resident[4].get('__kui_native_sdk_sci_route_callback'),
             worker[4].get('__kui_native_sdk_sci_route_worker'),
             stage[4].get('__kui_native_sdk_sci_route_stage'))
    if any(markers):
        require(markers==(MARKER,)*4,
                'SDK timeline variant requires all paired resident/callback/worker/stage markers')
        return True
    return False


def bridge_bindings(resident,bridge,sdk,read_ahead=False):
    require(len(bridge)==36 and bridge[:28]==BRIDGE_INSTRUCTIONS,
            'timeline bridge instruction/halfword literal bytes differ from the reviewed original')
    expected=READAHEAD_BRIDGE_BINDINGS if read_ahead else SDK_BRIDGE_BINDINGS if sdk else BRIDGE_BINDINGS
    require(struct.unpack_from('<II',bridge,28)==expected,
            'timeline bridge must retain its exact reviewed region/SR literal pair')
    for name,address in zip(('_kui_retail_async_region_storage','_kui_retail_hook_sr'),expected):
        actual,size,kind,owner=resident[4][name]
        require(actual==address and size==4 and kind==1 and owner<len(resident[3]),
                'timeline bridge low binding lacks its exact typed4B object: '+name)
        section=resident[3][owner]
        require(section[1]==8 and section[2]&7==3 and
                section[3]<=actual<actual+size<=section[3]+section[5],
                'timeline bridge low binding escapes owned writable NOBITS: '+name)


def check(directory,*,self_test=False,read_ahead_proof=None):
    require(__debug__,'timeline instruction model requires Python assertions; run without -O/PYTHONOPTIMIZE')
    directory=Path(directory)
    resident,worker,stage=(read_elf(directory/name) for name in ('resident-sci.elf','native-worker.elf','stage.elf'))
    sdk=sdk_route_variant(resident,worker,stage)
    ra_markers=tuple(image[4].get('__kui_native_readahead_'+part) for image,part in
                     ((resident,'resident'),(worker,'worker'),(stage,'stage')))
    read_ahead=any(ra_markers)
    if read_ahead:
        require(sdk and ra_markers==(MARKER,)*3,
                'read-ahead timeline requires the paired native SDK resident/worker/stage variant')
    diagnostic=worker[4].get('__kui_native_readahead_diagnostic_worker')
    if diagnostic and read_ahead_proof is None:
        from check_vmu_native_readahead import check as check_read_ahead
        read_ahead_proof=check_read_ahead(directory,self_test=False)
    require(bool(diagnostic)==bool(read_ahead_proof and read_ahead_proof.get('diagnostic_capture')),
            'timeline diagnostic worker/auxiliary proof differs')
    if diagnostic:
        require(read_ahead and diagnostic==MARKER and
                read_ahead_proof.get('enabled') and read_ahead_proof.get('paired_markers') and
                read_ahead_proof.get('schema')=='kui.native.read-ahead.audit.v1' and
                read_ahead_proof.get('diagnostics',{}).get('enabled') and
                read_ahead_proof.get('diagnostics',{}).get('schema')=='kui.native.read-ahead.diagnostics.audit.v1' and
                all(read_ahead_proof.get('artifact_sha256',{}).get(name)==hashlib.sha256(image[0]).hexdigest()
                    for name,image in (('resident',resident),('worker',worker),('stage',stage))),
                'timeline diagnostic arithmetic requires the complete exact auxiliary proof')
    from check_vmu_native_sdk_sci_route import pio_companion,pio_pair_companion
    pio=pio_companion(worker,read_ahead_proof)
    pio_pair=pio_pair_companion(worker,read_ahead_proof)
    require(not pio or diagnostic,'PIO timeline requires its diagnostic worker')
    markers=(resident[4].get('__kui_native_loader_timeline_resident'),
             worker[4].get('__kui_native_loader_timeline_worker'))
    if not any(markers):
        require(not sdk,'SDK SCI route requires the paired loader timeline markers')
        require('_kui_native_loader_timeline_exports' not in worker[4],
                'timeline descriptor lacks its paired resident/worker markers')
        return {'enabled':False}
    require(markers==(MARKER,MARKER),'timeline resident/worker marker mismatch')
    trace,trace_body,trace_section=symbol_bytes(worker,'_kui_vmu_native_trace_exports',1)
    desc,body,section=symbol_bytes(worker,'_kui_native_loader_timeline_exports',1)
    callback,callback_body,callback_section=symbol_bytes(worker,'_kui_native_loader_timeline_visit',2)
    require(len(trace_body)==40 and desc==trace+40 and len(body)==12 and
            section[2]&7==2 and trace_section[2]&7==2,
            'timeline descriptor must be the adjacent exact12B immutable object after the unchanged40B trace table')
    require(struct.unpack('<3I',body)==(0x4b4c5431,1,callback) and callback%2==0 and
            P1+80<=callback<callback+len(callback_body)<=P1+SPAN and callback_section[2]&7==6,
            'timeline descriptor does not bind its exact private P1 callback')
    private=private_state(worker)
    division=division_helper(resident,worker,read_ahead,bool(diagnostic),pio,pio_pair)
    begin,bridge,_=symbol_bytes(resident,'_kui_retail_async_trace',2)
    bridge_pin=READAHEAD_BRIDGE_PIN if read_ahead else SDK_BRIDGE_PIN if sdk else BRIDGE_PIN
    binder_pin=READAHEAD_BINDER_PIN if read_ahead else SDK_BINDER_PIN if sdk else BINDER_PIN
    require((hashlib.sha256(bridge).hexdigest(),len(bridge))==bridge_pin,
            'linked timeline bridge instructions/literals changed')
    bridge_bindings(resident,bridge,sdk,read_ahead)
    binder,binder_body,binder_section=symbol_bytes(stage,'_native_loader_timeline_bind',2)
    require((hashlib.sha256(binder_body).hexdigest(),len(binder_body))==binder_pin and
            binder_section[2]&7==6 and 0x8ce00000<=binder<binder+len(binder_body)<=0x8cfe0000,
            'linked timeline descriptor/region/profile binder changed')
    cases=0
    for seed in range(16):
        for enabled in (False,True):
            for exit in (0,1,0xffffffff):
                for function in (0,1,2,13,0xffffffff):
                    Bridge(resident,callback,seed,enabled,exit,function).run().check();cases+=1
    rejected=0
    if self_test:
        mutations=[]
        # Read each linked PC-relative literal as code executes it. Mutating
        # the exact region/SR binding must not gain an untyped memory view.
        for target in (resident[4]['_kui_retail_async_region_storage'][0],resident[4]['_kui_retail_hook_sr'][0]):
            at=bridge.find(struct.pack('<I',target));require(at>=0,'timeline bridge lacks exact data literal')
            changed=bytearray(bridge);struct.pack_into('<I',changed,at,target+4);mutations.append(bytes(changed))
        for changed in mutations:
            try:Bridge(resident,callback,7,True,1,2,changed).run().check()
            except (AssertionError,ValueError,KeyError,struct.error):rejected+=1
            else:raise ValueError('timeline bridge model accepted a changed memory binding')
    return {'enabled':True,'paired_resident_worker':True,'descriptor_address':hex(desc),
        'sdk_sci_route_variant':sdk,'sdk_sci_resident_callback_worker_stage_paired':sdk,
        'read_ahead_variant':read_ahead,
        'read_ahead_diagnostic_companion_required':bool(diagnostic),
        'read_ahead_pio_companion_required':pio,
        'read_ahead_pio_pair_companion_required':pio_pair,
        'descriptor_bytes':12,'trace_descriptor_bytes_unchanged':40,
        'callback_address':hex(callback),'callback_bytes':len(callback_body),
        'private_state':private,'private_state_bytes':sum(PRIVATE_OBJECTS.values()),
        'division_helper':division,
        'private_p2_nobits_only':True,'bridge_address':hex(begin),'bridge_bytes':len(bridge),
        'complete_linked_bridge_pinned':True,'actual_bridge_model_cases':cases,
        'original_bridge_instruction_bytes_unchanged':True,'exact_typed_low_bindings_pinned':True,
        'bridge_mutation_rejections':rejected,'null_callback_no_caller_state_access':True,
        'original_exit_function_caller_sr_region_forwarded':True,'nullable_tail_call_same_gd_frame':True,
        'cold_binder_address':hex(binder),'cold_binder_bytes':len(binder_body),
        'complete_linked_cold_binder_pinned':True,'cold_binder_is_instruction_modelled':False,
        'reviewed_binder_checks':['descriptor alignment/span','magic/version','private callback code bounds',
            'retained region admission','initially empty slot','PROFILE mode only','OCBP publication'],
        'runtime_sd_logging_audited_by_this_check':False,'console_hitch_attribution_established':False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path);parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args()
    try:result=check(args.directory,self_test=args.self_test)
    except (OSError,ValueError,AssertionError,KeyError,struct.error) as e:
        parser.exit(2,'Native loader timeline audit refused: '+str(e)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
