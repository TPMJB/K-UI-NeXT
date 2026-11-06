#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute an original SDK shutdown between capture and linked completion.

This is an artifact-bound instruction/ownership regression, not a console or
secondary-executable compatibility claim. Original bytes remain external. The
only hardware models are synchronous physical Maple replies, named readable
registers and a monotonically decrementing TMU counter. No worker predicate,
original finish routine or timer helper is replaced by a successful stub.
Historical compiled profile maps are neither imported nor extended.
"""
from pathlib import Path
import argparse
from collections import Counter
import copy
import hashlib
import json
import struct
import subprocess
import sys
import re

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tests")]
from profile_vmu_native_record_worker_sh import (  # noqa: E402
    Record, RecordWorker, OWNER_STACK, RETURN, fresh_verify, make_handoff)
from test_vmu_native_heap_execution import SH, MASK, signed  # noqa: E402
from native_runtime_layout import GENERAL, BACKGROUND_COMPACT, layout_for  # noqa: E402
from check_vmu_native_worker_gateway import read_elf, symbol_bytes  # noqa: E402
from vmu_native_relocations import generate  # noqa: E402
from check_loader_layout import SYM  # noqa: E402

ONE = (1, 0, 0, 0xfff1)
MDSTAR, MDTSEL, MDEN, MDST, MSYS, MDAPRO = (
    0xa05f6c04, 0xa05f6c10, 0xa05f6c14, 0xa05f6c18, 0xa05f6c80, 0xa05f6c8c)
TMU = 0xffd8000c
READABLE = (MDST, MDEN, MDTSEL, MSYS, MDSTAR)
STOPPED = (0, 0, 0, 0xc3500000, 0x0cff0000)
MSYS_MASK = 0xffff130f
STATE = dict(armed=0, pending=4, submitted=8, completed=12, source=20,
             fault_at=64, fault_value=68, fault_expect=72, runtime_result=76,
             proof=80)
CONFIG_RUNTIME = 672
CONFIG_LEASE = 116
WATCH_NAMES = ("_reader_cooperative_service", "_reader_readahead_visit",
               "_reader_region_get", "_native_session_controller_complete",
               "_kui_vmu_native_passive_sd_transaction",
               "_kui_vmu_native_persistent_sd_transaction", "_persistent_lease",
               "_kui_vmu_sd_lease_request", "_kui_vmu_sd_lease_restore")
# Independently authored from the source's complete shutdown grammar. Symbol
# suffixes are compiler bookkeeping; owner/type/bytes and exact pointer starts
# are mandatory. These are additional objects, never a broad .rodata mask.
FINISH_BODY = (
    0x2fe6,0x2fd6,0x2fc6,0x2fb6,0x2fa6,0x2f96,0x4f22,0xdd00,
    0x4d0b,0x0009,0xdb00,0x6903,0xde00,0xda00,0xa007,0xec01,
    0x4d0b,0x0009,0x6503,0x4b0b,0x6493,0x30a7,0x8905,0x9000,
    0x62e2,0x032e,0x5136,0x21c8,0x8bf2,
    0x9000,0xe400,0x62e2,0x032e,0x1346,0x63e2,0x023e,0x1245,
    0x63e2,0x023e,0x1244,0x63e2,0xd100,0x023e,0x9000,0x0216,
    0x9000,0x63e2,0xd100,0x023e,0x9000,0x0216,0x9000,0x63e2,
    0xd100,0x023e,0x4d0b,0x1211,
    0x9a00,0xa007,0x6903,0x4d0b,0x0009,0x6503,0x4b0b,0x6493,
    0x30a7,0x8905,0x9000,0x62e2,0x032e,0x5136,0x21c8,0x8bf2,
    0x4f26,0xe000,0x69f6,0x6af6,0x6bf6,0x6cf6,0x6df6,0x000b,0x6ef6)
LONG_ROLES = (14,20,24,26,82,94,106)
WORD_ROLES = ((46,0x10f4),(58,0x10f4),(86,0x8c),(90,0x10f4),
              (98,0x80),(102,0x10f4),(114,0x4268),(134,0x10f4))
OBJECT_BYTES = {
    "body": struct.pack("<82H", *FINISH_BODY),
    "longs": bytes(LONG_ROLES),
    "words": b"".join(struct.pack("<BxH", *row) for row in WORD_ROLES),
    "timer": struct.pack("<5H",0xd200,0xe0ff,0x6322,0xb,0x3038),
    "difference": struct.pack("<3H",0x6053,0xb,0x3048)}
# Actual72KiB A1/A2 SH review: exact literal sites and their executing MOV.L
# consumers. A dead duplicate pointer elsewhere cannot satisfy these roles.
OBJECT_BINDINGS = {
    "body": ("_controller_shutdown_source@vmu_native_passive.c", 0xc8, 0x4e, 0xd21e),
    "longs": ("_controller_shutdown_source@vmu_native_passive.c", 0xd8, 0x86, 0xde14),
    "words": ("_controller_shutdown_source@vmu_native_passive.c", 0x1e0, 0xf6, 0xd83a),
    "timer": ("_controller_shutdown_source@vmu_native_passive.c", 0x1e4, 0x136, 0xdc2b),
    "difference": ("_controller_shutdown_source@vmu_native_passive.c", 0x1f0, 0x16c, 0xd920)}
# The current compiler unrolls the readable-register table into this complete
# no-stack/no-call leaf. Bind all instructions and literal registers/mask; the
# expected MDSTAR and MDEN remain the source/enabled argument registers.
MMIO_STATE = bytes.fromhex(
    "0ed200e022611821158b0dd112611035118b0cd1126118210d8b0bd10bd312613921"
    "0bd33031068b0ad112611034028b2261182129000b0009000900186c5fa0146c5fa0"
    "106c5fa0806c5fa00f13ffff000050c3046c5fa0")
MMIO_NAME = "_controller_shutdown_mmio_state@vmu_native_passive.c"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def span(image, first, last):
    begin, end = image[4][first][0], image[4][last][0]
    sections = [section for section in image[3] if section[1] == 1 and section[2] & 2 and
                section[3] <= begin < end <= section[3]+section[5]]
    require(len(sections) == 1, "Embedded span is not uniquely owned: "+first)
    section = sections[0]
    return image[0][section[4]+begin-section[3]:section[4]+end-section[3]]


def typed_records(image):
    data, _, _, sections, _ = image
    result = {}
    for table in sections:
        if table[1] != 2:
            continue
        strings = sections[table[6]]
        names = data[strings[4]:strings[4]+strings[5]]
        source = ""
        for at in range(table[4], table[4]+table[5], SYM.size):
            name_at, address, size, info, _, owner = SYM.unpack_from(data, at)
            name = names[name_at:].split(b"\0", 1)[0].decode("ascii")
            kind = info & 15
            if kind == 4:
                source = name
            if kind not in (1, 2) or not size or not 0 < owner < len(sections):
                continue
            section = sections[owner]
            if section[1] != 1:
                continue
            require(section[3] <= address <= section[3]+section[5] and
                    size <= section[3]+section[5]-address,
                    "Typed symbol exceeds its initialized owner: "+name)
            qualified = name if info >> 4 else name+"@"+source
            require(qualified not in result, "Duplicate source-qualified typed symbol: "+qualified)
            begin = section[4]+address-section[3]
            result[qualified] = (address, data[begin:begin+size], kind, section)
    return result


def shutdown_objects(worker):
    records = typed_records(worker)
    result = {}
    for role, expected in OBJECT_BYTES.items():
        choices = [(name, value) for name, value in records.items()
                   if re.fullmatch("_"+role+r"\.\d+@vmu_native_passive\.c", name)]
        require(len(choices) == 1, "Shutdown immutable role is missing or ambiguous: "+role)
        name, (address, body, kind, section) = choices[0]
        require(kind == 1 and section[1] == 1 and section[2] & 7 == 2 and body == expected,
                "Shutdown immutable role has changed bytes/type/permissions: "+name)
        result[role] = dict(symbol=name, address=address, bytes=len(body), sha256=digest(body))
    # All internal pointers also have the complete typed relocation companion.
    # The exact source-qualified call site/consumer below adds semantic roles.
    for role, object_ in result.items():
        function, offset, consumer, opcode = OBJECT_BINDINGS[role]
        require(function in records, "Shutdown literal's function is missing: "+role)
        address, body, kind, section = records[function]
        require(kind == 2 and section[2] & 7 == 6 and offset+4 <= len(body) and
                consumer+2 <= len(body) and not (address+offset)&3 and
                struct.unpack_from("<I", body, offset)[0] == object_["address"] and
                struct.unpack_from("<H", body, consumer)[0] == opcode and
                ((address+consumer+4)&~3)+(opcode&255)*4 == address+offset,
                "Shutdown exact typed literal/consumer binding differs: "+role)
        object_["code_bindings"] = [dict(function=function, offset=offset,
            consumer_offset=consumer, consumer_opcode=opcode, target=object_["symbol"])]
    return result


def shutdown_mmio_leaf(worker):
    available = typed_records(worker)
    require(MMIO_NAME in available, "Shutdown readable-register leaf is missing")
    address, body, kind, section = available[MMIO_NAME]
    require(kind == 2 and section[2] & 7 == 6 and body == MMIO_STATE,
            "Shutdown readable-register instructions/constants differ")
    return dict(symbol=MMIO_NAME, address=address, bytes=len(body), sha256=digest(body),
                no_stack_or_calls=True, write_only_register_read=False)


def object_self_tests(worker):
    objects = shutdown_objects(worker)
    available = typed_records(worker)
    byte_bits = bindings = permissions = consumers = types = 0
    def refused(changed, message):
        try:
            shutdown_objects(changed)
        except ValueError:
            return
        raise AssertionError("Shutdown object proof accepted "+message)
    for role, item in objects.items():
        address, body, _, section = available[item["symbol"]]
        begin = section[4]+address-section[3]
        for offset in range(len(body)):
            for bit in range(8):
                data = bytearray(worker[0]); data[begin+offset] ^= 1 << bit
                refused((bytes(data), *worker[1:]), role+" bit mutation"); byte_bits += 1
        sections = list(worker[3]); at = sections.index(section)
        changed = list(section); changed[2] |= 1; sections[at] = tuple(changed)
        refused((worker[0], worker[1], worker[2], sections, worker[4]), role+" writable owner")
        permissions += 1
        # Remove all exact-start bindings for this one role. Other roles and
        # object bytes stay unchanged; an interior target cannot stand in for it.
        data = bytearray(worker[0])
        for binding in item["code_bindings"]:
            source, _, _, owner = available[binding["function"]]
            offset = owner[4]+source-owner[3]+binding["offset"]
            struct.pack_into("<I", data, offset, address+2)
        refused((bytes(data), *worker[1:]), role+" interior pointer binding"); bindings += 1
        for binding in item["code_bindings"]:
            source, _, _, owner = available[binding["function"]]
            offset = owner[4]+source-owner[3]+binding["consumer_offset"]
            data = bytearray(worker[0]); data[offset] ^= 1
            refused((bytes(data), *worker[1:]), role+" altered MOV.L consumer"); consumers += 1
        for table in worker[3]:
            if table[1] != 2:
                continue
            strings = worker[3][table[6]]; names = worker[0][strings[4]:strings[4]+strings[5]]
            source = ""
            for offset in range(table[4], table[4]+table[5], SYM.size):
                label_at, _, _, info, _, _ = SYM.unpack_from(worker[0], offset)
                label = names[label_at:].split(b"\0", 1)[0].decode("ascii")
                if info & 15 == 4:
                    source = label
                qualified = label if info >> 4 else label+"@"+source
                if qualified == item["symbol"]:
                    data = bytearray(worker[0]); data[offset+12] = (info & 0xf0) | 2
                    refused((bytes(data), *worker[1:]), role+" function-typed data"); types += 1
    address, body, _, section = available[MMIO_NAME]
    begin = section[4]+address-section[3]
    leaf_refusals = 0
    for byte in range(len(body)):
        data = bytearray(worker[0]); data[begin+byte] ^= 1
        try:
            shutdown_mmio_leaf((bytes(data), *worker[1:]))
        except ValueError:
            leaf_refusals += 1
        else:
            raise AssertionError("Readable-register leaf accepted a changed byte")
    return dict(object_bits_refused=byte_bits, interior_pointer_roles_refused=bindings,
                changed_movl_consumers_refused=consumers,
                mutable_owner_refusals=permissions, symbol_type_refusals=types,
                mmio_leaf_instruction_or_literal_bytes_refused=leaf_refusals)


def artifact_contract(build, allow_development=False, expected_commit=None):
    names = ("stage.elf", "native-worker.elf", "native-worker.bin", "native-worker.reloc",
             "entry.elf", "entry.compile.json")
    files = {name: (build / name).read_bytes() for name in names}
    worker, stage = read_elf(build / "native-worker.elf"), read_elf(build / "stage.elf")
    w, s = worker[4], stage[4]
    compile_ = json.loads(files["entry.compile.json"])
    require(type(compile_.get("source_dirty")) is bool and
            re.fullmatch(r"[0-9a-f]{40}", compile_.get("commit", "")) and
            compile_.get("elf_sha256") == digest(files["entry.elf"]),
            "Entry compile provenance does not bind the actual entry ELF")
    require(not compile_["source_dirty"] or allow_development,
            "Dirty development artifacts require --allow-development-artifacts")
    require(expected_commit is None or compile_["commit"] == expected_commit,
            "Entry was not compiled at the requested source commit")
    selected = layout_for(w)
    require(selected is BACKGROUND_COMPACT and layout_for(s) is selected,
            "This reviewed shutdown closure requires its actual72KiB compact geometry")
    require(w.get("__kui_native_general_lean_worker_layout") == ONE and
            w.get("__kui_native_isolation_lease_bytes") == (selected.LEASE_BYTES, 0, 0, 0xfff1) and
            w.get("__kui_native_worker_span") == (selected.WORKER_BYTES, 0, 0, 0xfff1),
            "General worker geometry markers are incomplete")
    require(not any("session_controls" in name or "session_virtual_slot" in name for name in w),
            "Session controls need their separate profile proof")
    require(w.get("__kui_native_bottom_heap_worker") == ONE and
            s.get("__kui_native_bottom_heap_stage") == ONE,
            "Paired original bottom-allocation policy is required")
    _, exports, _ = symbol_bytes(worker, "_kui_vmu_native_worker_exports", 1)
    words = struct.unpack("<20I", exports)
    require(words[:3] == (0x4b4e5031, 4, 80), "Persistent worker exports ABI changed")
    top = 0xac100000+selected.WORKER_BYTES
    require(words[9:12] == (top-selected.WORKER_STACK_BYTES, top, top),
            "General worker private stack geometry changed")
    require(w["__vmu_native_worker_bss_end"][0] <= words[9], "Worker BSS reaches its stack")
    require(span(stage, "__native_worker_blob_begin", "__native_worker_blob_end") ==
            files["native-worker.bin"], "Stage embeds a different worker")
    require(generate(files["native-worker.elf"], files["native-worker.bin"],
                     0x8c100000, selected.WORKER_BYTES) == files["native-worker.reloc"],
            "Worker relocation companion is not its complete typed extraction")
    # A slot is a complete immutable compiled choice, never a CLI override.
    if w.get("__kui_vmu_virtual_a1_worker") == ONE:
        slot = 1
    elif w.get("__kui_vmu_virtual_a2_worker") == ONE:
        slot = 2
    else:
        # Existing ordinary builds name the slot via their one-byte immutable
        # virtual-address object; absence must be resolved explicitly below.
        slot = None
    return worker, stage, files, dict(worker_bytes=selected.WORKER_BYTES,
        lease_bytes=selected.LEASE_BYTES, worker_stack_bytes=selected.WORKER_STACK_BYTES,
        bss_end=hex(w["__vmu_native_worker_bss_end"][0]),
        stack_bottom=hex(words[9]), space_before_stack=words[9]-w["__vmu_native_worker_bss_end"][0],
        stage_embedded_worker=True, typed_relocation_companion=True, marker_slot=slot,
        entry_compile_provenance=compile_)


class ShutdownWorker(RecordWorker):
    """Real hook instruction execution plus separately observed C boundaries."""
    def __init__(self, startup, build, handoff, record):
        self.timer = MASK
        self.busy_reads = 0
        self.observe = False
        self.inside_worker = False
        self.alias_views = []
        self.raw_reads, self.raw_writes = [], []
        self.watched_entries = Counter()
        self.worker_entries = []
        self.actual_gateways = False
        self.c_return = None
        self.last_worker = None
        self.negative_mmio = None
        self.disabled_start_attempts = []
        self.mmio_occurrences = Counter()
        self.gbr = self.mach = 0
        self.geometry = layout_for(read_elf(build / "native-worker.elf")[4])
        super().__init__(startup, build, handoff, record)

    def fork(self):
        trial = copy.copy(self)
        trial.memory = bytearray(self.memory)
        trial.mmio, trial.r = dict(self.mmio), self.r.copy()
        trial.writes, trial.mmio_reads, trial.mmio_writes = [], [], []
        trial.alias_views = list(self.alias_views)
        trial.disabled_start_attempts = list(self.disabled_start_attempts)
        trial.events_reset()
        return trial

    def events_reset(self):
        self.raw_reads, self.raw_writes = [], []
        self.watched_entries, self.mmio_occurrences = Counter(), Counter()

    def worker(self, name, args):
        if name == "_kui_vmu_native_passive_arm":
            # Existing generic replay used the stronger historical128KiB
            # minimum. Use this actual compiled profile's authored minimum.
            self.write(args[0]+CONFIG_RUNTIME+20, self.geometry.WORKER_BYTES)
        return super().worker(name, args)

    def bridge_submit(self):
        # The old replay folds this assembly seam. This tool executes the
        # actual12-byte patch and the complete linked hook instead.
        require(self.actual_gateways, "Unexpected submit bridge during cold preparation")

    def read(self, address, count=4):
        if address == MDAPRO:
            raise AssertionError("Write-only MDAPRO was read")
        if self.observe:
            self.raw_reads.append((self.pc, address, count))
        if address == TMU:
            require(count == 4, "Timer width changed")
            self.timer = self.timer-1 & MASK
            return self.timer
        if address in READABLE:
            self.mmio_occurrences[address] += 1
            if address == MDST and self.busy_reads:
                self.busy_reads -= 1
                if not self.busy_reads:
                    self.mmio[MDST] = 0
            if self.negative_mmio and (address, self.mmio_occurrences[address]) == self.negative_mmio[:2]:
                return self.negative_mmio[2]
        if self.inside_worker and address & 0xe0000000 == 0x80000000:
            for begin, data in self.alias_views:
                if begin <= address and address+count <= begin+len(data):
                    return int.from_bytes(data[address-begin:address-begin+count], "little")
        return super().read(address, count)

    def write(self, address, value, count=4):
        if self.observe:
            self.raw_writes.append((self.pc, address, value & ((1 << (8*count))-1), count))
        if address == MDST and value == 1 and not self.mmio.get(MDEN, 0):
            require(count == 4, "Disabled DMA start width changed")
            self.mmio_writes.append((self.executing, address, 1))
            self.mmio[MDST] = 0
            self.disabled_start_attempts.append(dict(pc=hex(self.pc),
                requested_mdst=1, effective_mdst=0, mden=0))
            return
        return super().write(address, value, count)

    def plain(self, op, pc):
        # Exact SH control-register stack forms used by the real hooks. There
        # is no FPU state or IRQ scheduling in this flat integer model.
        n = op >> 8 & 15
        push = {0x4002: "mach", 0x4012: "macl", 0x4013: "gbr"}
        pop = {0x4006: "mach", 0x4016: "macl", 0x4017: "gbr"}
        form = op & 0xf0ff
        if form in push:
            self.r[n] = self.r[n]-4 & MASK
            self.write(self.r[n], getattr(self, push[form])); self.instructions += 1
        elif form in pop:
            setattr(self, pop[form], self.read(self.r[n]))
            self.r[n] = self.r[n]+4 & MASK; self.instructions += 1
        else:
            super().plain(op, pc)

    def observe_begin(self):
        self.events_reset()
        self.observe = True
        self.active = True
        self.private_reads = self.private_writes = self.guest_reads = 0
        self.data_reads = self.data_writes = self.instruction_fetch_reads = self.private_p2_data_reads = 0
        self.access_reads, self.access_writes = Counter(), Counter()
        self.p1_stack_reads = self.p1_stack_writes = 0
        self.function_instructions, self.function_guest_reads = Counter(), Counter()
        self.function_p2_reads, self.function_p2_writes = Counter(), Counter()
        self.unknown_instruction_pcs = Counter()
        self.minimum_worker_sp = self.r[15] & 0x1fffffff

    def observation(self, start):
        data = [(pc, address, width) for pc, address, width in self.raw_reads
                if not (width == 2 and address in (pc, pc+2))]
        guest = [(pc, address, width) for pc, address, width in data if not
                 self.worker_physical_low <= address & 0x1fffffff < self.worker_physical_high]
        retained_reader = [(pc, address, width) for pc, address, width in data if
            self.worker_physical_low+self.geometry.WORKER_BYTES <= address & 0x1fffffff <
            self.worker_physical_low+self.geometry.LEASE_BYTES]
        return dict(instructions=self.instructions-start, reads=list(self.raw_reads),
                    writes=list(self.raw_writes), watched=dict(self.watched_entries),
                    executed_functions=dict(self.function_instructions),
                    guest_data_reads=len(guest), all_data_reads=len(data),
                    guest_read_histogram=[(address, width, count) for (address, width), count in
                        sorted(Counter((a, n) for _, a, n in guest).items())],
                    rx_data_reads=sum(any(lo <= address & 0x1fffffff < hi
                        for lo, hi in self.record.rx_ranges) for _, address, _ in data),
                    retained_reader_region_cache_reads=len(retained_reader),
                    observed_private_stack_bytes=self.stack_physical_high-self.minimum_worker_sp)

    def step(self):
        if self.observe:
            for name in WATCH_NAMES:
                if name in self.symbols and self.pc == self.symbols[name][0]:
                    self.watched_entries[name] += 1
        super().step()
        if self.observe:
            self.minimum_worker_sp = min(self.minimum_worker_sp, self.r[15] & 0x1fffffff)

    def start_real_hooks(self):
        self.original_ranges = []
        self.actual_gateways = True

    def original_until(self, entry, args=(), stop=RETURN, limit=2000000):
        self.pc, self.pr, self.r[15], self.sr = entry, RETURN, OWNER_STACK, self.owner_sr
        self.r[4:4+len(args)] = args
        before = self.instructions
        self.until(stop, limit)
        return self.instructions-before

    def submit_actual(self):
        r = self.record
        total_start = self.instructions
        self.pc, self.pr, self.r[15], self.sr = r.submit_caller, RETURN, OWNER_STACK, self.owner_sr
        self.until(r.submit_entry)
        require(self.pr == r.submit_caller+6, "Original submit caller PR changed")
        self.until(r.submit_hook)
        require(self.read(r.submit_hook, 2) >> 12 == 13, "Actual submit patch is missing")
        exports = self.symbols["_kui_vmu_native_worker_exports"][0]
        self.until(self.read(exports+16))
        targets = [self.symbols[n][0] for n in ("_kui_vmu_native_passive_split_submit",
            "_kui_vmu_native_passive_submit") if n in self.symbols]
        for _ in range(1000):
            if self.pc in targets:
                break
            self.step()
        else:
            raise AssertionError("Actual submit gateway did not enter its C callback")
        self.submit_frame = self.r[4]
        self.submit_frame_bytes = self.block(self.submit_frame, 76)
        self.submit_c_entry, self.submit_c_return = self.pc, self.pr
        self.submit_boundary_before = snapshot(self)
        self.inside_worker = True
        self.observe_begin()
        c_start = self.instructions
        self.until(self.submit_c_return)
        observed = self.observation(c_start)
        self.last_submit_observation = observed
        self.observe = self.active = self.inside_worker = False
        self.until(r.submit_caller+6)
        require(self.r[0] == 0, "Actual original submit did not return success")
        pair = self.symbols["_controller_pair"][0]
        return dict(c_entry=hex(self.submit_c_entry), frame=hex(self.submit_frame),
                    frame_pr=hex(struct.unpack_from("<I", self.submit_frame_bytes, 12)[0]),
                    frame_sr=hex(struct.unpack_from("<I", self.submit_frame_bytes, 68)[0]),
                    pending=self.read(pair), bank=self.read(pair+4), source=hex(self.read(pair+8)),
                    callback_instructions=observed["instructions"],
                    callback_guest_data_reads=observed["guest_data_reads"],
                    callback_guest_read_histogram=observed["guest_read_histogram"],
                    full_original_submit_and_gateway_instructions=self.instructions-total_start)

    def completion_entry(self):
        r = self.record
        self.completion_total_start = self.instructions
        self.pc, self.pr, self.r[15], self.sr = r.completion_wrapper, RETURN, OWNER_STACK, self.owner_sr
        self.until(self.read(r.driver[12]))
        require(self.pr == r.driver[13]+4, "Original completion wrapper PR changed")
        self.owner_before_hook = (self.r.copy(), self.pr, self.sr, self.macl, self.mach, self.gbr)
        target = self.symbols["_kui_vmu_native_passive_complete"][0]
        self.until(target)
        self.frame_address = self.r[4]
        self.complete_c_return = self.pr
        frame = self.block(self.frame_address, 80)
        regs, pr, sr, macl, mach, gbr = self.owner_before_hook
        expected = struct.pack("<20I", gbr, macl, mach, pr, *regs[14:1:-1], sr, regs[1], regs[0])
        require(frame == expected, "Actual complete gateway did not preserve its full owner frame")
        return dict(frame=hex(self.frame_address), pr=hex(pr), sr=hex(sr), all20_saved_words_equal=True)

    def complete_actual(self):
        self.inside_worker = True
        self.observe_begin()
        start = self.instructions
        self.until(self.complete_c_return)
        result = self.observation(start)
        self.observe = self.active = self.inside_worker = False
        self.until(self.record.consumer)
        regs, pr, sr, macl, mach, gbr = self.owner_before_hook
        # R3 is the authored tail-jump target. All other owner state is restored.
        require(all(self.r[i] == regs[i] for i in range(16) if i != 3) and
                (self.pr, self.sr, self.macl, self.mach, self.gbr) == (pr, sr, macl, mach, gbr),
                "Actual completion gateway damaged owner state before original consumer")
        result["actual_gateway_restored_owner_frame"] = True
        result["actual_tail_target"] = hex(self.pc)
        start = self.instructions
        self.until(pr)
        result["actual_original_consumer_instructions"] = self.instructions-start
        result["wrapper_prefix_gateway_callback_consumer_instructions"] = self.instructions-self.completion_total_start
        require(self.r[15] == regs[15], "Original consumer did not restore caller SP")
        return result


def state_words(cpu):
    address = cpu.symbols["_state"][0]
    return {name: cpu.read(address+offset) for name, offset in STATE.items()}


def snapshot(cpu):
    r = cpu.record
    private = {}
    for name in ("_held_report", "_session", "_durable_card"):
        if name in cpu.symbols:
            address, size = cpu.symbols[name][:2]
            private[name] = cpu.block(address, size)
    return dict(rx=cpu.rx_snapshot(), tx=cpu.block(r.tx, 0x6000),
                mmio={address: cpu.mmio.get(address, 0) for address in (*READABLE, MDAPRO)},
                state=state_words(cpu), private=private)


def no_publication(cpu, before, result):
    require(all(cpu.worker_physical_low <= address & 0x1fffffff and
                (address & 0x1fffffff)+width <= cpu.worker_physical_low+cpu.geometry.WORKER_BYTES
                for _, address, _, width in result["writes"]),
            "Cancelled C completion wrote outside its worker/private stack")
    require(cpu.rx_snapshot() == before["rx"] and cpu.block(cpu.record.tx, 0x6000) == before["tx"],
            "Cancelled completion modified physical RX/TX")
    owner_writes = [(pc, address, width) for pc, address, _, width in result["writes"]
        if cpu.record.tx <= address & 0x1fffffff < cpu.record.tx+0x6000 or
        any(lo <= address & 0x1fffffff < hi for lo, hi in cpu.record.rx_ranges)]
    require(not owner_writes, "Cancelled completion wrote an owner RX/TX location")
    require(not any(address in (*READABLE, MDAPRO) for _, address, _, _ in result["writes"]),
            "Cancelled completion wrote Maple MMIO")
    require(not result["watched"], "Cancelled completion entered reader/storage/session work")
    require(not result["retained_reader_region_cache_reads"],
            "Cancelled completion read the retained reader/cache reservation")
    require(not any(address == TMU for _, address, _ in result["reads"]),
            "Cancelled completion invoked a timer helper instead of proving its source")
    require(all(cpu.block(cpu.symbols[name][0], len(value)) == value
                for name, value in before["private"].items()),
            "Cancelled completion modified hold/session/durable state")
    require(all(cpu.mmio.get(address, 0) == value for address, value in before["mmio"].items()),
            "Cancelled completion changed hardware")


def original_finish(cpu, busy=0):
    entry = cpu.record.consumer+0xa2
    before = snapshot(cpu)
    manager = cpu.read(cpu.record.driver[9])
    original_pending = cpu.read(manager+0xfd8)
    cpu.busy_reads = busy
    cpu.mmio[MDST] = int(bool(busy))
    cpu.mmio_writes = []
    steps = cpu.original_until(entry)
    expected = [(MDST, 0), (MDEN, 0), (MDTSEL, 0), (MDAPRO, 0x6155404f),
                (MSYS, 0xc3500000), (MDSTAR, 0x0cff0000)]
    require([(address, value) for _, address, value in cpu.mmio_writes] == expected,
            "Original finish did not perform the reviewed six shutdown stores")
    require(cpu.rx_snapshot() == before["rx"] and cpu.block(cpu.record.tx, 0x6000) == before["tx"],
            "Original finish changed physical packets")
    require(cpu.read(manager+0xfd8) == original_pending,
            "Original finish no longer preserves pending RAM")
    return dict(entry=hex(entry), instructions=steps, busy_reads=busy,
                actual_timer_helpers=[hex(cpu.record.literal(entry+14, 13)[1]),
                                      hex(cpu.record.literal(entry+20, 11)[1])],
                unchanged_pending_word=hex(original_pending),
                hardware_stores=[dict(pc=hex(pc), address=hex(address), value=hex(value))
                                 for pc, address, value in cpu.mmio_writes])


def mutation_cases(baseline, slot):
    """Fork only after actual capture/finish/gateway frame construction."""
    record = baseline.record
    config = baseline.symbols["_config"][0]
    pair = baseline.symbols["_controller_pair"][0]
    manager = baseline.read(record.driver[9])
    bank = baseline.read(pair+4)
    rx = record.rx_slots[bank*24]
    results, positives = [], []

    def run(name, mutate, accepted=False):
        trial = baseline.fork()
        mutate(trial)
        before = snapshot(trial)
        try:
            complete = trial.complete_actual()
        except AssertionError as error:
            trial.observe = trial.active = trial.inside_worker = False
            require(not accepted and str(error).startswith("Actual worker refused with code "),
                    name+" failed outside the nonreturning refusal boundary: "+str(error))
            observed = trial.observation(0)
            no_publication(trial, before, observed)
            require(state_words(trial)["pending"] == before["state"]["pending"],
                    name+" retired an invalid capture")
            results.append(dict(case=name, refusal=str(error), rx_writes=0, tx_writes=0,
                                mmio_writes=0, reader_storage_session_entries=0))
        else:
            require(accepted, "Actual linked cancellation accepted "+name)
            no_publication(trial, before, complete)
            require(state_words(trial)["pending"] == 0, name+" did not cancel")
            positives.append(dict(case=name, cancelled=True, rx_writes=0, mmio_writes=0))

    for index, address in enumerate(READABLE):
        for bit in range(32):
            if address == MSYS and not MSYS_MASK & (1 << bit):
                run("reserved-msys-bit-"+str(bit),
                    lambda c, a=address, b=bit: c.mmio.__setitem__(a, c.mmio[a] ^ (1 << b)), True)
            else:
                run("readable-register-bit-%d-%d" % (index, bit),
                    lambda c, a=address, b=bit: c.mmio.__setitem__(a, c.mmio[a] ^ (1 << b)))
    # A later DMA start/selector change must not pass the second snapshot.
    for address, occurrence, value in ((MDST, 4, 1), (MDSTAR, 3, 0x0cff0020)):
        run("late-hardware-change-"+hex(address),
            lambda c, a=address, n=occurrence, v=value: setattr(c, "negative_mmio", (a, n, v)))

    frame = baseline.frame_address
    state = baseline.symbols["_state"][0]
    contexts = [
        ("wrong-completion-pr", lambda c: c.write(frame+12, c.read(frame+12)+2)),
        ("wrong-inherited-sr", lambda c: c.write(frame+68, c.read(frame+68)&~0xf0)),
        ("wrong-live-sr", lambda c: setattr(c, "sr", c.sr&~0xf0)),
        ("private-p2-stack", lambda c: c.r.__setitem__(15, c.r[15]+0x20000000)),
        ("stack-guard", lambda c: c.write(c.symbols["__vmu_native_worker_stack_bottom"][0], 0)),
        ("manager-pointer", lambda c: c.write(record.driver[9], manager+4)),
        ("manager-unsupported-alias", lambda c: c.write(record.driver[9], (manager&0x1fffffff)|0xe0000000)),
        ("lock-pointer", lambda c: c.write(record.driver[11], c.read(record.driver[11])+1)),
        ("lock-byte", lambda c: c.write(c.read(record.driver[11]), 0, 1)),
        ("bank-selector", lambda c: c.write(manager+0x10b4, c.read(manager+0x10b4)^1)),
        ("source-p2-context", lambda c: c.write(manager+0x10b0, c.read(manager+0x10b0)^0x20000000)),
        ("physical-source-word", lambda c: c.write(c.read(state+20), c.read(c.read(state+20))^1)),
        ("physical-response-header", lambda c: c.write(rx, c.read(rx)^1)),
        ("physical-controller-function", lambda c: c.write(rx+4, c.read(rx+4)^1)),
        ("physical-virtual-slot-occupied", lambda c: c.write(rx, c.read(rx)|(slot<<16))),
        ("initializer-lease-header", lambda c: c.write(c.read(config+CONFIG_LEASE+100)+4, 0)),
        ("completion-hook", lambda c: c.write(record.driver[12], record.consumer)),
        ("consumer-role-p2-alias", lambda c: c.write(config+12+14*4, record.consumer+0x20000000)),
        ("nonuniform-driver-family", lambda c: c.write(config+12+8, 2)),
        ("truncated-source-span", lambda c: c.write(config+12+12, record.consumer+0xa2+163-record.load)),
        ("live-plan-not-ready", lambda c: c.write(c.symbols["_live_plan"][0]+4, 0)),
        ("filtered-overlay-pending", lambda c: c.write(c.symbols["_overlay"][0]+76, 1)),
        ("illegal-pair-pending", lambda c: c.write(pair, 3))]
    # Poll identity alone does not walk the SDK free list. Link the real
    # remaining free block into the retained allocation, preserving all eight
    # identity words, and require the cancellation's full lease proof to refuse.
    first_free = baseline.read(record.heap[13])
    lease_header = baseline.read(config+CONFIG_LEASE+100)
    contexts.append(("full-lease-free-intrusion", lambda c: c.write(first_free, lease_header)))
    for name, mutate in contexts:
        run(name, mutate)

    entry = record.consumer+0xa2
    source_roles = []
    for index, expected in enumerate(FINISH_BODY):
        source_roles.append(("finish-word-"+str(index), entry+2*index, 2,
                             0x100 if expected >> 12 in (9, 13) else 1))
    pools = set()
    for offset in LONG_ROLES:
        pc = entry+offset; op = record.half(pc)
        pools.add((((pc+4)&~3)+(op&255)*4, 4))
    for offset, _ in WORD_ROLES:
        pc = entry+offset; op = record.half(pc)
        pools.add((pc+4+(op&255)*2, 2))
    timers = (record.literal(entry+14, 13)[1], record.literal(entry+20, 11)[1])
    for number, (timer, count) in enumerate(zip(timers, (5, 3))):
        for index in range(count):
            source_roles.append(("timer-%d-word-%d" % (number, index), timer+2*index, 2,
                                 0x100 if number == index == 0 else 1))
    op = record.half(timers[0]); pools.add((((timers[0]+4)&~3)+(op&255)*4, 4))
    source_roles += [("source-pool-"+hex(address), address, width, 1)
                     for address, width in sorted(pools)]
    for name, address, width, bit in source_roles:
        run(name, lambda c, a=address, n=width, b=bit: c.write(a, c.read(a, n)^b, n))
    # P1 source-data can be stale while P2 reflects a replaced executable.
    # Instruction fetches of the original consumer are outside these C-only
    # operand-view fixtures; this is not a cache-coherence emulator.
    alias_samples = ((entry, 2), (record.literal(entry+24, 14)[0], 4), (timers[0]+2, 2))
    for number, (address, width) in enumerate(alias_samples):
        original = baseline.block(address, width)
        changed = bytes([original[0]^1])+original[1:]
        def stale(c, a=address, n=width, old=original, new=changed):
            c.store_block(a, new); c.alias_views.append((a, old))
        run("fresh-p2-damaged-stale-p1-clean-"+str(number), stale)
        run("fresh-p2-clean-stale-p1-damaged-"+str(number),
            lambda c, a=address, new=changed: c.alias_views.append((a, new)), True)
    for arbitrary in (0, MASK, 0xdeadbeef):
        run("write-only-backing-irrelevant-"+hex(arbitrary),
            lambda c, v=arbitrary: c.mmio.__setitem__(MDAPRO, v), True)
    return dict(refusals=results, accepted_reserved_or_stale_views=positives,
                refusal_count=len(results), accepted_count=len(positives),
                finish_source_roles=len(source_roles),
                limits="Negative C paths stop at actual _fail before display; original instruction/data bytes remain external.")


def normal_trace(startup, build, handoff, record, slot, rounds=6):
    cpu = ShutdownWorker(startup, build, handoff, record)
    cpu.prepare()
    require(cpu.read(cpu.symbols["_overlay"][0]+80) == slot,
            "Actual linked arm selected a different virtual address")
    initializer = cpu.initialize_sdk()
    cpu.start_real_hooks()
    rows = []
    for ordinal in range(rounds):
        submit = cpu.submit_actual()
        frame = cpu.completion_entry()
        before = snapshot(cpu)
        complete = cpu.complete_actual()
        changes = [(index, old, new) for index, (old, new) in
                   enumerate(zip(before["rx"], cpu.rx_snapshot())) if old != new]
        if ordinal >= 2:
            expected = [(submit["bank"]*24*1024+2, 0x20, 0x20 | slot)]
            require(changes == expected and before["tx"] == cpu.block(record.tx, 0x6000),
                    "Normal controller publication changed")
        ready = cpu.original_until(record.ready)
        rows.append(dict(ordinal=ordinal, submit=submit, frame=frame,
            callback_instructions=complete["instructions"],
            callback_guest_data_reads=complete["guest_data_reads"],
            callback_guest_read_histogram=complete["guest_read_histogram"],
            wrapper_prefix_gateway_callback_consumer_instructions=
                complete["wrapper_prefix_gateway_callback_consumer_instructions"],
            original_consumer_instructions=complete["actual_original_consumer_instructions"],
            ready_instructions=ready, rx_changes=changes,
            executed_functions=complete["executed_functions"]))
    return cpu, initializer, rows


def stopped_cycles(cpu, slot, count=3):
    """Keep hardware disabled across actual SDK submissions and consumers."""
    pair = cpu.symbols["_controller_pair"][0]
    initial = state_words(cpu)
    rows = []
    for ordinal in range(count):
        require(cpu.mmio[MDEN] == 0, "Stopped replay must not restore MDEN")
        submit = cpu.submit_actual()
        submitted = cpu.last_submit_observation
        no_publication(cpu, cpu.submit_boundary_before, submitted)
        require(submitted["rx_data_reads"] == 0, "Stopped submit read physical RX data")
        require(cpu.read(pair) == 3 and state_words(cpu)["pending"] == 0,
                "Stopped submit created an owned transfer")
        frame = cpu.completion_entry()
        before = snapshot(cpu)
        complete = cpu.complete_actual()
        no_publication(cpu, before, complete)
        require(complete["rx_data_reads"] == 0, "Stopped complete read physical RX data")
        after = state_words(cpu)
        require(cpu.read(pair) == 3 and not after["pending"] and
                (after["submitted"], after["completed"]) ==
                (initial["submitted"], initial["completed"]),
                "Stopped callbacks changed transfer accounting")
        ready = cpu.original_until(cpu.record.ready)
        rows.append(dict(ordinal=ordinal, bank=submit["bank"], submit=submit, frame=frame,
            submit_instructions=submitted["instructions"], complete_instructions=complete["instructions"],
            submit_stack_bytes=submitted["observed_private_stack_bytes"],
            complete_stack_bytes=complete["observed_private_stack_bytes"],
            submit_rx_reads=0, complete_rx_reads=0, rx_writes=0, mmio_writes_by_worker=0,
            reader_storage_session_entries=0, original_consumer_instructions=
                complete["actual_original_consumer_instructions"], ready_instructions=ready))
    # Separate enabled boundary: an idle completion is still transparent.
    # Only the following enabled submission may leave the stopped state.
    submit = cpu.submit_actual()
    no_publication(cpu, cpu.submit_boundary_before, cpu.last_submit_observation)
    require(cpu.last_submit_observation["rx_data_reads"] == 0, "Stopped submit read RX before resume")
    cpu.mmio[MDEN] = 1
    idle_frame = cpu.completion_entry(); before = snapshot(cpu); idle = cpu.complete_actual()
    no_publication(cpu, before, idle)
    idle_state = state_words(cpu)
    require(idle["rx_data_reads"] == 0 and cpu.read(pair) == 3 and not idle_state["pending"] and
            (idle_state["submitted"], idle_state["completed"]) ==
            (initial["submitted"], initial["completed"]),
            "Enabled idle completion published or left stopped mode")
    cpu.original_until(cpu.record.ready)
    before_resume = state_words(cpu)
    resumed = cpu.submit_actual()
    require(cpu.read(pair) != 2 and state_words(cpu)["pending"] == 1 and
            state_words(cpu)["submitted"] == before_resume["submitted"]+1,
            "First enabled submit did not use fresh ordinary admission")
    frame = cpu.completion_entry(); before = snapshot(cpu); complete = cpu.complete_actual()
    after = state_words(cpu)
    parent = cpu.record.rx_slots[resumed["bank"]*24]
    require(cpu.read(parent) & (slot << 16) and cpu.read(pair) == 0 and not after["pending"] and
            after["submitted"] == after["completed"] == before_resume["submitted"]+1,
            "Normal publication did not resume after enabled submission")
    require(cpu.block(cpu.record.tx, 0x6000) == before["tx"], "Resumed completion changed TX")
    cpu.original_until(cpu.record.ready)
    return dict(disabled_cycles=rows, disabled_start_attempts=cpu.disabled_start_attempts,
        enabled_idle_completion=dict(frame=idle_frame, rx_reads=idle["rx_data_reads"],
            stayed_stopped=True, observed_stack_bytes=idle["observed_private_stack_bytes"]),
        first_enabled_submit=dict(submit=resumed, fresh_ordinary_admission=True,
            skipped_capture=True, resumed_publication=True,
            completion_instructions=complete["instructions"]),
        hardware_boundary="MDEN0 ignores the original SDK MDST1 request; no DMA packet transfer. MDEN is changed only for the separate resume control.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--startup", required=True, type=Path)
    parser.add_argument("--record", required=True, type=Path)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--slot", required=True, type=int, choices=(1, 2),
                        help="Expected immutable virtual slot; not a profile selector")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--baseline-build", type=Path,
                        help="Optional immutable pre-fix build for identical normal callback comparison")
    parser.add_argument("--self-test", action="store_true",
                        help="Also reject every immutable-object bit and malformed typed object binding")
    parser.add_argument("--allow-development-artifacts", action="store_true",
                        help="Explicitly admit artifact compile provenance marked dirty; report remains development-only")
    args = parser.parse_args()
    if not __debug__:
        parser.error("Python assertions must remain enabled")
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    status = subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True)
    require(not status or args.allow_development_artifacts,
            "Dirty proof source requires --allow-development-artifacts")
    proof_paths = ("tools/check_native_controller_shutdown.py",
        "tools/profile_vmu_native_record_worker_sh.py", "tools/profile_vmu_native_sdk_worker_sh.py",
        "tools/profile_vmu_native_release_sh.py", "tests/test_vmu_native_sdk_poll_execution.py",
        "tests/test_vmu_native_heap_execution.py", "tools/sh4_delay_slots.py",
        "tools/native_runtime_layout.py", "tools/vmu_native_relocations.py",
        "tools/check_vmu_native_worker_gateway.py", "tools/check_loader_layout.py",
        "tools/vmu_native_cache_seed.py", "tools/vmu_native_resolution_cache_tool.c",
        "src/loader/vmu_native_passive.c", "src/loader/vmu_native_controller_shutdown.inc",
        "src/loader/vmu_native_passive_hooks.S", "src/loader/vmu_native_split_controller.inc",
        "include/kui/vmu_native_passive.h", "include/kui/vmu_native_runtime.h",
        "include/kui/vmu_readonly_overlay.h", "include/kui/vmu_durable.h")
    proof_sources = {name:digest((ROOT/name).read_bytes()) for name in proof_paths}
    worker, stage, artifacts, geometry = artifact_contract(args.build,
        args.allow_development_artifacts, head)
    objects = shutdown_objects(worker)
    mmio_leaf = shutdown_mmio_leaf(worker)
    source, image, extent = fresh_verify(args.startup, args.record)
    record = Record(args.record.read_bytes(), image, source)
    require(record.driver[2] == 1 and record.layout[6] == 0, "A reviewed uniform SDK is required")
    handoff = make_handoff()
    cpu, initializer, history = normal_trace(args.startup, args.build, handoff, record, args.slot)
    warm_idle = cpu.fork()
    baseline = None
    if args.baseline_build:
        _, _, old_files, old_geometry = artifact_contract(args.baseline_build)
        _, _, old_rows = normal_trace(args.startup, args.baseline_build, handoff, record, args.slot)
        compared = []
        for old, new in zip(old_rows, history):
            old_reads = Counter({(a,n):count for a,n,count in old["submit"]["callback_guest_read_histogram"]})
            new_reads = Counter({(a,n):count for a,n,count in new["submit"]["callback_guest_read_histogram"]})
            extra_mden = new_reads == old_reads+Counter({(MDEN,4):1})
            require(old["rx_changes"] == new["rx_changes"] and
                    old["callback_guest_read_histogram"] == new["callback_guest_read_histogram"] and
                    (old_reads == new_reads or extra_mden),
                    "Normal publication or guest reads differ from the authentic baseline")
            compared.append(dict(ordinal=new["ordinal"],
                submit_callback_before=old["submit"]["callback_instructions"],
                submit_callback_after=new["submit"]["callback_instructions"],
                complete_callback_before=old["callback_instructions"],
                complete_callback_after=new["callback_instructions"],
                submit_guest_reads=new["submit"]["callback_guest_data_reads"],
                complete_guest_reads=new["callback_guest_data_reads"],
                additional_warm_slow_submit_mden_read=extra_mden,
                full_submit_before=old["submit"]["full_original_submit_and_gateway_instructions"],
                full_submit_after=new["submit"]["full_original_submit_and_gateway_instructions"],
                full_completion_before=old["wrapper_prefix_gateway_callback_consumer_instructions"],
                full_completion_after=new["wrapper_prefix_gateway_callback_consumer_instructions"]))
        require(all((args.baseline_build/name).read_bytes() == data for name, data in old_files.items()),
                "Authentic baseline artifacts changed during proof")
        baseline = dict(geometry=old_geometry,
            artifacts_sha256={name:digest(data) for name, data in old_files.items()},
            identical_normal_publication=True, unchanged_guest_reads_except_exact_warm_slow_mden_probe=True,
            cases=compared)
    cases = []
    negatives = None
    for bank in (0, 1):
        while True:
            submit = cpu.submit_actual()
            if submit["bank"] == bank:
                break
            cpu.completion_entry(); cpu.complete_actual(); cpu.original_until(record.ready)
        require(submit["pending"] in (1, 2), "Physical controller capture was not selected")
        finish = original_finish(cpu, bank*2)
        frame = cpu.completion_entry()
        if bank == 0:
            negatives = mutation_cases(cpu, args.slot)
        before = snapshot(cpu)
        complete = cpu.complete_actual()
        no_publication(cpu, before, complete)
        after = state_words(cpu)
        require(after["pending"] == 0 and after["completed"] == before["state"]["completed"]+1 and
                after["submitted"] == before["state"]["submitted"] and
                after["proof"] == before["state"]["proof"], "Cancellation state transition changed")
        require(cpu.read(cpu.symbols["_controller_pair"][0]) == 3, "Physical capture did not enter stopped mode")
        cases.append(dict(bank=bank, submit=submit, original_finish=finish, frame=frame,
                          complete_instructions=complete["instructions"],
                          observed_private_stack_bytes=complete["observed_private_stack_bytes"],
                          actual_gateway_restored_owner_frame=True,
                          actual_original_consumer_instructions=
                              complete["actual_original_consumer_instructions"],
                          cancelled=True, rx_writes=0, tx_writes=0, mmio_writes=0,
                          reader_storage_session_entries=complete["watched"]))
        cpu.original_until(record.ready)
        cases[-1]["stopped_lifecycle"] = stopped_cycles(cpu, args.slot)
    require(not state_words(warm_idle)["pending"] and warm_idle.read(warm_idle.symbols["_controller_pair"][0]) == 0,
            "Idle-finish control is not an ordinary warm idle state")
    idle_finish = original_finish(warm_idle)
    idle_lifecycle = stopped_cycles(warm_idle, args.slot)
    require(all((args.build/name).read_bytes() == data for name, data in artifacts.items()),
            "Linked artifacts changed during proof")
    require(all(digest((ROOT/name).read_bytes()) == value for name, value in proof_sources.items()),
            "Proof or reviewed runtime source changed during execution")
    report = dict(schema="kui.native.controller-shutdown.linked-sh.v1", passed=True,
        virtual_slot=args.slot, geometry=geometry, source_bytes=len(source), source_sha256=digest(source),
        program_extent=extent, record_sha256=digest(record.wire), handoff_sha256=digest(handoff),
        artifacts_sha256={name: digest(data) for name, data in artifacts.items()},
        proof_sources_sha256=proof_sources, source_commit=head,
        source_dirty=bool(status), development_artifacts=geometry["entry_compile_provenance"]["source_dirty"],
        authored_shutdown_immutable_objects=objects,
        authored_readable_register_leaf=mmio_leaf,
        immutable_object_self_tests=object_self_tests(worker) if args.self_test else None,
        original_bytes_embedded=False, worker_helper_stubs=False,
        actual_submit_and_complete_gateways_executed=True, sdk_initializer=initializer,
        normal_callbacks=history, baseline_comparison=baseline, cases=cases,
        idle_finish_without_prior_cancel=dict(original_finish=idle_finish, stopped_lifecycle=idle_lifecycle),
        negative_cases=negatives, limits=[
            "Flat aliased RAM and synchronous authored physical Maple replies; no console/cache/timing claim.",
            "Actual original finish and both timer helpers execute; only named registers and TMU reads are hardware boundaries.",
            "Original initializer IRQ-registration scaffolding is omitted by the reused explicit initializer contract.",
            "The original completion wrapper prefix and consumer run; its later epilogue/hardware callbacks are outside this scoped replay.",
            "Reported stack peaks are observed paths, not the historical all-path SD worker stack aggregate.",
            "This does not execute a later selected game, prove secondary loading lifetime or certify whole-game compatibility."])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(dict(output=str(args.output), passed=True, slot=args.slot, cases=len(cases))))


if __name__ == "__main__":
    main()
