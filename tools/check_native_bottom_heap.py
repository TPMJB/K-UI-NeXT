#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked bottom-lease installer and its actual ownership checks.

Original executable inputs remain external. The integer model executes the
compiled installer, cold code/geometry predicates, coherent reader and original
SDK allocator/free entries. Display output and SHA-256 compression are pure
leaves: their real arguments are checked, and the digest is computed from the
actual modeled original bytes. No allocator or ownership predicate is folded.
Execution stops at the validated native_passive_run handoff; separate mandatory
publication and worker proofs cover arming after that boundary.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from profile_vmu_native_release_sh import ReleaseSH
from test_vmu_native_heap_execution import SH, MASK, signed, verified_program_image
from sh4_delay_slots import validate_delay_slot

SCHEMA = 'kui.native.bottom-heap.compiled-cold.v1'
STAGE_MARKER = '__kui_native_bottom_heap_stage'
WORKER_MARKER = '__kui_native_bottom_heap_worker'
ABSOLUTE_ONE = (1, 0, 0, 0xfff1)
RETURN = 0xdeadbeee
LEASE_BYTES = 0x60000
REFUSALS = ('status', 'entry-begin', 'entry-bytes', 'return-pr', 'owner-sp',
            'initial-mask', 'initial-digest', 'bad-allocate-callback',
            'bad-release-callback', 'cold-release-code', 'placeholder-header',
            'placeholder-free-size', 'allocated-header', 'allocated-not-empty',
            'release-code-before-free', 'ready-free-size', 'ready-free-link',
            'release-candidate', 'release-free-overlap', 'release-size-mismatch',
            'final-free-intrusion', 'mask-1', 'digest-1', 'mask-2', 'digest-2',
            'mask-3', 'digest-3')
