#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check timed Maple shims and the returning gateways independently.

The 1152 gateway ABI cases use an adversarial ABI-compatible delay callee.
Separate cases execute the actual compiled C helpers against a read-only
TMU0 model, including every admitted profile bit and frozen-clock refusals.
The SDK integration receipt separately traverses both together. None of
these flat instruction models establishes cache, bus or console timing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

from check_native_maple_bypass import BypassGateway, OriginalSubmit, sha, call_site as bypass_call_site
from check_native_bottom_heap import ABSOLUTE_ONE, require
from check_vmu_native_readonly import ReadonlyGateway, relocated
from check_vmu_native_worker_gateway import P2, read_elf, symbol_bytes
from test_armada_ce_maple_hook import CPU, MASK
from test_vmu_native_heap_execution import verified_program_image
import profile_vmu_native_release_sh as release

ROOT=Path(__file__).resolve().parents[1]
SCHEMA='kui.native.maple-delay.linked-gateways.v1'
COMPLETION_SCHEMA='kui.native.maple-completion-delay.linked-gateways.v1'
SUBMISSION_SCHEMA='kui.native.maple-submission-delay.linked-gateways.v1'
TARGETS={'submit':59,'complete':76}
SOURCES=('tools/check_native_maple_delay.py','tools/check_native_maple_bypass.py',
    'tools/check_vmu_native_persistent.py','tools/check_vmu_native_readonly.py',
    'tools/check_vmu_native_worker_gateway.py','tools/profile_vmu_native_release_sh.py',
    'tests/test_armada_ce_maple_hook.py','tests/test_vmu_native_heap_execution.py',
    'src/loader/vmu_native_passive_hooks.S','src/loader/vmu_native_passive.c',
    'src/loader/vmu_native_maple_delay.inc','src/core/vmu_native_profile.c',
    'include/kui/vmu_native_profile.h','include/kui/toy_pilot_clock.h',
    'include/kui/native_isolation.h','tests/test_vmu_native_maple_delay.c')


class DelayGateway(BypassGateway):
    """Execute actual gateway bytes with an adversarial returning callee."""
    def __init__(self,*args):
        super().__init__(*args)
        self.original_c={self.visit,self.complete}
        self.visit=self.symbols.get('_kui_vmu_native_maple_delay_submit',self.visit)
        self.complete=self.symbols.get('_kui_vmu_native_maple_delay_complete',self.complete)

    def code(self,address,size):
        require(address not in getattr(self,'original_c',()),'Gateway reached an original Maple C callback')
        return super().code(address,size)

    def read(self,address):
        if address==self.symbols['_kui_vmu_native_passive_consumer']:
            require(self.mode=='complete' and len(self.called)==1 and self.r[15]==self.start_sp,
                    'Consumer cell read before delay return/owner-frame restoration')
            require(self.sr==self.initial[5],'Inherited SR not restored before consumer')
        return CPU.read(self,address)

    def callback(self,target):
        require(target==getattr(self,'visit' if self.mode=='submit' else 'complete'),
                'Gateway called the wrong delay shim')
        mark=len(self.reads)
        ReadonlyGateway.callback(self,target)
        # Its saved-frame assertions inspect the model, rather than execute
        # SH loads. Keep those assertion-only reads out of gateway traffic.
        del self.reads[mark:]

    def verify(self,image,driver):
        ReadonlyGateway.run(self)
        require(len(self.called)==1 and self.sr_loads==2,'Missing unique delay call/SR restoration')
        initial=self.initial
        if self.mode=='submit':
            original=OriginalSubmit(image,initial,self.manager).execute(driver[6],driver[7])
            expected=original.r
            require(self.pc==driver[7] and self.writes.count(0xa05f6c18)==1 and
                    self.mem[0xa05f6c18]==original.mem[0xa05f6c18], 'Original submit replay differs')
        else:
            expected=initial[0].copy();expected[3]=self.consumer
            require(self.pc==self.consumer and not self.writes.count(0xa05f6c18),
                    'Original completion forwarding differs')
        require(self.r==expected,'Delay gateway changed a live GPR/SP')
        require((self.pr,self.mach,self.macl,self.gbr,self.sr,self.fpscr,self.fr)==initial[1:],
                'Delay gateway changed inherited PR/SR/special/FPU state')
        owner_writes=[a for a in self.writes if self.start_sp-96<=a<self.start_sp]
        owner_reads=[a for a in self.reads if self.start_sp-96<=a<self.start_sp]
        private_writes=[a for a in self.writes if self.private_top-256<=a<self.private_top]
        private_reads=[a for a in self.reads if self.private_top-256<=a<self.private_top]
        require(len(owner_writes)==len(owner_reads)==20 and
                len(private_writes)==len(private_reads)==1,
                'Gateway frame traffic differs around the modeled delay call')
        return self


