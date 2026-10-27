#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the distinct bounded PIO producers as linked SH instructions.

The producer, cache operations, snapshots and clock checks execute unchanged.
Only the already typed low operations are bounded ABI substitutes. This proves
admission and publication behavior, not card timing or console performance.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
PIO_MARKER='__kui_native_readahead_pio_auxiliary'
PIO_PAIR_MARKER='__kui_native_readahead_pio_pair_auxiliary'


def require(ok,message):
    if not ok:raise ValueError(message)


def pio_profile(aux):
    marker=aux[4].get(PIO_MARKER)
    if marker is None:return False
    require(marker==(1,0,0,0xfff1),'PIO auxiliary marker is not absolute one')
    for name in ('__kui_native_readahead_diagnostic_auxiliary',
                 '__kui_native_readahead_repair_diagnostic_auxiliary'):
        require(aux[4].get(name)==(1,0,0,0xfff1),'PIO auxiliary lacks its exact diagnostic markers')
    require(aux[4]['_kui_native_readahead_aux_context'][1]==904,'PIO context layout differs')
    return True


def pio_pair_profile(aux):
    marker=aux[4].get(PIO_PAIR_MARKER)
    if marker is None:return False
    require(marker==(1,0,0,0xfff1) and pio_profile(aux),
            'PIO pair marker differs or lacks the exact PIO profile')
    return True


