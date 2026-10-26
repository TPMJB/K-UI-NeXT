#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the two explicit native isolation workers against the original SDK.

No production pin is changed. Actual linked C guards, packet filters, protocol
and owner-supplied SDK instructions execute in flat coherent RAM. Only the
Maple hardware, the auxiliary ABI boundary, and assembly save/restore bridges
are modeled. A Standard block read stops at its real low SD-loan entry, before
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
    RecordWorker, Record, fresh_verify, make_handoff, CFG, MDSTAR, MASK)
from check_vmu_native_worker_gateway import read_elf

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
    'src/loader/vmu_readonly_overlay.c', 'include/kui/native_isolation.h')


def sha(data): return hashlib.sha256(data).hexdigest()

def require(ok, message):
    if not ok: raise ValueError(message)


class LoanBoundary(Exception): pass


class IsolationWorker(RecordWorker):
    def __init__(self, startup, build, handoff, record, variant):
        self.variant = variant
        self.low = read_elf(build/'resident-sci.elf')
        self.sd_entry = self.low[4]['_kui_vmu_observe_sd_lease'][0]
        self.calls, self.tick_calls = Counter(), 0
        self.tick_entry = None
        self.inject_command, self.injected_rx = None, None
        self.last_aux_arguments = None
        super().__init__(startup, build, handoff, record)
        stage, worker = read_elf(build/'stage.elf'), read_elf(build/'native-worker.elf')
        marker = '__kui_native_'+variant+'_isolation_'
        for image, role in ((stage,'stage'),(worker,'worker')):
            require(image[4].get(marker+role) == ONE, 'Isolation marker differs: '+role)
            require(image[4].get('__kui_native_isolation_lease_bytes') ==
                    (LEASE,0,0,0xfff1), 'Isolation lease is not exact compiled384KiB')
            require(image[4].get('__kui_native_bottom_heap_'+role) == ONE,
                    'Isolation lacks current bottom placement: '+role)
        self.lease_bytes = LEASE
        self.worker_physical_high = self.worker_physical_low+LEASE
        self.lease_geometry_source = 'Paired actual absolute isolation lease and bottom markers'
        self.build_inputs['stage.elf'] = (build/'stage.elf').read_bytes()
        self.tracked_entries = {self.symbols[name][0]:name for name in BACKENDS if name in self.symbols}
        if variant == 'standard_vmu':
            for name in ('__kui_native_readahead_worker','__kui_native_dual_readers_worker'):
                require(name not in worker[4], 'Standard isolation contains background worker capability')
            for name in ('_kui_native_ra_reader_ops','_kui_retail_sdk_sci_entry'):
                require(name not in self.low[4], 'Standard isolation contains New reader entry')
        else:
            require(worker[4].get('__kui_native_readahead_worker') == ONE and
                    worker[4].get('__kui_native_dual_readers_worker') == ONE,
                    'Reader-only isolation lost its complete New reader worker')

    def worker(self, name, args):
        if name == '_kui_vmu_native_passive_arm':
            # Bind the authored config to the real low ELF role, so a storage
            # test stops at that exact entry rather than a fabricated address.
            self.write(args[0]+2236, self.sd_entry)
        return super().worker(name,args)

    def step(self):
        if getattr(self,'inside_worker',False):
            if self.pc in getattr(self,'tracked_entries',{}):
                name=self.tracked_entries[self.pc];self.calls[name]+=1
                require(self.variant!='reader_only',
                        'Reader-only isolation reached VMU/storage backend: '+name)
            if self.pc == self.sd_entry:
                require(self.variant == 'standard_vmu', 'Reader-only isolation reached SD loan')
                self.calls['actual_low_sd_loan_boundary']+=1
                raise LoanBoundary
            if self.tick_entry is not None and self.pc == self.tick_entry:
                require(self.variant=='reader_only', 'Standard isolation called auxiliary code')
                require(self.r[4:8] == [2,0,self.tick_region,0], 'New reader TICK arguments differ')
                require(self.sr&0xf0 == 0xf0, 'New reader TICK escaped masked worker scope')
                self.tick_calls+=1;self.last_aux_arguments=self.r[4:8].copy()
                continuation=self.pr
                for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd)&MASK
                self.r[0]=1;self.sr^=1;self.pr^=0x13579135;self.pc=continuation
                return
        return super().step()

    def seed_tick_boundary(self):
        require(self.variant=='reader_only', 'Only New reader has an auxiliary boundary')
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
        return super().bridge_submit()

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


def poll(cpu, consume=True):
    cpu.submit();wrapper,complete=cpu.complete()
    if consume:
        cpu.original_call(cpu.record.consumer);cpu.original_call(cpu.record.ready)
    return dict(submit_instructions=cpu.submit_counts['instructions'],
                completion_wrapper_instructions=wrapper,
                complete_instructions=complete['instructions'],overlay=cpu.overlay_stats(),
                physical_packets=cpu.batch['frames'],tick_calls=cpu.tick_calls,
                actual_parent_response_headers=[hex(cpu.read(int(frame['rx'],16)))
                    for frame in cpu.batch['frames']
                    if (int(frame['header'],16)>>8)&255 == 32])


