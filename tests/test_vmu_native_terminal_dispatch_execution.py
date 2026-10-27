#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute supplied original selector/wrappers around production lifetime proofs.

No retail code is embedded. This integer SH test does not model game timing,
hardware caches, or the linked terminal gateway itself.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

from test_vmu_native_heap_execution import SH, physical

ROOT=Path(__file__).resolve().parents[1]
READER=ctypes.CFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_uint32))
SHIM=r'''
#include "kui/vmu_native_terminal_dispatch.h"
#include "kui/vmu_native_lifetime.h"
struct context {const struct kui_vmu_native_heap_lease *lease;
 const struct kui_vmu_native_exit *exits;kui_vmu_native_heap_read read;uint32_t terminal;};
static bool adapted(void *opaque,uint32_t address,uint32_t *value) {
 struct context *c=opaque;
 if(!c->read(NULL,address,value))return false;
 (void)kui_vmu_native_terminal_dispatch_normalize(c->exits,c->read,NULL,c->terminal,
  address,c->lease->owner.dispatch_cell,c->lease->owner.dispatch_table,value);
 return true;
}
bool check(unsigned kind,const struct kui_vmu_native_heap_lease *lease,
 const struct kui_vmu_native_exit *exits,kui_vmu_native_heap_read read,
 uint32_t terminal,uint32_t payload) {
 struct context c={lease,exits,read,terminal};
 if(kind==0u)return kui_vmu_native_heap_lease_valid(lease,read,NULL,1u);
 if(kind==1u)return kui_vmu_native_heap_lease_identity_valid(lease,adapted,&c,1u);
 if(kind==2u)return kui_vmu_native_heap_lease_valid(lease,adapted,&c,1u);
 if(kind==3u)return kui_vmu_native_lifetime_release_safe(lease,adapted,&c,1u,payload);
 struct kui_vmu_native_lifetime_plan plan;
 return kui_vmu_native_lifetime_prepare(&plan,lease,adapted,&c,1u,NULL,0u) &&
  kui_vmu_native_lifetime_identity_prepared_inline(&plan,adapted,&c,1u);
}
'''


