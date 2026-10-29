#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute original SDK allocators with an owned bottom reservation.

Original executable bytes are external inputs, never embedded in this tool
or its report. The independent SH model executes unchanged owner code. The
production C transition, code-family, placement and lifetime predicates read
that same live model through a coherent callback. No target is built here.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools")]
import test_vmu_native_heap_execution as heap
from prove_vmu_native_evolution1_readahead_lease import LiveAllocatorProof
from native_runtime_layout import LEASE_BYTES, AUX_OFFSET, layout_for

SCHEMA = "kui.native.bottom-corpus-original-heap.v1"
MARKER = "__kui_native_bottom_heap_stage"
WORKER_MARKER = "__kui_native_bottom_heap_worker"
ABSOLUTE_ONE = (1, 0, 0, 0xfff1)
ARTIFACTS = ("stage.elf", "native-worker.elf", "native-worker.bin", "native-ra-aux.bin")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Trace(heap.SH):
    """The existing bounded SH interpreter with an observed data-read set."""
    def __init__(self, image):
        self.reads = []
        super().__init__(image)

    def read(self, address, count=4):
        value = super().read(address, count)
        if count == 4:
            self.reads.append((self.executing, address, value, count))
        return value

    def run(self, entry, args, stack):
        self.reads = []
        old_sr = self.sr
        value, writes, steps = super().run(entry, args, stack)
        assert not ((self.sr ^ old_sr) & ~1), "Original allocator changed inherited SR"
        return value, writes, steps


