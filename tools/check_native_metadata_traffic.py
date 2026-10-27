#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare actual compiled metadata aliases with identical VMU/SDK workloads.

The baseline is this same linked worker, after successful publication, with
only its metadata eligibility word cleared. This forces canonical P2 views;
it does not remove the real callbacks, virtual VMU, guard checks or TICK-off.
Warm and cold runs use the targeted operand-line model from the publication
proof. Counts are interpreted loads/instructions, never hardware cycle times.
"""
import argparse
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
from check_native_metadata_cache import CachedSDK, READY, SH, physical
from check_native_isolation_linked import poll
from profile_vmu_native_record_worker_sh import Record, fresh_verify, make_handoff

OBJECTS = ('_config', '_live_plan', '_lifetime_plan', '_hook_witness',
           '_hook_witness_count', '_hook_witness_ready', '_native_metadata_ready')
SOURCES = ('tools/check_native_metadata_traffic.py', 'tools/check_native_metadata_cache.py',
    'tools/check_native_isolation_linked.py', 'tools/profile_vmu_native_record_worker_sh.py',
    'tools/profile_vmu_native_sdk_worker_sh.py', 'tools/profile_vmu_native_release_sh.py',
    'tools/check_vmu_native_worker_gateway.py', 'tools/check_vmu_native_readonly.py',
    'tests/test_vmu_native_heap_execution.py', 'tests/test_vmu_native_sdk_poll_execution.py',
    'src/loader/vmu_native_passive.c', 'src/loader/vmu_native_metadata_cache.inc',
    'src/loader/vmu_native_metadata_cache.ld', 'include/kui/native_isolation.h')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def digest_events(events):
    return sha(json.dumps(events, separators=(',', ':')).encode())


class TrafficSDK(CachedSDK):
    def __init__(self, *args, **kwargs):
        self.traffic_enabled = False
        super().__init__(*args, **kwargs)
        self.metadata_ranges = [(physical(self.symbols[name][0]),
                                 physical(self.symbols[name][0])+self.symbols[name][1], name)
                                for name in OBJECTS]

    def reset_traffic(self):
        self.traffic_counts = Counter()
        self.metadata_counts = Counter()
        self.live_reads = []
        self.live_writes = []
        self.callbacks = []
        self.cache_before = dict(fills=self.cache.fills, hits=self.cache.hits,
                                 writebacks=self.cache.writebacks)
        self.traffic_enabled = True

    def private(self, address):
        return self.worker_physical_low <= physical(address) < self.worker_physical_high

    def read(self, address, count=4):
        value = super().read(address, count)
        if (self.traffic_enabled and getattr(self, 'active', False) and
                getattr(self, 'inside_worker', False) and
                not (count == 2 and address in (self.pc, self.pc+2))):
            alias = {0x80000000: 'p1', 0xa0000000: 'p2'}.get(address & 0xe0000000, 'other')
            self.traffic_counts['all_data_reads'] += 1
            if self.private(address):
                self.traffic_counts['private_'+alias+'_data_reads'] += 1
            else:
                self.traffic_counts['live_data_reads'] += 1
                self.live_reads.append((self.current_worker, address, count, value))
            for lo, hi, name in self.metadata_ranges:
                if lo <= physical(address) < hi:
                    self.metadata_counts[name+'.'+alias] += 1
                    self.traffic_counts['metadata_'+alias+'_reads'] += 1
                    break
        return value

    def write(self, address, value, count=4):
        if (self.traffic_enabled and getattr(self, 'active', False) and
                getattr(self, 'inside_worker', False) and not self.private(address)):
            self.live_writes.append((self.current_worker, address, count,
                                     value & ((1 << (8*count))-1)))
        return super().write(address, value, count)

    def worker(self, name, args):
        result = super().worker(name, args)
        if self.traffic_enabled:
            self.callbacks.append(dict(function=name, instructions=result['instructions']))
        return result

    def traffic_result(self):
        self.traffic_enabled = False
        require([row['function'] for row in self.callbacks] ==
                ['_kui_vmu_native_passive_submit', '_kui_vmu_native_passive_complete'],
                'Workload did not run both real compiled C callbacks exactly once')
        return dict(counts=dict(self.traffic_counts), metadata_objects=dict(self.metadata_counts),
            callback_instructions=self.callbacks,
            total_callback_instructions=sum(row['instructions'] for row in self.callbacks),
            cache_delta={name: getattr(self.cache, name)-before
                         for name, before in self.cache_before.items()},
            live_read_events=len(self.live_reads), live_read_sha256=digest_events(self.live_reads),
            live_write_events=len(self.live_writes), live_write_sha256=digest_events(self.live_writes),
            rx_sha256=sha(self.rx_snapshot()),
            tx_sha256=sha(self.block(self.record.tx, 16384)))


def trial(base, mode):
    cpu = copy.deepcopy(base)
    if mode == 'canonical_p2':
        # This is the sole workload difference. Use raw backing memory, outside
        # execution, so the proof's immutable-metadata assertions remain active.
        SH.write(cpu, cpu.ready, 0)
    elif mode == 'cold_p1':
        for at in cpu.expected:
            cpu.cache.ocbi(at)
    elif mode == 'warm_p1':
        # A defined all-resident warm case, including fields a later SDK poll
        # may reach for the first time. Priming is outside the measured C call;
        # cold refills remain inside it and are reported separately.
        for at in cpu.expected:
            cpu.cache.read(at, 4)
    else:
        raise ValueError('Unknown traffic mode: '+mode)
    cpu.reset_traffic()
    result = poll(cpu)
    traffic = cpu.traffic_result()
    observable = {key: value for key, value in result.items()
                  if key not in ('submit_instructions', 'complete_instructions')}
    require(cpu.publish_count == cpu.live_successes == 1 and cpu.cache.writebacks == 0,
            'Traffic workload changed metadata lifetime or wrote back a cache line')
    require(SH.block(cpu, cpu.begin, cpu.end-cpu.begin) == cpu.publication_snapshot,
            'Traffic workload mutated immutable metadata')
    return cpu, observable, traffic


def check(args):
    build = args.build.resolve()
    require(args.source_commit and len(args.source_commit) == 40 and
            args.source_commit[:12].encode() in (build/'entry.elf').read_bytes(),
            'Final entry does not bind the exact runtime freeze')
    source, image, extent = fresh_verify(args.startup, args.record)
    record = Record(args.record.read_bytes(), image, source)
    cpu = TrafficSDK(args.startup, build, make_handoff(), record,
                     'metadata_cache', True, cached_metadata=True)
    cpu.prepare()
    initializer = cpu.initialize_sdk()
    cpu.seed_tick_boundary()
    first = poll(cpu)
    require(cpu.publish_count == cpu.live_successes == 1 and SH.read(cpu, cpu.ready) == READY,
            'Initial full SDK poll did not publish the real bound runtime')
    rounds = []
    for index in range(args.polls):
        variants = {mode: trial(cpu, mode) for mode in ('canonical_p2', 'warm_p1', 'cold_p1')}
        baseline, observable, reference = variants['canonical_p2']
        for mode in ('warm_p1', 'cold_p1'):
            candidate, actual, traffic = variants[mode]
            require(actual == observable, f'Observable SDK/VMU result changed in {mode} round {index}')
            require(candidate.live_reads == baseline.live_reads,
                    f'Live SDK/heap/MMIO read addresses, values or order changed in {mode} round {index}')
            require(candidate.live_writes == baseline.live_writes and
                    traffic['rx_sha256'] == reference['rx_sha256'] and
                    traffic['tx_sha256'] == reference['tx_sha256'],
                    f'Live stores or TX/RX packets changed in {mode} round {index}')
            require(traffic['counts'].get('metadata_p1_reads', 0) > 0 and
                    traffic['counts'].get('metadata_p2_reads', 0) <
                    reference['counts'].get('metadata_p2_reads', 0),
                    f'Cached view did not reduce actual P2 metadata loads in {mode} round {index}')
        require(reference['counts'].get('metadata_p1_reads', 0) == 0,
                'P2 baseline accidentally used a cached metadata view')
        require(variants['cold_p1'][2]['cache_delta']['fills'] > 0,
                'Cold case did not refill invalidated metadata cache lines')
        require(variants['warm_p1'][2]['cache_delta']['fills'] == 0,
                'Warm case unexpectedly refilled a metadata line')
        rounds.append(dict(round=index+1,
            variants={mode: values[2] for mode, values in variants.items()},
            identical_live_read_order=True, identical_live_store_order=True,
            identical_sdk_vmu_packets=True))
        cpu = variants['warm_p1'][0]
    require(cpu.tick_calls == 0 and sum(cpu.c_callback_calls.values()) >= 2*(args.polls+1),
            'Traffic proof lost TICK-off or real C callback execution')
    return dict(schema='kui.native.metadata-cache-traffic.v1', status='PASS',
        source_commit=args.source_commit,
        artifact_sha256={name: sha((build/name).read_bytes()) for name in
            ('native-worker.elf', 'native-worker.bin', 'native-worker.reloc', 'stage.elf', 'entry.elf')},
        source_sha256={name: sha((ROOT/name).read_bytes()) for name in SOURCES},
        original_input_sha256=sha(source), nvr_sha256=sha(args.record.read_bytes()), input_extent=extent,
        baseline='Exact same linked VMU ON worker, after one successful publication; only eligibility word cleared to force P2 views.',
        real_virtual_a2=True, real_c_callbacks=True, controller_tick_calls=cpu.tick_calls,
        targeted_cold_metadata_eviction=True, warm_prime_poll=first, sdk_initializer_instructions=initializer,
        warm_cache_state='All immutable metadata tags resident before measured callbacks; priming excluded from load counts.',
        cold_cache_state='All immutable metadata tags absent before measured callbacks; refills counted.',
        rounds=rounds, limitations=[
            'Interpreted scalar data loads and integer instructions; no hardware cycle or deadline claim.',
            'Baseline isolates P1 alias selection in the current worker; it is not a pre-optimization build comparison.',
            'Only isolated metadata operand-line tags are modeled; no cache capacity, conflicts, bus or IRQ model.',
            'SDK initializer scaffolding and callback gateways follow the existing bounded fixture.',
            'Automatic INFO enumeration/controller polling only; no game transition, FMV, SD I/O or durable-write result.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--startup', type=Path, required=True)
    p.add_argument('--record', type=Path, default=ROOT/'tests/fixtures/native-cache/evolution1.nvr')
    p.add_argument('--source-commit', required=True)
    p.add_argument('--polls', type=int, default=6)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    require(1 <= args.polls <= 32, 'Traffic poll count must be in [1,32]')
    result = check(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(dict(schema=result['schema'], status=result['status'],
                          output=str(args.output), polls=len(result['rounds']))))


if __name__ == '__main__':
    main()