def prove(startup):
    image=startup.read_bytes();load=0x8c010000;stack=0x8c00ec00;low_terminal=0x8c005400
    with tempfile.TemporaryDirectory(prefix="kui-terminal-dispatch-") as temporary:
        temporary=Path(temporary);(temporary/'shim.c').write_text(SHIM)
        libfile=temporary/'proof.so'
        sources=('src/core/vmu_native_heap.c','src/core/vmu_native_lifetime.c',
            'src/core/vmu_native_exit.c','src/core/vmu_native_patch.c','src/core/data.c')
        subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O2',
            '-Wall','-Wextra','-Werror','-Wpedantic','-shared','-fPIC','-I',str(ROOT/'include'),
            str(temporary/'shim.c'),*[str(ROOT/x) for x in sources],'-o',str(libfile)],check=True)
        c=ctypes.CDLL(str(libfile));ptr=ctypes.c_void_p;word=ctypes.c_uint32
        owner=ctypes.create_string_buffer(72);call=ctypes.create_string_buffer(20)
        exits=ctypes.create_string_buffer(48);source=ctypes.create_string_buffer(image)
        for name in ('kui_vmu_native_heap_call_discover','kui_vmu_native_exit_discover'):
            getattr(c,name).restype=ctypes.c_bool
        c.kui_vmu_native_heap_discover.argtypes=[ptr,ctypes.c_size_t,word,ptr]
        c.kui_vmu_native_heap_call_discover.argtypes=[ptr,ctypes.c_size_t,ptr,ptr]
        c.kui_vmu_native_exit_discover.argtypes=[ptr,ctypes.c_size_t,word,ptr]
        assert c.kui_vmu_native_heap_discover(source,len(image),load,owner)==1
        assert c.kui_vmu_native_heap_call_discover(source,len(image),owner,call)
        assert c.kui_vmu_native_exit_discover(source,len(image),load,exits)
        h=struct.unpack('<18I',owner.raw);e=struct.unpack('<12I',exits.raw)
        assert e[2]==5 and e[5]==3
        common,table=e[8],e[10];begin,count=struct.unpack('<5I',call.raw)[3:]
        model=SH(image);lease_bytes=0x60000
        _,_,init_steps=model.run(h[6],[begin,count],stack)
        payload,_,allocation_steps=model.run(h[9],[lease_bytes],stack);header=payload-32
        model.store_block(payload,bytes([0xa5])*lease_bytes)
        lease=ctypes.create_string_buffer(struct.pack('<29I',1,116,2,1,*h,begin,count,
            lease_bytes,header,payload,lease_bytes//32+1,count//32-lease_bytes//32-1))
        read_errors=[]
        @READER
        def read(_context,address,out):
            try:
                assert not address&3
                out[0]=model.read(address);return True
            except (AssertionError,IndexError) as error:
                read_errors.append(str(error));return False
        c.check.argtypes=[word,ptr,ptr,READER,word,word];c.check.restype=ctypes.c_bool
        checks=0
        def check(kind,candidate=0,expected=True):
            nonlocal checks
            checks+=1
            assert bool(c.check(kind,lease,exits,read,low_terminal,candidate))==expected,(kind,hex(candidate),expected)
            assert not read_errors
        for kind in range(5):check(kind,0)
        retained=model.block(header,lease_bytes+32)
        # Actual original application selector wrapper: only dispatch changes.
        _,writes,switch_steps=model.run(common+0x90,[],stack)
        assert len(writes)==1 and writes[0][1]==h[11] and model.read(h[11])==table
        for kind in range(5):check(kind,0,False)
        patch=(ctypes.c_uint16*6)();c.kui_vmu_native_patch_encode.argtypes=[word,word,ptr]
        c.kui_vmu_native_patch_encode.restype=ctypes.c_bool
        assert c.kui_vmu_native_patch_encode(common,low_terminal,patch)
        model.store_block(common,bytes(patch))
        check(0,0,False)
        for kind in range(1,5):check(kind,0)
        check(3,payload,False)
        # Execute every actual callback through the newly encoded terminal
        # patch. Stop at the low gateway boundary, before its separate linked
        # implementation, and retain the original model's complete memory.
        class TerminalReached(Exception):
            pass
        raw_model_read=model.read
        def stop_at_terminal(address,size=4):
            if address==low_terminal and size==2:
                raise TerminalReached
            return raw_model_read(address,size)
        terminal_trials=0
        for index in range(4):
            entry=raw_model_read(table+4*index)
            for seed in range(8):
                model.r=[((0x11335577+0x01030507*i)^seed)&0xffffffff for i in range(16)]
                first=len(model.writes);model.read=stop_at_terminal
                try:
                    model.run(entry,[0xffffffff^seed,payload+seed*32,lease_bytes,0x12345678],stack)
                except TerminalReached:
                    pass
                else:
                    raise AssertionError('Callback returned instead of reaching terminal')
                finally:
                    model.read=raw_model_read
                assert model.r[15]==stack and model.pr==0xdeadbeee
                assert all(physical(stack-4)<=physical(w[1]) and
                    physical(w[1])+w[3]<=physical(stack) for w in model.writes[first:])
                assert model.block(header,lease_bytes+32)==retained
                terminal_trials+=1
        # Exercise real original allocate/free code while the alternate route
        # is selected; every original release first passes the production guard.
        allocations=[];allocator_steps=0
        for index in range(16):
            result,_,steps=model.run(h[9],[64+index*96],stack);allocator_steps+=steps
            assert result and physical(result)<physical(header)
            allocations.append(result)
            check(1);check(2);check(4)
        for result in allocations[::2]+allocations[1::2]:
            check(3,result)
            _,_,steps=model.run(h[10],[result],stack);allocator_steps+=steps
            check(1);check(2);check(4)
        assert model.block(header,lease_bytes+32)==retained
        # An unrecognized table, mutated callback, missing hook, and unrelated
        # identity corruption all remain failures under the new reader.
        for bad in (table+4,table^0x20000000,0,0xffffffff):
            model.write(h[11],bad);check(2,0,False)
        model.write(h[11],table)
        mutations=[(table,1),(common+0x26,1),
            (common&~3,1<<((common&2)*8)),(h[15],1),(header+4,1)]
        for at,bit in mutations:
            old=model.read(at);model.write(at,old^bit);check(2,0,False);model.write(at,old)
        # The original restore wrapper restores the raw, unadapted proof too.
        _,writes,restore_steps=model.run(common+0x98,[],stack)
        assert len(writes)==1 and writes[0][1]==h[11] and model.read(h[11])==h[12]
        for kind in range(5):check(kind,0)
        assert model.block(header,lease_bytes+32)==retained
        return dict(schema='kui.native.original-terminal-dispatch-execution.v1',
            source_bytes=len(image),source_sha256=hashlib.sha256(image).hexdigest(),
            source_family=5,common_handler=hex(common),alternate_table=hex(table),
            dispatch_cell=hex(h[11]),lease_payload=hex(payload),lease_bytes=lease_bytes,
            original_initializer_instructions=init_steps,original_allocation_instructions=allocation_steps,
            original_switch_instructions=switch_steps,original_restore_instructions=restore_steps,
            original_subsequent_allocator_operations=32,original_subsequent_allocator_instructions=allocator_steps,
            original_callback_executions_to_patched_terminal_boundary=terminal_trials,
            production_identity_full_list_prepared_and_release_checks=checks,
            raw_alternate_refused=True,alternate_without_terminal_hook_refused=True,
            exact_installed_capability_admitted=True,lease_header_and_entire_384k_preserved=True,
            unknown_changed_and_unrelated_identity_mutants_refused=True,
            limits=['Integer SH and production C ownership model, not observed console execution.',
                'The low terminal gateway and actual cache publication require separate linked/hardware proofs.'])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup',type=Path,required=True)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();report=prove(args.startup)
    text=json.dumps(report,indent=2)+'\n'
    if args.output:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(text)
    print(text,end='')
