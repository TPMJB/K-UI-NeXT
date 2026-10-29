#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Join independent VMU, original allocator and SCI source-family evidence.

This produces software test candidates only. It never promotes a source or
simulated-live result to console boot, VMU persistence or performance evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
from native_runtime_layout import LEASE_BYTES, GENERAL, DIAGNOSTIC


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--structural",type=Path,required=True)
    parser.add_argument("--heap",type=Path,required=True)
    parser.add_argument("--sci",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--expected-joint",type=int)
    parser.add_argument("--diagnostic-layout",action="store_true",
        help="Join the exact retained diagnostic runtime geometry")
    args=parser.parse_args()
    selected_layout=DIAGNOSTIC if args.diagnostic_layout else GENERAL
    structural=json.loads(args.structural.read_text());heaps=json.loads(args.heap.read_text());sci=json.loads(args.sci.read_text())
    assert structural["schema"]=="kui.native.structural-corpus.v1"
    assert heaps["schema"]=="kui.native.corpus-original-heap.v1"
    assert sci["schema"]=="kui.native.sdk-sci.structural-source-matrix.v1"
    assert heaps["structural_matrix_sha256"]==sha(args.structural)
    assert structural["lease_bytes"]==heaps["lease_bytes"]==selected_layout.LEASE_BYTES
    heap_by_slug={row["slug"]:row for row in heaps["results"]}
    sci_by_slug={row["slug"]:row for row in sci["inputs"]}
    assert len(sci_by_slug)==len(sci["inputs"])
    rows=[]
    for row in structural["results"]:
        slug=row["slug"];interrupt=sci_by_slug[slug];allocation=heap_by_slug.get(slug)
        assert row["source_sha256"]==interrupt["source_sha256"]
        assert row["source_bytes"]==interrupt["source_bytes"]
        assert row.get("windows_ce") is False and interrupt["windows_ce"] is False
        vm=bool(row["structural_384k_models_passed"])
        allocator=allocation is not None
        if allocator:
            assert allocation["source_sha256"]==row["source_sha256"]
            assert allocation["image_sha256"]==row["image_sha256"]
            assert allocation["lease_bytes"]==selected_layout.LEASE_BYTES
            assert allocation["stage_begin"]==structural["stage_begin"] and allocation["stage_end"]==structural["stage_end"]
            assert allocation["retained_header_and_entire_lease_preserved_during_allocator_operations"] is True
        route=interrupt["source_family_result"]==1
        reason=row["first_refusal"] if not vm else "original_allocator_proof" if not allocator else "sdk_sci_source_family" if not route else None
        rows.append(dict(slug=slug,title=row.get("title",slug),boot_filename=row.get("boot_filename",row["input_name"]),
            source_bytes=row["source_bytes"],source_sha256=row["source_sha256"],
            effective_image_bytes=row["image_bytes"],effective_image_sha256=row["image_sha256"],
            vmu_structural_and_original_maple_transport=vm,original_allocator_lifetime_384k=allocator,
            sdk_sci_source_and_simulated_live_family=route,joint_host_candidate=vm and allocator and route,
            first_refusal=reason,new_candidate_console_boot=False,new_candidate_console_a2_persistence=False,
            new_candidate_console_performance=False))
    admitted=[row["slug"] for row in rows if row["joint_host_candidate"]]
    if args.expected_joint is not None:assert len(admitted)==args.expected_joint,(admitted,args.expected_joint)
    report=dict(schema="kui.native.joint-corpus.v1",inputs=len(rows),lease_bytes=selected_layout.LEASE_BYTES,
        joint_host_candidates=len(admitted),
        joint_candidate_slugs=admitted,proof_sha256={"structural":sha(args.structural),"original_heap":sha(args.heap),"sci_source":sha(args.sci)},
        artifacts=heaps["artifacts"],results=rows,
        limits=["Joint admission combines independent source-family and modeled live-state proofs, not observed console execution.",
            "A selected launch still must pass every actual live initializer, allocator, SCI route, retained map, cache, DMA and VMU ownership guard.",
            "No source-family pass establishes a game revision outside its actual supplied image, physical VMU behavior, persistence or playback quality."])
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+"\n")
    print(f"Joint software candidates {len(admitted)}/{len(rows)}: "+", ".join(admitted))


if __name__=="__main__":main()
