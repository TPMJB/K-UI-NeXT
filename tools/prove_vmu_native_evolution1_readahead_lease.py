#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prove Evolution1's original allocator and selected read-ahead lease geometry.

Uses an existing linked read-ahead stage/worker/auxiliary; never rebuilds target
code. The host fixture compiles the production live worker, allocator, release,
placement and exact low resident destination predicates. The independent SH
model executes owner-supplied original allocator code, not a replacement heap.
No owner executable bytes are included in the JSON report.
"""
import argparse
import ctypes
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
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools")]
import test_vmu_native_heap_execution as heap

OWNER_SHA256 = "ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35"
EVOLUTION2_SHA256 = "16b45fcc84992a60d5a64bf64bf22b699b27306b95f177ba3d9730ca380069ae"
HOST_SOURCES = (
    "tests/test_vmu_native_evolution_transfer.c", "src/core/vmu_native_heap.c",
    "src/core/vmu_native_runtime.c", "src/core/vmu_native_layout.c",
    "src/core/vmu_native_live.c", "src/core/vmu_native_lifetime.c",
    "src/core/vmu_native_profile.c", "src/core/vmu_native_discovery.c",
    "src/core/vmu_native_patch.c", "src/core/hash.c", "src/core/data.c",
    "src/core/vmu_durable.c", "src/core/vmu_store.c", "src/core/vmu_emulation.c",
    "src/core/retail_image.c", "src/loader/vmu_readonly_overlay.c",
    "src/loader/vmu_volatile_chain.c", "src/loader/sd_reader.c",
    "src/loader/vmu_backing.c", "src/dreamcast/sci_sd_storage.c",
    "src/core/vmu_native_resolution_cache.c", "src/core/vmu_native_exit.c",
    "src/core/vmu_native_stage_placement.c",
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode:
        sys.stderr.write(result.stdout + result.stderr)
        result.check_returncode()
    return result


class LiveAllocatorProof:
    """Read the original SH model's live heap through production C proofs."""
    Reader = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_uint32,
                             ctypes.POINTER(ctypes.c_uint32))

    def __init__(self, directory, readahead):
        library = directory / f"allocator-proof-{int(readahead)}.so"
        checked([*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O2",
            "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-shared", "-fPIC",
            f"-DKUI_NATIVE_READAHEAD={int(readahead)}", "-I", str(ROOT / "include"),
            *[str(ROOT / name) for name in ("src/core/vmu_native_heap.c",
                "src/core/vmu_native_lifetime.c", "src/core/vmu_native_stage_placement.c")],
            "-o", str(library)])
        self.library = ctypes.CDLL(str(library))
        ptr, word = ctypes.c_void_p, ctypes.c_uint32
        self.fresh_c = self.library.kui_vmu_native_heap_fresh
        self.fresh_c.argtypes = [ptr, self.Reader, ptr, word, word, word, word, ptr]
        self.pre = self.library.kui_vmu_native_heap_preallocate
        self.pre.argtypes = [ptr, self.Reader, ptr, word]
        self.post = self.library.kui_vmu_native_heap_postallocate
        self.post.argtypes = [ptr, self.Reader, ptr, word, word]
        self.valid_c = self.library.kui_vmu_native_heap_lease_valid
        self.valid_c.argtypes = [ptr, self.Reader, ptr, word]
        self.release_c = self.library.kui_vmu_native_lifetime_release_safe
        self.release_c.argtypes = [ptr, self.Reader, ptr, word, word]
        self.placement = self.library.kui_vmu_native_stage_allocation_safe
        self.placement.argtypes = [ptr, word, word, word, word, word]
        for function in (self.fresh_c, self.pre, self.post, self.valid_c, self.release_c, self.placement):
            function.restype = ctypes.c_bool

    def fresh(self, model, words, begin, count, lease_bytes, image_bytes, stage_begin, stage_end):
        self.heap = ctypes.create_string_buffer(struct.pack("<18I", *words))
        self.lease = ctypes.create_string_buffer(116)
        self.reads = self.validations = self.releases = self.placement_cases = 0
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
        assert self.fresh_c(self.heap, self.reader, None, begin, count, 1, lease_bytes, self.lease)
        assert self.pre(self.lease, self.reader, None, 1)
        assert not self.pre(self.lease, self.reader, None, 2)
        # The selected production placement code sees metadata verified from
        # this same original executable and the actual linked stage bounds.
        header = heap.physical(begin) + count - lease_bytes - 32
        for alias in (0, 0x80000000, 0xa0000000):
            arena = heap.physical(begin) | alias
            assert self.placement(self.heap, image_bytes, stage_begin, stage_end, arena, count)
            self.placement_cases += 1
            for at in (header, header + 32, header + 32 + 0x20000, header + 32 + lease_bytes - 1):
                assert not self.placement(self.heap, image_bytes, at | alias, (at + 1) | alias, arena, count)
                self.placement_cases += 1
            assert self.placement(self.heap, image_bytes, (header - 32) | alias, header | alias, arena, count)
            assert not self.placement(self.heap, image_bytes, (header - 32) | alias, (header + 1) | alias, arena, count)
            self.placement_cases += 2
        assert not self.errors

    def allocated(self, payload):
        bad = ctypes.create_string_buffer(self.lease.raw, len(self.lease))
        assert not self.post(bad, self.reader, None, 1, payload + 32)
        assert self.post(self.lease, self.reader, None, 1, payload)
        assert not self.valid_c(self.lease, self.reader, None, 2)
        assert not self.release_c(self.lease, self.reader, None, 1, payload)
        self.valid()

    def valid(self):
        assert self.valid_c(self.lease, self.reader, None, 1)
        assert not self.errors
        self.validations += 1

    def release(self, payload):
        assert self.release_c(self.lease, self.reader, None, 1, payload)
        assert not self.errors
        self.releases += 1

    def report(self):
        return dict(production_full_free_list_validations=self.validations,
            production_release_admissions=self.releases, coherent_word_reads=self.reads,
            actual_metadata_placement_cases=self.placement_cases,
            wrong_payload_and_reinitializer_count_refused=True, lease_self_release_refused=True)