class DelayBody(release.ReleaseSH):
    """Run actual helper instructions; the only external values are TMU0 reads."""
    CLOCK={0xffc00000:(2,0xe0a),0xffd80008:(4,MASK),
           0xffd80010:(2,2),0xffd80004:(1,1),0xffd8000c:(4,None)}

    def __init__(self,path,role,*,start=10000,step=1,mutation=None,endpoint=False):
        self.enforce=False
        super().__init__(path)
        self.role,self.current,self.step_size=role,start,step
        self.mutation,self.endpoint=mutation,endpoint
        self.counter_reads=self.profiles=0
        self.timer_writes=0;self.state_writes=[];self.helper_reads=[]
        self.stack_low=self.symbols['__vmu_native_worker_stack_bottom'][0]&0x1fffffff
        self.stack_high=self.symbols['__vmu_native_worker_stack_top'][0]&0x1fffffff
        self.state=self.symbols['_state'][0]
        self.enforce=True

    def read(self,address,count=4):
        if address in self.CLOCK:
            width,value=self.CLOCK[address]
            require(count==width,'Timer read used the wrong width')
            require(self.sr&0xf0==0xf0,'Delay helper reopened interrupts')
            self.helper_reads.append(address)
            if address==0xffd8000c:
                self.counter_reads+=1
                if self.counter_reads>1:self.current=(self.current-self.step_size)&MASK
                return self.current
            if address==0xffc00000:self.profiles+=1
            if self.mutation and address==self.mutation[0] and (not self.endpoint or self.profiles>=2):
                value^=self.mutation[1]
            return value
        return super().read(address,count)

    def write(self,address,value,count=4):
        if getattr(self,'enforce',False):
            require(address not in self.CLOCK,'Delay helper wrote a timer register')
            at=address&0x1fffffff
            private=self.stack_low<=at<=self.stack_high-count
            # Actual failure witnesses are the only allowed non-stack stores.
            failure=address in {self.state+4*i for i in (16,17,18,19)} and count==4
            require(private or failure,'Delay helper wrote outside its stack/failure witness')
            if failure:self.state_writes.append((address,value))
        return super().write(address,value,count)

    def plain(self,op,pc):
        if op&0xf0ff==0x4011: # CMP/PZ Rn: signed nonnegative.
            self.executing=pc;self.instructions+=1
            self.flag(not(self.r[op>>8&15]&0x80000000))
            return
        if op&0xf0ff==0x4018: # SHLL8 Rn: T is unchanged.
            self.executing=pc;self.instructions+=1
            n=op>>8&15;self.r[n]=self.r[n]<<8&MASK
            return
        return super().plain(op,pc)

    def execute(self):
        release.STACK=(self.stack_high|0x80000000)-4
        self.sr=0x600000f0
        self.r[4]=0x8c00eb80 # Inherited frame argument is deliberately irrelevant.
        try:
            result=self.call('_kui_vmu_native_maple_delay_'+self.role,[])
        except AssertionError as error:
            require(str(error)=='Actual worker refused with code 0x4d',
                    'Unexpected actual delay failure: '+str(error))
            phase=0 if self.role=='submit' else 1
            runtime=self.read(self.state+76)
            require(len(self.state_writes)==4 and self.read(self.state+64)==phase and
                    self.read(self.state+72)==TARGETS[self.role] and
                    runtime&0xffffff00==0x4d0000|(phase<<8),
                    'Actual clock refusal lost its phase/target/status witness')
            return dict(refused=True,reason=str(error),counter_reads=self.counter_reads,
                        profiles=self.profiles,failure_writes=len(self.state_writes),
                        failure_status=runtime&255,failure_phase=phase,
                        failure_elapsed=self.read(self.state+68))
        require(not self.state_writes,'Successful delay changed private persistent state')
        require(self.profiles==2 and self.counter_reads==TARGETS[self.role]+2,
                'Actual compiled delay did not wait its exact target ticks')
        require(self.sr&0xf0==0xf0,'Actual delay changed inherited interrupt mask')
        return dict(refused=False,actual_helper_instructions=result['instructions'],
                    counter_reads=self.counter_reads,profiles=self.profiles,
                    elapsed_ticks=TARGETS[self.role]+1,timer_writes=0)