PIN_FUNCTIONS = {'_alias@vmu_native_stage_placement.c': (32,
                                         '5d5b341944aa4da08467cfca39b78f276224218bd3ed9fd094c99456668b974d'),
 '_allocation_safe@vmu_native_heap_probe_stage.c': (52,
                                                    'a5bbd0fab21d85267320e0aa1b51921690ab3109cbf4ee4a7a54fa3d994d6d06'),
 '_bottom_geometry@vmu_native_heap.c': (24,
                                        'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_code_sha@vmu_native_heap_probe_stage.c': (64,
                                             '3c9d5044617b9e0e05e6bb4a69177c9fb26a4cdeaf4e16c6651ef65a761114c5'),
 '_control@vmu_native_heap_probe_stage.c': (28,
                                            '4e5efaa5fdceffa3a31fbffb123c1333bf951e2e1fe70390a272c5c58fa661a7'),
 '_excludes_stage@vmu_native_stage_placement.c': (64,
                                                  '6da651591cc2da515e9c25f48d042e81f33a609676ac415ecdbf2195b7e14620'),
 '_fresh@vmu_native_heap.c': (116,
                              '02f6d55ab765cbc3bf6fedf6bf7cb3d21afb5774e38d0d5751ad8fb6dfbb62d2'),
 '_heap_bottom_code_span@vmu_native_heap.c': (88,
                                              '4415392667afcf642fd49d8b1b34d294f61ba05a253536ae25aa0b73907990ce'),
 '_heap_bottom_halfword@vmu_native_heap.c': (58,
                                             'd66affe12d8e87985e0bbe094cc393649bad89f9e4494f55004afda5f193ab39'),
 '_identity@vmu_native_heap.c': (412,
                                 '04ed603d3f01c8222f6dce2d3ffeb7d3e97b5312046dc2bf31acfef00e435ed2'),
 '_index@vmu_native_heap.c': (100,
                              '10c56bfc6f333ab7f0744d56ef343fb152b67ac43cc04706e1e5e58caa0bdada'),
 '_kui_vmu_native_heap_bottom_allocated': (172,
                                           'caee27c803ee0d1be0c8fefb22824c8276428db3e79be58aca9aaf75281782bf'),
 '_kui_vmu_native_heap_bottom_code_valid': (360,
                                            'f4f9fac38b9394247c0100926cba20d29cd3afa6bbe772b1d3f7702daf5837ab'),
 '_kui_vmu_native_heap_bottom_fresh': (148,
                                       '3a9db65bc20e642827041d3e4822aa1e6d58d5a012d384dafcefdcea826ce5f4'),
 '_kui_vmu_native_heap_bottom_placeholder': (196,
                                             '9a2a374b75ad14796a8d267fba4aedadc01ed6f2ad8c76737251c8260dfb2fd1'),
 '_kui_vmu_native_heap_bottom_ready': (144,
                                       'a73263dca0823568fe9a7cfab7f94475962de6361513d34de4a5f2cb526ddb41'),
 '_kui_vmu_native_heap_fresh': (268,
                                '19fe415758dbf7c42a28986582d458f7078a564becd11a13baeafd4e3dcba353'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               '35f3470bb3812f257b93dbadd8d79a5b639b47990dc88e08f74664de7646f0f2'),
 '_kui_vmu_native_heap_lease_valid': (28,
                                      '6c4884837dfbcb9b9e745d44221fea8627a00e1b0e660e20a5085c579ae4841a'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   '21fa8c57d262ba50c11b880ef521fa2078d2f3eb84d824d5456f3f0666dcf6e6'),
 '_kui_vmu_native_heap_preallocate': (36,
                                      '65aa1669c7e5bc491e729c7cf7b382913fa876f88c30732d482978e82586ed04'),
 '_kui_vmu_native_stage_bottom_allocation_safe': (308,
                                                  '9dbc957fe68fdbfa46209b78120e638959646b1ac0153f89c2b482883881ce34'),
 '_kui_vmu_observe_stage_install': (372,
                                    '22d19bf4831d645494c3df6faf81a83d37ef15a7f03e20682d8b5f126a540111'),
 '_memcmp': (34, '3d7da64606fe412df2341ca8e1c2c56d14d3f4798465f1ec02f6d2994c6aefd9'),
 '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
 '_metadata@vmu_native_heap.c': (416,
                                 'fad90c1390c1e91d1a963257793e3b2241a98c77bfef56e9624f48d9b0a91071'),
 '_native_allocate_bottom@vmu_native_heap_probe_stage.c': (304,
                                                           'f8dedfa6e259f858bfde9332390d61fa423e8c5c497b5f0d1492d1277e998592'),
 '_native_allocator_unchanged@vmu_native_heap_probe_stage.c': (68,
                                                               '897fba1f5473dc12f1f16d226da6ff6c3a7bb7efc9b2714c49656d97fcb2e587'),
 '_native_low_bindings@vmu_native_heap_probe_stage.c': (64,
                                                        'c15f5da877d31bd8268056682015a0c4c9fbbceeab2b3bf459bfb5e4f747a4a2'),
 '_native_read@vmu_native_heap_probe_stage.c': (84,
                                                'b6bc33c6d4c36f9e6625ccf6afdc55b98e2e8f684d95194687fa946071a4804d'),
 '_read_is@vmu_native_heap.c': (80,
                                '820990931f45d5fc926fd184de6c86ec74f5932e34fa691fef9d2747060f888d'),
 '_span@vmu_native_heap.c': (76,
                             '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_span@vmu_native_stage_placement.c': (80,
                                        '8d2d98b708d30eaab3113e455947ecd33b7ad73c36e8b4889bb1e14e463820b1')}
PIN_FUNCTIONS['_memset'] = (26, '84a19f7aa0536a77835dd9917cd47ea65e6d6edf6013064ab39a36c0a05e118a')
PIN_OBJECTS = {'_heap_bottom_allocate_words@vmu_native_heap.c': (206,
                                                   '528ea52471bc42b01f1f4c4a40e7510f5346db70a539a35a40b8c97fbe8ba174'),
 '_heap_bottom_release_words@vmu_native_heap.c': (154,
                                                  '441e8180758616454557f02badc0b5b6a2b23b752ab88f5f6fb8e442ce84cb9e')}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def records(image):
    """Retain STT_FILE identity for duplicate local helper names."""
    data, _, _, sections, _ = image
    result = {}
    for table in sections:
        if table[1] != 2: continue
        strings = sections[table[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        source = ''
        for at in range(table[4], table[4] + table[5], 16):
            name, address, size, info, _, owner = struct.unpack_from('<IIIBBH', data, at)
            label = names[name:].split(b'\0', 1)[0].decode('ascii')
            if info & 15 == 4: source = label
            elif label and info & 15 in (1, 2) and owner < len(sections):
                qualified = label + ('@' + source if not info >> 4 else '')
                section = sections[owner]
                if section[1] == 1 and section[2] & 2 and size:
                    require(section[3] <= address < address + size <= section[3] + section[5],
                            'Cold proof symbol escapes initialized owner: ' + qualified)
                    offset = section[4] + address - section[3]
                    result[qualified] = (address, data[offset:offset + size], info & 15, section)
    return result


def pinned_records(stage):
    available = records(stage)
    result = {}
    for pins, kind, flags in ((PIN_FUNCTIONS, 2, 6), (PIN_OBJECTS, 1, 2)):
        require(pins, 'Bottom cold reviewed whole-body map is missing')
        for name, expected in pins.items():
            require(name in available, 'Bottom cold symbol missing: ' + name)
            address, body, actual_kind, section = available[name]
            require(actual_kind == kind and section[2] & 7 == flags and
                    (len(body), sha(body)) == tuple(expected),
                    'Bottom cold exact instructions/data changed: ' + name)
            result[name] = dict(address=address, bytes=len(body), sha256=sha(body))
    return result


def scalar_truth():
    """Edge values for every additional scalar instruction form."""
    checks = 0
    values = (0, 1, 2, 0x7fffffff, 0x80000000, 0xfffffffe, MASK)
    c = object.__new__(Cold)
    c.r = [0] * 16
    c.instructions = c.executing = 0
    for opcode in (0x0008, 0x0018):
        for low in (0, 1):
            c.sr = 0x600080f0 | low
            c.plain(opcode, 0)
            assert c.sr == (0x600080f1 if opcode == 0x0018 else 0x600080f0)
            checks += 1
    for a in values:
        for b in values:
            for carry in (0, 1):
                for opcode in (0x312a, 0x312e):
                    c.r[1], c.r[2], c.sr = a, b, 0x600080f0 | carry
                    c.plain(opcode, 0)
                    if opcode & 15 == 0xa:
                        expected = (a + (1 << 33) - b - carry) % (1 << 32)
                        flag = a < b + carry
                    else:
                        expected = (a + b + carry) % (1 << 32)
                        flag = a + b + carry >= 1 << 32
                    assert c.r[1] == expected and c.sr == 0x600080f0 | flag
                    checks += 1
            for opcode in (0x3123, 0x3127):
                c.r[1], c.r[2], c.sr = a, b, 0x600080f0
                c.plain(opcode, 0)
                expected = (a ^ 0x80000000) >= (b ^ 0x80000000) if opcode & 15 == 3 else (
                    (a ^ 0x80000000) > (b ^ 0x80000000))
                assert c.sr == 0x600080f0 | expected
                checks += 1
    for base in (0x8c010000, 0xacffffff):
        for displacement in (0, 1, 15, 127, 255):
            c.sr = 0x600080f1
            c.plain(0xc700 | displacement, base)
            assert c.r[0] == ((base + 4) // 4) * 4 + displacement * 4
            assert c.sr == 0x600080f1
            checks += 1
    for value in (0, 1, 127, 128, 255):
        for displacement in (0, 1, 15):
            c.r[2], c.sr = 0x8c100000, 0x600080f1
            c.read = lambda address, count, v=value, d=displacement: (
                v if address == 0x8c100000 + d and count == 1 else None)
            c.plain(0x8420 | displacement, 0)
            assert c.r[0] == (value if value < 128 else value - 256) & MASK
            assert c.sr == 0x600080f1
            checks += 1
    return checks


def paired(stage, worker):
    from check_native_followup_profile import bottom_stage_profile, bottom_worker_profile
    a, b = bottom_stage_profile(stage), bottom_worker_profile(worker)
    require(a == b, 'Bottom heap stage/worker capabilities are not paired')
    return a


def require_companion(stage, worker, proof, corpus=None):
    require(paired(stage, worker), 'Bottom companion requires both exact capabilities')
    require(isinstance(proof, dict) and proof.get('schema') == SCHEMA and
            proof.get('enabled') is True and proof.get('development') is False and
            proof.get('artifact_sha256') == dict(stage=sha(stage[0]), worker=sha(worker[0])),
            'Bottom cold companion artifact/schema differs')
    require(proof.get('exact_linked_kernels') == pinned_records(stage),
            'Bottom cold companion omits a complete instruction/constant pin')
    require(all(proof.get(k) is True for k in (
        'actual_linked_installer_executed', 'actual_c_ownership_predicates_executed',
        'actual_original_allocate_and_release_executed')),
        'Bottom cold companion did not execute a mandatory guard/owner scope')
    positive = proof.get('positive_cases', [])
    require(len(positive) == 16 and len({(p.get('slug'), p.get('reader')) for p in positive}) == 16 and
            all(p.get('reader') in (1, 2) and isinstance(p.get('instructions'), int) and
                p['instructions'] > 0 for p in positive),
            'Bottom cold companion lacks both selected readers for every owner')
    require(len(proof.get('refusal_cases', [])) == len(REFUSALS) and
            {row.get('mutation') for row in proof.get('refusal_cases', [])} == set(REFUSALS) and
            len(proof.get('actual_placement_cases', [])) == 18 and
            proof.get('extra_scalar_truth_cases') == scalar_truth(),
            'Bottom cold companion refusal/geometry/scalar coverage differs')
    summary = proof.get('source_corpus_summary', {})
    require(summary.get('placement') == 'bottom' and summary.get('full_structural_selection') is True and
            summary.get('case_count') == 72 and summary.get('minimum_case_count') == 96 and
            summary.get('artifacts', {}).get('stage.elf') == sha(stage[0]) and
            summary.get('artifacts', {}).get('native-worker.elf') == sha(worker[0]),
            'Bottom cold companion is detached from the complete original allocator proof')
    require(len(summary.get('slugs', [])) == len(set(summary.get('slugs', []))) == 8 and
            {(p['slug'], p['reader']) for p in positive} ==
            {(slug, reader) for slug in summary['slugs'] for reader in (1, 2)},
            'Bottom cold companion owner/reader case matrix differs')
    if corpus is not None:
        require(proof.get('source_corpus_sha256') == sha(json.dumps(
                    corpus, sort_keys=True, separators=(',', ':')).encode()) and
                summary.get('artifacts') == corpus.get('artifacts'),
                'Bottom cold companion uses a different original-corpus report')
    from check_native_bottom_heap_worker import require_companion as worker_companion
    require(worker_companion(worker, proof.get('worker_heap_predicates')),
            'Bottom cold companion lacks actual worker geometry/free-list execution')
    require(proof.get('source_sha256') == {name: sha((ROOT / name).read_bytes()) for name in SOURCES},
            'Bottom cold proof source identities differ')
    tests = proof.get('self_tests', {})
    require(tests.get('whole_closure_mutations', 0) >= 35 and tests.get('marker_refusals', 0) >= 6 and
            tests.get('companion_refusals', 0) >= 8,
            'Bottom cold companion has no negative proof checks')
    return True


SOURCES = ('tools/check_native_bottom_heap.py', 'tools/check_native_bottom_heap_worker.py',
           'tools/profile_vmu_native_release_sh.py', 'tests/test_vmu_native_heap_execution.py',
           'src/loader/vmu_native_heap_probe_stage.c', 'src/core/vmu_native_heap.c',
           'src/core/vmu_native_heap_bottom_code.inc', 'src/core/vmu_native_stage_placement.c',
           'include/kui/vmu_native_heap.h', 'include/kui/vmu_native_stage_placement.h')


class Refused(Exception):
    pass


class Cold(ReleaseSH):
    """Actual stage and original SDK code share one physically aliased RAM."""
    def __init__(self, directory, row, *, reader=1, alias=None, seed=0,
                 mutation=None):
        self.watch = False
        super().__init__(directory / 'stage.elf')
        self.directory, self.row = directory, row
        self.source, self.image, self.extent = verified_program_image(
            row['startup'], row['record'])
        self.store_block(0x8c010000, self.image)
        wire = Path(row['record']).read_bytes()
        self.owner = list(struct.unpack_from('<18I', wire, 608))
        self.owner_call = list(struct.unpack_from('<5I', wire, 680))
        self.begin, self.count = self.owner_call[3:5]
        if alias is not None:
            self.begin = (self.begin & 0x1fffffff) | alias
        self.initialize_sr = 0x600001f0 | (seed & 1)
        self.sr = self.initialize_sr
        self.mach = self.gbr = self.fpscr = self.fpul = 0
        self.fr = [0] * 32
        SH.run(self, self.owner[6], [self.begin, self.count], 0x8c00efc0)
        self.initial_heap = self.block(self.begin, 64)
        self.heap_at = self.symbols['_native_heap'][0]
        self.call_at = self.symbols['_native_call'][0]
        self.store_block(self.heap_at, struct.pack('<18I', *self.owner))
        self.owner_call[3] = self.begin
        self.store_block(self.call_at, struct.pack('<5I', *self.owner_call))
        self.write(self.symbols['_exec_bytes'][0], len(self.image))
        self.store_block(self.symbols['_native_heap_code_sha'][0],
                         hashlib.sha256(self.block(self.owner[6], 0x1d0)).digest())
        manifest = self.symbols['_manifest'][0]
        self.write(manifest + 44, 1)
        self.write(manifest + 48, reader)
        table = self.symbols['_native_low_bindings_table'][0] + (reader - 1) * 24
        self.control_at = self.read(table + 8) | 0x20000000
        self.store_block(self.control_at, bytes(56))
        self.write(self.control_at + 8, 2)
        self.write(self.control_at + 20, self.begin)
        self.write(self.control_at + 24, self.count)
        self.write(self.control_at + 36, self.owner_call[2])
        self.write(self.control_at + 28, 0x8c00ed00)
        self.stack_low, stack_bytes = self.symbols['_native_installer_stack'][:2]
        self.stack = self.stack_low + stack_bytes - 32
        self.stage_begin = 0x8ce00000
        self.stage_end = self.symbols['__retail_stage_bss_end'][0]
        rng = random.Random(seed)
        self.r = [rng.getrandbits(32) for _ in range(16)]
        self.r[15] = self.stack
        self.sr = self.initialize_sr
        self.pr = RETURN
        self.pc = self.symbols['_kui_vmu_observe_stage_install'][0]
        self.instructions = 0
        self.writes.clear()
        self.before = self.r.copy(), self.pr, self.sr
        self.mutation = mutation
        self.entered, self.events, self.owner_calls = set(), [], []
        self.executed = set()
        self.hash_contexts = {}
        self.unchanged_calls = self.code_calls = 0
        self.release_walk_calls = 0
        self.lease_at = None
        self.published = self.refusal = None
        self.passive_calls = 0
        self.original_call_sp = None
        self.leaves = {self.symbols[n][0]: n for n in (
            '_kui_sha256_init', '_kui_sha256_update', '_kui_sha256_digest',
            '_retail_display_line', '_native_report', '_native_passive_run')}
        self.entries = {v[0]: n for n, v in self.symbols.items() if v[2] == 2}
        self.event_entries = {self.symbols[n][0]: n for n in (
            '_native_allocate_bottom', '_native_allocator_unchanged',
            '_kui_vmu_native_heap_bottom_code_valid',
            '_kui_vmu_native_heap_bottom_placeholder',
            '_kui_vmu_native_heap_bottom_allocated',
            '_kui_vmu_native_heap_bottom_ready',
            '_kui_vmu_native_heap_lease_valid_with_release')}
        if mutation:
            self.change('start')
        self.watch = True

    def change(self, event):
        previous = self.watch
        self.watch = False
        try:
            self._change(event)
        finally:
            self.watch = previous

    def _change(self, event):
        if not self.mutation:
            return
        name = self.mutation
        if event == 'start':
            if name == 'status': self.write(self.control_at + 8, 1)
            elif name == 'entry-begin': self.write(self.control_at + 20, self.begin + 32)
            elif name == 'entry-bytes': self.write(self.control_at + 24, self.count - 32)
            elif name == 'return-pr': self.write(self.control_at + 36, self.owner_call[2] ^ 2)
            elif name == 'owner-sp': self.write(self.control_at + 28, 0x8c008000)
            elif name == 'initial-mask': self.sr &= ~0xf0
            elif name == 'initial-digest':
                at = self.symbols['_native_heap_code_sha'][0]
                self.write(at, self.read(at) ^ 1)
            elif name == 'bad-allocate-callback': self.write(self.heap_at + 36, self.owner[9] ^ 2)
            elif name == 'bad-release-callback': self.write(self.heap_at + 40, self.owner[10] ^ 2)
            elif name == 'cold-release-code':
                at = self.owner[10] + 0x96
                self.write(at & ~3, self.read(at & ~3) ^ (1 << ((at & 2) * 8)))
        if event.startswith('unchanged:'):
            at = int(event.split(':')[1])
            if name == f'mask-{at}': self.sr &= ~0xf0
            if name == f'digest-{at}':
                dest = self.symbols['_native_heap_code_sha'][0]
                self.write(dest, self.read(dest) ^ 1)
        if event == 'placeholder':
            if name == 'placeholder-header':
                at = self.begin + LEASE_BYTES + 36
                self.write(at, self.read(at) ^ 1)
            elif name == 'placeholder-free-size':
                self.write(self.begin + 4, self.read(self.begin + 4) ^ 1)
        if event == 'allocated':
            if name == 'allocated-header': self.write(self.begin + 4, self.read(self.begin + 4) ^ 1)
            elif name == 'allocated-not-empty': self.write(self.owner[13], self.begin)
            elif name == 'release-code-before-free':
                at = self.owner[10] + 0x96
                self.write(at & ~3, self.read(at & ~3) ^ (1 << ((at & 2) * 8)))
        if event == 'ready':
            if name == 'ready-free-size':
                at = self.begin + LEASE_BYTES + 36
                self.write(at, self.read(at) ^ 1)
            elif name == 'ready-free-link':
                at = self.begin + LEASE_BYTES + 32
                self.write(at, self.begin)
        if event == 'release-proof':
            if name == 'release-candidate': self.write(self.r[15], self.begin + 64)
            elif name == 'release-free-overlap': self.write(self.owner[13], self.begin)
            elif name == 'release-size-mismatch':
                at = self.begin + LEASE_BYTES + 36
                self.write(at, self.read(at) - 1)
        if event == 'final-proof' and name == 'final-free-intrusion':
            self.write(self.begin + LEASE_BYTES + 32, self.begin)

    def write(self, address, value, count=4):
        if self.watch:
            p = address & 0x1fffffff
            stage = self.stage_begin & 0x1fffffff
            stage_end = self.stage_end & 0x1fffffff
            original = self.owner[9] <= self.pc < self.owner[9] + 0xce or (
                self.owner[10] <= self.pc < self.owner[10] + 0x9a)
            if original:
                header = self.begin & 0x1fffffff
                upper = header + LEASE_BYTES + 32
                sentinel = self.owner[13] & 0x1fffffff
                stack = self.original_call_sp & 0x1fffffff
                require((count == 4 and (
                    header <= p < p + 4 <= header + 8 or
                    upper <= p < p + 4 <= upper + 8 or
                    sentinel <= p < p + 4 <= sentinel + 0x34)) or
                    count in (2, 4) and stack - 16 <= p < p + count <= stack,
                    f'Original cold allocator store outside its exact metadata: {address:#x}')
            else:
                require(stage <= p < p + count <= stage_end or
                        p == (self.control_at & 0x1fffffff) + 12 or
                        p == (self.control_at & 0x1fffffff) + 8,
                        f'Compiled cold stage wrote outside owned stage/control: {address:#x}')
            if p == (self.control_at & 0x1fffffff) + 12:
                require(self.passive_calls == 0, 'Lease republished after worker entry')
                self.published = value
        return super().write(address, value, count)

    def plain(self, op, pc):
        n, m = op >> 8 & 15, op >> 4 & 15
        if op in (0x0008, 0x0018):
            self.flag(op == 0x0018)
        elif op & 0xf00f in (0x300a, 0x300e):
            a, b, carry = self.r[n], self.r[m], self.sr & 1
            value = a - b - carry if op & 15 == 0xa else a + b + carry
            self.r[n] = value & MASK
            self.flag(value < 0 if op & 15 == 0xa else value > MASK)
        elif op & 0xf00f == 0x3003:
            self.flag(signed(self.r[n], 32) >= signed(self.r[m], 32))
        elif op & 0xf00f == 0x3007:
            self.flag(signed(self.r[n], 32) > signed(self.r[m], 32))
        elif op & 0xff00 == 0x8400:
            self.r[0] = signed(self.read(self.r[m] + (op & 15), 1), 8) & MASK
        elif op & 0xff00 == 0xc700:
            self.r[0] = ((pc + 4) & ~3) + (op & 255) * 4
        else:
            return super().plain(op, pc)
        self.executing = pc
        self.instructions += 1

    def abi_return(self, result=0):
        for i in range(8): self.r[i] = (0x99887766 + i * 0x112233) & MASK
        self.r[0], self.pc = result & MASK, self.pr
        self.flag(True)

    def leaf(self, name):
        if name == '_native_report':
            self.refusal = (self.r[4], self.r[5])
            raise Refused()
        if name == '_retail_display_line':
            text = self.block(self.r[4], 48).split(b'\0', 1)[0]
            require(text.startswith(b'COLD '), 'Unexpected cold display call')
            self.abi_return(); return
        if name == '_kui_sha256_init':
            self.hash_contexts[self.r[4]] = bytearray()
            self.abi_return(); return
        if name == '_kui_sha256_update':
            context, at, count = self.r[4:7]
            require(context in self.hash_contexts and at == self.owner[6] and count == 0x1d0,
                    'Cold digest changed its exact source and byte count')
            self.hash_contexts[context] += self.block(at, count)
            self.abi_return(); return
        if name == '_kui_sha256_digest':
            context, at = self.r[4:6]
            require(context in self.hash_contexts and len(self.hash_contexts[context]) == 0x1d0,
                    'Cold digest does not follow exact update')
            digest = hashlib.sha256(self.hash_contexts.pop(context)).digest()
            for i, value in enumerate(digest): self.write(at + i, value, 1)
            self.abi_return(); return
        require(name == '_native_passive_run', 'Unknown abstract leaf')
        self.passive_calls += 1
        require(self.lease_at == self.r[4] and self.published == self.begin + 32 and
                self.read(self.control_at + 8) == 3,
                'Worker entered without the exact published bottom lease')
        lease = struct.unpack('<29I', self.block(self.r[4], 116))
        require(lease[2] == 2 and lease[-4] == self.begin and lease[-3] == self.begin + 32,
                'Worker received a non-bottom or unarmed lease')
        self.abi_return()

    def run_cold(self):
        try:
            for _ in range(1000000):
                pc = self.pc
                if pc == RETURN:
                    require(self.r[15] == self.before[0][15] and self.r[8:15] == self.before[0][8:15],
                            'Compiled cold installer lost C ABI registers/stack')
                    require(self.sr & ~1 == self.before[2] & ~1, 'Compiled cold installer changed inherited SR')
                    return True
                if pc in self.entries: self.entered.add(self.entries[pc])
                if pc in self.event_entries:
                    event = self.event_entries[pc]
                    self.events.append(event)
                    if event == '_native_allocate_bottom': self.lease_at = self.r[4]
                    elif event == '_native_allocator_unchanged':
                        self.unchanged_calls += 1
                        self.change(f'unchanged:{self.unchanged_calls}')
                    elif event == '_kui_vmu_native_heap_bottom_code_valid': self.code_calls += 1
                    elif event.endswith('_bottom_placeholder'): self.change('placeholder')
                    elif event.endswith('_bottom_allocated'): self.change('allocated')
                    elif event.endswith('_bottom_ready'): self.change('ready')
                    elif event == '_kui_vmu_native_heap_lease_valid_with_release':
                        self.release_walk_calls += 1
                        self.change('release-proof' if self.release_walk_calls == 1 else 'final-proof')
                if pc in (self.owner[9], self.owner[10]):
                    self.original_call_sp = self.r[15]
                    self.owner_calls.append(('allocate' if pc == self.owner[9] else 'release', self.r[4]))
                if pc in self.leaves:
                    self.leaf(self.leaves[pc]); continue
                if self.stage_begin <= pc < self.stage_end: self.executed.add(pc)
                op = self.read(pc, 2)
                target, delay, n = None, False, op >> 8 & 15
                if op >> 12 in (0xa, 0xb):
                    target, delay = (pc + 4 + 2 * signed(op & 4095, 12)) & MASK, True
                    if op >> 12 == 0xb: self.pr = pc + 4
                elif op & 0xf0ff in (0x400b, 0x402b):
                    target, delay = self.r[n], True
                    if op & 0xf0ff == 0x400b: self.pr = pc + 4
                elif op == 0xb:
                    target, delay = self.pr, True
                elif op >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
                    taken = bool(self.sr & 1) == (op >> 8 in (0x89, 0x8d))
                    delay = op >> 8 in (0x8d, 0x8f)
                    target = pc + 4 + 2 * signed(op & 255, 8) if taken else pc + (4 if delay else 2)
                if target is None:
                    self.plain(op, pc); self.pc = pc + 2
                else:
                    self.instructions += 1
                    if delay:
                        slot = self.read(pc + 2, 2)
                        validate_delay_slot(slot, pc)
                        if self.stage_begin <= pc + 2 < self.stage_end: self.executed.add(pc + 2)
                        self.plain(slot, pc + 2)
                    self.pc = target & MASK
            raise ValueError('Compiled bottom cold installer exceeded instruction bound')
        except Refused:
            return False


def check(directory, *, self_test=False, corpus=None, development=False):
    directory = Path(directory).resolve()
    stage, worker = read_elf(directory / 'stage.elf'), read_elf(directory / 'native-worker.elf')
    if not paired(stage, worker): return dict(schema=SCHEMA, enabled=False)
    if corpus is None:
        corpus = json.loads((directory / 'native-bottom-heap-corpus.json').read_text())
    if not development:
        require(corpus.get('schema') == 'kui.native.corpus-original-heap.v1' and
                corpus.get('bottom_heap_schema') == 'kui.native.bottom-corpus-original-heap.v1' and
                corpus.get('placement') == 'bottom' and corpus.get('full_structural_selection') is True and
                corpus.get('enabled') is True and corpus.get('lease_bytes') == LEASE_BYTES,
                'Bottom cold proof requires the complete original-corpus bottom proof')
        for name in ('stage.elf', 'native-worker.elf', 'native-worker.bin', 'native-ra-aux.bin'):
            require(corpus.get('artifacts', {}).get(name) == sha((directory / name).read_bytes()),
                    'Bottom original-corpus proof does not bind this artifact: ' + name)
        require(corpus.get('case_count') == 72 and corpus.get('minimum_case_count') == 96 and
                len(corpus.get('results', [])) == len(corpus.get('inputs', [])) == 8,
                'Bottom cold proof lacks complete original source cases')
        for name, expected in corpus.get('source_sha256', {}).items():
            require(sha((ROOT / name).read_bytes()) == expected,
                    'Bottom original-corpus source changed: ' + name)
        require(corpus.get('source_sha256'), 'Bottom original-corpus proof omits its source bindings')
    rows = corpus['inputs']
    kernels = pinned_records(stage) if not development else {}
    positive, refusals, entered, executed = [], [], set(), set()
    placement_cases = []
    for row in rows:
        for reader in (1, 2):
            cpu = Cold(directory, row, reader=reader, seed=reader)
            require(cpu.run_cold(), f'Actual bottom installer refused {row["slug"]}: {cpu.refusal}')
            require(cpu.owner_calls == [('allocate', cpu.count - LEASE_BYTES - 64),
                                        ('allocate', LEASE_BYTES),
                                        ('release', cpu.begin + LEASE_BYTES + 64)],
                    'Compiled bottom installer changed original call order/arguments')
            require(cpu.unchanged_calls == 3 and cpu.code_calls == 2 and cpu.passive_calls == 1,
                    'Compiled bottom installer omitted a mandatory complete guard')
            entered |= cpu.entered
            executed |= cpu.executed
            positive.append(dict(slug=row['slug'], reader=reader, instructions=cpu.instructions))
    if self_test:
        for name in REFUSALS:
            cpu = Cold(directory, rows[0], mutation=name)
            require(not cpu.run_cold() and cpu.refusal is not None,
                    'Actual bottom installer accepted mutation: ' + name)
            require(cpu.published is None and cpu.passive_calls == 0,
                    'Bottom installer published/armed after refusal: ' + name)
            if name == 'cold-release-code':
                require(cpu.refusal == (6, 0x63) and not cpu.owner_calls,
                        'Bad cold code reached an original allocation')
            if name in ('bad-allocate-callback', 'bad-release-callback'):
                require(cpu.refusal[0] == 4 and not cpu.owner_calls,
                        'Invalid original callback reached an allocator entry')
            if name in ('allocated-header', 'allocated-not-empty', 'release-code-before-free',
                        'release-candidate', 'release-free-overlap', 'release-size-mismatch'):
                require(cpu.refusal == (6, 0x61) and len(cpu.owner_calls) == 2 and
                        all(call[0] == 'allocate' for call in cpu.owner_calls),
                        'Invalid placeholder/release proof reached original free: ' + name)
            entered |= cpu.entered
            executed |= cpu.executed
            refusals.append(dict(mutation=name, phase=cpu.refusal[0], detail=cpu.refusal[1],
                                 original_calls=cpu.owner_calls))
        first = 0x0ce00000
        end = stage[4]['__retail_stage_bss_end'][0] & 0x1fffffff
        geometry = [(first - LEASE_BYTES - 64, LEASE_BYTES + 96, True),
                    (first - LEASE_BYTES - 32, LEASE_BYTES + 96, False),
                    (first - LEASE_BYTES, LEASE_BYTES + 96, False),
                    ((end + 31) & ~31, LEASE_BYTES + 96, True),
                    (0x0c008000, LEASE_BYTES + 96, False),
                    (0x0cffffe0, LEASE_BYTES + 96, False)]
        for alias in (0, 0x80000000, 0xa0000000):
            for at, count, expected in geometry:
                cpu = Cold(directory, rows[0])
                cpu.pc = cpu.symbols['_allocation_safe'][0]
                cpu.r[4:6] = [at | alias, count]
                cpu.before = cpu.r.copy(), cpu.pr, cpu.sr
                require(cpu.run_cold() and cpu.r[0] == int(expected) and not cpu.owner_calls,
                        'Actual bottom placement accepted an incorrect metadata/stage boundary')
                executed |= cpu.executed
                placement_cases.append(dict(arena=at | alias, bytes=count, admitted=expected))
    if not development:
        spans = [(item['address'], item['address'] + item['bytes'])
                 for name, item in kernels.items() if name in PIN_FUNCTIONS]
        require(all(any(begin <= pc < end for begin, end in spans) for pc in executed),
                'Actual bottom cold control flow escaped its complete pinned closure')
    from check_native_bottom_heap_worker import check as check_worker
    report = dict(schema=SCHEMA, enabled=True, development=development,
                artifact_sha256=dict(stage=sha(stage[0]), worker=sha(worker[0])),
                actual_linked_installer_executed=True, actual_c_ownership_predicates_executed=True,
                actual_original_allocate_and_release_executed=True,
                pure_leaf_models=['display', 'SHA-256 compression over exact actual bytes'],
                validated_handoff_boundary='_native_passive_run; subsequent worker arming is proved separately',
                positive_cases=positive, refusal_cases=refusals,
                actual_placement_cases=placement_cases, extra_scalar_truth_cases=scalar_truth(),
                entered_stage_functions=sorted(entered), exact_linked_kernels=kernels,
                source_corpus_sha256=sha((json.dumps(corpus, sort_keys=True, separators=(',', ':'))).encode()),
                source_corpus_summary=dict(placement=corpus.get('placement'),
                    full_structural_selection=corpus.get('full_structural_selection'),
                    slugs=sorted(row['slug'] for row in rows),
                    case_count=corpus.get('case_count'), minimum_case_count=corpus.get('minimum_case_count'),
                    artifacts=corpus.get('artifacts')),
                worker_heap_predicates=check_worker(directory, self_test=self_test),
                source_sha256={name: sha((ROOT / name).read_bytes()) for name in SOURCES})
    if self_test and not development:
        whole_mutants = 0
        available = records(stage)
        for name in (*PIN_FUNCTIONS, *PIN_OBJECTS):
            address, body, _, section = available[name]
            at = section[4] + address - section[3]
            # Every complete body/table contributes beginning, interior and
            # final byte mutations through the production pin checker.
            for offset in sorted({0, len(body) // 2, len(body) - 1}):
                changed = bytearray(stage[0]); changed[at + offset] ^= 1
                try: pinned_records((bytes(changed), *stage[1:]))
                except ValueError: whole_mutants += 1
                else: raise ValueError('Bottom cold pin accepted a changed closure byte: ' + name)
        marker_refusals = 0
        for which, marker in ((0, STAGE_MARKER), (1, WORKER_MARKER)):
            for value in (None, (0, 0, 0, 0xfff1), (2, 0, 0, 0xfff1)):
                images = [stage, worker]
                symbols = dict(images[which][4])
                if value is None: symbols.pop(marker)
                else: symbols[marker] = value
                images[which] = (*images[which][:4], symbols)
                try: paired(*images)
                except ValueError: marker_refusals += 1
                else: raise ValueError('Bottom cold accepted a missing/changed paired marker')
        # This local candidate cannot escape before every refusal below is
        # exercised. Seed the expected count solely to validate other fields.
        report['self_tests'] = dict(whole_closure_mutations=whole_mutants,
                                   marker_refusals=marker_refusals, companion_refusals=8)
        companion_refusals = 0
        for name, value in (('schema', 'bad'), ('enabled', False), ('development', True),
                            ('artifact_sha256', {}), ('positive_cases', []),
                            ('refusal_cases', []), ('actual_placement_cases', []),
                            ('worker_heap_predicates', {}), ('exact_linked_kernels', {}),
                            ('source_sha256', {}), ('source_corpus_summary', {})):
            changed = {**report, name: value}
            try: require_companion(stage, worker, changed, corpus)
            except ValueError: companion_refusals += 1
            else: raise ValueError('Bottom cold companion accepted changed field: ' + name)
        report['self_tests']['companion_refusals'] = companion_refusals
        require_companion(stage, worker, report, corpus)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--corpus', type=Path)
    parser.add_argument('--development', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = check(args.directory, self_test=args.self_test,
                   corpus=json.loads(args.corpus.read_text()) if args.corpus else None,
                   development=args.development)
    if args.output: args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