def pio_cases(resident,aux,*,self_test=False):
    require(__debug__,'PIO producer proof requires Python assertions')
    pair=pio_pair_profile(aux)
    if not pio_profile(aux):return {'enabled':False}
    # The main audit imports this module only after its own definitions exist.
    # Keeping this dependency lazy also permits a standalone invocation.
    from check_vmu_native_readahead import (SnapshotCPU,pinned_code,typed_ops,
        HW_READS,STACK_BOTTOM,STACK_TOP,CANARY,MASK)
    from check_vmu_native_sdk_sci_route import assert_c_abi
    pinned_code(aux);ops=typed_ops(resident)
    low_names=tuple(ops['functions']);low_addresses=tuple(ops['functions'].values())
    hardware=set(HW_READS)|{(0xffc00000,2),(0xffd80008,4),
                           (0xffd80010,2),(0xffd80004,1)}

    class ProducerCPU(SnapshotCPU):
        def __init__(self,seed=0,bank=0,blocked=False):
            self.recording=False;self.in_stub=False
            super().__init__(resident,aux,seed,bank,blocked)
            self.api=dict(zip(low_addresses,low_names));self.targets=set(self.api)
            self.operations=[];self.actual_writes=[];self.stub_writes=[]
            self.data=0xac430000;self.block=self.region+0x20;self.next=0x123401
            self.payload=bytes((i*37+seed*19)&255 for i in range(512))
            self.fetch_result=0;self.take_result=0;self.take_null=False
            self.fetch_results=None;self.take_results=None;self.last_fetch_lba=None
            self.after_first=None;self.after_first_done=False;self.clock_schedule=None
            self.clock_samples=0;self.clock_pending=False;self.clock_valid=None
            self.fetch_payloads=[]
            self.transport_busy=False;self.corrupt_append=False
            self.append_fail_at=None;self.take_null_at=None
            self.dma_chcr=0;self.dmaor=1
            self.timer=0x12345000;self.timer_step=97;self.timer_reads=0
            self.bad_clock=False;self.bad_after=False
            values={0:1,656:1,660:32,664:self.region,668:ops['address'],
                    672:self.data,676:0x30000,680:0x0c400000,684:0x60000,
                    688:self.data,692:self.next-1,696:0,700:1,704:self.next+1000,
                    708:1,720:1,900:2}
            for offset,value in values.items():self.write(self.context+offset,value)
            for offset,value in ((0x2c4,1),(0x470,1),(0x474,self.next)):
                self.write(self.region+offset,value)
            self.write(self.region+0x4eb,1,1)
            for i,b in enumerate(self.payload):self.write(self.block+i,b,1)
            self.writes=[];self.recording=True

        def read(self,address,count=4):
            if address&0xe0000000==0xe0000000:
                require((address,count) in hardware,'PIO producer read unexpected hardware register/width')
                self.hw_reads.append((address,count))
                if address==0xffa0001c:return self.dma_chcr
                if address==0xffa00040:return self.dmaor
                if address==0xffc00000:
                    if pair:
                        if (self.after_first is not None and not self.after_first_done and
                                self.read(self.context+16)==1):
                            self.after_first_done=True;self.in_stub=True
                            self.after_first(self);self.in_stub=False
                        self.clock_pending=True
                        if self.clock_valid is not None:
                            return 0xe0a if self.clock_valid(self.clock_samples) else 0
                    return 0 if self.bad_clock or (self.bad_after and self.timer_reads) else 0xe0a
                if address==0xffd80008:return MASK
                if address==0xffd80010:return 2
                if address==0xffd80004:return 1
                if address==0xffd8000c:
                    if pair and self.clock_pending:
                        self.clock_pending=False
                        if self.clock_schedule is not None:
                            require(self.clock_samples<len(self.clock_schedule),'PIO pair read unexpected extra clock sample')
                            value=self.clock_schedule[self.clock_samples]&MASK
                            self.clock_samples+=1;self.timer_reads+=1;return value
                        self.clock_samples+=1
                    value=self.timer&MASK;self.timer=(self.timer-self.timer_step)&MASK
                    self.timer_reads+=1;return value
                return ((address^0x13579bdf)+(self.hw_seed+1)*13)&((1<<(8*count))-1)
            return super().read(address,count)

        def write(self,address,value,count=4):
            super().write(address,value,count)
            if self.recording:
                (self.stub_writes if self.in_stub else self.actual_writes).append((address,count))

        def plain(self,op,pc):
            if op&0xf00f==0x6007: # NOT Rm,Rn; does not modify T.
                self.r[op>>8&15]=(~self.r[op>>4&15])&MASK;return
            if op>>8==0xca: # XOR #imm,R0; does not modify T.
                self.r[0]^=op&255;return
            if op&0xf00f in (0x6004,0x6005): # MOV.B/W @Rm+,Rn.
                n,m=op>>8&15,op>>4&15;count=1 if op&15==4 else 2
                at=self.r[m];value=self.read(at,count);bits=8*count
                self.r[n]=(value-(1<<bits) if value&(1<<(bits-1)) else value)&MASK
                if n!=m:self.r[m]=(at+count)&MASK
                return
            if op&0xf0ff==0x4011: # CMP/PZ Rn.
                self.flag(not self.r[op>>8&15]&0x80000000);return
            if op&0xf0ff==0x4015: # CMP/PL Rn.
                value=self.r[op>>8&15];self.flag(0<value<0x80000000);return
            if op&0xf00f==0x3003: # CMP/GE Rm,Rn, signed comparison.
                n,m=op>>8&15,op>>4&15
                self.flag((self.r[n]^0x80000000)>=(self.r[m]^0x80000000));return
            super().plain(op,pc)

        def callee(self):
            name=self.api[self.pc];continuation=self.pr;args=self.r[4:8].copy()
            require(STACK_BOTTOM+16<=self.r[15]<=STACK_TOP-32,'PIO low callback lacks owned private stack')
            require(self.sr&0xf0==0xf0,'PIO low callback escaped full masking')
            self.operations.append((name,args));self.in_stub=True;result=0
            if name=='_kui_sci_stream_busy':result=int(self.transport_busy)
            elif name=='_kui_sci_stream_fetch':
                index=len(self.calls_named(name))-1;expected=self.next+(index if pair else 0)
                assert args[:3]==[expected,256,1],('PIO fetch ABI',args)
                assert self.read(self.region+0x470)==1 and self.read(self.region+0x474)==expected
                assert not self.dma_chcr&7 and self.dmaor&7==1
                self.last_fetch_lba=expected
                result=self.fetch_results[index] if self.fetch_results is not None else self.fetch_result
                if not result:
                    self.write(self.region+0x474,expected+1)
                    self.write(self.region+0x498,expected+1)
                    payload=bytes(b^(index*0x5a) for b in self.payload) if pair else self.payload
                    self.fetch_payloads.append(payload)
                    if pair:
                        for i,b in enumerate(payload):self.write(self.block+i,b,1)
            elif name=='_kui_sci_stream_take':
                assert args[0]==(self.last_fetch_lba if pair else self.next)
                assert STACK_BOTTOM+16<=args[1]<=STACK_TOP-36
                index=len(self.calls_named(name))-1
                take_result=self.take_results[index] if self.take_results is not None else self.take_result
                self.write(args[1],take_result)
                result=0 if self.take_null or take_result or index==self.take_null_at else self.block
                if self.corrupt_append or index==self.append_fail_at:self.write(self.context+696,384)
            elif name=='_unhook':pass
            elif name=='_deliver_work':assert args[0]==0
            else:raise ValueError('optional PIO called forbidden low operation: '+name)
            self.in_stub=False
            for i in range(8):self.r[i]=(0x91a357bd+i*0x13579+len(self.operations))&MASK
            self.r[0]=result;self.sr^=1;self.pr^=0x13572468
            self.mach^=0x13579bdf;self.macl^=0xfdb97531;self.pc=continuation

        def execute(self,*,dispatch=False,event=2,interrupt=0):
            self.write(self.context+900,event)
            self.r[4:8]=[event,0,self.region,0] if dispatch else [self.region,interrupt,0,0]
            before=self.snapshot();self.actual_writes=[];self.stub_writes=[];self.writes=[];self.hw_reads=[]
            self.operations=[]
            self.fetch_payloads=[];self.last_fetch_lba=None;self.after_first_done=False
            self.run_symbol('_kui_native_readahead_aux_dispatch' if dispatch else '_producer')
            assert_c_abi(self,before)
            for address,count in self.actual_writes:
                require(self.context<=address<=self.context+904-count or
                    STACK_BOTTOM+16<=address<=STACK_TOP-32-count or
                    self.data<=address<=self.data+0x30000-count or
                    (address==self.region+0x470 and count==4 and self.read(address)==3),
                    'PIO producer wrote outside owned context/private stack/cache')
            for at in (STACK_BOTTOM,STACK_TOP-16):
                assert all(self.read(at+4*i)==CANARY for i in range(4))
            assert self.read(self.context)==1,'producer changed gateway busy ownership'
            assert len(self.calls_named('_kui_sci_stream_fetch'))<=(2 if pair else 1),'heartbeat exceeded its exact block cap'
            return self

        def calls_named(self,name):return [args for called,args in self.operations if called==name]

        def metrics(self):return [self.read(self.context+80+4*i) for i in range(3)]

        def pair_metrics(self):return [self.read(self.context+80+4*i) for i in range(8)]

    if pair:return _pair_cases(ProducerCPU,aux,self_test=self_test)

    positive=refusals=failures=clocks=0
    for bank in (0,1):
        for blocked in (False,True):
            for dispatch in (False,True):
                for head,count in ((0,1),(383,1),(2,383)):
                    c=ProducerCPU(positive,bank,blocked)
                    c.write(c.context+696,head);c.write(c.context+700,count)
                    c.next=c.read(c.context+692)+count
                    c.write(c.region+0x474,c.next)
                    c.execute(dispatch=dispatch)
                    slot=(head+count)%384
                    assert bytes(c.read(c.data+slot*512+i,1) for i in range(512))==c.payload
                    publication=[i for i,(at,size) in enumerate(c.actual_writes)
                                 if at==c.context+700 and size==4]
                    copied=[i for i,(at,size) in enumerate(c.actual_writes)
                            if c.data+slot*512<=at<c.data+(slot+1)*512 and size==1]
                    assert len(publication)==1 and len(copied)==512 and max(copied)<publication[0]
                    assert c.read(c.context+700)==count+1 and c.read(c.context+740)==0
                    assert c.read(c.context+16)==1 # producer_blocks
                    assert len(c.calls_named('_kui_sci_stream_fetch'))==len(c.calls_named('_kui_sci_stream_take'))==1
                    assert not c.calls_named('_kui_sci_stream_poll') and not c.calls_named('_unhook')
                    assert c.metrics()==[1,4*c.timer_step,0]
                    if dispatch:assert c.read(c.context+68)==6*c.timer_step
                    positive+=1

    gates=(('event_deliver',None,None,1,0),('interrupt_argument',None,None,2,1),
        ('reader_in_irq','r',0x2cc,2,0),('quiesced','c',712,2,0),
        ('abandoned','c',716,2,0),('no_seed','c',720,2,0),
        ('loan','r',0x2f4,2,0),('active','r',0x2bc,2,0),
        ('pending','r',0x370,2,0),('executing','r',0x374,2,0),
        ('failed','r',0x2c0,2,0),('closed','r',0x2c4,2,0),
        ('not_acquired','r',0x4eb,2,0),('port_fault','r',0x4ec,2,0),
        ('busy',None,None,2,0),('deferred','c',748,2,0),
        ('spec_owned','c',740,2,0),('full','c',700,2,0),
        ('extent_end','c',704,2,0),('resume','r',0x470,2,0),
        ('closed_stream','r',0x470,2,0),('lost_stream','r',0x470,2,0),
        ('receiving','r',0x470,2,0),('position','r',0x474,2,0),
        ('guest_dma',None,None,2,0),('dmaor',None,None,2,0))
    for bank in (0,1):
        for blocked in (False,True):
            for name,space,offset,event,interrupt in gates:
                c=ProducerCPU(refusals,bank,blocked)
                if space:
                    value=0 if name in ('no_seed','closed','not_acquired','closed_stream') else \
                        384 if name=='full' else c.next if name=='extent_end' else \
                        4 if name=='resume' else 3 if name=='lost_stream' else \
                        2 if name=='receiving' else c.next+1 if name=='position' else 1
                    c.write((c.context if space=='c' else c.region)+offset,value,
                            1 if name in ('not_acquired','port_fault') else 4)
                if name=='busy':c.transport_busy=True
                if name=='guest_dma':c.dma_chcr=1
                if name=='dmaor':c.dmaor=3
                before=c.read(c.context+700)
                c.execute(event=event,interrupt=interrupt)
                assert not c.calls_named('_kui_sci_stream_fetch') and not c.calls_named('_kui_sci_stream_take'),name
                assert c.metrics()==[0,0,0] and c.read(c.context+700)==before,name
                assert not any(c.data<=at<c.data+0x30000 for at,_ in c.actual_writes),name
                refusals+=1

    for stage in ('fetch','take','null_take','append'):
        results=range(1,8) if stage in ('fetch','take') else (0,)
        for result in results:
            c=ProducerCPU(failures)
            if stage=='fetch':c.fetch_result=result
            elif stage=='take':c.take_result=result
            elif stage=='null_take':c.take_null=True
            else:c.corrupt_append=True
            before=c.read(c.context+700);c.execute()
            expected_result=2 if stage=='append' else result
            expected_stage=4 if stage=='fetch' else 3 if stage=='append' else 2
            assert c.read(c.context+716)==1 and c.read(c.context+740)==0 and c.read(c.context+748)==0
            assert c.read(c.context+76)==1 and c.read(c.context+360)==expected_stage
            assert c.read(c.context+364)==expected_result
            assert c.read(c.context+508)==expected_stage and c.read(c.context+512)==expected_result
            assert c.read(c.context+360+44)==c.read(c.context+508+44)==c.next
            assert c.read(c.context+80+4*(8*expected_stage+expected_result))==1
            assert c.read(c.context+700)==before
            assert len(c.calls_named('_unhook'))==1 and c.metrics()[0]==1
            assert not any(c.data<=at<c.data+0x30000 for at,_ in c.actual_writes)
            failures+=1

    for mode in ('bad_before','bad_after','backward','half_range','prior_max'):
        c=ProducerCPU(clocks)
        if mode=='bad_before':c.bad_clock=True
        if mode=='bad_after':c.bad_after=True
        if mode=='backward':c.timer_step=-97
        if mode=='half_range':c.timer_step=0x20000000
        if mode=='prior_max':c.write(c.context+84,10000)
        c.execute()
        assert c.metrics()==([1,10000,0] if mode=='prior_max' else [1,0,1]),mode
        clocks+=1

    repeated=0
    for mode in ('two_ticks','capacity','extent'):
        c=ProducerCPU(repeated)
        if mode=='capacity':
            c.write(c.context+700,383);c.next=c.read(c.context+692)+383
            c.write(c.region+0x474,c.next)
        if mode=='extent':c.write(c.context+704,c.next+1)
        c.execute(dispatch=True)
        assert c.metrics()[0]==1
        first_count=c.read(c.context+700);c.next+=1
        c.execute(dispatch=True)
        if mode=='two_ticks':
            assert c.metrics()[0]==2 and c.read(c.context+700)==first_count+1
            assert len(c.calls_named('_kui_sci_stream_fetch'))==1
        else:
            assert c.metrics()[0]==1 and c.read(c.context+700)==first_count
            assert not c.calls_named('_kui_sci_stream_fetch') and len(c.calls_named('_unhook'))==1
        repeated+=1

    # DELIVER's normal fallback remains typed demand work; it must not create
    # fresh optional traffic, even when called in a reader IRQ.
    for irq in (0,1):
        c=ProducerCPU(irq);c.write(c.region+0x2cc,irq)
        c.execute(dispatch=True,event=1)
        assert len(c.calls_named('_deliver_work'))==1 and not c.calls_named('_kui_sci_stream_fetch')
        assert c.metrics()==[0,0,0];refusals+=1

    # Truth-check only the additional scalar instructions needed by real
    # clock/cache code; unknown instructions continue to fail closed.
    instruction_truth=0;c=ProducerCPU()
    for flag in (0,1):
        for value in (0,1,0x7fffffff,0x80000000,0xffff8000,MASK):
            c.sr=(c.sr&~1)|flag;sr=c.sr;c.r[12]=value
            c.plain(0x6dc7,0);assert c.r[13]==(~value)&MASK and c.sr==sr
            c.r[0]=value;c.plain(0xca5a,0);assert c.r[0]==value^0x5a and c.sr==sr
            c.plain(0x4c11,0)
            assert c.r[12]==value and c.sr&~1==sr&~1 and c.sr&1==int(value<0x80000000)
            instruction_truth+=3
        for count,values in ((1,(0,1,127,128,255)),(2,(0,1,0x7fff,0x8000,0xff80,0xffff))):
            for value in values:
                for same in (False,True):
                    c.write(c.block,value,count);c.r[12]=c.block
                    c.sr=(c.sr&~1)|flag;sr=c.sr;n=12 if same else 13
                    c.plain(0x6004+(count-1)+(n<<8)+(12<<4),0)
                    expected=int.from_bytes(value.to_bytes(count,'little'),'little',signed=True)&MASK
                    assert c.r[n]==expected and c.sr==sr
                    if not same:assert c.r[12]==c.block+count
                    instruction_truth+=1

    schema_mutations=0
    if self_test:
        for name,value in ((PIO_MARKER,(0,0,0,0xfff1)),(PIO_MARKER,(1,4,0,0xfff1)),
                (PIO_MARKER,(1,0,1,0xfff1)),(PIO_MARKER,(1,0,0,1)),
                ('__kui_native_readahead_diagnostic_auxiliary',None),
                ('__kui_native_readahead_repair_diagnostic_auxiliary',None)):
            symbols=aux[4].copy()
            if value is None:symbols.pop(name)
            else:symbols[name]=value
            try:pio_profile((*aux[:4],symbols))
            except ValueError:schema_mutations+=1
            else:raise ValueError('PIO proof accepted changed profile marker')
    return dict(enabled=True,actual_linked_producer_executed=True,tick_only=True,
        one_fresh_block_per_tick=True,no_optional_dma_or_new_commands=True,
        producer_only_timing=True,modelcases=positive+refusals+failures+clocks+repeated,
        additional_instruction_truth_cases=instruction_truth,repeated_tick_and_boundary_cases=repeated,
        actual_linked_producer_positive_cases=positive,
        actual_linked_admission_refusals=refusals,actual_linked_error_paths=failures,
        actual_linked_clock_cases=clocks,profile_marker_mutations=schema_mutations,
        fresh_optional_fetch_only_on_tick=True,never_resume_or_seek_or_command=True,
        fetch_token_limit=256,fetch_polled_true=True,maximum_blocks_per_visit=1,
        actual_cache_copy_before_validity_publication=True,actual_cache_extent_capacity_checks=True,
        actual_read_only_clock_and_failure_snapshots=True,metrics_offsets=[80,84,88],
        both_integer_banks_and_bl_states=True,callee_saved_and_private_stack_preserved=True,
        typed_low_ops='Bounded ABI-adversarial substitutes; no low transport emulation',
        no_hardware_write_or_rdr_read=True,console_boot_verified=False,console_performance_verified=False)