def helper_cases(build,completion_delay=False,submission_delay=False):
    rows=[]
    path=build/'native-worker.elf'
    for role in (('submit',) if submission_delay else ('complete',) if completion_delay else TARGETS):
        for start in (10000,0,1):
            row=DelayBody(path,role,start=start).execute()
            require(not row['refused'],'Advancing/wrapping timer was refused')
            rows.append(dict(role=role,name='advance-'+str(start),**row))
        for name,step in (('frozen',0),('reverse',-1)):
            row=DelayBody(path,role,step=step).execute()
            require(row['refused'],'Frozen/backward timer was accepted')
            require(row['failure_status']==(8 if name=='frozen' else 5),'Wrong clock-progress refusal status')
            if name=='frozen':require(row['counter_reads']==4097,'Actual counter-read cap differs')
            rows.append(dict(role=role,name=name,**row))
        masks={0xffc00000:0xfff,0xffd80008:MASK,0xffd80010:0x27,0xffd80004:1}
        for address,mask in masks.items():
            for bit in range(32):
                if not mask&(1<<bit):continue
                for endpoint in (False,True):
                    row=DelayBody(path,role,mutation=(address,1<<bit),endpoint=endpoint).execute()
                    require(row['refused'],'Actual compiled helper accepted an invalid clock profile')
                    require(row['failure_status']==(7 if endpoint else 3),'Wrong clock-profile refusal status')
                    rows.append(dict(role=role,name=('end' if endpoint else 'begin')+'-'+hex(address)+'-'+str(bit),**row))
    return rows


def call_site(binary,symbols,role):
    start=symbols['_kui_vmu_native_passive_'+role+'_hook']
    target=symbols['_kui_vmu_native_maple_delay_'+role]
    matches=[]
    for pc in range(start,start+128,2):
        at=(pc&0x1fffffff)-(P2&0x1fffffff)
        word=struct.unpack_from('<H',binary,at)[0]
        if word>>8!=0xd0:continue
        literal=(((pc+4)&~3)+(word&255)*4&0x1fffffff)-(P2&0x1fffffff)
        if struct.unpack_from('<I',binary,literal)[0]==target:
            require(binary[at+2:at+6]==struct.pack('<2H',0x400b,9),'Delay is not called by JSR/NOP')
            matches.append(dict(instruction_offset=at+2,literal_offset=literal))
    require(len(matches)==1,'Missing unique actual delay call site: '+role)
    original=symbols['_kui_vmu_native_passive_'+role]
    require(struct.pack('<I',original) in binary,'Original C reachability anchor was discarded')
    return matches[0]


