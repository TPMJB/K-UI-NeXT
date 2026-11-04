#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the opt-in native performance worker against a capture-off control.

The reference worker's loaded bytes were reproduced from pristine b7b148e
sources with capture off. Historical General pin maps predate that source;
their refusal is reported, never converted into a strict-audit success.
Unchanged candidate functions are compared to that reference using typed
relocations and mapped unchanged constant spans. New entry instrumentation
has separate real-C and linked-SH clock/chord tests. This proves software
contracts and stack bounds, not hardware timing or game compatibility.
"""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tests")]
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from check_vmu_native_persistent import prepared_signature, prepared_symbol_records, worker_stack_paths
from package_vmu_maple_observe import inspect_elf
from vmu_native_relocations import generate
from check_retail_instructions import audit as instruction_audit
import check_vmu_native_readahead as ra
import check_native_controller_failure_witness as witness
from check_vmu_native_sdk_sci_route import assert_c_abi

ONE = (1, 0, 0, 0xfff1)
BASE_SOURCE = "b7b148e4a158d46022f9c58c0d3acec22b0cbdc4"
# Clean original passive.c + split.inc compiled with the exact General flags,
# then linked with the remaining unchanged worker objects. Complete -dr
# object disassembly/relocations and complete loaded worker bytes matched.
# Only the one existing BUILD <12 hex> string is normalized here.
BASE_WORKER_BYTES = 49664
BASE_WORKER_NORMALIZED_SHA256 = "8fcd1923561305286b4f62b1d2c81d430f392af432fc249791294350a1f057b1"
CHANGED = {
    "_persistent_lease", "_kui_vmu_native_passive_arm", "_kui_vmu_native_passive_submit",
    "_kui_vmu_native_passive_complete", "_kui_vmu_native_readonly_release_check",
    "_kui_vmu_native_passive_split_submit",
}
ADDED = {
    "_native_perf_gesture", "_native_perf_reader_snapshot", "_native_perf_begin",
    "_native_perf_end", "_native_perf_poll", "_kui_vmu_native_profile_sample_valid",
    "_kui_vmu_native_profile_begin", "_kui_vmu_native_profile_end", "_retail_display_pause",
}

def require(ok, message):
    if not ok:
        raise ValueError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def config(directory):
    result = dict(line.split("=", 1) for line in (directory / "readonly-config").read_text().splitlines()
                  if "=" in line)
    require(re.fullmatch(r"[0-9a-f]{12}", result.get("BUILD_ID", "")), "expected exact twelve-hex build identity")
    require(result.get("NATIVE_SPAN") == "0x1c000" and
            result.get("NATIVE_LAYOUT") == "src/loader/vmu_native_general_lean.ld",
            "native performance profile must retain General 112 KiB worker")
    return result

def normalize_build(data, build):
    return data.replace(b"BUILD " + build.encode() + b"\0", b"BUILD " + bytes(12) + b"\0")

def geometry(directory):
    worker = read_elf(directory / "native-worker.elf")
    symbols = worker[4]
    for name in ("__kui_native_general_lean_worker_layout", "__kui_native_split_controller_general_worker",
                 "__kui_native_split_controller_isolation_worker", "__kui_native_ra_tick_off_isolation_worker"):
        require(symbols.get(name) == ONE, "missing exact demand-only General marker: " + name)
    require("__kui_native_split_controller_prefetch_worker" not in symbols and
            "__kui_native_split_controller_stream_guard_worker" not in symbols,
            "speculative prefetch must be absent")
    require(symbols.get("__kui_native_isolation_lease_bytes", (None,))[0] == 0x51000,
            "changed complete reader/worker reservation")
    bottom = symbols["__vmu_native_worker_stack_bottom"][0]
    top = symbols["__vmu_native_worker_stack_top"][0]
    end = symbols["__vmu_native_worker_bss_end"][0]
    require(bottom == 0xac11b000 and top == 0xac11c000 and end <= bottom,
            "changed private stack geometry or BSS overlap")
    binary = (directory / "native-worker.bin").read_bytes()
    wire = (directory / "native-worker.reloc").read_bytes()
    require(generate(worker[0], binary, 0x8c100000, 0x1c000) == wire,
            "worker relocation records differ from actual ELF")
    stage = read_elf(directory / "stage.elf")
    image = inspect_elf(stage[0], 0x8ce00000, 0x8cfe0000)["payload"]
    for a, b, expected in (("__native_worker_blob_begin", "__native_worker_blob_end", binary),
                           ("__native_worker_reloc_begin", "__native_worker_reloc_end", wire)):
        begin, finish = stage[4][a][0], stage[4][b][0]
        require(finish - begin == len(expected) and image[begin - 0x8ce00000:finish - 0x8ce00000] == expected,
                "stage embeds different worker or relocation bytes")
    return worker, dict(worker_span=0x1c000, complete_reservation=0x51000,
                        stack_bytes=top-bottom, bss_margin=bottom-end,
                        exact_elf_relocations=True, stage_embeds_exact_worker=True)

def baseline(directory):
    cfg = config(directory)
    worker, layout = geometry(directory)
    require("__kui_native_perf_capture_worker" not in worker[4] and "_perf_capture" not in worker[4],
            "reference must compile capture out completely")
    binary = (directory / "native-worker.bin").read_bytes()
    require(len(binary) == BASE_WORKER_BYTES and
            sha(normalize_build(binary, cfg["BUILD_ID"])) == BASE_WORKER_NORMALIZED_SHA256,
            "capture-off worker differs from independently reproduced pristine b7b148e source")
    return worker, dict(source_commit=BASE_SOURCE, loaded_worker_bytes=len(binary),
                        normalized_worker_sha256=BASE_WORKER_NORMALIZED_SHA256,
                        pristine_head_loaded_worker_equivalent=True, **layout)

def constant_section(worker, directory):
    records = prepared_symbol_records(worker)
    name, address, size, _, owner = next(r for r in records if r[0] == ".rodata")
    section = worker[3][owner]
    raw = bytearray(worker[0][section[4]:section[4]+size])
    wire = (directory / "native-worker.reloc").read_bytes()
    first = address & 0x1fffffff
    for i in range(struct.unpack_from("<I", wire, 28)[0]):
        at, _ = struct.unpack_from("<II", wire, 72+8*i)
        offset = at - (first - 0x0c100000)
        if 0 <= offset <= len(raw)-4:
            raw[offset:offset+4] = bytes(4)
    return normalize_build(bytes(raw), config(directory)["BUILD_ID"])

def function_form(worker, directory, name, constants_map):
    address, raw, _ = symbol_bytes(worker, name, 2)
    binary = (directory / "native-worker.bin").read_bytes()
    wire = (directory / "native-worker.reloc").read_bytes()
    _, _, refs = prepared_signature(worker, binary, wire, name)
    body = bytearray(raw)
    canonical = []
    records = {r[0]: r for r in prepared_symbol_records(worker)}
    for offset, target, addend, kind in refs:
        body[offset:offset+4] = bytes(4)
        if target == ".rodata":
            found = [block for block in constants_map if block[1] <= addend < block[1]+block[2]]
            require(len(found) == 1 and found[0][2] >= 4,
                    "constant pointer is not in an unchanged reference span: " + name)
            a, b, _ = found[0]
            addend = a+addend-b
        elif re.fullmatch(r"_CSWTCH\.\d+@vmu_native_passive\.c", target):
            # GCC renumbers this scalar switch table when new functions are
            # inserted. Bind the complete immutable table, not its ordinal.
            _, value, size, actual_kind, owner = records[target]
            section = worker[3][owner]
            require(actual_kind == 1 and size and section[2] & 7 == 2,
                    "compiler switch table lost immutable typed ownership")
            table = worker[0][section[4]+value-section[3]:section[4]+value-section[3]+size]
            require(not any(target_at >= value-0x8c100000 and target_at < value-0x8c100000+size
                            for target_at, _ in (struct.unpack_from("<II", wire, 72+8*i)
                                                 for i in range(struct.unpack_from("<I", wire, 28)[0]))),
                    "scalar switch table gained a pointer relocation")
            target = "immutable-scalar-switch@vmu_native_passive.c:"+sha(table)
        canonical.append((offset, target, addend, kind))
    return bytes(body), canonical

def compare_functions(candidate, directory, reference, refdir):
    before = constant_section(reference, refdir)
    after = constant_section(candidate, directory)
    blocks = [(m.a, m.b, m.size) for m in difflib.SequenceMatcher(None, before, after, autojunk=False).get_matching_blocks()
              if m.size]
    identity = [(0, 0, len(before))]
    old = {n for n, r in reference[4].items() if r[2] == 2}
    new = {n for n, r in candidate[4].items() if r[2] == 2}
    require(new-old <= ADDED and old <= new, "unexpected added or removed native worker function")
    compared = []
    for name in sorted(old-CHANGED):
        require(function_form(reference, refdir, name, identity) ==
                function_form(candidate, directory, name, blocks),
                "unchanged worker function differs in instructions or typed targets: " + name)
        compared.append(name)
    require({"_controller_context_valid", "_transfer_context_valid", "_persistent_storage_guard",
             "_persistent_context_guard", "_installed_hooks_valid", "_poll_lease_valid"} <= set(compared),
            "fresh controller, identity or storage guard missing from comparison")
    return dict(unchanged_functions=len(compared), compared_functions=compared,
                changed_entry_functions=sorted(CHANGED), added_functions=sorted(new-old),
                normalization="Exact body bytes; ELF-proven typed pointer symbol/addend; unchanged anonymous constant spans")

def linked_guard_models(directory, worker):
    resident = read_elf(directory / "resident-sci.elf")
    layout = witness.console_layout()
    witness.MODELED_FUNCTIONS.clear()
    result = dict(helper_cases=witness.helper_cases(resident, worker, layout),
                  callback_ownership=witness.callback_ownership_cases(resident, worker, layout),
                  controller_context=witness.context_cases(resident, worker, layout),
                  identity_cases=witness.identity_cases(resident, worker, layout),
                  terminal_identity_cases=witness.terminal_identity_cases(resident, worker, layout))
    class PerfCPU(witness.WitnessCPU):
        def plain(self, opcode, pc):
            if opcode >> 8 == 0xca:  # XOR #imm,R0; control flags unchanged.
                self.r[0] ^= opcode & 255
                return
            if opcode & 0xf0ff == 0x4019:  # SHLR8: logical right, T unchanged.
                self.r[(opcode >> 8) & 15] >>= 8
                return
            if opcode & 0xf0ff == 0x4011:  # CMP/PZ Rn.
                self.flag(not (self.r[(opcode >> 8) & 15] & 0x80000000))
                return
            if opcode & 0xf00f == 0x6000:  # MOV.B @Rm,Rn, sign-extended.
                value = self.read(self.r[(opcode >> 4) & 15], 1)
                self.r[(opcode >> 8) & 15] = ((value ^ 0x80)-0x80) & 0xffffffff
                return
            if opcode & 0xf00f == 0x6007:  # NOT Rm,Rn, T unchanged.
                self.r[(opcode >> 8) & 15] = (~self.r[(opcode >> 4) & 15]) & 0xffffffff
                return
            return super().plain(opcode, pc)
    chord_cases = 0
    c = PerfCPU(resident, worker, layout, 731, 0)
    for low in range(0x10000):
        # Exhaustive raw button word, independently specified active-low mask.
        controls = 0xffff0000 | low
        before = c.snapshot();c.writes=[]
        c.execute("_native_perf_gesture", [controls])
        expected = not (low & 2) and low & 0x060c == 0x060c
        require(c.r[0] == int(expected), "linked capture chord accepts the wrong buttons")
        assert_c_abi(c, before);c.assert_no_owner_writes();chord_cases += 1
    result["linked_chord_cases"] = chord_cases
    class ClockCPU(PerfCPU):
        def read(self, address, count=4):
            clocks = getattr(self, "clocks", {})
            if address in clocks:
                self.clock_reads.append((address, count))
                return clocks[address]
            return super().read(address, count)
        def write(self, address, value, count=4):
            require(address not in getattr(self, "clocks", {}), "performance sampler wrote owner timer MMIO")
            return super().write(address, value, count)
    clock_cases = 0
    for bad in range(5):
        c = ClockCPU(resident, worker, layout, bad, 0)
        c.clocks = {0xffc00000: 0xe0a, 0xffd80008: 0xffffffff,
                    0xffd80010: 2, 0xffd80004: 1, 0xffd8000c: 1000}
        c.clock_reads = []
        capture = c.addr("_perf_capture")
        span = c.r[15]-256
        c.write(capture+168, 0)
        c.execute("_native_perf_begin", [span])
        require(not c.clock_reads, "inactive capture sampled timer")
        c.write(capture+168, 1)
        if bad:
            c.clocks[(0xffc00000, 0xffd80008, 0xffd80010, 0xffd80004)[bad-1]] = 0
        c.execute("_native_perf_begin", [span])
        c.clocks[0xffd8000c] = 800
        before = c.snapshot();c.writes=[]
        c.execute("_native_perf_end", [0, span])
        require(c.read(capture) == 1 and c.read(capture+4) == int(not bad) and
                c.read(capture+8) == int(bool(bad)), "linked clock admission counted an invalid duration")
        require(c.read(capture+12) == (0 if bad else 200), "linked timing conversion or total differs")
        assert_c_abi(c, before);c.assert_no_owner_writes();clock_cases += 1
    result["linked_clock_cases"] = clock_cases
    result["scope"] = "Actual unchanged guard/identity SH instructions and exhaustive new chord; modified entry/storage lifecycle uses real-C host fixture"
    return result

def entry_call_contract(directory, reference):
    from check_retail_stack import load as load_graph
    def selected(path):
        _, calls = load_graph(sorted((path/"native").rglob("*.ci")), qualified=True)
        return {a: [b for b, _ in targets] for a, targets in calls.items()}
    old, new = selected(reference), selected(directory)
    for symbol in CHANGED:
        name = symbol[1:]
        for owner in [k for k in old if k.rsplit(":", 1)[-1] == name]:
            require(owner in new, "modified entry lacks emitted callgraph: "+name)
            # Diagnostics may add calls; existing ownership/storage calls
            # cannot disappear. The full-submit variant deliberately changes
            # the short capture into the existing complete submit entry.
            for target in set(old[owner]):
                require(new[owner].count(target) >= old[owner].count(target),
                        "modified entry lost an original emitted guard/callback: "+name+" -> "+target)
    return dict(existing_emitted_calls_preserved=True,
                scope="Actual compiler call sites; predicates additionally exercised by linked guard and real-C refusal tests")

def source_reproduction(directory, full):
    """Recompile reviewed hooks, relink, and compare all loaded worker bytes.

    This binds newly instrumented entry bodies to source and exact build
    flags, rather than accepting hashes learned from the candidate itself.
    Other worker functions are independently compared to pristine capture0.
    """
    build = str(directory.relative_to(ROOT))
    target = build+"/native-worker.elf"
    command = ["make", "-s", "-n", "-B", "-f", "Makefile.performance-native",
               "BUILD="+build, "BUILD_ID="+config(directory)["BUILD_ID"],
               "PERF_NATIVE_CAPTURE=1", "PERF_NATIVE_FULL_SUBMIT="+str(int(full)), target]
    text = subprocess.check_output(command, cwd=ROOT, text=True).replace("\\\n", " ")
    rows = [shlex.split(line) for line in text.splitlines()
            if line.startswith(("sh-elf-gcc ", str(ROOT/".deps/sh-elf/bin/sh-elf-gcc")+" "))]
    sources = ("src/loader/vmu_native_passive.c", "src/core/vmu_native_profile.c", "src/loader/retail_display.c")
    compiles = [r for r in rows if "-c" in r and any(source in r for source in sources)]
    links = [r for r in rows if "-o" in r and r[r.index("-o")+1] == target]
    require(len(compiles) == len(sources) and len(links) == 1 and
            all(sum(source in row for row in compiles) == 1 for source in sources),
            "cannot reconstruct unique reviewed source compile/link commands")
    link_command = links[0]
    with tempfile.TemporaryDirectory(prefix="native-perf-source-") as temporary:
        temporary = Path(temporary)
        replacements = {}
        for index, compile_command in enumerate(compiles):
            old_object = compile_command[compile_command.index("-o")+1]
            output = str(temporary/("reviewed"+str(index)+".o"))
            replacements[old_object] = output
            compile_command[compile_command.index("-o")+1] = output
            subprocess.run(compile_command, cwd=ROOT, check=True, capture_output=True)
        link_command = [replacements.get(part, part) for part in link_command]
        link_command = ["-Wl,-Map,"+str(temporary/"worker.map") if part.startswith("-Wl,-Map,")
                        else part for part in link_command]
        linked = temporary/"worker.elf"
        link_command[link_command.index("-o")+1] = str(linked)
        subprocess.run(link_command, cwd=ROOT, check=True, capture_output=True)
        binary = temporary/"worker.bin"
        subprocess.run([str(ROOT/".deps/sh-elf/bin/sh-elf-objcopy"), "-O", "binary", str(linked), str(binary)],
                       cwd=ROOT, check=True, capture_output=True)
        actual = (directory/"native-worker.bin").read_bytes()
        require(binary.read_bytes() == actual, "recompiled reviewed hooks do not reproduce complete candidate worker bytes")
    return dict(recompiled_sources=list(sources),
                loaded_worker_bytes=len(actual), loaded_worker_sha256=sha(actual),
                exact_loaded_bytes_reproduced=True, compiler_flags=compiles[0][1:compiles[0].index("-c")])

def stack_closure(directory, worker):
    resident = read_elf(directory / "resident-sci.elf")
    auxiliary = read_elf(directory / "native-ra-aux.elf")
    ra.pinned_code(auxiliary)
    ra.image_contract(directory, auxiliary, resident)
    ra.typed_ops(resident)
    ra.pinned_loan_boundary(resident)
    gateway = ra.gateway_cases(resident, auxiliary)
    auxiliary_stack = ra.private_stack(directory, resident)
    abi = dict(enabled=True, paired_markers=True, gateway=gateway)
    closure = worker_stack_paths(directory, worker,
        inspect_elf(resident[0], 0x8c004000, 0x8c007ba0), read_ahead_proof=abi)
    return dict(worker=closure, auxiliary=auxiliary_stack, gateway=gateway,
                scope="Separate actual pinned auxiliary/loan ABI proof; historical complete RA audit is not claimed")

def check(directory, reference=None):
    directory = Path(directory).resolve()
    cfg = config(directory)
    worker, layout = geometry(directory)
    capture = worker[4].get("__kui_native_perf_capture_worker")
    full = worker[4].get("__kui_native_perf_full_submit_worker")
    require(capture in (None, ONE) and full in (None, ONE) and (not full or capture),
            "inexact or inconsistent performance markers")
    result = dict(schema="kui.native.performance.audit.v1", build_id=cfg["BUILD_ID"],
                  capture=bool(capture), full_submit=bool(full), prefetch=False,
                  console_execution_verified=False, geometry=layout)
    if not capture:
        _, proof = baseline(directory)
        result["pristine_reference"] = proof
        result["historical_strict_audit"] = "Unsupported current General/toggle pins; clean b7b148e loaded-worker equivalence is checked instead"
        return result
    require(reference is not None, "instrumented worker requires separate --reference capture-off build")
    reference = Path(reference).resolve()
    ref, proof = baseline(reference)
    result["pristine_reference"] = proof
    result["function_equivalence"] = compare_functions(worker, directory, ref, reference)
    result["entry_call_contract"] = entry_call_contract(directory, reference)
    result["reviewed_source_reproduction"] = source_reproduction(directory, bool(full))
    for name in ("resident-sci.bin", "resident-legacy.bin", "native-ra-aux.bin", "native-ra-aux.reloc"):
        a = normalize_build((directory/name).read_bytes(), cfg["BUILD_ID"])
        b = normalize_build((reference/name).read_bytes(), config(reference)["BUILD_ID"])
        require(a == b, "performance hooks changed low reader or auxiliary bytes: " + name)
    result["low_and_auxiliary_identical_except_build_label"] = True
    require(worker[4].get("_perf_capture", (0,0))[1] == 372, "unreviewed private collector layout")
    result["linked_guard_models"] = linked_guard_models(directory, worker)
    result["stack_closure"] = stack_closure(directory, worker)
    objdump = os.environ.get("OBJDUMP", str(ROOT/".deps/sh-elf/bin/sh-elf-objdump"))
    disassembly = subprocess.check_output([objdump, "-d", str(directory/"native-worker.elf")], text=True)
    result["integer_instruction_audit"] = instruction_audit("native-readonly", disassembly)
    result["host_fixture"] = "python3 tests/test_vmu_native_perf_capture.py (default/split/full/reader snapshots; run as a separate required suite)"
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = json.dumps(check(args.directory, args.reference), indent=2)+"\n"
    except (ValueError, KeyError, AssertionError, OSError, struct.error, subprocess.CalledProcessError) as error:
        parser.exit(2, "Native performance audit refused: "+str(error)+"\n")
    if args.output:
        args.output.write_text(result)
    print(result, end="")

if __name__ == "__main__":
    main()
