#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute native isolation workers against the original SDK.

No production pin is changed. Actual linked C guards, packet filters, protocol
and owner-supplied SDK instructions execute in flat coherent RAM. Only the
Maple hardware, the auxiliary ABI boundary, and assembly save/restore bridges
are modeled. A VMU-enabled block read stops at its real low SD-loan entry, before
hardware or an acknowledgement; no successful storage result is substituted.
"""
import argparse
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
from profile_vmu_native_record_worker_sh import (
    RecordWorker, Record, fresh_verify, make_handoff, CFG, MDSTAR, MDST, MASK)
from check_vmu_native_worker_gateway import read_elf
from test_vmu_native_sdk_poll_execution import PollSH
from profile_vmu_native_record_worker_sh import OWNER_STACK, RETURN

ONE = (1, 0, 0, 0xfff1)
LEASE = 0x60000
STATS = ('prepared', 'completed', 'advertisements', 'replies', 'backend_calls',
         'devinfo', 'allinfo', 'media', 'reads', 'failures', 'flags',
         'last_request', 'last_reply', 'last_rx')
BACKENDS = ('_persistent_handler', '_persistent_lease',
            '_kui_vmu_durable_handle', '_kui_vmu_durable_transaction',
            '_kui_vmu_native_persistent_sd_transaction')
SOURCE_FILES = ('tools/check_native_isolation_linked.py',
    'tools/profile_vmu_native_record_worker_sh.py',
    'tools/profile_vmu_native_sdk_worker_sh.py',
    'tests/test_vmu_native_sdk_poll_execution.py',
    'src/loader/vmu_native_passive.c', 'src/loader/vmu_native_reader_capture.inc',
    'src/loader/vmu_native_phase.inc', 'src/loader/vmu_native_wait_capture.inc',
    'src/loader/vmu_native_maple_delay.inc', 'src/loader/vmu_native_metadata_cache.inc',
    'src/loader/vmu_native_passive_hooks.S',
    'src/loader/vmu_readonly_overlay.c', 'include/kui/native_isolation.h')


def sha(data): return hashlib.sha256(data).hexdigest()

def require(ok, message):
    if not ok: raise ValueError(message)


class LoanBoundary(Exception): pass


class IsolationWorker(RecordWorker):
    def __init__(self, startup, build, handoff, record, variant, tick_off=False, wait_capture=False,
                 maple_bypass=False, maple_delay=False, cached_metadata=False, completion_delay=False,
                 submission_delay=False, post_start_delay=False, split_controller=False):
        self.variant = variant
        self.tick_off = tick_off
        self.wait_capture = wait_capture
        self.maple_bypass = maple_bypass
        self.maple_delay = maple_delay
        self.completion_delay = completion_delay
        self.submission_delay = submission_delay
        self.post_start_delay = post_start_delay
        self.split_controller = split_controller
        require(not split_controller or (tick_off and not any((wait_capture,maple_bypass,maple_delay,
                    cached_metadata,completion_delay,submission_delay,post_start_delay))),
                'Split controller requires TICK off and real callbacks without other diagnostics')
        require(not(completion_delay and submission_delay), 'Select only one Maple delay side')
        require(not post_start_delay or submission_delay, 'Post-start delay requires submission delay')
        self.cached_metadata = cached_metadata
        self.flat_ocbi = []
        self.low = read_elf(build/'resident-sci.elf')
        self.sd_entry = self.low[4]['_kui_vmu_observe_sd_lease'][0]
        sd_symbol = self.low[4]['_kui_vmu_observe_sd_lease']
        require(sd_symbol[2] == 2 and sd_symbol[3] < len(self.low[3]),
                'SD loan boundary lacks a typed resident ELF entry')
        sd_section = self.low[3][sd_symbol[3]]
        require(sd_section[1] == 1 and sd_section[2]&6 == 6 and
                sd_section[3] <= self.sd_entry < self.sd_entry+2 <= sd_section[3]+sd_section[5],
                'SD loan boundary escaped its actual executable resident section')
        self.calls, self.tick_calls = Counter(), 0
        self.c_callback_calls = Counter()
        self.tick_entry = None
        self.inject_command, self.injected_rx = None, None
        self.last_aux_arguments = None
        self.loan_boundary = None
        super().__init__(startup, build, handoff, record)
        stage, worker = read_elf(build/'stage.elf'), read_elf(build/'native-worker.elf')
        require(cached_metadata is (variant == 'metadata_cache'),
                'Cached metadata must use its explicit combined variant')
        require(split_controller is (variant == 'split_controller'),
                'Split controller must use its explicit combined variant')
        marker = '__kui_native_'+variant+'_isolation_'
        for image, role in ((stage,'stage'),(worker,'worker')):
            require(image[4].get(marker+role) == ONE, 'Isolation marker differs: '+role)
            require(image[4].get('__kui_native_isolation_lease_bytes') ==
                    (LEASE,0,0,0xfff1), 'Isolation lease is not exact compiled384KiB')
            require(image[4].get('__kui_native_bottom_heap_'+role) == ONE,
                    'Isolation lacks current bottom placement: '+role)
            for other in {'reader_only', 'standard_vmu', 'metadata_cache', 'split_controller'} - {variant}:
                require('__kui_native_'+other+'_isolation_'+role not in image[4],
                        'Conflicting isolation marker: '+other+'/'+role)
        for image, role in ((stage,'stage'),(worker,'worker')):
            tick_marker = image[4].get('__kui_native_ra_tick_off_isolation_'+role)
            require(tick_marker == ONE if tick_off else tick_marker is None,
                    'Controller TICK isolation marker differs: '+role)
        require(not tick_off or variant in ('reader_only', 'metadata_cache','split_controller'),
                'TICK isolation belongs only to a New reader variant')
        require(not cached_metadata or (tick_off and not wait_capture and not maple_bypass and not maple_delay),
                'Cached metadata requires real callbacks with TICK off and no other diagnostic')
        for image, role in ((stage,'stage'),(worker,'worker')):
            capture_marker=image[4].get('__kui_native_e1_wait_capture_'+role)
            require(capture_marker == ONE if wait_capture else capture_marker is None,
                    'Evolution wait-capture marker differs: '+role)
        require(not wait_capture or (tick_off and variant=='reader_only'),
                'Evolution wait capture requires the reader-only TICK-off control')
        for image, role in ((stage,'stage'),(worker,'worker')):
            bypass_marker=image[4].get('__kui_native_maple_bypass_isolation_'+role)
            require(bypass_marker == ONE if maple_bypass else bypass_marker is None,
                    'Maple C-gateway bypass marker differs: '+role)
        require(not maple_bypass or (tick_off and variant=='reader_only' and not wait_capture),
                'Maple C-gateway bypass requires reader-only TICK-off without wait capture')
        for image, role in ((stage,'stage'),(worker,'worker')):
            marker=image[4].get('__kui_native_maple_delay_isolation_'+role)
            require(marker == ONE if maple_delay else marker is None,
                    'Maple delay marker differs: '+role)
        require(not maple_delay or maple_bypass, 'Maple delay requires actual C bypass')
        require(not completion_delay or maple_delay, 'Completion delay requires Maple delay')
        require(not submission_delay or maple_delay, 'Submission delay requires Maple delay')
        for image, role in ((stage,'stage'),(worker,'worker')):
            marker=image[4].get('__kui_native_maple_delay_complete_only_isolation_'+role)
            require(marker == ONE if completion_delay else marker is None,
                    'Maple completion-only delay marker differs: '+role)
            marker=image[4].get('__kui_native_maple_delay_submit_only_isolation_'+role)
            require(marker == ONE if submission_delay else marker is None,
                    'Maple submission-only delay marker differs: '+role)
            marker=image[4].get('__kui_native_maple_delay_post_start_isolation_'+role)
            require(marker == ONE if post_start_delay else marker is None,
                    'Maple post-start delay marker differs: '+role)
        self.lease_bytes = LEASE
        self.worker_physical_high = self.worker_physical_low+LEASE
        self.lease_geometry_source = 'Paired actual absolute isolation lease and bottom markers'
        self.build_inputs['stage.elf'] = (build/'stage.elf').read_bytes()
        self.tracked_entries = {self.symbols[name][0]:name for name in BACKENDS if name in self.symbols}
        self.callback_entries = {self.symbols['_kui_vmu_native_passive_'+name][0]:name
                                 for name in ('submit','complete')}
        if split_controller:
            self.callback_entries[self.symbols['_kui_vmu_native_passive_split_submit'][0]]='split_submit'
        if variant == 'standard_vmu':
            for name in ('__kui_native_readahead_worker','__kui_native_dual_readers_worker'):
                require(name not in worker[4], 'Standard isolation contains background worker capability')
            for name in ('_kui_native_ra_reader_ops','_kui_retail_sdk_sci_entry'):
                require(name not in self.low[4], 'Standard isolation contains New reader entry')
        else:
            require(worker[4].get('__kui_native_readahead_worker') == ONE and
                    worker[4].get('__kui_native_dual_readers_worker') == ONE,
                    'New reader isolation lost its complete reader worker')

    def worker(self, name, args):
        if name == '_kui_vmu_native_passive_arm':
            # Bind the authored config to the real low ELF role, so a storage
            # test stops at that exact entry rather than a fabricated address.
            self.write(args[0]+2236, self.sd_entry)
        return super().worker(name,args)

    def plain(self, op, pc):
        if op&0xf0ff == 0x0093:  # OCBI; this replay deliberately has coherent flat RAM.
            require(self.cached_metadata, 'Unexpected cache invalidation outside the combined variant')
            address = self.r[op>>8&15]
            require(address&0xe0000000 == 0x80000000 and not address&31,
                    'Metadata cache invalidation lacks its aligned P1 line')
            self.flat_ocbi.append(dict(pc=hex(pc),address=hex(address),coherent_memory_noop=True))
            self.executing=pc;self.instructions+=1
            return
        return super().plain(op,pc)

    def step(self):
        if getattr(self,'inside_worker',False):
            if self.pc in getattr(self,'callback_entries',{}):
                self.c_callback_calls[self.callback_entries[self.pc]]+=1
            if self.pc in getattr(self,'tracked_entries',{}):
                name=self.tracked_entries[self.pc];self.calls[name]+=1
                require(self.variant!='reader_only',
                        'Reader-only isolation reached VMU/storage backend: '+name)
            if self.pc == self.sd_entry:
                require(self.variant in ('standard_vmu', 'metadata_cache','split_controller'),
                        'Reader-only isolation reached SD loan')
                require(self.r[4]==self.symbols['_kui_vmu_native_persistent_sd_transaction'][0] and
                        self.r[5]==self.symbols['_durable_card'][0] and self.sr&0xf0==0xf0,
                        'Actual SD-loan call lost its typed transaction/context or masked scope')
                self.loan_boundary=dict(entry=hex(self.pc),callback=hex(self.r[4]),
                                        context=hex(self.r[5]),imask15=True)
                self.calls['actual_low_sd_loan_boundary']+=1
                raise LoanBoundary
            if self.tick_entry is not None and self.pc == self.tick_entry:
                require(self.variant in ('reader_only','metadata_cache','split_controller'),
                        'Standard isolation called auxiliary code')
                require(self.r[4:8] == [2,0,self.tick_region,0], 'New reader TICK arguments differ')
                require(self.sr&0xf0 == 0xf0, 'New reader TICK escaped masked worker scope')
                self.tick_calls+=1;self.last_aux_arguments=self.r[4:8].copy()
                continuation=self.pr
                for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd)&MASK
                self.r[0]=1;self.sr^=1;self.pr^=0x13579135;self.pc=continuation
                return
        return super().step()

    def seed_tick_boundary(self):
        require(self.variant in ('reader_only','metadata_cache','split_controller'),
                'Only New reader has an auxiliary boundary')
        lease=self.actual_heap_payload&0x1fffffff|0x80000000
        manifest=lease+0x20000;region=lease+0x20400;aux=(lease+0x21000)|0x20000000
        self.tick_region=region;self.tick_entry=lease+0x22000
        context=aux+64
        self.write(manifest+44,1);self.write(manifest+48,1)
        self.write(region+0x27c,manifest);self.write(region+0x530,0x53434931)
        values=(0x4b524131,1,64,self.tick_entry,self.tick_entry+2,context,
                lease+0x24000,(lease+0x27000)|0x20000000,
                (lease+0x28000)|0x20000000,(lease+0x28000)|0x20000000,0,0,0,0,0,0)
        self.store_block(aux,struct.pack('<16I',*values))
        self.write(region+0x500,self.tick_entry);self.write(region+0x504,context)

    def bridge_submit(self):
        if self.inject_command is not None:
            self.inject_packet(self.inject_command)
            self.inject_command=None
        if not self.split_controller:return super().bridge_submit()
        before=self.r.copy(),self.pr,self.sr,self.pc,self.macl,self.instructions
        self.frame('submit')
        self.submit_counts=self.worker('_kui_vmu_native_passive_split_submit',[self.frame_address])
        self.submit_callbacks+=1
        self.r,self.pr,self.sr,self.pc,self.macl,self.instructions=before

    def split_callback_counts(self):
        return dict(tiny_submit_calls=self.c_callback_calls['split_submit'],
                    full_submit_fallback_calls=self.c_callback_calls['submit'],
                    complete_calls=self.c_callback_calls['complete'])

    def inject_packet(self, command):
        # Add one independently authored virtual packet to an otherwise real
        # original-SDK transfer before the worker snapshots it. It is not an
        # assertion that the original game issued this particular command.
        source=self.mmio[MDSTAR];at=source;used=[];last=None
        for _ in range(48):
            control=self.read(at)
            if control==0x700:at+=4;continue
            used.append(self.read(at+4));last=at
            at+=12+4*(control&255)
            if control&0x80000000:break
        require(last is not None and at-source<=0x2f00, 'Authored packet lacks bounded original transfer')
        bank=0 if source==self.record.tx else 1
        require(source==self.record.tx+bank*0x3000, 'Authored packet selected unknown original TX bank')
        rx=self.record.rx_slots[bank*24+2]
        require(rx and rx not in used, 'Authored A2 RX slot is not independently owned')
        payload=b'' if command in (1,2) else struct.pack('<2I',0x02000000,0xff000000 if command==11 else 0)
        words=len(payload)//4
        self.write(last,self.read(last)&0x7fffffff)
        self.store_block(at,struct.pack('<3I',0x80000000|words,rx,(words<<24)|(2<<8)|command)+payload)
        self.store_block(rx,bytes([0xa5])*1024)
        self.injected_rx=rx

    def overlay_stats(self):
        address=self.symbols['_overlay'][0]
        return dict(zip(STATS,struct.unpack('<14I',self.block(address,56))))


class BypassWorker(IsolationWorker):
    """Execute the actual patched SDK and complete assembly gateways.

    Only this explicit diagnostic disables the legacy assembly-to-C bridges.
    The linked save/mask/private-stack/restore/replay/tail instructions execute.
    """
    def __init__(self,*args,**kwargs):
        self.post_start_current=None;self.post_start_submissions=[]
        super().__init__(*args,**kwargs)
        require(self.maple_bypass,'Actual bypass runner requires paired explicit markers')
        self.bypass_running=False
        self.mach,self.gbr=0x12345678,0x8c556678
        self.gateway_counts=Counter()
        self.gateway_ranges={name:(self.symbols['_kui_vmu_native_passive_'+name+'_hook'][0],
            self.symbols['_kui_vmu_native_passive_'+name+'_hook'][0]+
            self.symbols['_kui_vmu_native_passive_'+name+'_hook'][1]) for name in ('submit','complete')}
        names=(*BACKENDS,'_kui_vmu_native_passive_submit','_kui_vmu_native_passive_complete',
               '_reader_cooperative_service','_reader_readahead_visit','_native_wait_capture_observe')
        self.forbidden_maple_entries={self.symbols[name][0]:name for name in names if name in self.symbols}
        self.delay_entries={}
        self.delay_calls=Counter();self.delay_visits=[];self.delay_active=None
        self.delay_counter=0xffff0000;self.delay_counter_reads=0;self.delay_timer_writes=0
        if self.maple_delay:
            roles=(('complete',) if self.completion_delay else
                   ('submit',) if self.submission_delay else ('submit','complete'))
            self.delay_entries={self.symbols['_kui_vmu_native_maple_delay_'+name][0]:name
                                for name in roles}
            for name in {'submit','complete'}-set(roles):
                symbol='_kui_vmu_native_maple_delay_'+name
                if symbol in self.symbols:
                    self.forbidden_maple_entries[self.symbols[symbol][0]]=name+' delay helper'

    def post_start_clock_read(self,address):
        trace=self.post_start_current
        if not self.post_start_delay or trace is None or trace['first_clock_read'] is not None:return
        require(len(trace['mdst_writes'])==1,
                'Post-start first clock read did not follow exactly one MDST write')
        trace['mdst_writes_before_first_clock_read']=len(trace['mdst_writes'])
        trace['first_clock_read']=dict(address=hex(address),pc=hex(self.pc),instruction=self.instructions)

    def post_start_tail_state(self):
        manager=self.read(self.record.driver[9])
        lock=self.read(self.record.driver[11])
        return dict(fc0=self.read(manager+0xfc0),lockbyte=self.read(lock,1))

    def post_start_pending_observe(self):
        trace=self.post_start_current
        if not self.post_start_delay or trace is None or not trace['pending_bit12']:return
        imask=(self.sr>>4)&15
        state=self.post_start_tail_state()
        ready=state['fc0']==state['lockbyte']==0
        trace['pending_instruction_samples']+=1
        trace['min_imask']=min(trace['min_imask'],imask)
        if not ready and imask<4:
            trace['premature_dispatches']+=1
            raise ValueError('Pending Maple priority4 became eligible before SDK tail publication')
        if ready and trace['first_publication'] is None:
            trace['first_publication']=dict(pc=hex(self.pc),instruction=self.instructions,**state)

    def dma(self):
        super().dma()
        if self.post_start_delay and self.post_start_current is not None:
            require(self.mmio[MDST]==0,'Instant-completion model did not retire MDST')
            self.post_start_current['pending_bit12']=True

    def read(self,address,count=4):
        if getattr(self,'bypass_running',False) and self.maple_delay:
            # Author only the existing read-only clock profile. Each TCNT read
            # advances the downcounter one tick; this is no CPU-cycle model.
            clock={0xffc00000:(2,0xe0a),0xffd80008:(4,0xffffffff),
                   0xffd80010:(2,2),0xffd80004:(1,1)}
            if address in clock:
                self.post_start_clock_read(address)
                width,value=clock[address]
                require(count==width,'Delay clock read width differs')
                return value
            if address==0xffd8000c:
                self.post_start_clock_read(address)
                require(count==4,'Delay TCNT read width differs')
                value=self.delay_counter;self.delay_counter=(value-1)&MASK
                self.delay_counter_reads+=1
                return value
        if getattr(self,'bypass_running',False) and count==2:
            return PollSH.read(self,address,count)
        return super().read(address,count)

    def write(self,address,value,count=4):
        if (getattr(self,'bypass_running',False) and self.post_start_delay and
                self.post_start_current is not None and address==MDST):
            require(count==4,'Post-start MDST write used the wrong width')
            self.post_start_current['mdst_writes'].append(
                dict(pc=hex(self.pc),instruction=self.instructions,value=hex(value&MASK)))
        if getattr(self,'bypass_running',False) and self.maple_delay:
            require(address not in (0xffc00000,0xffd80008,0xffd80010,0xffd80004,0xffd8000c),
                    'Delay helper wrote a clock register')
        return super().write(address,value,count)

    def plain(self,op,pc):
        if getattr(self,'bypass_running',False):
            n=op>>8&15;kind=op&0xf0ff
            stores={0x4002:'mach',0x4012:'macl',0x4013:'gbr'}
            loads={0x4006:'mach',0x4016:'macl',0x4017:'gbr'}
            if kind in stores:
                self.r[n]=(self.r[n]-4)&MASK;self.write(self.r[n],getattr(self,stores[kind]))
                self.instructions+=1;return
            if kind in loads:
                setattr(self,loads[kind],self.read(self.r[n]));self.r[n]=(self.r[n]+4)&MASK
                self.instructions+=1;return
            if pc==self.record.submit_hook:
                return PollSH.plain(self,op,pc)
        return super().plain(op,pc)

    def step(self):
        if not getattr(self,'bypass_running',False):return super().step()
        self.post_start_pending_observe()
        trace=self.post_start_current
        sr_op=None
        if self.post_start_delay and trace is not None:
            if self.pc==self.gateway_ranges['submit'][0]:
                trace['inherited_imask']=(self.sr>>4)&15
                trace['gateway_inherited_sr']=hex(self.sr)
                trace['gateway_entry_observed']=True
                require(trace['inherited_imask'] in (6,15),
                        'Post-start actual gateway inherited an unadmitted interrupt mask')
            op=self.read(self.pc,2)
            if op&0xf0ff in (0x0002,0x4003,0x400e,0x4007):
                sr_op=dict(pc=hex(self.pc),opcode=hex(op),sr_before=hex(self.sr))
        if self.delay_active is not None and self.pc==self.delay_active['return_pc']:
            visit=self.delay_active
            visit['counter_reads']=self.delay_counter_reads-visit.pop('first_counter_read')
            require(visit['counter_reads']==visit['target_ticks']+2,
                    'Actual delay helper counter interval differs from its target and endpoint checks')
            self.delay_visits.append(visit);self.delay_active=None
        if self.pc in self.delay_entries:
            require(self.delay_active is None,'Delay helpers unexpectedly nested')
            name=self.delay_entries[self.pc];self.delay_calls[name]+=1
            self.delay_active=dict(kind=name,target_ticks=59 if name=='submit' else 76,
                return_pc=self.pr,first_counter_read=self.delay_counter_reads)
        require(self.pc not in self.forbidden_maple_entries,
                'Bypass gateway reached C payload: '+self.forbidden_maple_entries.get(self.pc,''))
        require(self.pc!=self.sd_entry and self.pc!=self.tick_entry,
                'Bypass gateway reached storage or auxiliary work')
        name=next((name for name,(lo,hi) in self.gateway_ranges.items() if lo<=self.pc<hi),None)
        no_call_role=('submit' if self.completion_delay else 'complete' if self.submission_delay else None)
        if no_call_role is not None and name==no_call_role:
            op=self.read(self.pc,2)
            require(op&0xf0ff not in (0x400b,0x0003) and op>>12!=0xb,
                    ('Completion-only' if self.completion_delay else 'Submission-only')+
                    ' '+name+' gateway executed a helper call')
        before=self.instructions
        super().step()
        if name:self.gateway_counts[name]+=self.instructions-before
        if sr_op is not None:
            trace['sr_instructions'].append(dict(sr_op,sr_after=hex(self.sr)))
        self.post_start_pending_observe()

    def bridge_submit(self):
        raise ValueError('Bypass runner must execute the actual patched submit gateway')

    def submit(self):
        r=self.record;before=self.gateway_counts['submit']
        if self.post_start_delay:
            imask=(self.owner_sr>>4)&15
            require(imask in (6,15),'Post-start SDK trial requires admitted inherited IMASK6 or15')
            self.post_start_current=dict(inherited_imask=imask,caller_imask=imask,gateway_entry_observed=False,
                mdst_writes=[],first_clock_read=None,
                mdst_writes_before_first_clock_read=0,pending_bit12=False,pending_instruction_samples=0,
                min_imask=15,premature_dispatches=0,first_publication=None,sr_instructions=[])
        self.pc,self.pr,self.r[15],self.sr=r.submit_caller,RETURN,OWNER_STACK,self.owner_sr
        self.bypass_running=True
        try:
            self.until(r.submit_entry)
            steps=self.until(r.submit_caller+6)
        finally:self.bypass_running=False
        if self.post_start_delay:
            trace=self.post_start_current
            final=self.post_start_tail_state()
            require(len(trace['mdst_writes'])==1 and trace['first_clock_read'] is not None and
                    trace['mdst_writes_before_first_clock_read']==1 and trace['gateway_entry_observed'] and
                    trace['inherited_imask']==trace['caller_imask'],
                    'Post-start submit did not execute exactly one MDST before its clock helper')
            require(trace['pending_bit12'] and self.mmio[MDST]==0 and
                    final['fc0']==final['lockbyte']==0 and trace['first_publication'] is not None and
                    trace['premature_dispatches']==0 and trace['min_imask']>=4,
                    'Post-start pending IRQ escaped the bounded SDK tail publication proof')
            trace.update(mdst_write_events=trace['mdst_writes'],mdst_writes=len(trace['mdst_writes']),
                first_clock_address=trace['first_clock_read']['address'],MDST=self.mmio[MDST],
                maple_irq_priority=4,sdk_tail_published=True,final_sdk_state=final)
            self.post_start_submissions.append(trace)
            self.post_start_current=None
        count=self.gateway_counts['submit']-before
        require(count>0 and self.r[0]==0,'Original submit did not execute and return through the compiled gateway')
        self.submit_counts=dict(instructions=count,original_sdk_instructions=steps)
        self.submit_callbacks+=1
        return steps

    def complete(self):
        r=self.record;before=self.gateway_counts['complete']
        self.pc,self.pr,self.r[15],self.sr=r.completion_wrapper,RETURN,OWNER_STACK,self.owner_sr
        self.bypass_running=True
        try:
            prefix=self.until(self.read(r.driver[12]))
            owner_sp=self.r[15]
            # The exact consumer returns to this actual wrapper continuation.
            # Later SDK IRQ work (including PVR access) is outside this packet
            # replay, just as it is outside the ordinary C-bridge audit.
            steps=self.until(r.driver[13]+4)
        finally:self.bypass_running=False
        count=self.gateway_counts['complete']-before
        require(count>0 and self.r[15]==owner_sp,
                'Original completion did not execute its compiled gateway/consumer tail and return')
        self.complete_callbacks+=1
        return prefix,dict(instructions=count,original_sdk_instructions=prefix+steps,
                           original_consumer_tail_executed=True)


def poll(cpu, consume=True):
    cpu.submit();wrapper,complete=cpu.complete()
    if consume:
        if not cpu.maple_bypass:cpu.original_call(cpu.record.consumer)
        cpu.original_call(cpu.record.ready)
    return dict(submit_instructions=cpu.submit_counts['instructions'],
                completion_wrapper_instructions=wrapper,
                complete_instructions=complete['instructions'],overlay=cpu.overlay_stats(),
                physical_packets=cpu.batch['frames'],tick_calls=cpu.tick_calls,
                actual_parent_response_headers=[hex(cpu.read(int(frame['rx'],16)))
                    for frame in cpu.batch['frames']
                    if (int(frame['header'],16)>>8)&255 == 32])


def run_variant(startup, record_path, build, variant, tick_off=False, wait_capture=False,
                maple_bypass=False,maple_delay=False,cached_metadata=False,completion_delay=False,
                submission_delay=False,post_start_delay=False,split_controller=False):
    require(not split_controller or (tick_off and not any((wait_capture,maple_bypass,maple_delay,
                cached_metadata,completion_delay,submission_delay,post_start_delay))),
            'Split controller requires TICK off and real callbacks without other diagnostics')
    require(split_controller is (variant=='split_controller'),
            'Split controller must use its explicit combined variant')
    require(not(completion_delay and submission_delay), 'Select only one Maple delay side')
    require(not completion_delay or maple_delay, 'Completion delay requires Maple delay')
    require(not submission_delay or maple_delay, 'Submission delay requires Maple delay')
    require(not post_start_delay or submission_delay, 'Post-start delay requires submission delay')
    source,image,extent=fresh_verify(startup,record_path)
    record=Record(record_path.read_bytes(),image,source)
    cls=BypassWorker if maple_bypass else IsolationWorker
    cpu=cls(startup,build,make_handoff(),record,variant,tick_off,wait_capture,maple_bypass,maple_delay,
            cached_metadata,completion_delay,submission_delay,post_start_delay,split_controller)
    cpu.prepare();initializer=cpu.initialize_sdk()
    cpu.calls.clear()
    pristine=copy.deepcopy(cpu)
    if variant in ('reader_only','metadata_cache','split_controller'):cpu.seed_tick_boundary()
    rounds=[]
    for _ in range(8):
        if split_controller:
            before=cpu.split_callback_counts()
            ready=cpu.read(cpu.symbols['_live_plan'][0]+4)
        row=poll(cpu)
        if split_controller:
            after=cpu.split_callback_counts()
            row.update(split_controller_callbacks={key:after[key]-before[key] for key in after},
                live_ready_before_submit=ready,
                live_ready_after_complete=cpu.read(cpu.symbols['_live_plan'][0]+4))
        rounds.append(row)
    pre_gesture=None
    if wait_capture:
        capture=cpu.symbols['_diagnostic_phase'][0]+24
        attempts,valid=struct.unpack('<2I',cpu.block(capture,8))
        ready=cpu.read(capture+52)
        normal=cpu.read(cpu.symbols['_config'][0]+2296)
        require(attempts==valid==ready==cpu.tick_calls==normal==0,
                'Normal pre-gesture polling captured contexts, dispatched TICK, or changed diagnostics')
        pre_gesture=dict(polls=8,capture_attempts=attempts,captured_contexts=valid,
                         capture_ready=ready,controller_tick_calls=cpu.tick_calls,normal_diagnostics=normal)
    stats=cpu.overlay_stats()
    if split_controller:
        require(cpu.c_callback_calls['split_submit']==cpu.c_callback_calls['complete']==8,
                'Original SDK polling did not execute split submit and real completion eight times')
    elif not maple_bypass:
        require(cpu.c_callback_calls==dict(submit=8,complete=8),
                'Original SDK polling did not execute both real Maple C callbacks eight times')
    if variant=='reader_only':
        require(not any(stats[key] for key in ('advertisements','replies','backend_calls','devinfo','allinfo','media','reads')),
                'Reader-only SDK enumeration exposed virtual storage')
        require(not cpu.calls and (cpu.tick_calls==0 if tick_off else cpu.tick_calls>0),
                'Reader-only controller TICK selection or backend isolation differs')
        require(all((int(header,16)>>16)&2 == 0
                    for row in rounds for header in row['actual_parent_response_headers']),
                'Reader-only hardware model unexpectedly contained virtual attachment')
    else:
        require(stats['advertisements']>0 and stats['devinfo']>0 and stats['backend_calls']>0,
                'Virtual VMU did not execute actual virtual INFO enumeration')
        require(cpu.tick_calls==0 and cpu.calls['_persistent_handler']>0,
                'Virtual VMU backend/TICK separation differs')
        require(all((int(header,16)>>16)&2 != 0
                    for row in rounds for header in row['actual_parent_response_headers']),
                'Virtual VMU did not advertise A2 through the original parent response')
    forced=[]
    for command in (() if maple_bypass else (1,2,10,11)):
        trial=copy.deepcopy(pristine)
        if variant in ('reader_only','metadata_cache','split_controller'):trial.seed_tick_boundary()
        trial.inject_command=command
        try:
            result=poll(trial,consume=False)
        except LoanBoundary:
            require(variant in ('standard_vmu','metadata_cache','split_controller') and command==11 and
                    trial.calls['actual_low_sd_loan_boundary']==1,
                    'Unexpected storage boundary')
            require(trial.block(trial.injected_rx,4)==bytes([0xa5])*4,
                    'Block read published RX before storage completion')
            forced.append(dict(command=command,storage_boundary_reached=True,
                               success_or_ack_substituted=False,rx_unpublished=True,calls=dict(trial.calls),
                               loan_boundary=trial.loan_boundary))
            if split_controller:forced[-1]['split_controller_callbacks']=trial.split_callback_counts()
            continue
        require(command!=11 or variant=='reader_only', 'Virtual VMU block read did not reach storage loan')
        reply=trial.read(trial.injected_rx)
        s=trial.overlay_stats()
        if variant=='reader_only':
            require(reply==MASK and not trial.calls and s['backend_calls']==s['advertisements']==s['replies']==0,
                    'Reader-only forced packet reached storage or advertised a virtual device')
        else:
            require(reply&255=={1:5,2:6,10:8}[command] and s['backend_calls']>0,
                    'Virtual VMU metadata packet lacks actual backend reply')
        forced.append(dict(command=command,reply=hex(reply),overlay=s,calls=dict(trial.calls),
                           storage_boundary_reached=False,tick_calls=trial.tick_calls))
        if split_controller:forced[-1]['split_controller_callbacks']=trial.split_callback_counts()
    cpu.check_build_unchanged()
    post_start_cases=[]
    if post_start_delay:
        for imask in (6,15):
            trial=copy.deepcopy(pristine)
            trial.owner_sr=(trial.owner_sr&~0xf0)|(imask<<4)
            trial.seed_tick_boundary()
            poll(trial)
            require(len(trial.post_start_submissions)==1 and not trial.calls and not trial.tick_calls,
                    'Post-start immediate-completion case reached backend/TICK or missed its submit')
            require(trial.delay_calls==dict(submit=1),
                    'Post-start immediate-completion case did not execute only its59-tick submit helper')
            case=dict(trial.post_start_submissions[0],original_completion_wrapper_executed=True,
                original_consumer_tail_executed=True,delay_calls=dict(submit=1,complete=0))
            post_start_cases.append(case)
            trial.check_build_unchanged()
    bypass=None
    if maple_bypass:
        state=cpu.symbols['_state'][0]
        pending,submitted,completed=struct.unpack('<3I',cpu.block(state+4,12))
        require(pending==submitted==completed==0,'Bypass unexpectedly entered the Maple C state machine')
        require(cpu.submit_callbacks==cpu.complete_callbacks==8 and not cpu.calls and not cpu.tick_calls,
                'Bypass did not execute both actual assembly gateway paths for every original SDK poll')
        bypass=dict(polls=8,actual_patched_submit_executed=True,
            actual_completion_gateway_and_original_consumer_executed=True,
            maple_c_callbacks_executed=0,submitted=submitted,completed=completed,pending=pending,
            gateway_instructions=dict(cpu.gateway_counts),assembly_frames_and_masking_retained=True,
            forced_virtual_packet_injection=False)
        if maple_delay:
            calls=dict(submit=0 if completion_delay else 8,complete=0 if submission_delay else 8)
            actual_calls={name:cpu.delay_calls[name] for name in ('submit','complete')}
            require(actual_calls==calls and len(cpu.delay_visits)==sum(calls.values()) and
                    cpu.delay_active is None, 'Delay control did not execute the selected actual helpers each poll')
            bypass.update(actual_delay_helper_executed=True,delay_calls=actual_calls,
                          delay_ticks=dict(submit=0 if completion_delay else 59,complete=0 if submission_delay else 76),
                          delay_visits=cpu.delay_visits,
                          timer_writes=cpu.delay_timer_writes,
                          timer_model='Valid read-only TMU0; counter advances one tick per TCNT read, no CPU timing')
            if completion_delay:bypass.update(completion_delay=True,submit_gateway_no_calls=True)
            if submission_delay:bypass.update(submission_delay=True,complete_gateway_no_calls=True)
            if post_start_delay:
                require(len(cpu.post_start_submissions)==8 and
                        all(row['mdst_writes_before_first_clock_read']==row['mdst_writes']==1
                            for row in cpu.post_start_submissions),
                        'Post-start natural SDK polls lost their MDST-before-clock order')
                bypass.update(post_start_delay=True,mdst_before_first_clock_read=True,
                    post_start_submissions=cpu.post_start_submissions,
                    post_start_immediate_completion_cases=post_start_cases,
                    pending_irq_model='Synchronous MDST0 plus pending normal IRQ bit12 at Maple priority4; '
                        'instruction-boundary eligibility through SDK publication only, no outer RTE or IRQ dispatcher')
    report=dict(variant=variant,controller_tick_off=tick_off,e1_wait_capture=wait_capture,
                cached_metadata=cached_metadata,virtual_a2=variant!='reader_only',
                maple_c_callbacks_enabled=not maple_bypass,
                maple_bypass=maple_bypass,maple_delay=maple_delay,maple_bypass_execution=bypass,
                normal_pre_gesture=pre_gesture,artifact_sha256={name:sha((build/name).read_bytes()) for name in
                    ('entry.elf','stage.elf','native-worker.elf','native-worker.bin','native-worker.reloc','resident-sci.elf','resident-sci.bin')},
                original_source_sha256=sha(source),record_sha256=sha(record.wire),program_extent=extent,
                lease_bytes=cpu.lease_bytes,worker_bytes=0x20000,placement='bottom',heap_calls=cpu.heap_calls,
                sdk_initializer=initializer,natural_rounds=rounds,final_overlay=stats,
                natural_backend_calls=dict(cpu.calls),tick_calls=cpu.tick_calls,
                actual_maple_c_callbacks=dict(cpu.c_callback_calls),
                metadata_cache_invalidations=cpu.flat_ocbi,
                sd_loan_boundary=dict(symbol='_kui_vmu_observe_sd_lease',address=hex(cpu.sd_entry),
                    resident_sha256=sha((build/'resident-sci.elf').read_bytes()),
                    reader='New (X)' if variant in ('reader_only','metadata_cache','split_controller') else 'Standard'),
                forced_packets=forced,worker_helpers_substituted=False,
                auxiliary_boundary_substituted=variant=='reader_only' and not maple_bypass,
                original_game_or_sdk_functions_substituted=False,
                actual_cold_stage_installer_executed=False,
                sh4_delay_slot_legality_enforced=True)
    if completion_delay:report['completion_delay']=True
    if submission_delay:report['submission_delay']=True
    if post_start_delay:
        report.update(post_start_delay=True,post_start_immediate_completion_cases=post_start_cases)
    if split_controller:
        first=rounds[0]
        require(first['live_ready_before_submit']==0 and first['live_ready_after_complete']==0x4e4c5031 and
                first['split_controller_callbacks']['tiny_submit_calls']==1 and
                first['split_controller_callbacks']['complete_calls']==1 and
                any(int(row['header'],16)&0xffff==0x2001 for row in first['physical_packets']) and
                first['overlay']['advertisements']>0,
                'Cold first A INFO did not execute split submission and lazy completion binding')
        cold=dict(natural_poll=1,live_ready_before_submit=first['live_ready_before_submit'],
            live_ready_after_complete=first['live_ready_after_complete'],
            callbacks=first['split_controller_callbacks'],actual_original_a_info=True,
            advertisements=first['overlay']['advertisements'],backend_calls=first['overlay']['backend_calls'])
        report.update(split_controller=True,
            split_controller_execution=dict(**cpu.split_callback_counts(),cold_first_info=cold))
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup',type=Path,required=True)
    parser.add_argument('--record',type=Path,required=True)
    parser.add_argument('--reader-build',type=Path,required=True)
    parser.add_argument('--standard-build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--source-commit',required=True)
    parser.add_argument('--reader-tick-off',action='store_true',
                        help='Require the explicit paired reader-only controller TICK-off markers')
    parser.add_argument('--wait-capture',action='store_true',
                        help='Require the paired exact Evolution1 operand-capture control')
    parser.add_argument('--collector-output',type=Path,
                        help='Write the artifact-bound collector host and NORMAL pre-gesture receipt')
    parser.add_argument('--maple-bypass',action='store_true',
                        help='Execute the paired diagnostic Maple gateways with their C calls omitted')
    parser.add_argument('--maple-delay',action='store_true',
                        help='Execute the read-only 59/76 tick delays while real Maple C calls stay off')
    delay_side=parser.add_mutually_exclusive_group()
    delay_side.add_argument('--completion-delay',action='store_true',
                        help='Select only the 76-tick completion helper; requires --maple-delay')
    delay_side.add_argument('--submission-delay',action='store_true',
                        help='Select only the 59-tick submit helper; requires --maple-delay')
    parser.add_argument('--post-start-delay',action='store_true',
                        help='Require the 59-tick submit helper after the original MDST start; needs --submission-delay')
    parser.add_argument('--cached-metadata',action='store_true',
                        help='Execute the real-callback combined VMU/New metadata-cache variant; requires TICK off')
    parser.add_argument('--split-controller',action='store_true',
                        help='Execute the combined tiny-submit/full-completion controller variant; requires TICK off')
    args=parser.parse_args()
    require(__debug__, 'Assertions must remain enabled')
    require(not args.wait_capture or (args.reader_tick_off and args.collector_output),
            'Wait capture requires TICK-off and a collector receipt output')
    require(not args.collector_output or args.wait_capture,
            'Collector receipt belongs only to the explicit wait-capture control')
    require(not args.maple_bypass or (args.reader_tick_off and not args.wait_capture),
            'Maple bypass requires TICK-off and is mutually exclusive with wait capture')
    require(not args.maple_delay or args.maple_bypass, 'Maple delay requires Maple C bypass')
    require(not args.completion_delay or args.maple_delay, 'Completion delay requires Maple delay')
    require(not args.submission_delay or args.maple_delay, 'Submission delay requires Maple delay')
    require(not args.post_start_delay or args.submission_delay, 'Post-start delay requires submission delay')
    require(not args.cached_metadata or (args.reader_tick_off and not args.wait_capture and
                not args.maple_bypass and not args.maple_delay),
            'Cached metadata requires TICK-off and excludes wait capture, bypass, and delay')
    require(not args.split_controller or (args.reader_tick_off and not any((args.cached_metadata,
                args.wait_capture,args.maple_bypass,args.maple_delay,args.completion_delay,
                args.submission_delay,args.post_start_delay))),
            'Split controller requires TICK off and real callbacks without other diagnostics')
    require(len(args.source_commit)==40 and all(c in '0123456789abcdef' for c in args.source_commit),
            'Expected exact runtime freeze commit')
    for build in (args.reader_build,args.standard_build):
        require(args.source_commit[:12].encode() in (build/'entry.elf').read_bytes(),
                'Actual linked entry does not contain runtime freeze build identifier')
    reports=[run_variant(args.startup,args.record,args.reader_build,
                         'split_controller' if args.split_controller else
                         'metadata_cache' if args.cached_metadata else 'reader_only',args.reader_tick_off,
                         args.wait_capture,args.maple_bypass,args.maple_delay,args.cached_metadata,args.completion_delay,
                         args.submission_delay,args.post_start_delay,args.split_controller),
             run_variant(args.startup,args.record,args.standard_build,'standard_vmu')]
    report=dict(schema='kui.native.isolation-linked-sdk.v1',status='PASS',
                source_commit=args.source_commit,build=args.source_commit[:12],variants=reports,
                source_sha256={name:sha((ROOT/name).read_bytes()) for name in SOURCE_FILES},
                limitations=[
                    'Flat coherent RAM and synchronous authored Maple hardware; no bus latency, IRQ schedule, cache, full game, FMV or timing model. Completion stops at the original consumer return, before the remaining SDK IRQ tail.',
                    ('Reader bypass executes the actual patched SDK, complete assembly gateways, original resume and consumer tail; the two Maple C callbacks are forbidden. Standard executes its existing linked C path.' if args.maple_bypass else
                     'Assembly save/restore bridges use executed original SDK register contexts; all linked worker C guards and packet/protocol helpers execute.'),
                    'On TICK-enabled reader controls dispatch executes to an authored auxiliary ABI boundary; TICK-off controls must never reach it. Auxiliary producer instructions are covered by the separate reader audit.',
                    'Injected A2 requests on C-enabled variants are authored in original SDK-owned TX/RX memory; the bypass reader is tested with unmodified original SDK packets and physical A2 absent.',
                    'Every VMU-enabled block read stops at the actual configured low ELF SD-loan entry. No SD hardware or success response is modeled.',
                    'Original allocator performs the current384KiB bottom sequence; the cold stage C installer requires its separate audit.'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    if args.wait_capture:
        import re
        import subprocess
        result=subprocess.run([sys.executable,str(ROOT/'tests/test_native_wait_capture.py')],
                              cwd=ROOT,check=True,text=True,capture_output=True)
        match=re.search(r'native wait capture: (\d+) checks PASS; record=(\d+) capture=(\d+) phase=(\d+)',result.stdout)
        require(match is not None,'Exact collector host test did not produce its layout/check result')
        checks,record_bytes,capture_bytes,phase_bytes=map(int,match.groups())
        receipt=dict(schema='kui.native.e1-wait-capture-collector.v1',status='PASS',
            source_commit=args.source_commit,paired_markers=True,
            artifact_sha256={name:reports[0]['artifact_sha256'][name] for name in
                             ('stage.elf','native-worker.elf','native-worker.bin','native-worker.reloc')},
            normal_pre_gesture=reports[0]['normal_pre_gesture'],
            collector_layout=dict(record_bytes=record_bytes,capture_bytes=capture_bytes,phase_bytes=phase_bytes),
            host_tests=dict(status='PASS',checks=checks,undefined_behavior_sanitizer=True),
            source_sha256={name:sha((ROOT/name).read_bytes()) for name in
                ('src/loader/vmu_native_wait_capture.inc','src/loader/vmu_native_phase.inc',
                 'tests/test_vmu_native_wait_capture.c','tests/test_native_wait_capture.py')},
            limits=['No game mainloop, physical IRQ scheduling, or console timing is modeled.',
                    'Host refusal/operand tests and actual linked NORMAL pre-gesture callbacks are separate evidence.'])
        args.collector_output.parent.mkdir(parents=True,exist_ok=True)
        args.collector_output.write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(dict(status='PASS',output=str(args.output),variants=len(reports))))


if __name__=='__main__':main()