def flag_zero_comparison():
    cc=ROOT/'.deps/sh-elf/bin/sh-elf-gcc'
    original=subprocess.check_output(['git','show','cec542aefa8ad907ed063a6b8b471d7151eef38c:src/loader/vmu_native_passive_hooks.S'],cwd=ROOT)
    flags=['-x','assembler-with-cpp','-c','-DKUI_VMU_NATIVE_CACHED_CODE=1',
           '-DKUI_VMU_NATIVE_READONLY_PROBE=1','-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1']
    with tempfile.TemporaryDirectory(prefix='kui-delay-flag-zero-') as directory:
        path=Path(directory);old=path/'old.S';old.write_bytes(original)
        for bypass in (0,1):
            blobs=[]
            for index,source in enumerate((old,ROOT/'src/loader/vmu_native_passive_hooks.S')):
                obj=path/(str(index)+'.o');text=path/(str(index)+'.text')
                subprocess.run([str(cc),*flags,'-DKUI_NATIVE_MAPLE_BYPASS_ISOLATION='+str(bypass),
                    '-DKUI_NATIVE_MAPLE_DELAY_ISOLATION=0',str(source),'-o',str(obj)],check=True,capture_output=True)
                subprocess.run([str(cc.with_name('sh-elf-objcopy')),'--dump-section','.text='+str(text),str(obj)],check=True,capture_output=True)
                blobs.append(text.read_bytes())
            require(blobs[0]==blobs[1],'Flag-zero assembly differs from the console-passed freeze')
    return dict(prior_runtime='cec542aefa8ad907ed063a6b8b471d7151eef38c',normal_equal=True,bypass_equal=True)


def completion_submit_comparison(worker,submission_delay=False):
    """Bind the non-delayed gateway to its exact compiled passing CEC body."""
    cc=ROOT/'.deps/sh-elf/bin/sh-elf-gcc'
    prior='cec542aefa8ad907ed063a6b8b471d7151eef38c'
    original=subprocess.check_output(['git','show',prior+':src/loader/vmu_native_passive_hooks.S'],cwd=ROOT)
    flags=['-x','assembler-with-cpp','-c','-DKUI_VMU_NATIVE_CACHED_CODE=1',
           '-DKUI_VMU_NATIVE_READONLY_PROBE=1','-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1',
           '-DKUI_NATIVE_MAPLE_BYPASS_ISOLATION=1']
    role='complete' if submission_delay else 'submit'
    name='_kui_vmu_native_passive_'+role+'_hook'
    with tempfile.TemporaryDirectory(prefix='kui-completion-submit-') as directory:
        path=Path(directory);old=path/'cec.S';old.write_bytes(original)
        rows=[]
        for index,source in enumerate((old,ROOT/'src/loader/vmu_native_passive_hooks.S')):
            obj=path/(str(index)+'.o')
            mode=['-DKUI_NATIVE_MAPLE_DELAY_ISOLATION=1',
                  '-DKUI_NATIVE_MAPLE_DELAY_'+('SUBMIT' if submission_delay else 'COMPLETE')+
                  '_ONLY_ISOLATION=1'] if index else []
            subprocess.run([str(cc),*flags,*mode,str(source),'-o',str(obj)],check=True,capture_output=True)
            # gcc -c produces ET_REL with no program headers. Keep the strict
            # ET_EXEC reader for the final linked worker; extract this one
            # compiler-owned object range from its typed nm symbol instead.
            symbols=subprocess.check_output([str(cc.with_name('sh-elf-nm')),'-S','--defined-only',str(obj)],text=True)
            matches=[line.split() for line in symbols.splitlines() if line.split()[-1:]==[name]]
            require(len(matches)==1 and len(matches[0])==4 and matches[0][2] in ('T','t'),
                    'CEC reference object lacks its unique typed submit function')
            address,size=(int(value,16) for value in matches[0][:2])
            raw=path/(str(index)+'.text')
            subprocess.run([str(cc.with_name('sh-elf-objcopy')),'--dump-section','.text='+str(raw),str(obj)],
                           check=True,capture_output=True)
            text_bytes=raw.read_bytes()
            require(size>0 and address+size<=len(text_bytes),'CEC reference submit escaped .text')
            body=text_bytes[address:address+size]
            text=subprocess.check_output([str(cc.with_name('sh-elf-objdump')),'-r','-j','.text',str(obj)],text=True)
            reloc=[]
            for line in text.splitlines():
                match=re.match(r'^([0-9a-fA-F]+)\s+(R_SH_\w+)\s+(\S+)',line)
                if match and address<=int(match[1],16)<address+len(body):
                    reloc.append((int(match[1],16)-address,match[2],match[3]))
            rows.append((body,reloc))
        require(rows[0]==rows[1], 'Completion-only submit differs from the exact CEC object body/relocations')
    _,linked,_=symbol_bytes(worker,name,2)
    body,relocations=rows[0];normalized=bytearray(linked)
    require(len(linked)==len(body) and relocations,'Completion-only linked submit size/relocations differ')
    bindings=[]
    for offset,kind,target in relocations:
        require(kind=='R_SH_DIR32','Unexpected submit relocation kind')
        match=re.fullmatch(r'([_A-Za-z0-9]+)(?:([+-])0x([0-9a-fA-F]+))?',target)
        require(match is not None and match[1] in worker[4], 'Unknown submit relocation target: '+target)
        # SH ET_REL uses the literal word itself as R_SH_DIR32's addend.
        # objdump may omit that raw addend from its target column.
        addend=struct.unpack_from('<I',body,offset)[0]
        expected=(worker[4][match[1]][0]+addend)&MASK
        require(struct.unpack_from('<I',linked,offset)[0]==expected,'Linked submit target differs: '+target)
        normalized[offset:offset+4]=body[offset:offset+4]
        bindings.append(dict(offset=offset,target=target,address=hex(expected)))
    require(bytes(normalized)==body,'Linked submit changed beyond exact CEC relocation targets')
    return dict(prior_runtime=prior,prior_source_sha256=sha(original),
        object_body_and_relocations_equal=True,linked_body_equal_after_exact_relocations=True,
        bytes=len(body),canonical_body_sha256=sha(body),relocation_bindings=bindings,
        **{'no_'+role+'_helper_call':True})