class BottomObserver(LiveAllocatorProof):
    """Use production C proofs without replacing the original SH allocator."""
    def __init__(self, directory, selected_layout):
        super().__init__(directory, True, selected_layout=selected_layout)
        ptr, word = ctypes.c_void_p, ctypes.c_uint32
        library = self.library
        self.bottom_fresh = library.kui_vmu_native_heap_bottom_fresh
        self.bottom_fresh.argtypes = [ptr, self.Reader, ptr, word, word, word, word, ptr]
        self.placeholder_c = library.kui_vmu_native_heap_bottom_placeholder
        self.placeholder_c.argtypes = [ptr, self.Reader, ptr, word, word]
        self.allocated_c = library.kui_vmu_native_heap_bottom_allocated
        self.allocated_c.argtypes = [ptr, self.Reader, ptr, word, word]
        self.ready_c = library.kui_vmu_native_heap_bottom_ready
        self.ready_c.argtypes = [ptr, self.Reader, ptr, word]
        self.code_c = library.kui_vmu_native_heap_bottom_code_valid
        self.code_c.argtypes = [ptr, self.Reader, ptr]
        self.placement_c = library.kui_vmu_native_stage_bottom_allocation_safe
        self.placement_c.argtypes = [ptr, word, word, word, word, word]
        self.full_release_c = library.kui_vmu_native_heap_lease_valid_with_release
        self.full_release_c.argtypes = [ptr, self.Reader, ptr, word, word, ptr]
        for function in (self.bottom_fresh, self.placeholder_c, self.allocated_c,
                         self.ready_c, self.code_c, self.placement_c, self.full_release_c):
            function.restype = ctypes.c_bool

    def fresh(self, model, words, begin, count, lease_bytes, image_bytes, stage_begin, stage_end,
              *, placement_expected=True):
        self.model = model
        self.heap = ctypes.create_string_buffer(struct.pack("<18I", *words))
        self.lease = ctypes.create_string_buffer(116)
        self.reads = self.validations = self.releases = self.placement_cases = 0
        self.transition_checks = self.code_checks = 0
        self.errors = []

        def read(_context, address, out):
            self.reads += 1
            try:
                assert not address & 3
                out[0] = model.read(address)
                return True
            except (AssertionError, IndexError) as error:
                self.errors.append(str(error))
                return False

        self.reader = self.Reader(read)
        assert self.bottom_fresh(self.heap, self.reader, None, begin, count, 1,
                                 lease_bytes, self.lease)
        assert self.pre(self.lease, self.reader, None, 1)
        assert not self.pre(self.lease, self.reader, None, 2)
        assert not self.valid_c(self.lease, self.reader, None, 1)
        assert self.code_c(self.heap, self.reader, None)
        self.code_checks += 1
        # The whole lower lease and the adjacent placeholder header must
        # exclude the complete linked stage, not just its initialized bytes.
        assert bool(self.placement_c(self.heap, image_bytes, stage_begin, stage_end,
                                     begin, count)) == placement_expected
        self.placement_cases += 1
        for at in (heap.physical(begin), heap.physical(begin) + lease_bytes + 31,
                   heap.physical(begin) + lease_bytes + 32,
                   heap.physical(begin) + lease_bytes + 63):
            assert not self.placement_c(self.heap, image_bytes, at | 0x80000000,
                                        (at + 1) | 0x80000000, begin, count)
            self.placement_cases += 1
        assert not self.errors

    def cloned(self):
        return ctypes.create_string_buffer(self.lease.raw, len(self.lease))

    def placeholder(self, payload):
        bad = self.cloned()
        assert not self.placeholder_c(bad, self.reader, None, 1, payload + 32)
        assert self.placeholder_c(self.lease, self.reader, None, 1, payload)
        assert not self.pre(self.lease, self.reader, None, 1)
        assert not self.valid_c(self.lease, self.reader, None, 1)
        self.transition_checks += 1

    def allocated(self, payload, placeholder):
        bad = self.cloned()
        assert not self.allocated_c(bad, self.reader, None, 1, payload + 32)
        assert self.allocated_c(self.lease, self.reader, None, 1, payload)
        assert self.code_c(self.heap, self.reader, None)
        self.code_checks += 1
        size = ctypes.c_uint32(0)
        assert self.full_release_c(self.lease, self.reader, None, 1, placeholder,
                                   ctypes.byref(size))
        fields = struct.unpack("<29I", self.lease.raw)
        assert size.value == fields[28] * 32
        assert not self.release_c(self.lease, self.reader, None, 1, payload)
        assert not self.valid_c(self.lease, self.reader, None, 2)
        self.valid()
        self.transition_checks += 1

    def ready(self):
        assert self.ready_c(self.lease, self.reader, None, 1)
        self.valid()
        self.transition_checks += 1

    def report(self):
        return dict(super().report(), bottom_transition_checks=self.transition_checks,
                    complete_allocator_code_checks=self.code_checks,
                    intermediate_phase_not_runtime_owned=True,
                    allocated_placeholder_full_release_admission=True)


def _stage_unchanged(model, original, begin, end, sp):
    after = model.block(begin, end - begin)
    low, high = sp - 16 - begin, sp - begin
    assert original[:low] == after[:low] and original[high:] == after[high:]


