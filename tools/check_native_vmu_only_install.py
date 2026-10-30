#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute compact native_passive_run's actual SH cold installation sequence.

Finite authored callees model already-separately-tested byte relocation, C
library operations, ownership checks and worker arm. The surrounding emitted
installer instructions execute without replacement. This is not an SD/game
hardware model or a full lifetime proof for arbitrary executables.
"""
from pathlib import Path
import struct

from check_vmu_native_stage_stack import Machine, canonical
from check_vmu_native_worker_gateway import symbol_bytes
from check_vmu_native_readonly import relocated

SPAN=0x1c000


def require(condition,message):
    if not condition:raise ValueError(message)


class Install(Machine):
    instruction_limit=100000

    def __init__(self,stage,low,worker,binary,wire,seed=7,*,failure=None,body=None):
        stack=stage[4]['_native_installer_stack'][0]+65536-32
        super().__init__(stage,'_native_passive_run',seed,15,stack,body=body)
        self.stage,self.low,self.worker=stage,low,worker
        self.binary,self.wire,self.failure=binary,wire,failure
        self.ram=bytearray(0x1000000)
        for kind,offset,va,pa,size,memory,flags,align in stage[2]:
            if kind==1:self.raw_write(va,stage[0][offset:offset+size])
        self.lease=0x8c900000;self.payload=0x0c600020
        self.dest=self.payload|0xa0000000;self.load=self.payload|0x80000000
        self.raw_write(self.dest,b'\xa5'*SPAN)
        self.words(self.lease,[1,116,2,1]+[0]*18+[0x0c600000,0x80000,SPAN,0x0c600000,self.payload,SPAN//32+1,0x2000])
        self.r[4]=self.lease
        self.events=[];self.purges=[];self.armed=False;self.published_worker=False;self.did_relocate=False
        self.aborted=False;self.post_arm_mutation=False
        self.sites={}
        self.control=(low[4]['_kui_vmu_observe_boot_control'][0]|0x20000000)
        self.words(self.control,[0x8ce00100,stack,3,0,0,0,0,0,0,0,0,0,0,0])
        self.targets={v[0]:n for n,v in stage[4].items() if v[2]==2 and n!='_native_passive_run'}
        for name in ('_kui_native_passive_publish', '_kui_vmu_native_worker_call'):
            self.targets[stage[4][name][0]]=name
        self.driver=stage[4]['_native_driver'][0]
        driver=[1,104,1,0x200000,0x8c010000,7,0x8c020000,0x8c02000c,0x8c023000,
                0x0c030000,0x0c031000,0x0c030100,0x8c030200,0x8c020100,0x8c020200,0,0,0]+[0]*8
        self.words(self.driver,driver)
        self.heap=stage[4]['_native_heap'][0]
        heap=[1,72,1,1,0x200000,0x8c010000,0x8c023000,0x0c023ff0,0x8c023000,
              0x8c024000,0x8c025000,0x0c030400,0x0c030500,0x0c030600,0x0c030700,
              0x0c030800,0x0c030900,0x0c030a00]
        self.words(self.heap,heap)
        self.words(self.lease+16,heap)
        self.words(0x0c023ff0,[low[4]['_kui_vmu_observe_heap_hook'][0]])
        self.words(0x0c030200,[0x8c020200])
        self.raw_write(0x8c020000,bytes.fromhex('2890e2633e02c6122990e263'))
        self.raw_write(0x8c023000,bytes(12));self.raw_write(0x8c025000,bytes(20))
        if '_native_persist_ready' in stage[4]:self.raw_write(stage[4]['_native_persist_ready'][0],b'\1')
        self.words(stage[4]['_native_exits'][0],[2,52,4,0x8c010000,0x200000,2,
                   0x8c026000,0x8c027000,0,0,0,0,0])
        self.raw_write(0x8c026000,bytes(12));self.raw_write(0x8c027000,bytes(12))
        self.words(stage[4]['_exec_bytes'][0],[0x200000])
        self.initial=(self.r.copy(),self.pr,self.sr,self.fpscr,self.fr.copy())

    def offset(self,address,count=1):
        physical=address&0x1fffffff
        require(address&0xe0000000 in (0,0x80000000,0xa0000000) and
                0x0c000000<=physical<physical+count<=0x0d000000,'Installer model access leaves mainRAM')
        return physical-0x0c000000

    def raw_write(self,address,data):
        at=self.offset(address,len(data));self.ram[at:at+len(data)]=data

    def raw_read(self,address,count):
        at=self.offset(address,count);return bytes(self.ram[at:at+count])

    def words(self,address,words):self.raw_write(address,struct.pack('<'+'I'*len(words),*words))

    def read(self,address):
        require(not address&3,'Unaligned installer read')
        return struct.unpack('<I',self.raw_read(address,4))[0]

    def write(self,address,value):
        require(not address&3,'Unaligned installer write')
        physical=address&0x1fffffff
        ranges=[(self.start_sp-8192,self.start_sp),
                (self.dest&0x1fffffff,(self.dest&0x1fffffff)+SPAN),
                (self.control&0x1fffffff,(self.control&0x1fffffff)+56),
                (0x0c030200,0x0c030204)]
        require(any((a&0x1fffffff)<=physical<physical+4<=(b&0x1fffffff) for a,b in ranges),
                'Installer writes outside stack, worker, typed control or completion slot: '+hex(address))
        if (self.control&0x1fffffff)<=physical<(self.control&0x1fffffff)+56:
            require(self.armed and self.published_worker,'Control published before initialized worker arm')
            self.events.append(('control',physical-(self.control&0x1fffffff),value))
        self.words(address,[value&0xffffffff])

    def plain(self,word,pc):
        n,m=word>>8&15,word>>4&15
        if word&0xf00f==0x6000:
            value=self.raw_read(self.r[m],1)[0];self.r[n]=(value-256 if value&128 else value)&0xffffffff;return
        if word&0xf00f==0x0006:
            self.write((self.r[0]+self.r[n])&0xffffffff,self.r[m]);return
        if word>>8==0x81:
            address=self.r[m]+(word&15)*2
            require(self.start_sp-8192<=canonical(address)<self.start_sp-1,
                    'Installer halfword store leaves its private frame')
            self.raw_write(address,struct.pack('<H',self.r[0]&0xffff));return
        if word&0xf0ff==0x4029:
            self.r[n]>>=16;return
        if word&0xf00f==0x2001:
            address=self.r[n]
            require(self.start_sp-8192<=canonical(address)<self.start_sp-1,
                    'Installer halfword store leaves its private frame')
            self.raw_write(address,struct.pack('<H',self.r[m]&0xffff));return
        if word&0xf0ff==0x00a3:
            address=self.r[n]&~31
            if self.load<=address<self.load+SPAN:
                require(not self.did_relocate,'Worker lease purge after P2 relocation')
                self.purges.append(address)
            return
        super().plain(word,pc)

    def call(self,target):
        if target==self.stop_pc:return False
        if self.base<=target<self.base+len(self.blob):return False
        name=self.targets.get(target)
        require(name is not None,'Installer calls unmodeled address '+hex(target))
        self.calls.append(name)
        result=0
        if name in ('_retail_display_restore','_retail_display_line'):
            pass
        elif name=='_native_runtime_mode':result=0
        elif name=='_native_runtime_diagnostics':result=0
        elif name=='_kui_vmu_native_heap_lease_valid':
            require(self.r[4]==self.lease,'Installer ownership checks wrong lease')
            result=0 if self.failure=='lease' else 1
        elif name=='_kui_vmu_persist_stage_handoff_matches':result=0
        elif name=='_memset':
            destination,value,count=self.r[4:7]
            require(count<=SPAN,'Installer memset exceeds known bound')
            if destination==self.dest+len(self.binary):
                require(self.did_relocate and count==SPAN-len(self.binary) and value==0,
                        'Worker BSS/stack zero fill differs from exact lease tail')
                self.events.append(('zero',destination,count))
                self.sites['zero']=self.pc
            else:require(self.start_sp-8192<=canonical(destination)<self.start_sp,'Unexpected installer memset')
            self.raw_write(destination,bytes([value&255])*count);result=destination
        elif name=='_memcpy':
            destination,source,count=self.r[4:7]
            require(count<=4096,'Installer memcpy exceeds bounded config/patch work')
            self.raw_write(destination,self.raw_read(source,count));result=destination
        elif name=='_memcmp':
            a,b,count=self.r[4:7]
            result=0 if self.raw_read(a,count)==self.raw_read(b,count) else 1
        elif name=='_kui_vmu_native_relocate':
            args=self.r[4:8]+[self.read(self.r[15]+4*i) for i in range(3)]
            expected=[self.stage[4]['__native_worker_blob_begin'][0],len(self.binary),
                      self.stage[4]['__native_worker_reloc_begin'][0],len(self.wire),self.dest,SPAN,self.load]
            require(args==expected,'Compiled relocation arguments differ from complete worker/lease')
            require(self.purges==list(range(self.load,self.load+SPAN,32)),
                    'Complete P1 lease must be purged before P2 relocation')
            if self.failure=='relocate':result=1
            else:
                self.raw_write(self.dest,relocated(self.binary,self.wire,self.dest))
                self.did_relocate=True;self.events.append(('relocate',self.dest,SPAN))
        elif name=='_passive_entry':
            entry,dest,count=self.r[4:7]
            result=int(dest==self.dest and count==len(self.binary) and
                       self.load+80<=entry<self.load+count and not entry&1)
        elif name=='_kui_native_passive_publish':
            begin,end=self.r[4:6]
            if begin==self.load:
                require(self.did_relocate and not self.armed,'Worker publication precedes relocation or follows arm')
                require(end==self.load+len(self.binary),'Worker publication wrong initialized byte span')
                require(('zero',self.dest+len(self.binary),SPAN-len(self.binary)) in self.events,
                        'Worker publication precedes complete BSS/stack zero fill')
                self.published_worker=True;self.events.append(('publish_worker',begin,end))
                self.sites['publish']=self.pc
            else:
                require(self.armed and begin==0x8c020000 and end==0x8c02000c,
                        'Unexpected SDK code publication before worker arm')
                self.events.append(('publish_submit',begin,end))
        elif name=='_kui_vmu_native_worker_call':
            require(self.published_worker,'Worker arm precedes code/cache publication')
            entry,top,config,result_pointer=self.r[4:8]
            require(entry==self.read(self.dest+12) and top==self.dest+SPAN,
                    'Worker gateway uses incorrect relocated entry or private stack')
            cfg=self.raw_read(config,2304)
            require(struct.unpack_from('<2I',cfg)==(1,2304),'Worker config ABI differs')
            require(not any(0x8ce00000<=v<0x8cfe0000 for v in struct.unpack('<576I',cfg)),
                    'Copied worker config retains a temporary-stage pointer')
            require(self.read(self.read(self.dest+48))==0x8c02000c and
                    self.read(self.read(self.dest+52))==0x8c020200 and
                    self.read(self.read(self.dest+60))==0x8c025014,
                    'Worker resume/consumer/release cells not initialized before arm')
            require(self.raw_read(self.dest+SPAN-4096,64)==struct.pack('<16I',*([0x4b535431]*16)),
                    'Worker stack canaries not initialized before arm')
            self.armed=self.failure!='arm';self.events.append(('arm',entry,top));result=1 if self.failure=='arm' else 0
        elif name=='_readonly_release_replay_valid':result=1
        elif name=='_native_sdk_coherence_install':
            require(self.armed,'SDK coherence hooks precede successful worker arm');result=1
        elif name=='_readonly_patch_entry':
            require(self.armed,'Game hook published before successful worker arm')
            address,target=self.r[4:6]
            require((address,target) in ((0x8c023000,self.low[4]['_kui_vmu_observe_return_hook'][0]),
                    (0x8c025000,self.read(self.dest+56)),
                    (0x8c026000,self.low[4]['_kui_vmu_observe_return_hook'][0]),
                    (0x8c027000,self.low[4]['_kui_vmu_observe_return_hook'][0])),'Unexpected SDK hook target')
            self.events.append(('patch_sdk',address,target))
        elif name in ('_native_evolution_fault_install','_native_evolution_trace_install'):result=0
        elif name=='_native_report':
            require(self.failure is not None,'Unmutated installer refused '+hex(self.r[4])+'/'+hex(self.r[5]))
            self.aborted=True;self.pc=self.stop_pc;return True
        else:raise ValueError('Installer calls unmodeled function '+name)
        self.abi_return(result);return True

    def check(self):
        self.run()
        if self.failure:
            require(self.aborted,'Injected installer failure did not refuse')
            require(not any(event[0]=='control' for event in self.events),'Failed install published live control')
            if self.failure in ('lease','relocate','arm'):
                require(not any(event[0]=='patch_sdk' for event in self.events),'Failed install patched SDK')
        else:
            require(self.armed and self.published_worker,'Installer returned without worker arm/publication')
            require(self.read(self.control)==self.read(self.control+4)==0 and self.read(self.control+8)==4,
                    'Installer retained high-stage callback/stack or failed live status publication')
            require(self.read(self.control+12)==self.payload and self.read(self.control+32)==self.dest+SPAN and
                    self.read(self.control+40)==self.read(self.dest+24) and
                    self.read(self.control+52)==self.read(self.dest+64),'Installed control does not pin relocated VMU entries')
            require(self.read(0x0c030200)==self.read(self.dest+20),'SDK completion slot lacks relocated hook')
            registers,pr,sr,fpscr,fr=self.initial
            require(self.r[8:]==registers[8:] and self.pr==pr and self.sr&~1==sr&~1 and
                    self.fpscr==fpscr and self.fr==fr,'Compiled installer violates preserved caller ABI')
        return self


def audit(stage,low,worker,binary,wire):
    success=refusals=0
    for seed in range(8):
        Install(stage,low,worker,binary,wire,seed).check();success+=1
    for failure in ('lease','relocate','arm'):
        Install(stage,low,worker,binary,wire,11,failure=failure).check();refusals+=1
    reference=Install(stage,low,worker,binary,wire).check()
    mutations=0
    for site in ('zero','publish'):
        body=bytearray(reference.blob)
        at=reference.sites[site]-reference.base
        opcode=struct.unpack_from('<H',body,at)[0]
        require(opcode&0xf0ff==0x400b,'Installer mutation site is not its actual JSR')
        struct.pack_into('<H',body,at,9)
        try:Install(stage,low,worker,binary,wire,body=bytes(body)).check()
        except (ValueError,AssertionError):mutations+=1
        else:raise ValueError('Installer accepted missing '+site+' call')
    return {'actual_installer_instruction_cases':success,'injected_callee_refusals':refusals,
            'actual_jsr_mutation_rejections':mutations,
            'p1_purge_bytes':SPAN,'exact_shipped_relocation_bytes_applied':True,
            'complete_worker_tail_zeroed_before_arm':True,'cache_publisher_before_arm':True,
            'sdk_hooks_after_arm':True,'live_control_pinned_after_hooks':True,
            'stage_pointers_absent_from_copied_config':True,'finite_authored_abi_callees':True,
            'console_execution_verified':False}