def check(build,startup,record,completion_delay=False,submission_delay=False):
    require(not(completion_delay and submission_delay),'Select only one delayed gateway')
    build,startup,record=map(lambda p:Path(p).resolve(),(build,startup,record))
    source,image,extent=verified_program_image(startup,record)
    driver=struct.unpack_from('<18I',record.read_bytes(),64)
    worker=read_elf(build/'native-worker.elf')
    for elf,role in ((read_elf(build/'stage.elf'),'stage'),(worker,'worker')):
        for marker in ('maple_delay','maple_bypass','reader_only','ra_tick_off'):
            require(elf[4].get('__kui_native_'+marker+'_isolation_'+role)==ABSOLUTE_ONE,'Missing paired marker: '+marker)
        require('__kui_native_e1_wait_capture_'+role not in elf[4],'Delay includes wait capture')
        marker=elf[4].get('__kui_native_maple_delay_complete_only_isolation_'+role)
        require(marker==ABSOLUTE_ONE if completion_delay else marker is None,
                'Completion-only paired marker differs: '+role)
        marker=elf[4].get('__kui_native_maple_delay_submit_only_isolation_'+role)
        require(marker==ABSOLUTE_ONE if submission_delay else marker is None,
                'Submission-only paired marker differs: '+role)
        require('__kui_native_metadata_cache_isolation_'+role not in elf[4],
                'Delay includes incompatible cached metadata')
    symbols={name:value[0] for name,value in worker[4].items()}
    binary,wire=(build/'native-worker.bin').read_bytes(),(build/'native-worker.reloc').read_bytes()
    roles=('submit',) if submission_delay else ('complete',) if completion_delay else TARGETS
    sites={role:call_site(binary,symbols,role) for role in roles}
    bypass_role='complete' if submission_delay else 'submit' if completion_delay else None
    bypass_site=bypass_call_site(binary,symbols,bypass_role) if bypass_role else None
    cec_comparison=completion_submit_comparison(worker,submission_delay) if bypass_role else None
    cases=0;writes=dict(submit_mdst=0,complete_mdst=0)
    for destination in (P2,0xac35e220,0xac700000):
        blob=relocated(binary,wire,destination)
        mapped={name:address+destination-P2 for name,address in symbols.items()}
        for alias in (0,0x80000000,0xa0000000):
            for seed in (0,1):
                for mask in range(16):
                    for t in (0,1):
                        for role in TARGETS:
                            cls=BypassGateway if role==bypass_role else DelayGateway
                            cpu=cls(seed,mask,destination,blob,mapped,role,alias,t,driver).verify(image,driver)
                            cases+=1;writes[role+'_mdst']+=cpu.writes.count(0xa05f6c18)
    mutations=[]
    for role,site in sites.items():
        for kind in ('call-omitted','original-C-restored'):
            changed=bytearray(binary)
            if kind=='call-omitted':struct.pack_into('<H',changed,site['instruction_offset'],9)
            else:struct.pack_into('<I',changed,site['literal_offset'],symbols['_kui_vmu_native_passive_'+role])
            try:DelayGateway(0,0,P2,bytes(changed),symbols,role,0x80000000,0,driver).verify(image,driver)
            except (AssertionError,ValueError) as error:mutations.append(dict(name=role+'-'+kind,reason=str(error)))
            else:raise ValueError('Delay call mutation was accepted')
    if bypass_role:
        changed=bytearray(binary);struct.pack_into('<H',changed,bypass_site,0x400b)
        try:BypassGateway(0,0,P2,bytes(changed),symbols,bypass_role,0x80000000,0,driver).verify(image,driver)
        except (AssertionError,ValueError) as error:
            mutations.append(dict(name=bypass_role+'-original-C-restored',reason=str(error)))
        else:raise ValueError('Completion-only restored submit C call was accepted')
    helpers=helper_cases(build,completion_delay,submission_delay)
    artifacts={p.name:sha(p.read_bytes()) for p in build.iterdir() if p.suffix in ('.elf','.bin','.reloc') and p.is_file()}
    provenance=json.loads((build/'entry.compile.json').read_text())
    require(provenance['source_dirty'] is False and provenance['elf_sha256']==artifacts['entry.elf'],'Unbound runtime provenance')
    return dict(schema=SUBMISSION_SCHEMA if submission_delay else COMPLETION_SCHEMA if completion_delay else SCHEMA,
        status='PASS',source_commit=provenance['commit'],
        artifact_sha256=artifacts,source_sha256={name:sha((ROOT/name).read_bytes()) for name in SOURCES},
        inputs=dict(startup_sha256=sha(source),record_sha256=sha(record.read_bytes()),extent=extent),
        C_calls=0,delay_ticks=dict(submit=59,complete=0) if submission_delay else
            dict(submit=0,complete=76) if completion_delay else TARGETS,
        completion_delay=completion_delay,submit_cec_equivalence=cec_comparison if completion_delay else None,
        **(dict(submission_delay=True,complete_cec_equivalence=cec_comparison) if submission_delay else {}),
        actual_delay_helper_executed=True,timer_writes=0,
        instruction_cases=cases,gateway_callee_model='adversarial ABI-compatible delay callee',
        delay_helper_cases=helpers,actual_helper_case_count=len(helpers),
        exact_original_submit_replay=True,inherited_registers_PR_SR_SP_preserved=True,
        gateway_frames_and_stack_traffic_retained=True,all_imasks_and_T=True,
        mdst_store_counts=writes,mutation_refusals=mutations,
        flag_zero_assembly=flag_zero_comparison(),scope=__doc__.strip())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','startup','record','output'):parser.add_argument('--'+name,type=Path,required=True)
    selection=parser.add_mutually_exclusive_group()
    selection.add_argument('--completion-delay',action='store_true',
                        help='Require exact CEC submit and only the existing76-tick completion helper')
    selection.add_argument('--submission-delay',action='store_true',
                        help='Require exact CEC completion and only the existing59-tick submit helper')
    args=parser.parse_args();report=check(args.build,args.startup,args.record,args.completion_delay,args.submission_delay)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(schema=report['schema'],status='PASS',gateway_cases=report['instruction_cases'],
        helper_cases=report['actual_helper_case_count'],output=str(args.output))))


if __name__=='__main__':main()