def _cold(model, owner, begin, count, sp, stage_begin, stage_end, original_stage,
          program_bytes, observer):
    lease_bytes=observer.selected_layout.LEASE_BYTES
    allocate, release, sentinel, cursor = owner[9], owner[10], owner[13], owner[14]
    upper_header = begin + lease_bytes + 32
    request = count - lease_bytes - 64
    assert request >= 0 and not request & 31
    assert not heap.intersects(begin, lease_bytes + 64, stage_begin, stage_end - stage_begin)
    assert not heap.intersects(begin, lease_bytes + 32, 0x8c010000, program_bytes)
    calls = []
    placeholder, writes, steps = model.run(allocate, [request], sp)
    calls.append(("upper_placeholder_allocate", writes, steps, list(model.reads)))
    assert placeholder == upper_header + 32
    assert model.read(begin + 4) == lease_bytes // 32 + 1
    assert model.read(sentinel) == begin and model.read(begin) == sentinel
    assert model.read(cursor) == sentinel
    _stage_unchanged(model, original_stage, stage_begin, stage_end, sp)
    observer.placeholder(placeholder)
    lower, writes, steps = model.run(allocate, [lease_bytes], sp)
    calls.append(("lower_exact_allocate", writes, steps, list(model.reads)))
    assert lower == begin + 32
    assert model.read(sentinel) == sentinel and model.read(cursor) == sentinel
    assert model.read(lower - 28) == lease_bytes // 32 + 1
    _stage_unchanged(model, original_stage, stage_begin, stage_end, sp)
    observer.allocated(lower, placeholder)
    _, writes, steps = model.run(release, [placeholder], sp)
    calls.append(("upper_placeholder_release", writes, steps, list(model.reads)))
    assert model.read(sentinel) == upper_header and model.read(upper_header) == sentinel
    assert model.read(upper_header + 4) == (count - lease_bytes - 32) // 32
    assert model.read(cursor) == sentinel
    _stage_unchanged(model, original_stage, stage_begin, stage_end, sp)
    observer.ready()
    controls = [owner[11], owner[12], owner[12] + 4, sentinel, sentinel + 4,
                cursor, owner[15], owner[16], owner[17]]
    headers = [begin, begin + 4, upper_header, upper_header + 4]
    physical_words = {heap.physical(x) for x in controls + headers}
    for label, writes, _steps, reads in calls:
        for _pc, address, _value, width in writes:
            assert (width == 4 and heap.physical(address) in physical_words) or (
                sp - 16 <= address < address + width <= sp), (label, hex(address))
        for _pc, address, _value, width in reads:
            assert heap.intersects(address, width, 0x8c010000, program_bytes) or (
                heap.physical(address) in physical_words) or (
                heap.intersects(address, width, sp - 16, 16)), (label, hex(address))
        # The upper allocation may numerically contain the already-live
        # installer stack. That private frame remains usable; the temporary
        # allocation is never treated as a data buffer or initialized.
        for _pc, address, _value, width in writes + reads:
            if heap.intersects(address, width, placeholder, begin + count - placeholder):
                assert sp - 16 <= address < address + width <= sp
    return lower, placeholder, upper_header, calls


