#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the actual relocated native persistent worker, low SCI loan and source.

Integer gateway/ABI models establish the authored control transfers; they do
not establish console execution or universal game/reset compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import random
import shutil
import struct
import subprocess
import tempfile

import retail_package as layout
import check_retail_loader_layout as resident_layout
from check_vmu_native_heap_probe import (
    LOW, HIGH, LIMIT, typed_function, check_bss, inspect_dynamic_hook, regression,
)
from check_vmu_native_readonly import (ReadonlyGateway, relocated, model_hooks, model_mutations)
from check_vmu_native_worker_gateway import (
    MASK, P1, P2, literal_aware_instructions, model_gateway, read_elf, symbol_bytes,
    Gateway as StageGateway, ENTRY as P2_ENTRY, TOP, CONFIG, RESULT,
)
from check_loader_layout import inspect_elf, padded, region, SYM
from check_retail_instructions import audit as instruction_audit
from runtime_package import verify
from package_vmu_maple_observe import (linked_stack_rows, _function_base, check_pio_callgraph, linked_function_names)
from check_retail_stack import EDGE, NODE, load as load_graph
from vmu_maple_observe_symbols import sd_gateway_span
from check_vmu_pio_write import check_pio_entry
from check_vmu_native_cache_seed import audit as audit_cache_seed
from check_vmu_native_cache_gateway import check as check_cache_gateway
from check_vmu_native_stage_stack import check as check_stage_stack
from check_vmu_native_stage_stack import Machine
from check_vmu_native_mode import audit as audit_runtime_mode
from check_vmu_native_trace import check as audit_trace_gates, GATES as TRACE_GATES
from check_vmu_native_map_refusal import check as audit_map_refusal
from vmu_native_relocations import generate, validate_loads
from test_armada_ce_maple_hook import CPU

ROOT = Path(__file__).resolve().parents[1]
SPAN = 0x20000

# The prepared kernels are reviewed linked SH instructions, including their
# literal pools. Only ELF-proven pointer words are normalized; their exact
# typed object/function, local source owner and interior offset remain in the
# signature. This admits relocation/layout changes, not new code or targets.
PREPARED_PINS = {'_poll_lease_valid': ('b67c432db445f780a4b2e93871f7907b7f7de1fd8c0795faf112f30fe0673302', 64),
 '_installed_hooks_valid': ('819cc9127506f6790fa189371f453b3474bd2b3f9e39752d43a5d8501dd2e286', 76),
 '_kui_vmu_native_readonly_release_check': ('87433dd479877714f7773607b751f56b629a27792417dbb31ccfa430499e1474', 588),
 '_hook_word': ('6c579e60911823ed1fbf6a57622bab2d7de2ebf46228c2c2c21d27f8fc144249', 112),
 '_prepare_hook_entry': ('a725563c7ef1e7429dd214860f8f7ecfddac54cf0981577fe679602117bebca3', 188),
 '_kui_vmu_native_lifetime_prepare': ('4691642d40ae532e2f81cfe46c3f81a019412c0da0e43fb4f7913d4a44b87312', 640),
 '_kui_vmu_native_passive_arm': ('e57595f0aac0df61359e7c79052c02298b376e9683f869b1e022be80b722c6ab', 1388),
 '_persistent_storage_guard': ('d230251892a758f65469e9cb692703a0270511756e1fbdc94cc28e04472a15fe', 396),
 '_persistent_lease': ('8658f150d0d4811335e46332a9a075f199fa8219edb5ccf1730223a4e34d78e3', 148),
 '_kui_vmu_native_passive_submit': ('c05e1537d6db77176d0050a9ed706ec2e6d7454df824e559d5bf774a25b51300', 1300),
 '_kui_vmu_native_passive_complete': ('71cf577a9e8bdd29046e4bafe7d81fed589b3e4390ae197a3b97c049fdba62d9', 1992),
 '_controller_context_valid': ('475104ef13c6350c759cd7c09714ece5cc3f71410114f3808d7d549743241084', 496),
 '_kui_vmu_readonly_overlay_complete_absent': ('6085deac9edfd8886b3300663daa32875389280d2daca88d509846e17f1b31e7', 284),
 '_kui_vmu_readonly_overlay_complete': ('6413dfd099e7eeb01b7f0b9bf12e07ca276bf3bc0b8080efcce61a067c8b1bb4', 888),
 '_readonly_overlay_parent_response': ('d396e68edd3158708ba5a8c9ff5235821bb62b71cc9dc40650cadb9098eaeb12', 116),
 '_readonly_overlay_increment': ('e3a6368a10593e4ae7510faf736d644a9d9e383e262070923241741b3f6893b1', 14),
 '_readonly_overlay_le32': ('1daa47eecc292df8356826c0e5a683e3a2c27cec212f901db7b1f01cf3b897df', 30),
 '_transfer_context_valid': ('733f508a62ec00d8a34a05b654b27e4a2fe2477cdc0113ca419dd8946baf099e', 1048),
 '_persistent_context_guard': ('7d7b44453ed058c85b22f3624a96001a93f68e06bd6dc581757cbff364c96df7', 132),
 '_kui_vmu_native_runtime_fault': ('7aecf42ff60104b0b146df6ddad0f1b74c5e380c4cc104680ab351d4592ef13c', 236),
 '.rodata': ('79fca272a77490961924a12035bc15affc6b3be5584a07d54d1aca8d1163ea27', 3084),
 '_kui_vmu_native_trace_enabled': ('0a920c3cf23dad2c3aabfe188567a6d1425af67dd54b46b619555831e9748eac', 16),
 '_kui_vmu_native_trace_image': ('e85462a3e477dee0a0124220436dc355360013e47136f21eb8576af465cdd9a9', 60),
 '_kui_vmu_native_trace_point': ('f29078442d6ec0295e0d47680f02bdc96b3e656ddc2c0cd98d02975392b09db6', 1132),
 '_kui_vmu_native_trace_wait_loop': ('8d3fb2b39e884b13c0061c722bcca07d6082aaafbc8de4d6e3b1c446759560d8', 176),
 '_kui_vmu_native_trace_init': ('bf7a45b36958e63813bfb7eef0800982671734ef8d4414b6c1ff477658b183e8', 96),
 '_kui_vmu_native_trace_wait_before': ('8d16ecf62a40b00531b18bb6d68b876b67be8c9cb7d4775c41e329babcedabcf', 152),
 '_kui_vmu_native_trace_frame_entry': ('d5949f5ee897f0fd40d653db5d53691e227882a1bb6aed384b44ea5f83104081', 164),
 '_kui_vmu_native_trace_report': ('e6401bd806d7626717aec3ece819a5310b5da0d24502d834db8fbc25503086e9', 1120),
 '_kui_vmu_native_trace_wait_after': ('399109af8351d1f2f0d0fa1541afa51056dfd44e86b607e10afd8fad1fe55015', 180),
 '_kui_vmu_native_trace_game': ('367d93d9d799bac69baf843535f58f139cac582df3d494ff5e4e4721ece346e3', 112),
 '_trace_history': ('964a375e91521fb64ec60396925aad7c54b1027ef50d58d30ff7293af5c41cd9', 164),
 '_report': ('4eeafa97f7ee4fb103eb5f2133911baacd57e7434bd6418f2ed4a6f17f7b7933', 440),
 '_kui_vmu_native_trace_failure': ('7db0cdf5462a9ddb02e0bfa486f9fdcd17b92f60a9a7769e4e835fcc4f5335d3', 120),
 '_trace_row.constprop.0': ('16645f1e2adfcd48b40c4ead39f05787980996cfdb4e94dd0d573c88d3db4a82', 92),
 '_trace_packets': ('d8839c1de099f8119113b57748d05ac74037261f9c787118bb5105d694b96215', 116),
 '_kui_vmu_native_trace_irq': ('31b72b692190f12dabe3bee1767a9d43913918730d08b7c7c802bea816c8177e', 14)}


def prepared_symbol_records(worker):
    data, _, _, sections, _ = worker
    records = []
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = region(data, strings[4], strings[5], "prepared symbol strings")
        source = ""
        for at in range(section[4], section[4] + section[5], SYM.size):
            name, value, size, info, _, owner = SYM.unpack(region(data, at, SYM.size, "prepared symbol"))
            name = names[name:].split(b"\0", 1)[0].decode("ascii")
            if info & 15 == 4:
                source = name
            elif name and (info & 15 in (1, 2) or name in
                           ("__vmu_native_worker_stack_bottom", "__vmu_native_worker_stack_top")):
                qualified = name + ("@" + source if not info >> 4 else "")
                records.append((qualified, value, size, info & 15, owner))
    strings = sections[worker[1][13]]
    names = region(data, strings[4], strings[5], "prepared section names")
    for owner, section in enumerate(sections):
        # GCC's anonymous switch tables have no STT_OBJECT. Their complete
        # constant section is separately pinned, including relocations into
        # exact function offsets. The separately bounded stack below is the
        # only other synthetic section target admitted here.
        if names[section[0]:].split(b"\0", 1)[0] == b".rodata" and section[2] & 7 == 2:
            records.append((".rodata", section[3], section[5], 3, owner))
    # Failure diagnostics can materialize stack_bottom+256. Retain the exact
    # zero-size linker-bound labels above, and admit an interior reference only
    # through this unique, allocated, private P2 NOBITS stack section. Its
    # complete4KiB extent must match both bound symbols and the worker lease.
    bottom = worker[4]["__vmu_native_worker_stack_bottom"]
    top = worker[4]["__vmu_native_worker_stack_top"]
    require(bottom[1:3] == top[1:3] == (0, 0) and bottom[3] == top[3] and
            bottom[3] < len(sections), "prepared private stack boundary symbols differ")
    owner = bottom[3]
    stack = sections[owner]
    require(names[stack[0]:].split(b"\0", 1)[0] == b".stack" and
            stack[1] == 8 and stack[2] & 7 == 3 and
            bottom[0] == stack[3] == P2 + SPAN - 4096 and
            top[0] == bottom[0] + stack[5] == P2 + SPAN and stack[5] == 4096 and
            not (bottom[0] & 31) and not (top[0] & 31),
            "prepared private stack lacks exact P2 NOBITS ownership")
    require(sum(names[s[0]:].split(b"\0", 1)[0] == b".stack" for s in sections) == 1,
            "prepared private stack section is ambiguous")
    records.append((".stack", stack[3], stack[5], 3, owner))
    return records


def prepared_signature(worker, binary, wire, name, build=None):
    """Closed signature over all body bytes and typed relocation bindings."""
    records = prepared_symbol_records(worker)
    if name == ".rodata":
        strings = worker[3][worker[1][13]]
        names = region(worker[0], strings[4], strings[5], "prepared section names")
        sections = [s for s in worker[3] if names[s[0]:].split(b"\0", 1)[0] == b".rodata" and
                    s[1] == 1 and s[2] & 7 == 2 and s[3] & 0xff000000 == 0x8c000000]
        require(len(sections) == 1, "prepared worker lacks unique cached constant section")
        section = sections[0]
        address, body = section[3], bytearray(region(worker[0], section[4], section[5], name))
        # Exactly one authored diagnostic label carries the independently
        # verified module/entry build identity. Normalize only its twelve
        # lowercase hexadecimal bytes; prefix, position and NUL stay pinned.
        require(isinstance(build, str) and re.fullmatch(r"[0-9a-f]{12}", build),
                "prepared constant signature needs verified runtime build identity")
        label = b"BUILD " + build.encode("ascii") + b"\0"
        require(body.count(b"BUILD ") == 1 and body.count(label) == 1,
                "prepared build identity differs from verified runtime")
        at = body.index(label) + 6
        body[at:at + 12] = bytes(12)
    else:
        address, raw, section = symbol_bytes(worker, name, 2)
        body = bytearray(raw)
    first = physical(address) - physical(P1)
    references = []
    for i in range(struct.unpack_from("<I", wire, 28)[0]):
        at, value = struct.unpack_from("<II", wire, 72 + 8 * i)
        if not first <= at < first + len(body):
            continue
        offset = at - first
        require(offset % 4 == 0 and offset + 4 <= len(body), "prepared pointer crosses body")
        require(struct.unpack_from("<I", body, offset)[0] == value, "prepared pointer differs from ELF body")
        found = [r for r in records if r[1] <= value < r[1] + r[2] or
                 not r[2] and r[1] == value]
        require(found, "prepared pointer has no typed target: " + hex(value))
        target = min(found, key=lambda r: (r[2], r[0]))
        require(target[4] < len(worker[3]) and worker[3][target[4]][2] & 2,
                "prepared pointer target is not allocated")
        references.append((offset, target[0], value - target[1], target[3]))
        body[offset:offset + 4] = bytes(4)
    value = {"body": body.hex(), "references": references}
    digest = hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return digest, len(body), references