def prove(build, startup, evolution2=None):
    assert __debug__, "Run the independent SH model with assertions enabled"
    assert len(startup.read_bytes()) == 2143652 and sha(startup) == OWNER_SHA256
    artifact_names = ("stage.elf", "native-worker.elf", "native-worker.bin", "native-ra-aux.bin")
    artifact_hashes = {name: sha(build / name) for name in artifact_names}
    first, end, stack, stack_bytes, _, _ = heap.stage_layout(build)
    if evolution2:
        assert len(evolution2.read_bytes()) == 4057056 and sha(evolution2) == EVOLUTION2_SHA256
    original = heap.OPTIONS
    results = []
    with tempfile.TemporaryDirectory(prefix="kui-original-live-allocator-") as directory:
        try:
            for lease_bytes in (0x30000, 0x60000):
                observer = LiveAllocatorProof(Path(directory), lease_bytes == 0x60000)
                for slug, path in (("evolution1", startup), ("evolution2", evolution2)):
                    if path is None:
                        continue
                    heap.OPTIONS = argparse.Namespace(build=build, lease_bytes=lease_bytes,
                        allocator_observer=observer, **{slug: path})
                    test = heap.HeapExecutionTests("test_" + slug)
                    test.execute(slug)
                    results.append(heap.RESULTS[-1])
        finally:
            heap.OPTIONS = original
    source = ROOT / "src/loader/vmu_native_background_resident.c"
    matched = re.search(r"/\* NATIVE_RESIDENT_GUARDS_BEGIN.*?\*/\n(.*?)/\* NATIVE_RESIDENT_GUARDS_END \*/",
                        source.read_text(), re.S)
    assert matched and "native_resident_transaction" in matched.group(1)
    records = []
    with tempfile.TemporaryDirectory(prefix="kui-evolution1-selected-lease-") as directory:
        directory = Path(directory)
        (directory / "vmu_native_background_guards.h").write_text(matched.group(1))
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        for enabled, cached in ((0, 0), (0, 1), (1, 0), (1, 1)):
            binary = directory / f"transfer-{enabled}-{cached}"
            command = [*compiler, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-Wpedantic", "-ffunction-sections", "-fdata-sections", "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-I", str(ROOT / "include"),
                "-I", str(ROOT / "src/loader"), "-I", str(ROOT / "tests"), "-I", str(directory),
                "-DKUI_VMU_EXPERIMENTAL=1", f"-DKUI_NATIVE_READAHEAD={enabled}",
                f"-DKUI_VMU_NATIVE_CACHED_CODE={cached}",
                "-DKUI_VMU_NATIVE_ENLARGED_LEASE_TEST=1", *[str(ROOT / name) for name in HOST_SOURCES],
                "-Wl,--gc-sections", "-o", str(binary)]
            checked(command)
            run = checked([str(binary), str(startup),
                str(ROOT / "tests/fixtures/native-cache/evolution1.nvr"), hex(first), hex(end)],
                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
            print(run.stdout, end="")
            counts = re.search(r"Selected Evolution lease (\d+) KiB: destination=(\d+) lifetime=(\d+) release-hook=(\d+) placement=(\d+)", run.stdout)
            transfers = re.search(r"Actual uniform native first transfer: (\d+) checks,108 owner/manager/PR alias-bank-SR cases,(\d+) context read refusals", run.stdout)
            assert counts and transfers
            size, destinations, lifetime, releases, placement = map(int, counts.groups())
            assert size == (384 if enabled else 192) and int(transfers[2]) == 26
            records.append(dict(lease_bytes=size * 1024, checks=int(transfers[1]),
                cached_code=bool(cached),
                first_transfer_cases=108, context_read_refusals=26,
                low_destination_refusals=destinations, paired_lifetime_refusal_cases=lifetime,
                actual_release_hook_refusals=releases, actual_metadata_placement_cases=placement,
                asan_ubsan_passed=True, virtual_info_publication_only=True, storage_calls=0))
    assert artifact_hashes == {name: sha(build / name) for name in artifact_names}, "Linked artifacts changed during proof"
    return dict(schema="kui.native.evolution1.read-ahead-lease-proof.v1",
        input=dict(bytes=2143652, sha256=OWNER_SHA256,
            record_sha256=sha(ROOT / "tests/fixtures/native-cache/evolution1.nvr")),
        retained_evolution2_input=dict(bytes=4057056, sha256=EVOLUTION2_SHA256) if evolution2 else None,
        artifacts=artifact_hashes,
        geometry=dict(stage_begin=first, stage_end=end, installer_stack=stack,
            installer_stack_bytes=stack_bytes, worker_bytes=0x20000,
            readahead_lease_bytes=0x60000, cache_bytes=0x30000),
        original_sh=results, live_worker=records,
        low_destination_guards_sha256=hashlib.sha256(matched.group(1).encode()).hexdigest(),
        test_source_sha256={name: sha(ROOT / name) for name in
            ("tests/test_vmu_native_heap_execution.py", "tests/test_vmu_native_evolution_transfer.c",
             "tools/prove_vmu_native_evolution1_readahead_lease.py")},
        production_source_sha256={name: sha(ROOT / name) for name in
            ("src/core/vmu_native_heap.c", "src/core/vmu_native_lifetime.c",
             "src/core/vmu_native_stage_placement.c", "src/loader/vmu_native_passive.c")},
        limits=["Allocator model executes original integer SH instructions; it does not model hardware cache or scheduling.",
            "192KiB runs are historical-request-size controls with the current worker footprint, not a claim that the read-ahead auxiliary fits or runs in192KiB.",
            "Worker copy, auxiliary initialization and full cache writes are maximum owned-footprint writes, not simulated producer execution.",
            "Host worker executes real C admission/release/transfer logic with modeled RAM, MMIO and SCI storage.",
            "This proves the selected linked artifact geometry only; rerun against the final candidate's stage and worker.",
            "No SDK SCI-route admission, console boot, gameplay, persistence or PIO timing result is established here."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--startup", type=Path, required=True)
    parser.add_argument("--evolution2", type=Path, help="Also prove the retained Evolution2 owner with both request sizes")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = prove(args.build.resolve(), args.startup.resolve(), args.evolution2.resolve() if args.evolution2 else None)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    else:
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