def prove(build, manifest_path, matrix_path, records, *, selected_slugs=None):
    assert __debug__, "Run the independent original-code proof with assertions enabled"
    build, manifest_path = Path(build).resolve(), Path(manifest_path).resolve()
    matrix_path, records = Path(matrix_path).resolve(), Path(records).resolve()
    stage = heap.read_elf(build / "stage.elf")
    worker_image = heap.read_elf(build / "native-worker.elf")
    selected_layout=layout_for(stage[4])
    assert selected_layout==layout_for(worker_image[4]),"Stage/worker layouts differ"
    lease_bytes=selected_layout.LEASE_BYTES
    assert stage[4].get(MARKER) == worker_image[4].get(WORKER_MARKER) == ABSOLUTE_ONE
    manifest, matrix = json.loads(manifest_path.read_text()), json.loads(matrix_path.read_text())
    assert manifest["schema"] == "kui.private.native-startup-inputs.v1"
    assert matrix["schema"] == "kui.native.structural-corpus.v1" and matrix["lease_bytes"] == lease_bytes
    assert matrix["input_manifest_sha256"] == sha(manifest_path)
    entries = {entry["slug"]: entry for entry in manifest["files"]}
    selected = [row for row in matrix["results"] if row["structural_384k_models_passed"]]
    assert selected
    structural_slugs = {row["slug"] for row in selected}
    assert len(structural_slugs) == len(selected), "Duplicate structural owner"
    if selected_slugs is not None:
        requested = tuple(selected_slugs)
        assert requested and len(set(requested)) == len(requested), "Empty or duplicate requested owner set"
        assert set(requested) <= structural_slugs, "Requested owner lacks structural admission"
        selected = [row for row in selected if row["slug"] in requested]
    first, end, stack, stack_bytes, _loads, worker = heap.stage_layout(build)
    assert (first, end) == (matrix["stage_begin"], matrix["stage_end"])
    sp = stack + stack_bytes - 32
    artifact_hashes = {name: sha(build / name) for name in ARTIFACTS}
    auxiliary = (build / "native-ra-aux.bin").read_bytes()
    assert 0x1002 <= len(auxiliary) <= 0x5000
    stage_pattern = bytes([0xa5]) * (end - first)
    payload_pattern = bytearray([0xa7]) * lease_bytes
    payload_pattern[:len(worker)] = worker
    payload_pattern[selected_layout.AUX_OFFSET:selected_layout.AUX_OFFSET + len(auxiliary)] = auxiliary
    inputs, results, cases, minimum_cases = [], [], [], []
    total_operations = 0
    with tempfile.TemporaryDirectory(prefix="kui-original-bottom-heap-") as temporary:
        observer = BottomObserver(Path(temporary),selected_layout)
        for row in selected:
            slug = row["slug"]
            entry, record = entries[slug], records / (slug + ".nvr")
            startup = Path(entry["startup"]).resolve()
            assert entry["windows_ce"] is False
            assert startup.stat().st_size == entry["source_bytes"]
            assert sha(startup) == entry["source_sha256"] == row["source_sha256"]
            raw, program, extent = heap.verified_program_image(startup, record)
            wire = record.read_bytes()
            owner = struct.unpack_from("<18I", wire, 608)
            call = struct.unpack_from("<5I", wire, 680)
            original_begin, count = call[3:]
            initializer, allocate, release, sentinel, cursor = owner[6], owner[9], owner[10], owner[13], owner[14]
            inputs.append(dict(slug=slug, startup=str(startup), record=str(record),
                               source_sha256=sha(startup), record_sha256=sha(record)))
            for alias in (0, 0x80000000, 0xa0000000):
                begin = heap.physical(original_begin) | alias
                for sr in (0x600000f0, 0x600001f1, 0x600080f1):
                    baseline, model = heap.SH(raw), Trace(raw)
                    baseline.sr = model.sr = sr
                    model.store_block(first, stage_pattern)
                    for machine in (baseline, model):
                        _value, init_writes, init_steps = machine.run(initializer, [begin, count], 0x8c00efc0)
                    assert model.block(first, end - first) == stage_pattern
                    observer.fresh(model, owner, begin, count, lease_bytes, len(program), first, end)
                    payload, placeholder, upper, cold = _cold(model, owner, begin, count, sp,
                        first, end, stage_pattern, len(program), observer)
                    after_cold = model.block(first, end - first)
                    model.store_block(payload, payload_pattern)
                    assert model.block(first, end - first) == after_cold
                    retained = model.block(begin, lease_bytes + 32)
                    identity = {at: model.read(at) for at in (owner[11], owner[12], owner[12] + 4,
                                                            owner[15], owner[16], owner[17])}
                    live, matching = [], []
                    operations = future_steps = 0
                    for size in (1, 31, 32, 33, 1023, 4096, 8192, 16384, 128, 32768, 64, 127):
                        a, _wa, _ia = baseline.run(allocate, [size], sp)
                        b, writes, instructions = model.run(allocate, [size], sp)
                        assert a == b and a, (slug, size, hex(a), hex(b))
                        assert all(not heap.intersects(at, width, begin, lease_bytes + 32)
                                   for _pc, at, _value, width in writes)
                        live.append(a)
                        matching.append(dict(request=size, payload=b))
                        operations += 1
                        future_steps += instructions
                        observer.valid()
                        assert model.block(begin, lease_bytes + 32) == retained
                    for pointer in live[1::2] + live[::2]:
                        baseline.run(release, [pointer], sp)
                        observer.release(pointer)
                        _value, writes, instructions = model.run(release, [pointer], sp)
                        operations += 1
                        future_steps += instructions
                        assert all(not heap.intersects(at, width, begin, lease_bytes + 32)
                                   for _pc, at, _value, width in writes)
                        observer.valid()
                        assert model.block(begin, lease_bytes + 32) == retained
                    assert baseline.read(sentinel) == begin and baseline.read(begin + 4) == count // 32
                    assert model.read(sentinel) == upper and model.read(upper + 4) == (count - lease_bytes - 32) // 32
                    assert all(model.read(at) == value for at, value in identity.items())
                    # Keep the historical fragmentation/exact-fit/coalescing
                    # stress for one actual alias/SR per owner. All72 cases
                    # independently execute the same-address prefix above.
                    stress_operations = maximum_nodes = stress_steps = 0
                    primary = alias == (original_begin & 0xe0000000) and sr == 0x600000f0
                    if primary:
                        test = heap.HeapExecutionTests("test_evolution1")
                        stress_operations, maximum_nodes, stress_steps = test.allocator_evolution(
                            model, owner, begin, count, payload, sp, lease_bytes, observer)
                        operations += stress_operations
                        future_steps += stress_steps
                    failed, writes, _instructions = model.run(allocate, [count - lease_bytes], sp)
                    assert not failed and model.read(sentinel) == upper
                    assert all(not heap.intersects(at, width, begin, lease_bytes + 32)
                               for _pc, at, _value, width in writes)
                    assert model.block(begin, lease_bytes + 32) == retained
                    observer.valid()
                    case = dict(slug=slug, alias=alias, caller_sr=sr,
                        arena_begin=begin, arena_bytes=count, placeholder_request=count - lease_bytes - 64,
                        placeholder_payload=placeholder, payload=payload, header=begin,
                        lease_bytes=lease_bytes, original_upper_end=begin + count,
                        upper_free_header=upper, remaining_units=model.read(upper + 4),
                        cold_original_instruction_counts=[step[2] for step in cold],
                        cold_original_write_counts=[len(step[1]) for step in cold],
                        cold_metadata_write_addresses=sorted({at for _label, writes, _steps, _reads in cold
                            for _pc, at, _value, width in writes if not heap.intersects(at, width, sp - 16, 16)}),
                        matching_baseline_allocations=matching,
                        subsequent_original_operations=operations, subsequent_original_instructions=future_steps,
                        full_header_and_payload_preserved=True, stage_and_installer_preserved=True,
                        placeholder_payload_not_initialized_or_consumed=True,
                        only_preexisting_installer_frame_accesses_inside_placeholder=True,
                        oversized_request_refused=True,
                        production_live_allocator_proof=observer.report())
                    cases.append(case)
                    total_operations += operations
                    if primary:
                        results.append(dict(slug=slug, source_bytes=len(raw), source_sha256=sha(startup),
                            image_bytes=len(program), image_sha256=hashlib.sha256(program).hexdigest(),
                            record_sha256=sha(record), lease_bytes=lease_bytes,
                            arena_begin=begin, arena_bytes=count, payload=payload, header=begin,
                            lease_end=payload + lease_bytes, header_units=lease_bytes // 32 + 1,
                            remaining_units=model.read(upper + 4),
                            initializer_instructions=init_steps, initializer_writes=len(init_writes),
                            allocation_instructions=sum(step[2] for step in cold),
                            allocation_writes=sum(len(step[1]) for step in cold),
                            allocation_free_operations=operations, maximum_free_nodes=maximum_nodes,
                            subsequent_allocator_instructions=future_steps, worker_bytes=len(worker),
                            auxiliary_bytes=len(auxiliary), protected_write_span=lease_bytes,
                            stage_begin=first, stage_end=end, installer_stack=stack,
                            installer_stack_bytes=stack_bytes, installer_sp=sp,
                            stage_preserved=True, installer_stack_preserved=True,
                            retained_header_and_entire_lease_preserved_during_allocator_operations=True,
                            placement="bottom", original_upper_allocation_boundary_preserved=True,
                            production_live_allocator_proof=observer.report()))
                # Include the exact minimum where allocate(0) returns the
                # arena endpoint: its payload must never be dereferenced.
                for extra in (64, 96, 128, 160):
                    minimum = lease_bytes + extra
                    model = Trace(raw)
                    model.sr = 0x600000f0
                    model.store_block(first, stage_pattern)
                    model.run(initializer, [begin, minimum], 0x8c00efc0)
                    observer.fresh(model, owner, begin, minimum, lease_bytes, len(program), first, end,
                                   placement_expected=extra >= 96)
                    payload, placeholder, _upper, _cold_calls = _cold(model, owner, begin, minimum,
                        sp, first, end, stage_pattern, len(program), observer)
                    assert payload == begin + 32
                    assert placeholder <= begin + minimum
                    if extra == 64:
                        assert placeholder == begin + minimum
                    minimum_cases.append(dict(slug=slug, alias=alias, arena_bytes=minimum,
                        placeholder_request=extra - 64, placeholder_payload=placeholder,
                        stage_transaction_admitted=extra >= 96,
                        zero_request_endpoint_not_dereferenced=True,
                        production_bottom_transitions_and_full_release_admitted=True))
    assert len(results) == len(selected) and len(cases) == len(selected) * 9
    assert len(minimum_cases) == len(selected) * 12
    assert artifact_hashes == {name: sha(build / name) for name in ARTIFACTS}
    return dict(schema="kui.native.corpus-original-heap.v1", placement="bottom",
        bottom_heap_schema=SCHEMA, enabled=True, lease_bytes=lease_bytes,
        input_manifest_path=str(manifest_path), input_manifest_sha256=sha(manifest_path),
        structural_matrix_path=str(matrix_path), structural_matrix_sha256=sha(matrix_path),
        records_directory=str(records), artifacts=artifact_hashes, inputs=inputs,
        selected_slugs=[row["slug"] for row in selected],
        structural_candidate_count=len(structural_slugs),
        full_structural_selection={row["slug"] for row in selected} == structural_slugs,
        case_count=len(cases), minimum_case_count=len(minimum_cases),
        future_original_operations=total_operations, cases=cases, minimum_cases=minimum_cases,
        results=results,
        source_sha256={name: sha(ROOT / name) for name in (
            "tools/native_runtime_layout.py", "tools/prove_vmu_native_bottom_corpus_heap.py", "tools/prove_vmu_native_corpus_heap.py",
            "tests/test_vmu_native_heap_execution.py", "tools/prove_vmu_native_evolution1_readahead_lease.py",
            "src/core/vmu_native_heap.c", "src/core/vmu_native_heap_bottom_code.inc",
            "src/core/vmu_native_lifetime.c", "src/core/vmu_native_stage_placement.c")},
        limits=["Original integer SH allocator execution plus production C state/code/placement/lifetime admission; no cycle or hardware-cache model.",
            "The same-address claim applies to allocations that still fit the reduced free capacity; oversized allocations are refused.",
            "The placeholder is never initialized or consumed as a buffer. Its numerical span may contain the existing installer stack; only the independently bounded private call frame is accessed there.",
            "Full worker/auxiliary/cache storage footprints are modeled, not runtime producer instructions or game resource decoding.",
            "Actual compiled cold-stage instruction execution and hardware acceptance require separate evidence."])


def write_report(report, build, output):
    data = json.dumps(report, indent=2) + "\n"
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(data)
    # The compiled cold-stage audit reopens the exact external inputs from
    # this artifact-bound source proof. No external executable is packaged.
    (Path(build) / "native-bottom-heap-corpus.json").write_text(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "manifest", "matrix", "records", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    report = prove(args.build, args.manifest, args.matrix, args.records)
    write_report(report, args.build, args.output)
    print(json.dumps(dict(schema=SCHEMA, titles=len(report["results"]),
                          cases=report["case_count"], minimum_cases=report["minimum_case_count"],
                          future_original_operations=report["future_original_operations"])))


if __name__ == "__main__":
    main()