def _pair_cases(CPU,aux,*,self_test=False):
    """The distinct pair image executes both full admission passes in SH."""
    mask=0xffffffff;positives=refusals=between=failures=clocks=repeated=0

    def copied_before_publication(c,head,count,blocks):
        publications=[i for i,(at,size) in enumerate(c.actual_writes)
                      if at==c.context+700 and size==4]
        assert len(publications)==blocks
        for block in range(blocks):
            slot=(head+count+block)%384
            assert bytes(c.read(c.data+slot*512+i,1) for i in range(512))==c.fetch_payloads[block]
            writes=[i for i,(at,size) in enumerate(c.actual_writes)
                    if c.data+slot*512<=at<c.data+(slot+1)*512 and size==1]
            assert len(writes)==512 and max(writes)<publications[block]

    for bank in (0,1):
        for blocked in (False,True):
            for dispatch in (False,True):
                for head,count in ((0,1),(383,1),(2,382)):
                    c=CPU(positives,bank,blocked)
                    c.write(c.context+696,head);c.write(c.context+700,count)
                    c.next=c.read(c.context+692)+count;c.write(c.region+0x474,c.next)
                    c.execute(dispatch=dispatch)
                    copied_before_publication(c,head,count,2)
                    assert c.read(c.context+700)==count+2 and c.read(c.context+740)==0
                    assert c.read(c.context+16)==2 and c.read(c.context+20)==1
                    assert len(c.calls_named('_kui_sci_stream_fetch'))==len(c.calls_named('_kui_sci_stream_take'))==2
                    assert not c.calls_named('_kui_sci_stream_poll') and not c.calls_named('_unhook')
                    assert c.pair_metrics()==[2,10*c.timer_step,0,1,0,4*c.timer_step,0,0]
                    if dispatch:assert c.read(c.context+68)==12*c.timer_step
                    positives+=1

    # Every mutable ordinary guard is tested both before any work and again
    # after the first block's real CRC-checked publication. The injection is
    # external fixture state, performed at the first post-block clock read.
    guards=(('event','c',900,1,4),('in_irq','r',0x2cc,1,4),
        ('quiesced','c',712,1,4),('abandoned','c',716,1,4),
        ('no_seed','c',720,0,4),('loan','r',0x2f4,1,4),
        ('active','r',0x2bc,1,4),('pending','r',0x370,1,4),
        ('executing','r',0x374,1,4),('failed','r',0x2c0,1,4),
        ('closed','r',0x2c4,0,4),('not_acquired','r',0x4eb,0,1),
        ('port_fault','r',0x4ec,1,1),('busy','x',0,1,4),
        ('deferred','c',748,1,4),('spec_owned','c',740,1,4),
        ('full','c',700,384,4),('extent_end','c',704,None,4),
        ('resume','r',0x470,4,4),('closed_stream','r',0x470,0,4),
        ('lost_stream','r',0x470,3,4),('receiving','r',0x470,2,4),
        ('position','r',0x474,None,4),('guest_dma','x',1,1,4),
        ('dmaor','x',2,3,4))

    def apply_guard(c,guard):
        name,space,offset,value,size=guard
        if name=='busy':c.transport_busy=True
        elif name=='guest_dma':c.dma_chcr=1
        elif name=='dmaor':c.dmaor=3
        else:
            if name=='extent_end':value=c.read(c.context+692)+c.read(c.context+700)
            if name=='position':value=c.read(c.region+0x474)+1
            c.write((c.context if space=='c' else c.region)+offset,value,size)

    for bank in (0,1):
        for blocked in (False,True):
            for guard in guards:
                c=CPU(refusals,bank,blocked);apply_guard(c,guard)
                count=c.read(c.context+700)
                c.execute(event=1 if guard[0]=='event' else 2)
                assert not c.calls_named('_kui_sci_stream_fetch') and c.pair_metrics()==[0]*8,guard[0]
                assert c.read(c.context+700)==count
                assert not any(c.data<=at<c.data+0x30000 for at,_ in c.actual_writes)
                refusals+=1
                c=CPU(between,bank,blocked)
                c.after_first=lambda current,g=guard:apply_guard(current,g)
                c.execute()
                assert c.after_first_done and len(c.calls_named('_kui_sci_stream_fetch'))==1,guard[0]
                assert c.pair_metrics()==[1,5*c.timer_step,0,0,0,4*c.timer_step,0,0],guard[0]
                assert c.read(c.context+16)==1 and c.read(c.context+20)==1
                copied_before_publication(c,0,1,1)
                between+=1
            c=CPU();c.execute(interrupt=1)
            assert not c.calls_named('_kui_sci_stream_fetch') and c.pair_metrics()==[0]*8
            refusals+=1

    for failed_block in (0,1):
        for stage in ('fetch','take','null_take','append'):
            for result in (range(1,8) if stage in ('fetch','take') else (0,)):
                c=CPU(failures)
                if stage=='fetch':c.fetch_results=[0,result] if failed_block else [result]
                elif stage=='take':c.take_results=[0,result] if failed_block else [result]
                elif stage=='null_take':c.take_null_at=failed_block
                else:c.append_fail_at=failed_block
                c.execute()
                expected_result=2 if stage=='append' else result
                expected_stage=4 if stage=='fetch' else 3 if stage=='append' else 2
                assert len(c.calls_named('_kui_sci_stream_fetch'))==failed_block+1
                assert c.read(c.context+700)==1+failed_block and c.read(c.context+16)==failed_block
                assert c.read(c.context+716)==1 and c.read(c.context+740)==c.read(c.context+748)==0
                assert c.read(c.context+76)==1
                for offset in (360,508):
                    assert c.read(c.context+offset)==expected_stage
                    assert c.read(c.context+offset+4)==expected_result
                    assert c.read(c.context+offset+44)==c.next+failed_block
                assert c.read(c.context+80+4*(8*expected_stage+expected_result))==1
                metrics=c.pair_metrics()
                assert metrics[0]==failed_block+1 and metrics[2]==0 and metrics[3]==failed_block and metrics[4]==0
                assert len(c.calls_named('_unhook'))==1
                copied_before_publication(c,0,1,failed_block)
                failures+=1

    # Script only actual clock_read samples. Snapshot TMU observations retain
    # their own reads and cannot consume or hide an admission-clock sample.
    schedules=(
        ('under',[10000,9600,9500,9100,9000],None,[2,1000,0,1,0,400,0,0]),
        ('threshold',[10000,9600,8856,8456,8400],None,[2,1600,0,1,0,400,0,0]),
        ('over_threshold',[10000,9600,8855,8800],None,[1,1200,0,0,1,400,0,0]),
        ('zero_first',[10000,10000,9900,9800],None,[1,0,1,0,1,0,0,0]),
        ('zero_admission',[10000,9600,10000,9500],None,[1,0,0,0,1,400,0,0]),
        ('backward_first',[10000,10001,9900,9800],None,[1,0,1,0,1,0,0,0]),
        ('half_first',[0x80001000,0x1000,0x80000f00,0x80000e00],None,[1,0,1,0,1,0,0,0]),
        ('half_admission',[0x80001000,0x80000e70,0x1000,0x80000e00],None,[1,0,0,0,1,400,0,0]),
        ('wrap',[0x100,0xffffffa0,0xffffff00,0xfffffd70,0xfffffd00],None,[2,1024,0,1,0,400,0,0]),
        ('bad_before',[10000,9600,9500,9000],0,[1,0,1,0,1,0,0,0]),
        ('bad_after_first',[10000,9600,9500,9000],1,[1,0,1,0,1,0,0,0]),
        ('bad_admission',[10000,9600,9500,9000],2,[1,0,0,0,1,400,0,0]),
        ('bad_after_second',[10000,9600,9500,9100,9000],3,[2,0,1,1,0,400,0,0]),
        ('bad_final',[10000,9600,9500,9100,9000],4,[2,0,0,1,0,400,0,0]),
        ('window_exact',[10000,9600,9500,8000,7656],None,[2,2344,0,1,0,1500,0,0]),
        ('window_overrun',[10000,9600,9500,8000,7655],None,[2,2345,0,1,0,1500,1,0]),
        ('zero_second',[10000,9600,9500,9500,9000],None,[2,0,1,1,0,400,0,0]),
        ('half_second',[0x80001000,0x80000e70,0x80000e0c,0xe0c,0x80000d00],None,[2,0,1,1,0,400,0,0]),
    )
    for name,schedule,invalid,expected in schedules:
        for bank in (0,1):
            c=CPU(clocks,bank);c.clock_schedule=schedule
            if invalid is not None:c.clock_valid=lambda index,bad=invalid:index!=bad
            c.execute()
            assert c.clock_samples==len(schedule),(name,c.clock_samples,len(schedule))
            assert c.pair_metrics()==expected,(name,c.pair_metrics(),expected)
            assert c.read(c.context+700)==1+expected[0]
            copied_before_publication(c,0,1,expected[0]);clocks+=1

    for mode in ('two_ticks','one_slot','one_extent_block','two_slots','two_extent_blocks','demand_pending'):
        c=CPU(repeated)
        if mode in ('one_slot','two_slots'):
            c.write(c.context+700,383 if mode=='one_slot' else 382)
            c.next=c.read(c.context+692)+c.read(c.context+700);c.write(c.region+0x474,c.next)
        if mode in ('one_extent_block','two_extent_blocks'):
            c.write(c.context+704,c.next+(1 if mode=='one_extent_block' else 2))
        c.execute(dispatch=True);first_count=c.read(c.context+700);attempts=c.pair_metrics()[0]
        assert attempts==(1 if mode in ('one_slot','one_extent_block') else 2)
        c.next=c.read(c.context+692)+first_count
        if mode=='demand_pending':c.write(c.region+0x370,1)
        c.execute(dispatch=True)
        if mode=='two_ticks':
            assert len(c.calls_named('_kui_sci_stream_fetch'))==2 and c.read(c.context+700)==first_count+2
            assert c.pair_metrics()[0]==4
        else:
            assert not c.calls_named('_kui_sci_stream_fetch') and c.read(c.context+700)==first_count
            assert c.pair_metrics()[0]==attempts
        repeated+=1

    for irq in (0,1):
        c=CPU();c.write(c.region+0x2cc,irq);c.execute(dispatch=True,event=1)
        assert len(c.calls_named('_deliver_work'))==1 and not c.calls_named('_kui_sci_stream_fetch')
        assert c.pair_metrics()==[0]*8;refusals+=1

    # A longer prior maximum is retained independently for whole/single work.
    c=CPU();c.write(c.context+84,2000);c.write(c.context+100,1000);c.execute()
    assert c.pair_metrics()==[2,2000,0,1,0,1000,0,0];clocks+=1
    instruction_truth=0;c=CPU()
    for flag in (0,1):
        for value in (0,1,0x7fffffff,0x80000000,0xffff8000,mask):
            c.r[12]=value;c.sr=(c.sr&~1)|flag;before=c.sr
            c.plain(0x4c15,0)
            assert c.r[12]==value and c.sr&~1==before&~1 and c.sr&1==int(0<value<0x80000000)
            instruction_truth+=1
            for other in (0,1,0x7fffffff,0x80000000,0xffff8000,mask):
                c.r[12]=value;c.r[13]=other;c.sr=(c.sr&~1)|flag;before=c.sr
                c.plain(0x3dc3,0)
                expected=int((other^0x80000000)>=(value^0x80000000))
                assert c.r[12:14]==[value,other] and c.sr&~1==before&~1 and c.sr&1==expected
                instruction_truth+=1
    marker_mutations=0
    if self_test:
        for name,value in ((PIO_PAIR_MARKER,(0,0,0,0xfff1)),(PIO_PAIR_MARKER,(1,4,0,0xfff1)),
                (PIO_PAIR_MARKER,(1,0,1,0xfff1)),(PIO_PAIR_MARKER,(1,0,0,1)),
                (PIO_MARKER,None),('__kui_native_readahead_diagnostic_auxiliary',None),
                ('__kui_native_readahead_repair_diagnostic_auxiliary',None)):
            symbols=aux[4].copy()
            if value is None:symbols.pop(name)
            else:symbols[name]=value
            try:pio_pair_profile((*aux[:4],symbols))
            except ValueError:marker_mutations+=1
            else:raise ValueError('PIO pair proof accepted changed profile marker')
    return dict(enabled=True,pair_profile=True,pair_candidate=True,
        actual_linked_producer_executed=True,tick_only=True,
        one_fresh_block_per_tick=False,maximum_blocks_per_visit=2,
        no_optional_dma_or_new_commands=True,producer_only_timing=True,
        modelcases=positives+refusals+between+failures+clocks+repeated,
        actual_linked_producer_positive_cases=positives,actual_linked_admission_refusals=refusals,
        actual_linked_second_guard_refusals=between,actual_linked_error_paths=failures,
        actual_linked_clock_cases=clocks,repeated_tick_and_boundary_cases=repeated,
        additional_instruction_truth_cases=instruction_truth,
        profile_marker_mutations=marker_mutations,second_elapsed_limit_ticks=1144,
        second_limit_ticks=1144,second_block_timing_admitted=True,
        second_limit_inclusive=True,reserve_ticks=1200,soft_window_ticks=2344,
        positive_valid_intervals_required=True,all_ordinary_guards_rechecked_before_second=True,
        per_block_positive_elapsed_required=True,all_admission_guards_rechecked_between_blocks=True,
        actual_cache_copy_before_each_validity_publication=True,
        first_and_second_errors_do_not_issue_stop=True,fetch_token_limit=256,fetch_polled_true=True,
        metrics_offsets=[80,84,88,92,96,100,104,108],
        both_integer_banks_and_bl_states=True,callee_saved_and_private_stack_preserved=True,
        typed_low_ops='Bounded ABI-adversarial substitutes; no low transport emulation',
        no_hardware_write_or_rdr_read=True,console_boot_verified=False,console_performance_verified=False)


def main():
    from check_vmu_native_worker_gateway import read_elf
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    p.add_argument('--self-test',action='store_true');a=p.parse_args()
    try:
        result=pio_cases(read_elf(a.directory/'resident-sci.elf'),
                         read_elf(a.directory/'native-ra-aux.elf'),self_test=a.self_test)
    except (OSError,ValueError,AssertionError,KeyError) as e:
        p.exit(2,'Read-ahead PIO producer audit refused: '+str(e)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
