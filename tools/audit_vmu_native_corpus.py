#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run production structural gates and original Maple transport over a corpus.

The report is necessary structural evidence, not SCI-route or console admission.
Explicit --input SLUG=PATH values and recursive --inputs directories are allowed.
No game bytes are written to the report or copied into the repository.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCES=("tools/vmu_native_corpus_probe.c","src/core/vmu_native_program.c",
    "src/core/vmu_native_resolution_cache.c","src/core/vmu_native_discovery.c",
    "src/core/vmu_native_layout.c","src/core/vmu_native_runtime.c","src/core/vmu_native_heap.c",
    "src/core/vmu_native_exit.c","src/core/vmu_native_patch.c","src/core/vmu_native_stage_placement.c",
    "src/core/hash.c","src/core/data.c")
PROOF_FILES=(*SOURCES,"tests/test_vmu_native_layout.c","tools/audit_vmu_native_corpus.py",
    *tuple("include/kui/"+name for name in ("vmu_native_program.h","vmu_native_discovery.h",
        "vmu_native_layout.h","vmu_native_runtime.h","vmu_native_heap.h","vmu_native_exit.h",
        "vmu_native_patch.h","vmu_native_resolution_cache.h","vmu_native_stage_placement.h",
        "vmu_native_background.h","native_readahead_layout.h")))
GATES=("extent","driver","layout","heap","initializer_slot","initializer_call",
       "aligned_hook_entries","release_hook_shape","exits","guarded_exits",
       "combined_resolution","placement_384k","transport_execution","live_bind_384k")


def input_pair(value):
    slug,separator,path=value.partition("=")
    if not separator or not slug or not path:raise argparse.ArgumentTypeError("Expected SLUG=PATH")
    return slug,Path(path)


def audit(inputs,stage_begin,stage_end,records=None):
    rows=[]
    hashes={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in PROOF_FILES}
    with tempfile.TemporaryDirectory(prefix="kui-native-structural-corpus-") as temporary:
        executable=Path(temporary)/"probe"
        command=[*shlex.split(os.environ.get("HOST_CC","cc")),"-std=c11","-O2","-Wall","-Wextra",
            "-Werror","-Wpedantic","-DKUI_NATIVE_READAHEAD=1","-I",str(ROOT/"include"),
            *[str(ROOT/name) for name in SOURCES],"-o",str(executable)]
        subprocess.run(command,check=True)
        for slug,path in inputs:
            run=subprocess.run([str(executable),str(path),hex(stage_begin),hex(stage_end)],
                capture_output=True,text=True)
            lines=[json.loads(line) for line in run.stdout.splitlines() if line.startswith("{")]
            row=dict(slug=slug,input_name=path.name,probe_returncode=run.returncode)
            for line in lines:row.update(line)
            wire=row.pop("record_hex",None)
            if wire is not None and records:
                records.mkdir(parents=True,exist_ok=True)
                destination=records/(slug+".nvr")
                destination.write_bytes(bytes.fromhex(wire));row["record_sha256"]=hashlib.sha256(destination.read_bytes()).hexdigest()
            if run.returncode:row["probe_diagnostic"]=run.stderr.strip()
            row["first_refusal"]=next((name for name in GATES if row.get(name)!=(1 if name in ("driver","heap") else True)),None)
            row["structural_384k_models_passed"]=not row["first_refusal"] and not run.returncode
            row["sci_route_admission"]="independent_required"
            row["console_compatibility"]="not_tested_by_this_probe"
            rows.append(row)
            print(f"{slug}: {row['first_refusal'] or 'structural models passed'}",file=sys.stderr)
    assert hashes=={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in PROOF_FILES},"Structural proof sources changed during audit"
    return dict(schema="kui.native.structural-corpus.v1",lease_bytes=0x60000,
        detector_epoch=rows[0]["detector_epoch"],
        stage_begin=stage_begin,stage_end=stage_end,inputs=len(rows),
        distinct_source_hashes=len({row.get("source_sha256") for row in rows if row.get("source_sha256")}),
        first_refusals=dict(Counter(row["first_refusal"] or "models_passed" for row in rows)),
        source_sha256=hashes,results=rows,
        limits=["Static discovery, guarded terminal interception and384KiB placement are independent necessary gates.",
            "The original transport interpreter supplies adversarial ABI returns for unrelated helper calls; it is not a complete game emulator.",
            "Live layout binding uses the original initializer's modeled RAM result, not observations from the console.",
            "Original allocator execution, retained live ownership, SDK SCI route, full cache/DMA integration and console save/load require separate proofs.",
            "A generated NVR record is immutable discovery metadata and grants no live allocation or storage authority."])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs",type=Path,action="append",default=[])
    parser.add_argument("--manifest",type=Path,
        help="Verified private input manifest; permits non-1ST_READ native boot names")
    parser.add_argument("--input",type=input_pair,action="append",default=[])
    parser.add_argument("--stage-begin",type=lambda value:int(value,0),default=0x8ce00000)
    parser.add_argument("--stage-end",type=lambda value:int(value,0),required=True,
        help="Exact linked stage BSS end; do not substitute a shorter initialized-file boundary")
    parser.add_argument("--records",type=Path,help="Optional generated metadata records for independently accepted complete discovery")
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args();inputs=list(args.input);metadata={}
    if args.manifest:
        manifest=json.loads(args.manifest.read_text())
        if manifest.get("schema")!="kui.private.native-startup-inputs.v1":parser.error("Unknown input manifest schema")
        for entry in manifest["files"]:
            path=Path(entry["startup"]);source=path.read_bytes()
            if len(source)!=entry["source_bytes"] or hashlib.sha256(source).hexdigest()!=entry["source_sha256"]:
                parser.error("Input manifest source identity differs: "+entry["slug"])
            if entry.get("windows_ce") is not False:parser.error("Manifest row is not proved native: "+entry["slug"])
            inputs.append((entry["slug"],path))
            metadata[entry["slug"]]={key:entry[key] for key in ("title","boot_filename","windows_ce")}
    for directory in args.inputs:
        for path in sorted(directory.rglob("*")):
            if path.is_file() and path.name.lower()=="1st_read.bin":inputs.append((path.parent.name,path))
    if not inputs:parser.error("Provide at least one original startup")
    if len({slug for slug,_ in inputs})!=len(inputs):parser.error("Input slugs must be unique")
    result=audit(inputs,args.stage_begin,args.stage_end,args.records)
    for row in result["results"]:row.update(metadata.get(row["slug"],{}))
    if args.manifest:result["input_manifest_sha256"]=hashlib.sha256(args.manifest.read_bytes()).hexdigest()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+"\n")


if __name__=="__main__":main()