def run_variant(startup, record_path, build, variant):
    source,image,extent=fresh_verify(startup,record_path)
    record=Record(record_path.read_bytes(),image,source)
    cpu=IsolationWorker(startup,build,make_handoff(),record,variant)
    cpu.prepare();initializer=cpu.initialize_sdk()
    cpu.calls.clear()
    pristine=copy.deepcopy(cpu)
    if variant=='reader_only':cpu.seed_tick_boundary()
    rounds=[poll(cpu) for _ in range(8)]
    stats=cpu.overlay_stats()
    if variant=='reader_only':
        require(not any(stats[key] for key in ('advertisements','replies','backend_calls','devinfo','allinfo','media','reads')),
                'Reader-only SDK enumeration exposed virtual storage')
        require(not cpu.calls and cpu.tick_calls>0, 'Reader-only lost TICK or reached backend')
        require(all((int(header,16)>>16)&2 == 0
                    for row in rounds for header in row['actual_parent_response_headers']),
                'Reader-only hardware model unexpectedly contained virtual attachment')
    else:
        require(stats['advertisements']>0 and stats['devinfo']>0 and stats['backend_calls']>0,
                'Standard VMU did not execute actual virtual INFO enumeration')
        require(cpu.tick_calls==0 and cpu.calls['_persistent_handler']>0,
                'Standard VMU backend/TICK separation differs')
    forced=[]
    for command in (1,2,10,11):
        trial=copy.deepcopy(pristine)
        if variant=='reader_only':trial.seed_tick_boundary()
        trial.inject_command=command
        try:
            result=poll(trial,consume=False)
        except LoanBoundary:
            require(variant=='standard_vmu' and command==11 and trial.calls['actual_low_sd_loan_boundary']==1,
                    'Unexpected storage boundary')
            require(trial.block(trial.injected_rx,4)==bytes([0xa5])*4,
                    'Block read published RX before storage completion')
            forced.append(dict(command=command,storage_boundary_reached=True,
                               success_or_ack_substituted=False,rx_unpublished=True,calls=dict(trial.calls)))
            continue
        require(command!=11 or variant=='reader_only', 'Standard block read did not reach storage loan')
        reply=trial.read(trial.injected_rx)
        s=trial.overlay_stats()
        if variant=='reader_only':
            require(reply==MASK and not trial.calls and s['backend_calls']==s['advertisements']==s['replies']==0,
                    'Reader-only forced packet reached storage or advertised a virtual device')
        else:
            require(reply&255=={1:5,2:6,10:8}[command] and s['backend_calls']>0,
                    'Standard metadata packet lacks actual backend reply')
        forced.append(dict(command=command,reply=hex(reply),overlay=s,calls=dict(trial.calls),
                           storage_boundary_reached=False,tick_calls=trial.tick_calls))
    cpu.check_build_unchanged()
    return dict(variant=variant,artifact_sha256={name:sha((build/name).read_bytes()) for name in
                    ('entry.elf','stage.elf','native-worker.elf','native-worker.bin','native-worker.reloc','resident-sci.elf','resident-sci.bin')},
                original_source_sha256=sha(source),record_sha256=sha(record.wire),program_extent=extent,
                lease_bytes=cpu.lease_bytes,worker_bytes=0x20000,placement='bottom',heap_calls=cpu.heap_calls,
                sdk_initializer=initializer,natural_rounds=rounds,final_overlay=stats,
                natural_backend_calls=dict(cpu.calls),tick_calls=cpu.tick_calls,
                forced_packets=forced,worker_helpers_substituted=False,
                auxiliary_boundary_substituted=variant=='reader_only',
                original_game_or_sdk_functions_substituted=False,
                actual_cold_stage_installer_executed=False,
                sh4_delay_slot_legality_enforced=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup',type=Path,required=True)
    parser.add_argument('--record',type=Path,required=True)
    parser.add_argument('--reader-build',type=Path,required=True)
    parser.add_argument('--standard-build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--source-commit',required=True)
    args=parser.parse_args()
    require(__debug__, 'Assertions must remain enabled')
    require(len(args.source_commit)==40 and all(c in '0123456789abcdef' for c in args.source_commit),
            'Expected exact runtime freeze commit')
    for build in (args.reader_build,args.standard_build):
        require(args.source_commit[:12].encode() in (build/'entry.elf').read_bytes(),
                'Actual linked entry does not contain runtime freeze build identifier')
    reports=[run_variant(args.startup,args.record,args.reader_build,'reader_only'),
             run_variant(args.startup,args.record,args.standard_build,'standard_vmu')]
    report=dict(schema='kui.native.isolation-linked-sdk.v1',status='PASS',
                source_commit=args.source_commit,build=args.source_commit[:12],variants=reports,
                source_sha256={name:sha((ROOT/name).read_bytes()) for name in SOURCE_FILES},
                limitations=[
                    'Flat coherent RAM and synchronous authored Maple hardware; no bus latency, IRQ schedule, cache, full game, FMV or timing model.',
                    'Assembly save/restore bridges use executed original SDK register contexts; all linked worker C guards and packet/protocol helpers execute.',
                    'Reader TICK dispatch executes to an authored auxiliary ABI boundary; auxiliary producer instructions are covered by the separate reader audit.',
                    'Injected A2 requests are authored in original SDK-owned TX/RX memory before actual worker submission; not assertions of game-generated requests.',
                    'Standard block read stops at the actual configured low ELF SD-loan entry. No SD hardware or success response is modeled.',
                    'Original allocator performs the current384KiB bottom sequence; the cold stage C installer requires its separate audit.'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status='PASS',output=str(args.output),variants=len(reports))))


if __name__=='__main__':main()