class AbsentCompletion(Machine):
    """Execute the complete absent publisher and its closed integer callees.

    The caller's independent prepare/DMA proofs supply a valid frame array.
    No backend is substituted: any call outside these four actual linked
    functions is rejected, including handler, admit and SD callbacks.
    """
    helper = '_kui_vmu_readonly_overlay_complete_absent'
    closure = (helper, '_readonly_overlay_parent_response',
               '_readonly_overlay_le32', '_readonly_overlay_increment')

    def __init__(self, worker, seed, command, count, targets, outcome, *, body=None):
        super().__init__(worker, self.helper, seed, 15, 0xAC11FFE0, body=body)
        self.functions = [symbol_bytes(worker, name, 2)[:2] for name in self.closure]
        if body is not None:
            self.functions[0] = self.functions[0][0], body
        self.data, self.external_stores, self.outcome = {}, [], outcome
        self.overlay = worker[4]['_overlay'][0]
        require(worker[4]['_overlay'][1:3] == (17820, 1), 'absent model needs exact private overlay object')
        self.r[4] = 0 if outcome == 'null' else self.overlay
        self.before_regs = self.r.copy(), self.pr, self.sr, self.fpscr, self.fr.copy()
        self.overlay_end = self.overlay + 17820
        for i in range(17820):
            self.data[self.overlay + i] = (i * 13 + seed) & 255
        self.put(self.overlay + 4, MASK if seed & 1 else 17)
        self.put(self.overlay + 36, MASK if seed & 1 else 19)
        self.put(self.overlay + 60, 0 if outcome == 'backend' else 0x8C155000)
        self.put(self.overlay + 68, 0 if outcome == 'magic' else 0x56524F31)
        self.put(self.overlay + 72, int(outcome != 'admitted'))
        self.put(self.overlay + 76, int(outcome != 'pending'))
        self.put(self.overlay + 80, count); self.put(self.overlay + 84, count - 1)
        self.rx = []
        for i in range(count):
            rx = 0xAC200000 + 1024 * i
            destination = 32 if i == count - 1 else (1 if i < targets else 96)
            header = (0x01000000 if command == 9 else 0) | destination << 8 | command
            frame = self.overlay + 92 + 28 * i
            self.put(frame, header); self.put(frame + 12, rx & 0x1FFFFFFF); self.put(frame + 20, rx)
            for j in range(1024): self.data[rx + j] = (i + j * 19 + seed) & 255
            self.rx.append((rx, destination))
        parent = self.rx[-1][0]
        response = (28 if command == 1 else 48 if command == 2 else 3) << 24 | 32 << 16 | (5 if command == 1 else 6 if command == 2 else 8)
        if outcome == 'parent': response ^= 1
        if outcome == 'conflict': response |= 1 << 16
        self.put(parent, response); self.put(parent + 4, 0x01000000)
        self.before_data = self.data.copy()

    def put(self, address, value):
        for i in range(4): self.data[address + i] = value >> (8 * i) & 255

    def code(self, address, size):
        for first, body in self.functions:
            if first <= address < address + size <= first + len(body):
                return body[address - first:address - first + size]
        raise ValueError('absent completion leaves its exact linked integer closure: ' + hex(address))

    def read(self, address):
        require(not address & 3, 'unaligned absent word read')
        if all(address + i in self.data for i in range(4)):
            return sum(self.data[address + i] << (8 * i) for i in range(4))
        return struct.unpack('<I', self.code(address, 4))[0]

    def byte(self, address):
        require(address in self.data, 'absent byte read escapes prepared buffers')
        value = self.data[address]
        return (value - 256 if value & 128 else value) & MASK

    def store(self, address, value, width):
        if not self.start_sp - 256 <= address < address + width <= self.start_sp:
            require(self.overlay <= address < address + width <= self.overlay_end or
                    any(rx <= address < address + width <= rx + 1024 for rx, _ in self.rx),
                    'absent publisher writes outside overlay and prepared RX')
            self.external_stores.append((address, width))
        for i in range(width): self.data[address + i] = value >> (8 * i) & 255

    def write(self, address, value):
        require(not address & 3, 'unaligned absent word store'); self.store(address, value, 4)

    def plain(self, word, pc):
        n, m = word >> 8 & 15, word >> 4 & 15
        if word & 0xF00F == 0x6000:
            self.r[n] = self.byte(self.r[m]); return
        if word & 0xFF00 == 0x8400:
            self.r[0] = self.byte(self.r[m] + (word & 15)); return
        if word & 0xF00F == 0x2000:
            self.store(self.r[n], self.r[m], 1); return
        if word & 0xFF00 == 0x8000:
            self.store(self.r[m] + (word & 15), self.r[0], 1); return
        if word & 0xF00F == 0x600C:
            self.r[n] = self.r[m] & 255; return
        if word & 0xF00F == 0x6008:
            self.r[n] = self.r[m] & 0xFFFF0000 | (self.r[m] & 255) << 8 | (self.r[m] >> 8 & 255); return
        if word & 0xF0FF in (0x4018, 0x4028):
            self.r[n] = self.r[n] << (8 if word & 0xFF == 0x18 else 16) & MASK; return
        if word & 0xF00F == 0x0007:
            self.macl = self.r[n] * self.r[m] & MASK; return
        if word & 0xF0FF == 0x001A:
            self.r[n] = self.macl; return
        if word & 0xF00F == 0x600A:
            value = -self.r[m] - (self.sr & 1); self.r[n] = value & MASK; self.t(value < 0); return
        if word >> 8 == 0xCB:
            self.r[0] |= word & 255; return
        super().plain(word, pc)

    def check(self):
        r, pr, sr, fpscr, fr = self.before_regs
        require(self.r[8:] == r[8:] and self.pr == pr and self.sr & ~1 == sr & ~1 and
                self.fpscr == fpscr and self.fr == fr, 'absent publisher changes owner ABI state')
        result = 0 if self.outcome == 'ok' else -1 if self.outcome == 'null' else -6 if self.outcome == 'parent' else -7 if self.outcome == 'conflict' else -2
        require(self.r[0] == result & MASK, 'absent publisher changes refusal/return meaning')
        expected = self.before_data.copy()
        def old(offset):
            return sum(self.before_data[self.overlay + offset + i] << (8 * i) for i in range(4))
        def put(offset, value):
            for i in range(4): expected[self.overlay + offset + i] = value >> (8 * i) & 255
        if self.outcome in ('ok', 'parent', 'conflict'):
            parent = self.rx[-1][0]
            frame = self.overlay + 92 + 28 * (len(self.rx) - 1)
            for offset, value in ((44, self.read(frame)), (48, self.read(parent)), (52, parent & 0x1FFFFFFF)):
                put(offset, value)
        if self.outcome == 'ok':
            put(4, min(MASK, old(4) + 1)); put(76, 0)
            for index, (rx, destination) in enumerate(self.rx):
                if destination == 1:
                    for j in range(4): expected[rx + j] = 255
                    for offset, value in ((44, self.read(self.overlay + 92 + 28 * index)),
                                          (48, MASK), (52, rx & 0x1FFFFFFF)):
                        put(offset, value)
        elif self.outcome != 'null':
            put(72, 0); put(76, 0); put(36, min(MASK, old(36) + 1))
            put(40, old(40) | (64 if self.outcome == 'parent' else 128 if self.outcome == 'conflict' else 2))
        require(all(self.data[a] == value for a, value in expected.items()),
                'absent publisher changes a payload/physical response/private state or refuses after an RX store')
        for offset in (8, 12, 16):
            require(self.read(self.overlay + offset) == sum(self.before_data[self.overlay + offset + i] << (8 * i) for i in range(4)),
                    'absent publisher reports a virtual advertisement/reply/backend call')
        if self.outcome == 'ok':
            require(self.read(self.overlay + 76) == 0 and self.read(self.overlay + 72) == 1,
                    'absent completion loses its prepared lifecycle')
        require(all(self.overlay <= a < self.overlay_end or (width == 1 and
                    any(destination == 1 and rx <= a < rx + 4 for rx, destination in self.rx))
                    for a, width in self.external_stores), 'absent publisher has an unexpected store target')


