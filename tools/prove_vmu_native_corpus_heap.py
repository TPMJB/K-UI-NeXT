#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute original allocators for every structurally accepted corpus owner.

Requires current production-generated records, the verified private input
manifest, the structural matrix and an existing linked read-ahead build.
It creates no target build and embeds no original executable bytes in output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/"tests"),str(ROOT/"tools")]
import test_vmu_native_heap_execution as heap
from prove_vmu_native_evolution1_readahead_lease import LiveAllocatorProof


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build",type=Path,required=True)
    parser.add_argument("--manifest",type=Path,required=True)
    parser.add_argument("--matrix",type=Path,required=True)
    parser.add_argument("--records",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    manifest=json.loads(args.manifest.read_text());matrix=json.loads(args.matrix.read_text())
    assert manifest["schema"]=="kui.private.native-startup-inputs.v1"
    assert matrix["schema"]=="kui.native.structural-corpus.v1" and matrix["lease_bytes"]==0x60000
    assert matrix["input_manifest_sha256"]==sha(args.manifest)
    entries={entry["slug"]:entry for entry in manifest["files"]}
    selected=[row for row in matrix["results"] if row["structural_384k_models_passed"]]
    assert selected
    build=args.build.resolve();artifacts=("stage.elf","native-worker.elf","native-worker.bin","native-ra-aux.bin")
    artifact_hashes={name:sha(build/name) for name in artifacts}
    results=[];original=heap.OPTIONS
    with tempfile.TemporaryDirectory(prefix="kui-corpus-original-heap-") as temporary:
        observer=LiveAllocatorProof(Path(temporary),True)
        try:
            for row in selected:
                slug=row["slug"];entry=entries[slug];startup=Path(entry["startup"])
                assert entry["windows_ce"] is False and startup.stat().st_size==entry["source_bytes"]
                assert sha(startup)==entry["source_sha256"]==row["source_sha256"]
                heap.OPTIONS=argparse.Namespace(build=build,record_directory=args.records.resolve(),
                    lease_bytes=0x60000,allocator_observer=observer,**{slug:startup})
                heap.HeapExecutionTests("test_evolution1").execute(slug)
                results.append(heap.RESULTS[-1])
        finally:
            heap.OPTIONS=original
    assert artifact_hashes=={name:sha(build/name) for name in artifacts},"Linked artifacts changed during proof"
    report=dict(schema="kui.native.corpus-original-heap.v1",lease_bytes=0x60000,
        input_manifest_sha256=sha(args.manifest),structural_matrix_sha256=sha(args.matrix),
        artifacts=artifact_hashes,results=results,
        source_sha256={name:sha(ROOT/name) for name in (
            "tools/prove_vmu_native_corpus_heap.py","tools/prove_vmu_native_evolution1_readahead_lease.py",
            "tests/test_vmu_native_heap_execution.py","src/core/vmu_native_heap.c",
            "src/core/vmu_native_lifetime.c","src/core/vmu_native_stage_placement.c")},
        limits=["Original integer SH allocator execution and production live free-list/release admission only; no cycle or hardware-cache model.",
            "Worker, auxiliary and cache writes cover their complete legal footprint; auxiliary relocation/producer execution are proved separately.",
            "Actual linked stage/installer stack, lease header and full384KiB reservation must remain disjoint and preserved.",
            "A structural+allocator pass still requires independently admitted SDK SCI routing, full loader/VMU integration and console acceptance."])
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+"\n")


if __name__=="__main__":main()