def audit_absent_completion(worker, *, self_test=False):
    cases = 0
    for seed in range(2):
        for command in (1, 2, 9):
            for count, targets in ((1, 0), (2, 1), (4, 0), (4, 2), (32, 31)):
                for outcome in ('ok', 'null', 'magic', 'admitted', 'pending', 'backend', 'parent', 'conflict'):
                    AbsentCompletion(worker, seed, command, count, targets, outcome).run().check(); cases += 1
    # Execute the actual linked selector, with the config's exact last word.
    base, body, _ = symbol_bytes(worker, '_kui_vmu_native_passive_complete', 2)
    words = struct.unpack('<' + 'H' * (len(body) // 2), body)
    positions = [i for i in range(1,len(words)-10) if words[i]==0xe201 and words[i+2]&0xff0f==0x510f and
                 words[i+4:i+7]==(0x71fe,0x3126,0x8b00) and words[i+8:i+10]==(0x400b,9)]
    require(len(positions) == 1, 'missing unique actual hooks-only completion selector')
    i = positions[0]; config, size, kind, _ = worker[4]['_config']
    require((size, kind) == (2300, 1), 'hooks-only config word escaped its unchanged ABI')
    config_register=(words[i+2]>>4)&15
    require(words[i-1]&0xff00==0xd000|(config_register<<8),
            'completion selector lacks its exact config-base load')
    literal=((base+2*(i-1)+4)&~3)+4*(words[i-1]&255)
    require(struct.unpack_from('<I',body,literal-base)[0]==config+size-64,
            'completion selector config-base literal escaped the exact final word')
    routes = 0
    for seed in range(16):
        for mode in (0, 1, 2, 3, 4):
            m = Machine(worker, '_kui_vmu_native_passive_complete', seed, 15, 0xAC11FFE0)
            m.pc = base + 2 * i; m.r[config_register] = config + size - 64
            m.mem[(config & ~0x20000000) + size - 4] = mode
            m.stop_pc = base + 2 * (i + 10)
            def call(target, m=m, mode=mode):
                name = '_kui_vmu_readonly_overlay_complete_absent' if mode in (2,3) else '_kui_vmu_readonly_overlay_complete'
                require(target == worker[4][name][0] and m.r[4] == worker[4]['_overlay'][0],
                        'hooks-only selector changed mode or exact completion target')
                m.pc = m.stop_pc; m.calls.append(name); return True
            m.call = call; m.run(); require(len(m.calls) == 1, 'completion selector did not choose exactly one path'); routes += 1
    rejected = 0
    if self_test:
        # Exact source closure and typed pins catch arbitrary changes; this
        # independently exercises a changed header store and backend target.
        _, original, _ = symbol_bytes(worker, AbsentCompletion.helper, 2)
        mutations = [(struct.pack('<2H', 0xE300, 0xE0FF), struct.pack('<2H', 0xE300, 0xE000)),
                     (struct.pack('<H', 0x8023), struct.pack('<H', 0x8024)),
                     (struct.pack('<I', worker[4]['_readonly_overlay_le32'][0]),
                      struct.pack('<I', worker[4]['_kui_vmu_native_persistent_sd_transaction'][0]))]
        for old, new in mutations:
            require(original.count(old) == 1, 'missing unique absent header/callback mutation site')
            changed = original.replace(old, new)
            try: AbsentCompletion(worker, 0, 1, 2, 1, 'ok', body=changed).run().check()
            except (ValueError, AssertionError, KeyError): rejected += 1
            else: raise ValueError('absent model accepted changed no-response/header extent')
        changed = bytearray(body); struct.pack_into('<H', changed, 2 * (i + 4), 0x71fd)
        m = Machine(worker, '_kui_vmu_native_passive_complete', 0, 15, 0xAC11FFE0, body=bytes(changed))
        m.pc = base + 2 * i; m.r[config_register] = config + size - 64
        m.mem[(config & ~0x20000000) + size - 4] = 2; m.stop_pc = base + 2 * (i + 10)
        def reject_route(target):
            require(target == worker[4]['_kui_vmu_readonly_overlay_complete_absent'][0],
                    'wrong mode reached ordinary virtual publication')
            m.pc = m.stop_pc; return True
        m.call = reject_route
        try: m.run()
        except (ValueError, AssertionError, KeyError): rejected += 1
        else: raise ValueError('hooks-only selector accepted a profile/mode swap')
    return dict(helper_instruction_cases=cases, selector_instruction_cases=routes,
                mutation_rejections=rejected, actual_callees_only=True,
                no_backend_or_sd_call=True, no_parent_advertisement=True,
                only_filtered_a1_four_byte_headers_changed=True)


def check_prepared_lifetime(directory, worker, binary, wire, self_test=False, *, build=None):
    """Keep the legacy proof, or require the complete prepared proof family."""
    if "_lifetime_plan" not in worker[4]:
        _, _, section = symbol_bytes(worker, "_kui_vmu_native_lifetime_release_safe_ranges", 2)
        require(section[2] & 7 == 6, "missing legacy lifetime admission")
        return {"prepared": False, "legacy_full_release_admission": True}
    if build is None:
        build = verify((directory / "retail-maple-observe.kui").read_bytes())["build"]
    for name, size in (("_lifetime_plan", 616), ("_hook_witness", 384),
                       ("_hook_witness_count", 4), ("_hook_witness_ready", 4),
                       ("_protected_ranges", 512), ("_protected_count", 4),
                       ("_controller_pair", 188)):
        address, actual, kind, owner = worker[4][name]
        section = worker[3][owner]
        require(kind == 1 and actual == size and address % 4 == 0 and
                section[2] & 7 == 3 and section[1] == 8 and
                section[3] <= address < address + actual <= section[3] + section[5] and
                P2 <= address < address + actual <= worker[4]["__vmu_native_worker_stack_bottom"][0],
                "prepared object lacks exact private P2 BSS ownership: " + name)
    functions = {}
    for name, expected in PREPARED_PINS.items():
        signature, size, references = prepared_signature(worker, binary, wire, name, build)
        require((signature, size) == expected, "prepared linked instructions/targets changed: " + name)
        functions[name] = {"normalized_sha256": signature, "bytes": size,
                           "typed_pointer_bindings": len(references)}
    require(PREPARED_PINS, "missing reviewed prepared linked signatures")
    graph = (directory / "native/src/loader/vmu_native_passive.ci").read_text()
    edges = [(a, b) for a, b, _ in EDGE.findall(graph)]
    # GCC emits unlabeled edges for its split read_value.part.0 helper.
    # Retain those real emitted calls rather than guessing a source location.
    edges += re.findall(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)" \}', graph)
    local = "src/loader/vmu_native_passive.c:"
    for caller, required in (
            ("kui_vmu_native_passive_arm", ("kui_vmu_native_lifetime_prepare", local + "prepare_hook_entry", local + "hook_word")),
            ("kui_vmu_native_passive_submit", (local + "installed_hooks_valid", local + "poll_lease_valid")),
            ("kui_vmu_native_passive_complete", (local + "installed_hooks_valid", local + "poll_lease_valid")),
            ("kui_vmu_native_readonly_release_check", (local + "installed_hooks_valid",)),
            (local + "controller_context_valid", (local + "transfer_context_valid", local + "read_value.part.0")),
            (local + "persistent_storage_guard", ("kui_vmu_native_heap_lease_valid", local + "stack_valid",
                local + "installed_hooks_valid", "kui_vmu_native_layout_bind_checked", "kui_vmu_native_runtime_check",
                local + "chain", "kui_vmu_volatile_chain_verify"))):
        require(all((caller, target) in edges for target in required),
                "prepared admission loses required emitted call: " + caller)
    require(edges.count((local + "persistent_lease", local + "persistent_storage_guard")) == 2,
            "storage loan must retain full guards at both boundaries")
    rejected = 0
    if self_test:
        # Independent code and valid-but-wrong pointer-target mutations. The
        # signatures bind the loop bounds, fail branches and every literal.
        for name in PREPARED_PINS:
            signature, size, references = prepared_signature(worker, binary, wire, name, build)
            if name == ".rodata":
                continue
            address, body, _ = symbol_bytes(worker, name, 2)
            for at in (0, size - 2):
                changed = bytearray(worker[0])
                owner = worker[3][worker[4][name][3]]
                changed[owner[4] + address - owner[3] + at] ^= 1
                mutant = (bytes(changed),) + worker[1:]
                try:
                    different = prepared_signature(mutant, binary, wire, name)[:2] != (signature, size)
                except ValueError:
                    different = True
                require(different,
                        "prepared signature accepts instruction mutation: " + name)
                rejected += 1
            if references:
                changed = bytearray(wire)
                changed_elf = bytearray(worker[0])
                first = physical(address) - physical(P1)
                offset = references[0][0]
                for i in range(struct.unpack_from("<I", wire, 28)[0]):
                    at, value = struct.unpack_from("<II", wire, 72 + i * 8)
                    if at == first + offset:
                        struct.pack_into("<I", changed, 76 + i * 8, value + 4)
                        owner = worker[3][worker[4][name][3]]
                        struct.pack_into("<I", changed_elf, owner[4] + address - owner[3] + offset, value + 4)
                        break
                try:
                    mutant = (bytes(changed_elf),) + worker[1:]
                    different = prepared_signature(mutant, binary, bytes(changed), name)[:2] != (signature, size)
                except ValueError:
                    different = True
                require(different, "prepared signature accepts changed typed pointer: " + name)
                rejected += 1
    return {"prepared": True, "exact_linked_kernels": functions, "build_identity": build,
            "constant_build_identity_bound_to_verified_runtime": True, "identity_words": 8,
            "protected_interval_limit": 64, "hook_word_limit": 32, "free_node_limit": 1024,
            "full_dynamic_free_list_each_release": True, "full_storage_guards_before_and_after_loan": True,
            "emitted_callgraph_bound": True, "mutation_rejections": rejected,
            "controller_fastlane": {"source_word_limit": 32, "main_controller_limit": 4,
                "private_pair_bytes": 188, "original_dma_unchanged": True,
                "publication": "one parent-A presence byte after whole-batch validation",
                "full_storage_guards_unchanged": True,
                "synthetic_read_count_is_not_hardware_time": True},
            "hooks_only": audit_absent_completion(worker, self_test=self_test)}

# Whole integer trampoline, including its P2 resume literal and zero padding.
TRAMPOLINE_PREFIX = bytes.fromhex(
    "062f162f262f362f462f562f662f762f862f962fa62fb62fc62fd62fe62f224f"
    "034f234f134f024f124f0200f0cb0e4001d02b4009000900")


def resume_template(cached, relay):
    """Authored complete game_resume instruction body and all literal words."""
    ops = [0x68f3, 0, 0, 0x6902, 0, 0, 0x00a3, 0x7020, 0x3012, 0x8bfb,
           0, 0, 0x2012] + [9] * 8 + [0x6483, 0x6593, 0, 0x400b, 9,
           0, 0x209b, 0, 0x2102] + [9] * 8 + [0x6f83, 0x4f16, 0x4f06,
           0x4f17, 0x4f27, 0x7f04, 0x4f26] + [0x60f6 | r << 8 for r in range(14, 0, -1)] + \
          [0x60f3, 0x70c0, 0x6002, 0x400e, 0, 0x402b, 0x60f6]
    assert len(ops) == 66
    values = [0x8cff0000, 0xff00001c, 0x8c000000, 0x8d000000, 0x808]
    if cached:
        values.append(0x908)
    values += [relay, 0xac010000]
    for index, reg, word in ((1, 15, 0), (2, 0, 1), (4, 0, 2), (5, 1, 3),
                              (10, 0, 1), (11, 1, 5 if cached else 4),
                              (23, 0, 6 if cached else 5), (26, 0, 4),
                              (28, 1, 1), (63, 0, 7 if cached else 6)):
        at = 132 + 4 * word
        delta = at - ((2 * index + 4) & ~3)
        assert delta >= 0 and delta % 4 == 0
        ops[index] = 0xd000 | reg << 8 | delta // 4
    return struct.pack("<66H", *ops) + struct.pack("<" + "I" * len(values), *values)


class ResumeGateway(CPU):
    """Execute actual pinned assembly, folding only its proven RAM purge loop."""
    def __init__(self, seed, mask, ccr, resume, blob, trampoline, relay, cached):
        super().__init__(seed, resume, 1)
        self.resume, self.blob, self.trampoline, self.relay = resume, blob, trampoline, relay
        self.cached = cached
        self.r[15] = 0x8c00ed00
        self.start_sp = self.r[15]
        self.sr = (self.sr & ~0xf0) | mask << 4
        self.vbr = random.Random(seed + 83).getrandbits(32)
        self.owner_ccr, self.ccr = ccr, ccr
        self.pc, self.mem, self.called, self.purged = 0xac010000, {}, 0, False
        self.ccr_writes, self.wait = [], 0
        self.initial = (self.r.copy(), self.pr, self.mach, self.macl, self.gbr,
                        self.vbr, self.sr, self.fpscr, self.fr.copy())

    def code(self, address, size):
        if 0xac010000 <= address < address + size <= 0xac010000 + len(self.trampoline):
            return self.trampoline[address - 0xac010000:address - 0xac010000 + size]
        assert self.resume + 0x20000000 <= address < address + size <= self.resume + 0x20000000 + len(self.blob), \
            "cache-control relay must execute through P2"
        offset = address - self.resume - 0x20000000
        return self.blob[offset:offset + size]

    def read(self, address):
        if address == 0xff00001c:
            return self.ccr
        if address in self.mem:
            return self.mem[address]
        return struct.unpack("<I", self.code(address, 4))[0]

    def write(self, address, value):
        if address == 0xff00001c:
            assert self.pc >= self.resume + 0x20000000 and self.purged
            assert not self.wait
            self.ccr_writes.append(value & MASK)
            expected = (0x908 if self.cached else 0x808) if len(self.ccr_writes) == 1 else self.owner_ccr | 0x808
            assert value == expected and len(self.ccr_writes) <= 2, "unexpected CCR mode or owner-state loss"
            # ICI and OCI are command bits; reads retain the enable/mode bits.
            self.ccr, self.wait = value & ~0x808, 8
            return
        assert address % 4 == 0 and self.start_sp - 84 <= address < self.start_sp
        assert not self.ccr_writes, "relay writes original owner frame after publication"
        self.mem[address] = value & MASK

    def plain(self, word, pc):
        if self.wait:
            assert word == 9, "CCR access lacks eight following NOPs"
            self.wait -= 1
        if word in (0x0002,):
            self.r[0] = self.sr; return
        if word == 0x4f03:
            self.r[15] -= 4; self.write(self.r[15], self.sr); return
        if word in (0x4f23, 0x4f27):
            if word == 0x4f23:
                self.r[15] -= 4; self.write(self.r[15], self.vbr)
            else:
                self.vbr = self.read(self.r[15]); self.r[15] += 4
            return
        if word & 0xf00f == 0x200b:
            self.r[word >> 8 & 15] |= self.r[word >> 4 & 15]; return
        super().plain(word, pc)

    def relay_call(self):
        assert not self.wait and self.purged and self.called == 0
        assert self.r[4:6] == [self.start_sp - 84, self.owner_ccr]
        assert self.r[15] == 0x8cff0000 and self.sr & 0xf0 == 0xf0
        assert self.ccr == (0x100 if self.cached else 0) and not self.ccr & 1
        self.called += 1
        for i in range(8):
            self.r[i] = (0x92345678 + 0x1234567 * i) & MASK
        self.mach ^= 0x12345678; self.macl ^= 0x98765432
        self.gbr ^= 0x13572468; self.vbr ^= 0x87654321
        self.sr ^= 1
        self.pc = self.pr

    def run(self):
        for _ in range(200):
            pc = self.pc
            # Exact four-op loop is byte-pinned below. Folding preserves the
            # final R0/T and proves every aligned line in the 16MiB RAM range.
            if pc == self.resume + 0x20000000 + 12:
                assert self.code(pc, 8) == struct.pack("<4H", 0x00a3, 0x7020, 0x3012, 0x8bfb)
                assert self.r[:2] == [0x8c000000, 0x8d000000]
                self.r[0] = self.r[1]; self.t(True); self.purged = True
                self.pc += 8
                continue
            word = struct.unpack("<H", self.code(pc, 2))[0]
            if word & 0xf0ff in (0x402b, 0x400b):
                target = self.r[word >> 8 & 15]
                if word & 0xf0ff == 0x400b:
                    self.pr = pc + 4
                self.plain(struct.unpack("<H", self.code(pc + 2, 2))[0], pc + 2)
                if target == self.relay:
                    self.relay_call()
                elif target == 0xac010000:
                    self.pc = target
                    return self
                else:
                    self.pc = target
            else:
                self.plain(word, pc); self.pc += 2
        raise AssertionError("unbounded cache publication gateway")

    def check(self):
        r, pr, mach, macl, gbr, vbr, sr, fpscr, fr = self.initial
        assert self.r == r and self.pc == 0xac010000 and self.called == 1
        assert (self.pr, self.mach, self.macl, self.gbr, self.vbr, self.sr, self.fpscr, self.fr) == \
               (pr, mach, macl, gbr, vbr, sr, fpscr, fr)
        assert self.ccr_writes == [0x908 if self.cached else 0x808, self.owner_ccr | 0x808]
        assert self.ccr == self.owner_ccr & ~0x808 and not self.wait


def check_resume_publication(stage, cached, self_test=False):
    symbols = stage[4]
    resume = symbols["_kui_retail_game_resume"][0]
    relay, _, _ = symbol_bytes(stage, "_kui_retail_stage_relay", 2)
    require(resume % 4 == 0 and HIGH <= resume < LIMIT and HIGH <= relay < LIMIT,
            "unbounded cache relay symbols")
    expected = resume_template(cached, relay)
    image = inspect_elf(stage[0], HIGH, LIMIT)
    require(region(image["payload"], resume - HIGH, len(expected), "cache resume") == expected,
            "linked game_resume cache mode/publication/owner restoration changed")
    trampoline = symbols["__retail_trampoline_start"][0]
    require(symbols["__retail_trampoline_end"][0] == trampoline + 128 and
            symbols["__retail_trampoline_code_end"][0] == trampoline + 60,
            "owner trampoline span changed")
    template = TRAMPOLINE_PREFIX + struct.pack("<I", resume + 0x20000000) + bytes(68)
    require(region(image["payload"], trampoline - HIGH, 128, "cache trampoline") == template,
            "linked owner trampoline/P2 resume changed")
    cases = 0
    for seed in range(40):
        for mask in range(16):
            for ccr in (0, 1, 4, 0x20, 0x80, 0x100, 0x105, 0x1a7):
                ResumeGateway(seed, mask, ccr, resume, expected, template, relay, cached).run().check()
                cases += 1
    rejected = 0
    if self_test:
        # Coherent instruction/literal changes must fail the closed model too.
        mutations = [(12, 0x0009), (26, 0xe000), (60, 0xe000), (124, 0x0009),
                     (148, 0x0809), (156 if cached else 152, 0x0009)]
        if cached:
            mutations += [(152, 0x0808), (152, 0x0909)]
        for at, value in mutations:
            changed = bytearray(expected); struct.pack_into("<H", changed, at, value)
            try:
                ResumeGateway(0, 3, 0x100, resume, bytes(changed), template, relay, cached).run().check()
            except (AssertionError, struct.error):
                rejected += 1
            else:
                raise ValueError("resume gateway model accepted mutation at" + str(at))
    return {"instruction_cache_enabled_for_scan": cached, "operand_cache_disabled_for_scan": True,
            "scan_ccr": "0x00000908" if cached else "0x00000808", "owner_ccr_restored": True,
            "full_ram_purge_bytes": 0x1000000, "cache_write_nops": [8, 8],
            "owner_integer_frame_bytes": 84, "gateway_model_cases": cases,
            "gateway_mutation_rejections": rejected, "complete_linked_assembly_pinned": True}


def check_worker_publication(stage, cached, disassembly, self_test=False):
    """Pin the P2 publisher and witness initialization before its first arm."""
    image = inspect_elf(stage[0], HIGH, LIMIT)
    symbols = stage[4]
    address = symbols["_kui_native_passive_publish"][0]
    ops = [0xd001, 0x402b, 9, 9] + list(struct.unpack("<2H", struct.pack("<I", address + 12 + 0x20000000))) + \
          [0xe0e0, 0x2409, 0x3452, 0x8903, 0x04a3, 0x7420, 0xaffa, 9,
           0xd007, 0x6102, 0xd207, 0x212b, 0x2012] + [9] * 8 + [0x000b, 9, 9] + \
          list(struct.unpack("<4H", struct.pack("<2I", 0xff00001c, 0x800)))
    expected = struct.pack("<34H", *ops)
    require(address % 4 == 0 and region(image["payload"], address - HIGH, len(expected), "I-cache publisher") == expected,
            "linked P2 code publisher cache purge/ICI/owner CCR preservation changed")
    report = {"publisher_p2": hex(address + 12 + 0x20000000), "publisher_bytes": len(expected),
              "exact_linked_publisher_pinned": True, "ici_only_preserves_ccr": True,
              "post_ccr_write_nops": 8, "worker_prearm_publication_required": cached}
    if not cached:
        return report
    begin, body, _ = symbol_bytes(stage, "_native_passive_run", 2)
    words = list(struct.unpack("<" + "H" * (len(body) // 2), body))
    mode_config = "_native_runtime_mode" in symbols
    def literal(index):
        op = words[index]
        at = ((begin + index * 2 + 4) & ~3) + 4 * (op & 255)
        # This candidate scan also crosses embedded pool data. A data word
        # resembling MOV.L is not a witness if its supposed literal escapes
        # the function. Every accepted block still binds all exact targets.
        if not begin <= at <= begin + len(body) - 4:
            return None
        return struct.unpack("<I", region(body, at - begin, 4, "installer literal"))[0]
    candidates = []
    for i in range(len(words) - 20):
        # Original configuration layout: the publisher uses callee-saved R14.
        old = (words[i] & 0xff00 == 0xde00 and literal(i) == address and
               words[i + 1:i + 10] ==
               [0x6593, 0x57b7, 0x358c, 0x2372, 0x57be, 0x53ad, 0x2372, 0x4e0b, 0x1f23] and
               words[i + 10] & 0xff00 == 0xd000 and
               literal(i + 10) == symbols["_kui_vmu_native_worker_call"][0] and
               words[i + 11:i + 20] ==
               [0x67f3, 0x52f3, 0x7720, 0x55aa, 0x66f3, 0x54a3, 0x7630, 0x400b, 0x1f28])
        if old:
            require(words[i - 1] & 0xff00 == 0xdb00 and
                    literal(i - 1) == symbols["_native_driver"][0], "publisher uses a different driver witness")
            candidates.append((i, 8, 18, 0))
        # Prepared-plan configuration: exactly the same two dynamic writes,
        # [load,load+worker_bytes) publication and immediate arm; R2 carries
        # the publisher and the result/config stack offsets increase by8B.
        # The raw-lease alias check makes GCC exchange the cached load and
        # worker byte-count registers (R8/R9). Pin both complete witnesses;
        # the dynamic stores and their CFG domination remain identical.
        prepared_setups = (
            [0x6593, 0x358c, 0x53b7, 0x2232, 0x53be, 0x52ad, 0x2232],
            [0x6583, 0x359c, 0x53b7, 0x2232, 0x53be, 0x52ad, 0x2232])
        new = (words[i] & 0xff00 == 0xdb00 and literal(i) == symbols["_native_driver"][0] and
               words[i + 1:i + 8] in prepared_setups and
               words[i + 8] & 0xff00 == 0xd200 and literal(i + 8) == address and
               words[i + 9:i + 11] == [0x420b, 0x1f24 if mode_config else 0x1f23] and words[i + 11] & 0xff00 == 0xd000 and
               literal(i + 11) == symbols["_kui_vmu_native_worker_call"][0] and
               words[i + 12:i + 20] == [0x67f3, 0x55aa, 0x7728, 0x54a3, 0x66f3, 0x1fea, 0x400b, 0x7638])
        if new:
            candidates.append((i, 9, 18, prepared_setups.index(words[i + 1:i + 8])))
        # Persistent v2 validates two additional executable exports. GCC
        # retains P1 load inR9, bytes inR10 and P2 header inR8, loading the
        # driver inR13/publisher inR1. Pin the complete new argument/store
        # block rather than accepting an arbitrary publisher call.
        diagnostic = (mode_config and words[i] & 0xff00 == 0xdd00 and
            literal(i) == symbols["_native_driver"][0] and words[i + 1] & 0xff00 == 0xd100 and
            literal(i + 1) == address and words[i + 2:i + 11] ==
            [0x6593, 0x53d7, 0x35ac, 0x2232, 0x53de, 0x528d, 0x2232, 0x410b, 0x1f14] and
            words[i + 11] & 0xff00 == 0xd000 and
            literal(i + 11) == symbols["_kui_vmu_native_worker_call"][0] and
            words[i + 12:i + 20] == [0x67f3, 0x558a, 0x7728, 0x5483, 0x66f3, 0x1fca, 0x400b, 0x7644])
        if diagnostic:
            require(i >= 4 and words[i - 4:i] == [0x8d02, 0x6493, 0xae4d, 0xe503],
                    "v2 publisher lost unconditional delay-slot P1 span start")
            candidates.append((i, 9, 18, 2))
        traced = (mode_config and words[i]&0xff00==0xde00 and literal(i)==symbols['_native_driver'][0] and
            words[i+1]&0xff00==0xdc00 and literal(i+1)==address and
            words[i+2:i+10]==[0x65a3,0x53e7,0x359c,0x2232,0x53ee,0x528d,0x4c0b,0x2232] and
            words[i+10]&0xff00==0xd000 and literal(i+10)==symbols['_kui_vmu_native_worker_call'][0] and
            words[i+11:i+19]==[0x67f3,0x558a,0x772c,0x5483,0x66f3,0x1fdb,0x400b,0x7648])
        if traced:
            require(i>=4 and words[i-4:i]==[0x8d02,0x64a3,0xae4a,0xe503],
                    'v3 trace publisher lost exact unconditional P1 start')
            candidates.append((i,8,17,3))
    require(len(candidates) == 1, "missing unique linked worker-publication/first-arm block")
    first, publish_at, arm_at, register_family = candidates[0]
    # This bounded block loads the worker's own two dynamic target cells,
    # publishes exactly [load,load+worker_bytes), and immediately calls arm.
    publish, arm = begin + (first + publish_at) * 2, begin + (first + arm_at) * 2
    relocate = symbols["_kui_vmu_native_relocate"][0]
    relocations = []
    for i, op in enumerate(words[:-12]):
        if op & 0xff00 != 0xd000 or literal(i) != relocate:
            continue
        # Alias-normalized installation uses R8 for its destination and
        # explicitly copies its R9 byte-count into argument R5. Legacy
        # installation carries its byte-count in R8 and destination in R9.
        for extra, destination_store in ((0, 0x1f92), (1, 0x1f82)):
            j = i + extra
            if extra and words[i + 1] != 0x6593:
                continue
            if (words[j + 1] & 0xff00 == 0xd200 and literal(j + 1) == SPAN and
                words[j + 2] == 0x67e3 and words[j + 3] & 0xff00 == 0xd600 and
                literal(j + 3) == symbols["__native_worker_blob_end"][0] and
                words[j + 4] == 0x37d8 and words[j + 5] & 0xff00 == 0xd400 and
                literal(j + 5) == symbols["__native_worker_blob_begin"][0] and
                words[j + 6:j + 11] == [destination_store, 0x1f21, 0x2fa2, 0x400b, 0x1f23]):
                relocations.append((i, j + 9, extra, False))
        # The mode/diagnostics configuration retains R13 as the exact span
        # and moves the P2 destination store into the relocation delay slot.
        # Pin the entire argument block; a new callback literal alone is not
        # an installation/publication witness.
        if (mode_config and words[i + 1] == 0x6593 and
            words[i + 2] & 0xff00 == 0xdd00 and literal(i + 2) == SPAN and
            words[i + 3] == 0x37e8 and words[i + 4] & 0xff00 == 0xd600 and
            literal(i + 4) == symbols["__native_worker_blob_end"][0] and
            words[i + 5] & 0xff00 == 0xd400 and literal(i + 5) == symbols["__native_worker_blob_begin"][0] and
            words[i + 6:i + 10] == [0x1f82, 0x1fd1, 0x400b, 0x2fa2]):
            relocations.append((i, i + 8, 1, True))
        if (mode_config and words[i + 1] == 0x65a3 and
            words[i + 2] & 0xff00 == 0xdb00 and literal(i + 2) == SPAN and
            words[i + 3] == 0x37e8 and words[i + 4] & 0xff00 == 0xd600 and
            literal(i + 4) == symbols["__native_worker_blob_end"][0] and
            words[i + 5] & 0xff00 == 0xd400 and literal(i + 5) == symbols["__native_worker_blob_begin"][0] and
            words[i + 6:i + 10] == [0x1f92, 0x1fb1, 0x400b, 0x2f82]):
            relocations.append((i, i + 8, 2, True))
        if (mode_config and words[i+1]==0x6593 and words[i+2]&0xff00==0xdb00 and literal(i+2)==SPAN and
            words[i+3]==0x37e8 and words[i+4]&0xff00==0xd600 and literal(i+4)==symbols['__native_worker_blob_end'][0] and
            words[i+5]&0xff00==0xd400 and literal(i+5)==symbols['__native_worker_blob_begin'][0] and
            words[i+6:i+10]==[0x1fa2,0x1fb1,0x400b,0x2f82]):
            relocations.append((i,i+8,3,True))
    zero_patterns = (
        [0x56f3, 0x6ba3, 0x3b8c, 0x3688, 0x64b3, 0x4c0b, 0xe500],
        [0x56f3, 0x6ba3, 0x3b9c, 0x3698, 0x64b3, 0x4c0b, 0xe500],
        [0x6ba3, 0x3b9c, 0xe500, 0x66d3, 0x64b3, 0x4c0b, 0x3698],
        [0x6e83, 0x3eac, 0xe500, 0x66b3, 0x64e3, 0x4c0b, 0x36a8],
        [0x6e83,0x3e9c,0xe500,0x66b3,0x64e3,0x4c0b,0x3698])
    zero_families = (0, 1, 1, 2, 3)
    zeroes = [(i, zero_families[zero_patterns.index(words[i:i + 7])], zero_patterns.index(words[i:i + 7])) for i in range(len(words) - 7)
              if words[i:i + 7] in zero_patterns]
    releases = [i for i in range(len(words) - 3) if words[i:i + 3] ==
                ([0x52ba, 0x7214, 0x2022] if register_family in (2,3) else [0x53ca, 0x7314, 0x2032])]
    require(len(relocations) == len(zeroes) == len(releases) == 1,
            "missing linked relocation/BSS/release-cell initialization witnesses")
    require(relocations[0][2] == zeroes[0][1] == register_family,
            "installer relocation/BSS/publication use inconsistent load/length registers")
    require(relocations[0][3] == mode_config and (zeroes[0][2] in (2, 3, 4)) == mode_config,
            "installer mode configuration uses an unreviewed relocation/zero-fill ABI")
    relocate_pc = begin + relocations[0][1] * 2
    zero_pc = begin + (zeroes[0][0] + 5) * 2
    release_pc = begin + (releases[0] + 2) * 2
    # The compiler keeps memset in callee-saved R12 until this BSS call.
    # Pin its initializer and reject any intervening write to R12.
    memset_loads = [i for i in range(zeroes[0][0]) if words[i] & 0xff00 == 0xdc00 and
                    literal(i) == symbols["_memset"][0]]
    require(len(memset_loads) == 1, "BSS zeroing has no exact linked memset target")
    decoded = {pc: (op, mnemonic, operands.split("!", 1)[0].strip())
               for pc, op, mnemonic, operands in literal_aware_instructions(disassembly)
               if begin <= pc < begin + len(body)}
    for pc, (_, mnemonic, operands) in decoded.items():
        if begin + 2 * memset_loads[0] < pc < zero_pc and mnemonic not in ("cmp/eq", "cmp/hi", "cmp/hs", "tst"):
            require(not re.search(r"(?:^|,)\s*r12$", operands), "BSS memset target register is overwritten")
    # A conservative intraprocedural CFG treats every C call as returning.
    # The initialization witnesses must dominate the publication and arm;
    # byte order alone would allow a branch to bypass publication.
    edges, todo, seen = {}, [begin], set()
    while todo:
        pc = todo.pop()
        if pc in seen:
            continue
        require(pc in decoded, "installer branch enters non-instruction/literal bytes")
        seen.add(pc); op = decoded[pc][0]
        delay = op >> 12 in (0xa, 0xb) or op & 0xf0ff in (0x400b, 0x402b) or op == 0x000b or op >> 8 in (0x8d, 0x8f)
        if delay:
            require(pc + 2 in decoded, "installer delay slot is not an instruction")
        if op >> 12 == 0xa:
            d = op & 4095; d -= 4096 if d & 2048 else 0
            targets = [pc + 4 + 2 * d]
        elif op >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
            d = op & 255; d -= 256 if d & 128 else 0
            targets = [pc + 4 + 2 * d, pc + (4 if delay else 2)]
        elif op == 0x000b:
            targets = []
        else:
            require(op & 0xf0ff != 0x402b, "unmodeled installer indirect tail jump")
            targets = [pc + (4 if delay else 2)]
        edges[pc] = targets; todo.extend(targets)
    predecessors = {pc: set() for pc in seen}
    for pc, targets in edges.items():
        for target in targets:
            predecessors[target].add(pc)
    dom = {pc: ({pc} if pc == begin else set(seen)) for pc in seen}
    for _ in range(len(seen)):
        changed = False
        for pc in seen - {begin}:
            value = {pc} | set.intersection(*(dom[p] for p in predecessors[pc]))
            if value != dom[pc]:
                dom[pc] = value; changed = True
        if not changed:
            break
    # v3 puts the consumer-cell store in the publisher JSR delay slot.
    # The exact block above pins that store; the JSR node represents both
    # instructions in this CFG, before control reaches the publisher.
    dynamic_stores = (5, 8) if register_family in (2, 3) else (4, 7)
    witnesses = {relocate_pc, zero_pc, release_pc,
                 *(begin + (first + offset) * 2 for offset in dynamic_stores)}
    if register_family in (2, 3):witnesses.add(begin + (first - 4) * 2)
    require(witnesses <= dom[publish] and publish in dom[arm],
            "installer can enter P1 arm before initialization/publication")
    require(begin + 2 * memset_loads[0] in dom[zero_pc],
            "BSS memset target can be bypassed")
    diagnostics_getter = None
    if '_native_runtime_diagnostics' in symbols:
        # Bind the noipa getter's canonical 0/1/2/3 return to the exact last
        # config word. v3 adds four stack bytes ahead of the configuration.
        # The store is the next call's delay slot, before that call may
        # clobber R0. Both getter and store must dominate the first arm.
        config_start, config_base = (0x48, 0x904) if register_family == 3 else (0x44, 0x900)
        alias_add = 0x38AC if register_family == 3 else 0x389C
        choices = [j for j in range(len(words) - 9) if
            words[j] & 0xFF00 == 0xD000 and literal(j) == symbols['_native_runtime_diagnostics'][0] and
            words[j + 1] & 0xFF00 == 0xD800 and literal(j + 1) == 0x20000000 and
            words[j + 2] & 0xFF00 == 0xDE00 and literal(j + 2) == symbols['__native_worker_blob_end'][0] and
            words[j + 3:j + 5] == [0x400B, alias_add] and
            words[j + 5] & 0xFF00 == 0xD100 and literal(j + 5) == symbols['_retail_display_restore'][0] and
            words[j + 6] & 0xFF00 == 0xD400 and literal(j + 6) == symbols['_display'][0] and
            words[j + 7:j + 9] == [0x410B, 0x1D0F]]
        require(len(choices) == 1, 'missing exact runtime diagnostics return-to-config store')
        diagnostics_getter = choices[0]
        bases = []
        for j in range(diagnostics_getter - 3):
            if words[j] & 0xFF00 != 0x9D00 or words[j + 1] != 0xE630 or words[j + 3] != 0x3DFC:
                continue
            at = begin + 2 * j + 4 + 2 * (words[j] & 255)
            if struct.unpack('<h', region(body, at - begin, 2, 'diagnostics config stack offset'))[0] == config_base:
                bases.append(j)
        require(len(bases) == 1 and config_base + 60 - config_start == 0x8F8,
                'diagnostics store no longer names exact final config word')
        getter_pc = begin + 2 * (diagnostics_getter + 3)
        store_call_pc = begin + 2 * (diagnostics_getter + 7)
        base_pc = begin + 2 * (bases[0] + 3)
        require(base_pc in dom[getter_pc] and getter_pc in dom[store_call_pc] and store_call_pc in dom[arm],
                'runtime mode/config store can be bypassed before worker arm')
        for pc, (_, mnemonic, operands) in decoded.items():
            if base_pc < pc < store_call_pc and mnemonic not in ('cmp/eq', 'cmp/hi', 'cmp/hs', 'tst'):
                require(not re.search(r'(?:^|,)\s*r13$', operands), 'diagnostics config base is overwritten')
            if base_pc < pc < arm:
                require(not re.search(r'(?:^|,)\s*r15$', operands) and '@-r15' not in operands and '@r15+' not in operands,
                        'config stack moves between diagnostics assignment and arm')
    report.update(worker_relocation_before_publication=True, worker_bss_zero_before_publication=True,
                  dynamic_cells_before_publication=True, publication_dominates_first_arm=True,
                  first_worker_publish=hex(publish), first_worker_arm=hex(arm),
                  mode_configuration_compiler_witness=mode_config,
                  runtime_diagnostics_return_stored_in_exact_config=diagnostics_getter is not None,
                  runtime_diagnostics_store_dominates_arm=diagnostics_getter is not None,
                  linked_installer_cfg_nodes=len(seen))
    if self_test:
        # An individually recognized register-family block must not be mixed
        # with the other family's relocation/zero-fill/publication. Mutate the
        # actual ELF instruction bytes, keeping its typed geometry unchanged.
        alternate = 0 if register_family in (2, 3) else 1 - register_family
        mutations = [((zeroes[0][0] + 2, zero_patterns[alternate][2]),
                      (zeroes[0][0] + 3, zero_patterns[alternate][3])),
                     ((relocations[0][0] + 1, 0x0009),)]
        if register_family == 3:
            mutations.extend([((first + 4, 0x35ac),), ((first + 9, 0x0009),),
                              ((first - 3, 0x6493),), ((first - 2, 0x0009),)])
        elif register_family == 2:
            mutations.append(((first + 4, 0x359c),))
            mutations.extend([((first - 3, 0x6483),), ((first - 2, 0x0009),)])
        elif publish_at == 9:
            mutations.append(((first + 1, prepared_setups[1 - register_family][0]),
                              (first + 2, prepared_setups[1 - register_family][1])))
        if diagnostics_getter is not None:
            getter_literal = (((begin + 2 * diagnostics_getter + 4) & ~3) +
                              4 * (words[diagnostics_getter] & 255) - begin) // 2
            wrong_getter = symbols['_native_runtime_mode'][0]
            mutations.extend([((diagnostics_getter + 8, 0x0009),),
                              ((diagnostics_getter + 8, 0x1D0E),),
                              ((diagnostics_getter, 0xE000),),
                              ((getter_literal, wrong_getter & 0xFFFF),
                               (getter_literal + 1, wrong_getter >> 16))])
        _, _, section = symbol_bytes(stage, "_native_passive_run", 2)
        offset = section[4] + begin - section[3]
        rejected = 0
        for changes in mutations:
            changed = bytearray(stage[0])
            for index, value in changes:
                struct.pack_into("<H", changed, offset + index * 2, value)
            try:
                check_worker_publication((bytes(changed), *stage[1:]), cached, disassembly)
            except (ValueError, AssertionError, KeyError, struct.error):
                rejected += 1
            else:
                raise ValueError("worker publication accepted inconsistent initialization witness")
        report["initialization_witness_mutation_rejections"] = rejected
    return report


def require(value, message):
    if not value:
        raise ValueError(message)


def physical(address):
    return address & 0x1FFFFFFF


class CachedGateway(ReadonlyGateway):
    """Actual gateways fetch cached code/literals while frames/cells stayP2."""
    def code(self, address, size):
        assert address & 0xFF000000 == 0x8C000000, "gateway fetched uncached code/constant"
        offset = physical(address) - physical(self.base)
        assert 0 <= offset < offset + size <= len(self.blob)
        return self.blob[offset:offset + size]


def model_cached_hooks(binary, wire, symbols):
    cases = 0
    for destination in (P2, 0xAC38A880, 0xAC700000):
        blob = relocated(binary, wire, destination)
        mapped = {name: address + destination - P2 for name, address in symbols.items()}
        for seed in range(40):
            for mask in range(16):
                for mode in ("submit", "complete", "terminal", "release"):
                    CachedGateway(seed, mask, destination, blob, mapped, mode).run().check()
                    cases += 1
    return cases


class CachedStageGateway(StageGateway):
    def __init__(self, seed, mask, base, blob, **options):
        super().__init__(seed, mask, base, blob, entry=options.pop("entry", P2_ENTRY - 0x20000000), **options)
        self.cache_entry = P2_ENTRY - 0x20000000
    def run(self):
        steps = 0
        while self.pc != self.initial[1]:
            steps += 1;assert steps < 200
            pc = self.pc;word = struct.unpack("<H", self.code(pc, 2))[0]
            if word >> 12 == 0xA or word & 0xF0FF == 0x400B or word == 0xB:
                if word >> 12 == 0xA:
                    displacement = word & 4095
                    displacement -= 4096 if displacement & 2048 else 0
                    target = (pc + 4 + displacement * 2) & MASK
                elif word & 0xF0FF == 0x400B:
                    target = self.r[word >> 8 & 15];self.pr = pc + 4
                else:
                    target = self.pr
                self.plain(struct.unpack("<H", self.code(pc + 2, 2))[0], pc + 2)
                if target == self.cache_entry:
                    self.worker()
                else:
                    self.pc = target
            elif word >> 8 in (0x89, 0x8B):
                displacement = word & 255
                displacement -= 256 if displacement & 128 else 0
                take = bool(self.sr & 1) == (word >> 8 == 0x89)
                self.pc = (pc + 4 + displacement * 2 if take else pc + 2) & MASK
            else:
                self.plain(word, pc);self.pc = pc + 2
        return self


def model_cached_stage(base, blob):
    cases = 0
    for seed in range(80):
        for mask in range(16):
            CachedStageGateway(seed, mask, base, blob).run().check(int(mask == 15)); cases += 1
    entry = P2_ENTRY - 0x20000000
    invalid = (("entry", entry + 1), ("entry", P2_ENTRY), ("top", TOP + 4),
               ("top", 0xAD000020), ("top", 0xAC000FE0), ("config", 0), ("result", 0))
    for field, value in invalid:
        CachedStageGateway(1, 15, base, blob, **{field: value}).run().check(0); cases += 1
    CachedStageGateway(1, 15, base, blob, top=0xAD000000).run().check(1)
    return cases + 1

def model_cached_mutations(binary, symbols):
    """Reject control-flow/FPU changes without relying on ELF byte mismatch."""
    tests = []
    begin = physical(symbols["_kui_vmu_native_passive_submit_hook"]) - physical(P2)
    changed = bytearray(binary)
    struct.pack_into("<H", changed, begin, 0xF000)
    tests.append((changed, "submit", "FPU in gateway"))
    # Change the referenced callback literal in situ. Its wrong target is a
    # real known callback, so rejection must come from the frame/control-flow
    # model, rather than merely an out-of-bounds instruction read.
    for mode, target_name, wrong_name in (
            ("submit", "_kui_vmu_native_passive_submit", "_kui_vmu_native_passive_complete"),
            ("complete", "_kui_vmu_native_passive_complete", "_kui_vmu_native_passive_submit"),
            ("terminal", "_kui_vmu_native_passive_report", "_kui_vmu_native_passive_submit"),
            ("release", "_kui_vmu_native_readonly_release_check", "_kui_vmu_native_passive_complete")):
        function = symbols["_kui_vmu_native_readonly_release_hook"] if mode == "release" else symbols["_kui_vmu_native_passive_" + ("terminal" if mode == "terminal" else mode + "_hook")]
        matches = []
        # Authored hook functions are all shorter than 256 bytes. Inspect
        # PC-relative MOV.L operands, not arbitrary pointer-looking words.
        for offset in range(physical(function) - physical(P2), min(physical(function) - physical(P2) + 256, len(binary)) - 1, 2):
            word = struct.unpack_from("<H", binary, offset)[0]
            if word >> 12 != 0xD:
                continue
            literal = ((P2 + offset + 4) & ~3) + 4 * (word & 255) - P2
            if (0 <= literal <= len(binary) - 4 and
                    struct.unpack_from("<I", binary, literal)[0] == symbols[target_name]):
                matches.append(literal)
        require(matches, "missing actual gateway callback literal for mutation")
        changed = bytearray(binary)
        struct.pack_into("<I", changed, matches[0], symbols[wrong_name])
        tests.append((changed, mode, "wrong callback target"))
    # The completion must dereference its own consumer cell, not a stage
    # address, callback, or submission resume cell.
    complete = physical(symbols["_kui_vmu_native_passive_complete_hook"]) - physical(P2)
    matches = []
    for offset in range(complete, min(complete + 256, len(binary)) - 1, 2):
        word = struct.unpack_from("<H", binary, offset)[0]
        if word & 0xFF00 != 0xD300:
            continue
        literal = ((P2 + offset + 4) & ~3) + 4 * (word & 255) - P2
        if (0 <= literal <= len(binary) - 4 and
                struct.unpack_from("<I", binary, literal)[0] == symbols["_kui_vmu_native_passive_consumer"]):
            matches.append(literal)
    require(len(matches) == 1, "missing unique dynamic consumer-cell load")
    changed = bytearray(binary)
    struct.pack_into("<I", changed, matches[0], symbols["_kui_vmu_native_passive_resume"])
    tests.append((changed, "complete", "wrong consumer cell"))
    # Move inherited SR restoration one frame pop too early. This must fail
    # independently of final register equality, since a same-priority IRQ may
    # arrive as soon as the original mask is restored.
    pattern = struct.pack("<4H", 0x63F6, 0x61F6, 0x60F6, 0x430E)
    at = binary.find(pattern, complete, min(complete + 256, len(binary)))
    require(at >= 0, "missing complete-frame final restore sequence")
    changed = bytearray(binary)
    struct.pack_into("<2H", changed, at + 4, 0x430E, 0x60F6)
    tests.append((changed, "complete", "premature inherited SR restoration"))
    changed = bytearray(binary)
    struct.pack_into("<H", changed, at + 8, 0x6883)  # MOV R8,R8 is otherwise harmless.
    tests.append((changed, "complete", "missing SR latency instruction"))
    # Replay the original MDST store twice; the linked model admits that
    # exact address but requires precisely one store during submission.
    submit = physical(symbols["_kui_vmu_native_passive_submit_hook"]) - physical(P2)
    store = binary.find(struct.pack("<H", 0x12C6), submit, min(submit + 256, len(binary)))
    require(store >= 0, "missing original MDST replay")
    changed = bytearray(binary)
    struct.pack_into("<H", changed, store + 2, 0x12C6)
    tests.append((changed, "submit", "duplicate MDST replay"))
    # Apply the same lifetime-entry restoration mutation to the allocator
    # wrapper. Its C callback may execute in contexts different from Maple.
    release = physical(symbols["_kui_vmu_native_readonly_release_hook"]) - physical(P2)
    pattern = struct.pack("<5H", 0x60F6, 0x61F6, 0x6703, 0xCBF0, 0x400E)
    at = binary.find(pattern, release, min(release + 256, len(binary)))
    require(at >= 0, "missing release-frame final restore sequence")
    changed = bytearray(binary)
    struct.pack_into("<2H", changed, at + 2, 0x400E, 0x61F6)
    tests.append((changed, "release", "premature release SR restoration"))
    # Removing the OR before the first post-callback SR load produces the
    # same final state after the original mask-establishment sequence, yet
    # transiently permits IRQs to invalidate the just-checked free list.
    changed = bytearray(binary)
    struct.pack_into("<H", changed, at + 6, 0x0009)
    tests.append((changed, "release", "transient release IRQ admission"))
    # Cached code must never fall back to a P2 execution target, while its
    # private stack and mutable consumer cell must never acquire P1 aliases.
    for mode, hook, value, delta in (
            ("submit", "_kui_vmu_native_passive_submit_hook", "_kui_vmu_native_passive_submit", 0x20000000),
            ("complete", "_kui_vmu_native_passive_complete_hook", "_kui_vmu_native_passive_complete", 0x20000000),
            ("terminal", "_kui_vmu_native_passive_terminal", "_kui_vmu_native_passive_report", 0x20000000),
            ("release", "_kui_vmu_native_readonly_release_hook", "_kui_vmu_native_readonly_release_check", 0x20000000),
            ("complete", "_kui_vmu_native_passive_complete_hook", "_kui_vmu_native_passive_consumer", -0x20000000),
            ("complete", "_kui_vmu_native_passive_complete_hook", "__vmu_native_worker_stack_top", -0x20000000)):
        begin = physical(symbols[hook]) - physical(P2)
        references = []
        for offset in range(begin, min(begin + 256, len(binary)) - 1, 2):
            word = struct.unpack_from("<H", binary, offset)[0]
            if word >> 12 != 0xD: continue
            literal = ((P2 + offset + 4) & ~3) + 4 * (word & 255) - P2
            if 0 <= literal <= len(binary) - 4 and struct.unpack_from("<I", binary, literal)[0] == symbols[value]:
                references.append(literal)
        require(references, "missing actual cached gateway alias literal")
        changed = bytearray(binary)
        struct.pack_into("<I", changed, references[0], symbols[value] + delta)
        tests.append((changed, mode, "wrong cached code or mutable-state alias"))
    cases = 0
    for blob, mode, description in tests:
        try:
            CachedGateway(7, 6, P2, blob, symbols, mode).run().check()
        except (AssertionError, struct.error):
            cases += 1
        else:
            raise ValueError("gateway model accepted " + description)
    return cases


def check_runtime_fault_gateways(worker, binary, wire, self_test=False):
    """Execute the exact relocated failure-only ABI; owner RAM is read only."""
    symbols = {name: value[0] for name, value in worker[4].items()}
    runtime = "_kui_vmu_native_runtime_fault_hook"
    allocation = "_kui_vmu_native_allocation_failure_hook"
    report = "_kui_vmu_native_runtime_fault"
    def authored(image):
        for name, prefix, targets in (
                (runtime, (0x0002, 0xcbf0, 0x400e, 0xdf02, 0xd002, 0x402b, 0x0009, 0x0009),
                 (symbols["__vmu_native_worker_stack_top"], symbols[report])),
                (allocation, (0x65e3, 0x56f1, 0x67f3, 0xe401, 0xd001, 0x402b, 0x0009, 0x0009),
                 (symbols[runtime],))):
            address, body, section = symbol_bytes(image, name, 2)
            require(section[2] & 7 == 6 and address % 4 == 0 and
                    body == struct.pack("<" + "H" * len(prefix) + "I" * len(targets), *prefix, *targets),
                    "failure gateway integer capture/mask/private-stack transfer changed: " + name)
    authored(worker)
    cases = 0
    for destination in (P2, 0xAC38A880, 0xAC700000):
        blob = relocated(binary, wire, destination)
        mapped = {name: address + destination - P2 for name, address in symbols.items()}
        def read_code(address, size):
            require(address & 0xff000000 == mapped[runtime] & 0xff000000,
                    "failure gateway used wrong instruction alias")
            offset = physical(address) - physical(destination)
            require(0 <= offset < offset + size <= len(blob), "failure gateway escaped initialized code")
            return blob[offset:offset + size]
        for seed in range(40):
            rng = random.Random(seed)
            for mask in range(16):
                for mode in ("allocation", "runtime"):
                    r = [rng.getrandbits(32) for _ in range(16)]
                    sp = 0x8c00f100;request = (rng.randrange(1, 0x100000) + 31) & ~31
                    caller = 0x8c010000 + 2 * rng.randrange(0x100000)
                    r[15], r[14] = sp, request
                    inherited_sr = (rng.getrandbits(32) & ~0xf0) | 0x40000000 | mask << 4
                    sr = inherited_sr
                    r[4:8] = [2, rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)]
                    expected = [1, request, caller, sp] if mode == "allocation" else r[4:8].copy()
                    # These gateways contain no instruction touching PR, MAC,
                    # GBR, FPSCR or any FR/FPUL; the closed opcode vocabulary
                    # and authored byte proof exclude such side effects.
                    pc = mapped[allocation if mode == "allocation" else runtime]
                    owner_reads = []
                    for step in range(24):
                        if pc == mapped[report]: break
                        op, = struct.unpack("<H", read_code(pc, 2))
                        n, m = op >> 8 & 15, op >> 4 & 15
                        if op == 0x0002:r[0] = sr
                        elif op == 0xcbf0:r[0] |= 0xf0
                        elif op == 0x400e:sr = r[0]
                        elif op & 0xf00f == 0x6003:r[n] = r[m]
                        elif op >> 12 == 0xe:r[n] = ((op & 255) - (256 if op & 128 else 0)) & MASK
                        elif op >> 12 == 0x5:
                            at = (r[m] + 4 * (op & 15)) & MASK
                            require(mode == "allocation" and at == sp + 4,
                                    "failure shim followed an unproved owner address")
                            owner_reads.append(at);r[n] = caller
                        elif op >> 12 == 0xd:
                            at = ((pc + 4) & ~3) + 4 * (op & 255)
                            r[n], = struct.unpack("<I", read_code(at, 4))
                        elif op == 0x402b:
                            require(read_code(pc + 2, 2) == b"\x09\x00", "failure gateway delay slot changed")
                            pc = r[0];continue
                        elif op != 9:raise ValueError("unadmitted instruction/store in failure gateway")
                        pc += 2
                    else:raise ValueError("failure gateway did not terminate in bounded report")
                    require(r[4:8] == expected and r[15] == mapped["__vmu_native_worker_stack_top"] and
                            sr == inherited_sr | 0xf0 and owner_reads == ([sp + 4] if mode == "allocation" else []),
                            "failure gateway lost arguments, private SP or IRQ exclusion")
                    cases += 1
    rejected = 0
    if self_test:
        for name in (runtime, allocation):
            address, body, section = symbol_bytes(worker, name, 2)
            for offset in range(0, len(body), 2):
                changed = bytearray(worker[0]);changed[section[4] + address - section[3] + offset] ^= 1
                try:authored((bytes(changed),) + worker[1:])
                except ValueError:rejected += 1
                else:raise ValueError("failure gateway accepted a changed authored instruction/literal")
    return {"modeled_cases": cases, "mutation_rejections": rejected,
            "owner_data_writes": 0, "allocation_owner_reads": 1,
            "runtime_report_is_noreturn": True}


def worker_load_policy(worker, binary):
    data, _, segments, sections, _ = worker
    loads = validate_loads(data, binary, P1, SPAN, sections)
    if len(segments) == 1:
        kind, offset, va, pa, size, memory, flags, _ = segments[0]
        require((kind, va, pa, size, memory, flags) == (1, P2, P1, len(binary), SPAN, 7),
                "uncached worker differs from its exact legacy P2 load")
        require(binary == region(data, offset, size, "worker load"), "legacy flat worker differs from ELF")
        return {"cached_code": False, "code_alias": P2, "loads": loads}
    require(len(segments) == len(loads) == 3, "cached worker needs exactly3 physical loads")
    header, code, mutable = loads
    require((header["virtual"], header["physical"], header["bytes"], header["memory"], header["flags"]) ==
            (P2, P1, 80, 80, 4), "cached worker header must be immutable P2/80B")
    require(code["virtual"] == code["physical"] and code["virtual"] & 0xFF000000 == 0x8C000000 and
            code["flags"] == 5 and code["bytes"] == code["memory"] and code["bytes"] and
            not (code["first"] + code["bytes"]) & 31 and
            code["first"] in (physical(P1) + 80, (physical(P1) + 80 + 31) & ~31),
            "cached code/const load must own readonly P1 bytes through an aligned end")
    require(mutable["virtual"] == mutable["physical"] + 0x20000000 and mutable["flags"] == 6 and
            mutable["first"] == code["first"] + code["memory"] and not mutable["first"] & 31 and
            mutable["first"] + mutable["memory"] == physical(P1) + SPAN,
            "mutable worker load must be P2 and disjoint from every cached line")
    shstrings = sections[worker[1][13]]
    section_names = region(data, shstrings[4], shstrings[5], "section names")
    for section in sections:
        if not section[2] & 2 or not section[5]: continue
        if section[2] & 1:
            require(section[3] & 0xFF000000 == 0xAC000000 and
                    section[2] & 7 == 3, "cached worker has writable P1/code state")
        elif section[3] == P2:
            require(section[1] == 1 and section[5] == 80 and section[2] & 7 == 2,
                    "P2 header contains unexpected code/state")
        elif section[3] & 0xFF000000 == 0xAC000000:
            name = section_names[section[0]:].split(b"\0", 1)[0]
            require(name == b".image_align" and section[1] == 1 and section[2] & 7 == 2 and
                    0 < section[5] <= 32 and not any(region(data, section[4], section[5], "mutable tail padding")),
                    "P2 immutable bytes are neither header nor zero tail padding")
        else:
            require(section[3] & 0xFF000000 == 0x8C000000 and section[2] & 7 in (2, 6),
                    "code/const bytes do not exclusively use cached P1")
    return {"cached_code": True, "code_alias": P1, "loads": loads}


def inspect_base(directory, *, self_test=False, objdump="sh-elf-objdump"):
    """Reuse exact bootstrap/layout checks while admitting the one SCI loan."""
    images, elfs = {}, {}
    for name, base, limit in (("resident-sci", LOW, 0x8C007800),
                              ("stage", HIGH, LIMIT), ("entry", layout.EXEC_ADDRESS, 0x8C112000)):
        elfs[name] = (directory / (name + ".elf")).read_bytes()
        images[name] = inspect_elf(elfs[name], base, limit)
        if name != "entry":
            require((directory / (name + ".bin")).read_bytes() == images[name]["payload"],
                    name + " binary differs from ELF")
    resident, stage, entry = (images[name] for name in ("resident-sci", "stage", "entry"))
    check_bss(resident, LOW, "__retail_resident")
    check_bss(stage, HIGH, "__retail_stage")
    for name in ("resident-sci", "stage"):
        require(not any(symbol.startswith(("_kui_vmu_maple_observer_", "__vmu_observe_worker_blob_"))
                        for symbol in images[name]["symbols"]), "old fixed-address VMU worker linked")
    require(not any(symbol.startswith(("_kui_vmu_durable_", "_kui_vmu_persist_", "_kui_sci_sd_write",
                                       "_kui_sci_sd_sync")) for symbol in resident["symbols"]),
            "low reader contains a high-only persistent writer")
    for name in ("_kui_vmu_native_heap_discover", "_kui_vmu_native_heap_call_discover", "_kui_vmu_native_heap_fresh",
                 "_kui_vmu_native_heap_preallocate", "_kui_vmu_native_heap_postallocate",
                 "_kui_vmu_native_heap_lease_valid", "_kui_vmu_observe_stage_install",
                 "_kui_vmu_persist_stage_admit", "_kui_vmu_persist_stage_prepare",
                 "_kui_vmu_persist_stage_handoff_matches"):
        typed_function(elfs["stage"], stage, name, HIGH)
    for transport in ("scif", "sci", "ide", "scia"):
        prefix = "__retail_resident_" + transport + "_blob_"
        begin, end = (stage["symbols"].get(prefix + suffix, 0) for suffix in ("start", "end"))
        require(region(stage["payload"], begin - HIGH, end - begin, "embedded resident") ==
                padded(resident["payload"]), "stage embeds different resident: " + transport)
    es = entry["symbols"]
    require(es.get("__retail_map") == layout.EXEC_ADDRESS + layout.MAP_OFFSET and
            es.get("__retail_stage_blob_start") == layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET and
            es.get("__retail_stage_blob_end") == layout.EXEC_ADDRESS + len(entry["payload"]) and
            entry["memory_end"] == layout.EXEC_ADDRESS + len(entry["payload"]) and
            entry["payload"][layout.STAGE_BLOB_OFFSET:] == padded(stage["payload"]),
            "entry relocation/stage bytes differ from ELF")
    require(not any(region(entry["payload"], layout.MAP_OFFSET, layout.MAP_BYTES, "blank manifest")),
            "launch manifest must ship blank")
    require(region(entry["payload"], layout.HEADER_OFFSET, layout.HEADER_BYTES, "relocation header") ==
            layout.relocation_header(len(padded(stage["payload"])), False, True),
            "persistent probe must select low synchronous native reader")
    module = (directory / "retail-maple-observe.kui").read_bytes()
    runtime = verify(module)
    require(module[64:] == entry["payload"] and runtime["memory_bytes"] == entry["memory_end"] - layout.EXEC_ADDRESS,
            "module differs from actual entry ELF")
    instructions = {}
    for name in images:
        disassembly = subprocess.check_output([objdump, "-d", str(directory / (name + ".elf"))], text=True)
        instructions[name] = instruction_audit(name, disassembly)
    gateway = check_loan_gateway(directory, resident, elfs["resident-sci"])
    map_refusal = audit_map_refusal(directory / "resident-sci.elf",self_test=self_test,objdump=objdump)
    report = {"schema": "kui.native.persistent.bootstrap.v1", "runtime": runtime,
              "module_sha256": hashlib.sha256(module).hexdigest(),
              "resident_memory_end": f"0x{resident['memory_end']:08x}",
              "stage_memory_end": f"0x{stage['memory_end']:08x}",
              "dynamic_bootstrap": inspect_dynamic_hook(elfs["resident-sci"], resident),
              "integer_instruction_counts": instructions, "sd_lease_gateway": gateway,
              "gd_map_refusal_terminal":map_refusal,
              "old_fixed_worker_linked": False, "console_verified": False}
    if self_test:
        report["dynamic_bootstrap_mutation_rejections"] = regression(elfs["resident-sci"], resident)
    return report


def check_loan_gateway(directory, resident, elf):
    address, size, declared = sd_gateway_span(elf, resident)
    reports = sorted((directory / "resident/lto").glob("*.ltrans*.ci"))
    if not reports:
        reports = sorted(directory.glob("resident-sci.elf*.ci"))
    nodes = set()
    require(reports, "missing final-LTO SCI loan callgraph proof")
    for report in reports:
        data = report.read_text()
        nodes.update(_function_base(title) for title, _ in NODE.findall(data))
        require(not any(_function_base(target) == "kui_vmu_observe_sd_lease"
                        for _, target, _ in EDGE.findall(data)), "resident call enters worker-stack-only loan")
    require("kui_vmu_observe_sd_lease" in nodes, "final-LTO graph omits SCI loan")
    for offset in range(0, len(resident["payload"]) - 3, 4):
        at = LOW + offset
        if address <= at < address + size:
            continue
        value = struct.unpack_from("<I", resident["payload"], offset)[0]
        if value & 0xFF000000 in (0x0C000000, 0x8C000000, 0xAC000000):
            canonical = value & 0x1FFFFFFF | 0x80000000
            require(not address <= canonical < address + size, "low resident literal enters worker-stack-only loan")
    return {"address": f"0x{address:08x}", "bytes": size, "declared_symbol_bytes": declared,
            "incoming_resident_edges": 0, "resident_pointer_references": 0,
            "callgraph_reports": [report.name for report in reports]}


def check_pio_writers(directory, worker):
    results = {}
    for image_name, image, base, folder, adapter in (
            ("worker", worker, P1 if worker[4]["_kui_vmu_native_passive_arm"][0] < P2 else P2,
             "native", "durable_card_pio_write"),
            ("stage", read_elf(directory / "stage.elf"), HIGH, "stage", "native_persist_pio_write")):
        elf = image[0]
        binary = (directory / ("native-worker.bin" if image_name == "worker" else "stage.bin")).read_bytes()
        address, body, _ = symbol_bytes(image, "_" + adapter, 2)
        writer, _, _ = symbol_bytes(image, "_kui_sci_sd_write", 2)
        report = check_pio_entry({"payload": binary}, address, len(body), writer, base=base)
        report.update(check_pio_callgraph(directory / folder, adapter, linked_function_names(elf, {})))
        results[image_name] = report
    return results


def check_source(directory, runtime):
    raw = (directory / "entry.compile.json").read_bytes()
    record = json.loads(raw)
    require(isinstance(record, dict) and isinstance(record.get("commit"), str) and
            re.fullmatch(r"[0-9a-f]{40}", record["commit"]) and type(record.get("source_dirty")) is bool and
            record.get("elf_sha256") == hashlib.sha256((directory / "entry.elf").read_bytes()).hexdigest() and
            runtime["build"] == record["commit"][:12], "source provenance differs from compiled entry")
    return {"commit": record["commit"], "source_dirty": record["source_dirty"],
            "compile_provenance_sha256": hashlib.sha256(raw).hexdigest(),
            "elf_sha256": record["elf_sha256"]}


def worker_stack_paths(directory, worker, resident):
    """Walk source-qualified emitted graphs with finite authored callbacks."""
    report_sets = {"worker": sorted((directory / "native").rglob("*.ci")),
                   "low": sorted(directory.glob("resident-sci.elf*.ltrans*.ci"))}
    if not report_sets["low"]:
        report_sets["low"] = sorted((directory / "resident/lto").glob("*.ltrans*.ci"))
    images = {"worker": worker[4], "low": resident["symbols"]}
    frames, calls, origins = {}, {}, {}
    for domain, reports in report_sets.items():
        require(reports, "missing emitted callgraph: " + domain)
        f, c = load_graph(reports, qualified=True, scope=domain)
        live = {_function_base(name) for name in images[domain]}
        f = {name: value for name, value in f.items() if _function_base(name) in live}
        frames.update(f); calls.update(c)
        for report in reports:
            for title, label in NODE.findall(report.read_text()):
                pieces = label.split(r"\n")
                if len(pieces) >= 2:
                    origins[domain + ":" + title] = Path(pieces[1].rsplit(":", 2)[0]).name
    # This authored leaf has no compiler .su/.ci file. Admit zero stack only
    # after proving all seven actual instructions: three control-register
    # reads, three stores through R4 and RTS. It neither pushes nor calls.
    _, reader, owner = symbol_bytes(worker, "_kui_vmu_native_trace_irq", 2)
    require(owner[2] & 7 == 6 and reader == bytes.fromhex("420002243200011422000b000214"),
            "saved exception reader is not the exact zero-stack read-only leaf")
    frames["worker:kui_vmu_native_trace_irq"] = 0
    calls["worker:kui_vmu_native_trace_irq"] = []
    by_base = {}
    for name in frames:
        by_base.setdefault((name.split(":", 1)[0], _function_base(name)), []).append(name)
    def resolve(domain, function, owner=None):
        exact = domain + ":" + function
        found = [exact] if exact in frames else by_base.get((domain, _function_base(function)), [])
        if owner is not None:
            found = [name for name in found if origins.get(name) == owner]
        require(found, "missing emitted callback frame: " + domain + ":" + function)
        return found
    def high(*names):
        return [node for name in names for node in resolve("worker", name)]
    def bus(*names):
        return [node for name in names for node in resolve("low", name, "sci_sd_bus.c")]
    memo, paths, visiting, indirect_sites, null_sites = {}, {}, set(), set(), set()
    def indirect(name, label):
        domain, function = name.split(":", 1)[0], _function_base(name)
        source = Path(label.split(":", 1)[0]).name
        if domain == "low":
            require((function, source) == ("kui_vmu_observe_sd_lease", "vmu_sd_lease.inc"),
                    "unexpected low callback from worker stack: " + name + " at " + label)
            return high("kui_vmu_native_persistent_sd_transaction")
        fixed = {
            "vmu_durable.c": ("persistent_lease",),
            "vmu_emulation.c": ("durable_card_read", "durable_card_commit"),
            "vmu_native_heap.c": ("read_heap",),
            "vmu_native_layout.c": ("read_value",),
            "vmu_native_lifetime.c": ("read_heap",),
            "vmu_native_live.c": ("boundary_read",),
            "vmu_native_runtime.c": ("boundary_read",),
            "vmu_store.c": ("durable_card_sector_read", "durable_card_sector_write", "durable_card_sector_sync"),
            "vmu_readonly_overlay.c": ("persistent_admit", "persistent_handler"),
        }
        owners = {
            "vmu_durable.c": {"durable_card_execute"},
            "vmu_emulation.c": {"kui_vmu_emulation_handle"},
            "vmu_native_heap.c": {"read_is", "kui_vmu_native_heap_lease_valid", "kui_vmu_native_heap_lease_valid_with_release"},
            "vmu_native_layout.c": {"kui_vmu_native_layout_bind_checked"},
            "vmu_native_lifetime.c": {"kui_vmu_native_lifetime_release_safe_ranges"},
            "vmu_native_live.c": {"kui_vmu_native_live_check"},
            "vmu_native_runtime.c": {"kui_vmu_native_runtime_check"},
            "vmu_store.c": {"write_sync", "kui_vmu_store_open", "kui_vmu_store_read", "kui_vmu_store_sync"},
            "vmu_readonly_overlay.c": {"kui_vmu_readonly_overlay_prepare", "kui_vmu_readonly_overlay_complete"},
        }
        if source in fixed:
            require(function in owners[source], "unexpected callback source owner: " + name)
            return high(*fixed[source])
        if (function, source) == ("persistent_lease", "vmu_native_passive.c"):
            return resolve("low", "kui_vmu_observe_sd_lease")
        if source == "sd_reader.c":
            targets = {"transfer": ("transfer",), "ticks": ("ticks",), "release": ("select_card",),
                       "read_payload": ("transfer_block",), "kui_loader_sd_read": ("select_card",)}
            require(function in targets, "unexpected initialized-card reader callback: " + name)
            return bus(*targets[function])
        if source == "vmu_backing.c":
            require(function in ("byte", "wait_byte", "kui_vmu_sd_identity_read"),
                    "unexpected backing callback: " + name)
            return bus("transfer", "ticks", "select_card")
        if source == "sci_sd_storage.c":
            if function == "kui_sci_sd_write" and label.endswith(":83:17"):
                # Actual native PIO adapters independently prove this callback
                # NULL at the sole writer entry; this optional branch is dead.
                null_sites.add((name, label)); return []
            return bus("transfer", "ticks", "select_card")
        raise ValueError("unadmitted indirect worker call: " + name + " at " + label)
    def depth(name):
        if name in memo:
            return memo[name]
        require(name not in visiting, "recursive private-stack path: " + name)
        if name not in frames:
            if _function_base(name) in ("__udivsi3", "__sdivsi3", "__udivdi3", "__umoddi3", "__moddi3"):
                return 0, [name + "(library margin)"]
            domain, target = name.split(":", 1)
            alternatives = resolve(domain, target)
            return max((depth(node) for node in alternatives), key=lambda item: item[0])
        visiting.add(name); best = (0, [])
        for target, label in calls.get(name, []):
            targets = indirect(name, label) if target == "__indirect_call" else [target]
            if target == "__indirect_call":
                indirect_sites.add((name, label, tuple(targets)))
            for callee in targets:
                found = depth(callee)
                if found[0] > best[0]: best = found
        visiting.remove(name)
        memo[name] = (frames[name] + best[0], [name + "(" + str(frames[name]) + ")"] + best[1])
        return memo[name]
    for root in ("kui_vmu_native_passive_arm", "kui_vmu_native_passive_submit",
                 "kui_vmu_native_passive_complete", "kui_vmu_native_readonly_release_check",
                 "kui_vmu_native_passive_report", "kui_vmu_native_runtime_fault",
                 "kui_vmu_native_trace_game"):
        answer = max((depth(node) for node in resolve("worker", root)), key=lambda item: item[0])
        total = answer[0] + 80 + 64
        require(total <= 4096, f"private stack path {root} needs{total} bytes")
        paths[root] = {"c_frame_bytes": answer[0], "assembly_and_guard_margin": 144,
                       "conservative_bytes": total, "worst_path": answer[1]}
    require(null_sites, "missing independently guarded optional writer block callback")
    return {"paths": paths, "indirect_sites": len(indirect_sites),
            "null_writer_block_sites": [list(site) for site in sorted(null_sites)],
            "source_qualified_calls": True,
            "verified_zero_stack_assembly_leaves": ["kui_vmu_native_trace_irq"]}


def check_stacks(directory, worker):
    resident = inspect_elf((directory / "resident-sci.elf").read_bytes(), LOW, 0x8C007800)
    low_rows = linked_stack_rows(sorted((directory / "resident/lto").glob("*.ltrans*.su")), resident["symbols"])
    high_rows = linked_stack_rows(sorted((directory / "native").rglob("*.su")), worker[4])
    reported = {_function_base(name) for name, _ in high_rows}
    assembly = {"kui_vmu_native_passive_submit_hook", "kui_vmu_native_passive_complete_hook",
                "kui_vmu_native_passive_terminal", "kui_vmu_native_readonly_release_hook",
                "kui_vmu_native_runtime_fault_hook", "kui_vmu_native_allocation_failure_hook",
                "kui_vmu_native_trace_irq"}
    assembly.update(name[1:] for name, *_ in TRACE_GATES)
    required = {_function_base(name) for name, (_, _, kind, _) in worker[4].items()
                if kind == 2 and _function_base(name).startswith(("kui_", "durable_", "readonly_overlay_", "sci_storage_"))
                and _function_base(name) not in assembly}
    require(required <= reported, "missing linked native C stack frame: " + ", ".join(sorted(required - reported)))
    lease = [(name, frame) for name, frame in low_rows if _function_base(name) == "kui_vmu_observe_sd_lease"]
    require(len(lease) == 1, "missing unique worker-stack-only lease frame")
    # Every retained C frame is included, including low bus callbacks called
    # from the worker. Summing them overstates any single actual path.
    high_sum, low_sum = sum(frame for _, frame in high_rows), sum(frame for _, frame in low_rows)
    call_paths = worker_stack_paths(directory, worker, resident)
    total = max(path["conservative_bytes"] for path in call_paths["paths"].values())
    available = (resident_layout.stack_top("sci", False, True) -
                 resident_layout.stack_bottom("sci", False, True) -
                 resident_layout.STACK_GUARD_BYTES - resident_layout.STACK_ALIGNMENT_GAP)
    ordinary = low_sum - lease[0][1] + resident_layout.ASSEMBLY_STACK_BYTES
    require(ordinary <= available, f"ordinary native GD stack sum {ordinary} exceeds{available}")
    return {"worker_c_frame_sum": high_sum, "resident_callback_c_frame_sum": low_sum,
            "conservative_worker_bytes": total, "available_worker_bytes": 4096,
            "ordinary_low_stack_bytes": ordinary, "ordinary_low_available_bytes": available,
            "lease_only_frame_bytes": lease[0][1], "required_worker_frames": len(required), "callgraph": call_paths}


def check(directory, *, self_test=False, trace_startup=None):
    require(__debug__, "gateway model requires Python assertions")
    directory = Path(directory)
    base = inspect_base(directory, self_test=self_test,
                        objdump=shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump"))
    worker = read_elf(directory / "native-worker.elf")
    data, header, segments, sections, raw_symbols = worker
    symbols = {name: record[0] for name, record in raw_symbols.items()}
    binary = (directory / "native-worker.bin").read_bytes()
    wire = (directory / "native-worker.reloc").read_bytes()
    policy = worker_load_policy(worker, binary)
    cached, code_base = policy["cached_code"], policy["code_alias"]
    require(base['gd_map_refusal_terminal']['cached_worker_terminal']==cached,
            'low GD refusal terminal uses a different worker code alias')
    require(len(binary) >= 80, "worker header is truncated")
    fields = struct.unpack_from("<20I", binary)
    names = ("_kui_vmu_native_passive_arm", "_kui_vmu_native_passive_submit_hook",
             "_kui_vmu_native_passive_complete_hook", "_kui_vmu_native_passive_terminal",
             "__vmu_native_worker_bss_begin", "__vmu_native_worker_bss_end",
             "__vmu_native_worker_stack_bottom", "__vmu_native_worker_stack_top",
             "__vmu_native_worker_end", "_kui_vmu_native_passive_resume", "_kui_vmu_native_passive_consumer",
             "_kui_vmu_native_readonly_release_hook", "_kui_vmu_native_readonly_release_resume",
             "_kui_vmu_native_persistent_sd_transaction", "_kui_vmu_native_runtime_fault_hook",
             "_kui_vmu_native_allocation_failure_hook")
    require(fields[:3] == (0x4B4E5031, 3, 80) and
            fields[3:19] == tuple(symbols[name] for name in names) and fields[19] == symbols['_kui_vmu_native_trace_exports'],
            "persistent v3 worker exports differ from exact typed symbols")
    require(fields[3] == header[4] and all(not p & 1 and code_base + 80 <= p < code_base + len(binary) for p in fields[3:7]),
            "worker entry/hook points outside initialized code")
    require(P2 + len(binary) == fields[7] <= fields[8] <= fields[9] and
            fields[9:12] == (P2 + 0x1F000, P2 + SPAN, P2 + SPAN), "worker BSS/stack layout differs")
    address, exported, section = symbol_bytes(worker, "_kui_vmu_native_worker_exports", 1)
    require(address == P2 and exported == binary[:80] and section[2] & 7 == 2,
            "exports must be typed immutable header")
    trace_address, trace_exports, trace_section = symbol_bytes(worker, '_kui_vmu_native_trace_exports', 1)
    require(trace_address == fields[19] and trace_address % 4 == 0 and
            trace_section[2] & 7 == 2 and len(trace_exports) == 32 and
            struct.unpack('<8I', trace_exports) ==
            (0x4B545231, 1, 32, *(symbols[name] for name, *_ in TRACE_GATES), 0),
            'trace exports differ from exact typed immutable gateways')
    for name, *_ in TRACE_GATES:
        _, _, owner = symbol_bytes(worker, name, 2)
        require(owner[2] & 7 == 6, 'trace gateway is not executable')
    trace_audit = dict(exact_typed_exports=True, exact_image_only=True,
                       arbitrary_stall_pc_capture=False, startup_equivalence_checked=False)
    if trace_startup is not None:
        trace_audit.update(audit_trace_gates(directory, trace_startup, self_test=self_test),
                           startup_equivalence_checked=True)
    for name in names[:4] + (names[11], names[13], names[14], names[15]):
        _, _, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6, "exported entry is not executable")
    for name in ("_kui_vmu_native_passive_resume", "_kui_vmu_native_passive_consumer",
                 "_kui_vmu_native_readonly_release_resume"):
        address, cell, section = symbol_bytes(worker, name, 1)
        require(len(cell) == 4 and address % 4 == 0 and not any(cell) and section[2] & 7 == 3,
                "resume/consumer cells must be typed, zero-initialized worker-owned words")
    for section in sections:
        require(not section[2] & 2 or not section[5] or
                physical(P1) <= physical(section[3]) < physical(section[3]) + section[5] <= physical(P1) + SPAN,
                "allocated worker bytes escape physical lease span")
    for name, (value, _, symbol_kind, owner) in raw_symbols.items():
        require(symbol_kind not in (1, 2) or owner < len(sections) and
                physical(P1) <= physical(value) < physical(P1) + SPAN,
                "worker object/function escapes physical lease: " + name)
        if symbol_kind == 2:
            require(code_base <= value < code_base + len(binary), "function uses wrong cached/uncached alias: " + name)
        if symbol_kind == 1 and sections[owner][2] & 1:
            require(value & 0xFF000000 == 0xAC000000, "mutable object has a cached P1 alias: " + name)
        require(not name.startswith(("_kui_retail_storage_", "_kui_sci_sd_acquire", "_kui_sci_sd_release",
                                      "_kui_loader_sd_init", "_kui_loader_sd_adopt", "_kui_loader_sd_shutdown")),
                "persistent worker independently owns/reinitializes resident storage: " + name)
    for name in ("_kui_vmu_durable_init", "_kui_vmu_durable_handle", "_kui_vmu_durable_admit",
                 "_kui_vmu_durable_transaction", "_kui_vmu_store_open", "_kui_vmu_store_write",
                 "_kui_vmu_store_read", "_kui_sci_sd_write", "_kui_sci_sd_sync",
                 "_kui_vmu_readonly_overlay_prepare", "_kui_vmu_readonly_overlay_complete",
                 "_kui_vmu_volatile_chain_build", "_kui_vmu_volatile_chain_verify",
                 "_kui_vmu_native_readonly_release_check"):
        _, _, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6, "missing bounded persistent bridge implementation: " + name)
    require(generate(data, binary, P1, SPAN) == wire, "relocation table differs from exact ELF extraction")
    runtime_fault = check_runtime_fault_gateways(worker, binary, wire, self_test)
    prepared_lifetime = check_prepared_lifetime(directory, worker, binary, wire, self_test, build=base["runtime"]["build"])
    relocations = struct.unpack_from("<I", wire, 28)[0]
    for i in range(relocations):
        value = struct.unpack_from("<I", wire, 76 + i * 8)[0]
        area = value & 0xFF000000
        require(area in ((0x8C000000, 0xAC000000) if cached else (0xAC000000,)),
                "internal relocated pointer uses an unadmitted alias")
        if cached:
            code = policy["loads"][1]
            if area == 0x8C000000:
                require(code["first"] <= physical(value) <= code["first"] + code["memory"],
                        "cached pointer aliases mutable P2 state")
            else:
                require(not code["first"] <= physical(value) < code["first"] + code["memory"],
                        "uncached pointer aliases immutable cached code/constants")

    stage = read_elf(directory / "stage.elf")
    prepared = inspect_elf(stage[0], 0x8CE00000, 0x8CFE0000)
    stage_binary = (directory / "stage.bin").read_bytes()
    require(prepared["payload"] == stage_binary, "stage binary differs from ELF")
    publication = check_resume_publication(stage, cached, self_test)
    objdump = shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump")
    stage_disassembly = subprocess.check_output([objdump, "-d", str(directory / "stage.elf")], text=True)
    publisher = check_worker_publication(stage, cached, stage_disassembly, self_test)
    seed = audit_cache_seed(stage[0], (directory / "entry.elf").read_bytes(),
                            (directory / "retail-maple-observe.kui").read_bytes(), cached_code=cached)
    runtime_mode = audit_runtime_mode(stage[0], (directory / "entry.elf").read_bytes(),
        (directory / "retail-maple-observe.kui").read_bytes(), cached_code=cached,
        stage_image=stage,resident_image=read_elf(directory / "resident-sci.elf"),self_test=self_test)
    cache_gateway = check_cache_gateway(stage, self_test=self_test) if seed["enabled"] else {"enabled": False}
    installer_stack = check_stage_stack(stage, read_elf(directory / "resident-sci.elf"),
                                        worker=worker, self_test=self_test)
    require(not any(name.startswith(("__native_resolution_cache_seed_", "_kui_vmu_native_resolution_cache_",
                                    "_native_cached_resolution", "_native_resolution_replay"))
                    for name in symbols), "temporary resolution cache is retained in the gameplay worker")
    for begin, end, payload in (("__native_worker_blob_begin", "__native_worker_blob_end", binary),
                                ("__native_worker_reloc_begin", "__native_worker_reloc_end", wire)):
        a, b = stage[4][begin][0], stage[4][end][0]
        require(b - a == len(payload) and region(stage_binary, a - 0x8CE00000, b - a, begin) == payload,
                "stage embeds different worker/relocation bytes")
        require(any(s[1] == 1 and s[2] & 7 == 2 and s[3] <= a < b <= s[3] + s[5] for s in stage[3]),
                "embedded worker must occupy stage immutable data")
    gateway_address, gateway, _ = symbol_bytes(stage, "_kui_vmu_native_worker_call", 2)
    stage_cases = (model_cached_stage if cached else model_gateway)(gateway_address, gateway)
    hook_cases = (model_cached_hooks if cached else model_hooks)(binary, wire, symbols)
    objdump = shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump")
    disassembly = subprocess.check_output([objdump, "-d", str(directory / "native-worker.elf")], text=True)
    try:
        instructions = instruction_audit("native-readonly", disassembly)
    except SystemExit as error:
        raise ValueError(str(error)) from error
    hook_ranges = [symbol_bytes(worker, name, 2)[:2] for name in
                   names[1:4] + (names[11], names[14]) + tuple(gate[0] for gate in TRACE_GATES)]
    for address, _, mnemonic, operands in literal_aware_instructions(disassembly):
        require(mnemonic not in ("trapa", "sleep", "rte", "ldtlb"), "unexpected privileged worker instruction")
        if mnemonic in ("ldc", "ldc.l") and operands.split("!", 1)[0].strip().endswith(",sr"):
            require(any(begin <= address < begin + len(body) for begin, body in hook_ranges),
                    "C worker changes SR outside audited gateways")
    pio = check_pio_writers(directory, worker)
    stacks = check_stacks(directory, worker)
    source = check_source(directory, base["runtime"])
    report = {"schema": "kui.native.persistent.probe.artifact.v1", "allocator_bootstrap_audit": base,
              "module_sha256": base["module_sha256"], "runtime": base["runtime"],
              "worker_bytes": len(binary), "worker_span": SPAN, "worker_stack_bytes": 4096,
              "worker_load_policy": policy, "cached_code": cached,
              "prepared_lifetime_admission": prepared_lifetime,
              "failure_only_gateways": runtime_fault,
              "evolution_consolidated_trace": trace_audit,
              "stage_cache_publication": publication,
              "worker_icache_publication": publisher,
              "resolution_cache_seed": seed,
              "runtime_mode": runtime_mode,
              "resolution_cache_gateway": cache_gateway,
              "temporary_installer_stack": installer_stack,
              "relocation_records": relocations, "worker_instructions": instructions,
              "stage_gateway_model_cases": stage_cases, "persistent_gateway_model_cases": hook_cases,
              "original_release_mask_model_cases": hook_cases // 4,
              "stage_embedded_worker_verified": True, "maple_interception_installed": True,
              "virtual_replies_enabled": True, "storage_writes_enabled": True, "scoped_sci_transactions": True,
              "source_provenance": source, "stack_audit": stacks, "pio_writer_audit": pio,
              "console_execution_verified": False, "expected_phase": "native persistent session"}
    if self_test:
        report["gateway_mutation_rejections"] = (model_cached_mutations if cached else model_mutations)(binary, symbols)
        report["artifact_mutation_rejections"] = mutation_checks(directory)
    return report


def mutation_checks(directory):
    """Coherent changed flat images still must fail their typed ELF binding."""
    checks = 0
    with tempfile.TemporaryDirectory() as temp:
        target = Path(temp)
        for path in directory.iterdir():
            if path.is_file() and path.suffix in (".elf", ".bin", ".reloc", ".kui"):
                (target / path.name).symlink_to(path.resolve())
        for name, at in (("native-worker.bin", 0), ("native-worker.bin", 12),
                         ("native-worker.reloc", 72), ("stage.bin", 128)):
            path = target / name
            original = (directory / name).read_bytes()
            changed = bytearray(original); changed[at] ^= 1
            path.unlink(); path.write_bytes(changed)
            try:
                check(target)
            except (ValueError, AssertionError, KeyError, struct.error):
                checks += 1
            else:
                raise ValueError("modified artifact accepted: " + name)
            path.unlink(); path.symlink_to((directory / name).resolve())
    return checks



def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--output", type=Path)
    p.add_argument("--trace-startup", type=Path)
    args = p.parse_args()
    try:
        result = json.dumps(check(args.directory, self_test=args.self_test, trace_startup=args.trace_startup), indent=2) + "\n"
    except (OSError, ValueError, KeyError, AssertionError, struct.error, subprocess.CalledProcessError) as error:
        p.exit(2, f"Native persistent artifact audit refused: {error}\n")
    if args.output: args.output.write_text(result)
    print(result, end="")


if __name__ == "__main__":
    main()
