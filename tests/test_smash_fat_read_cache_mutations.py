#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject unsafe FAT-cache mutations using the actual admission fixture."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def audit():
    core=ROOT/"src/loader/smash_fat_read_cache.c"
    original=core.read_text()
    mutations=(
        ("unverified-suffix","w->count=verified;","w->count=count;"),
        ("uncertain-stop","if(result || !verified || verified>count)","if(!verified || verified>count)"),
        ("mirror-alias","*w=&c->windows[i];","*w=&c->windows[0];"),
        ("fat-tail-overrun","uint32_t count=c->fat_blocks-offset;","uint32_t count=KUI_SMASH_FAT_CACHE_WINDOW_BLOCKS;"),
        ("failed-vbr-learns","if(!result && (uint64_t)lba==c->partition_first)","if((uint64_t)lba==c->partition_first)"),
    )
    env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0","UBSAN_OPTIONS":"halt_on_error=1"}
    rejected=[]
    with tempfile.TemporaryDirectory(prefix="kui-smash-fat-mutants-") as folder:
        temporary=Path(folder)
        for name,needle,replacement in mutations:
            if original.count(needle)!=1:
                raise RuntimeError("Mutation target changed: "+name)
            changed=temporary/(name+".c");changed.write_text(original.replace(needle,replacement))
            binary=temporary/(name+"-fixture")
            sources=("tests/test_smash_fat_read_cache.c",str(changed),
                     "src/core/vmu_fs_admission.c","src/core/vmu_persist_profile.c",
                     "src/core/hash.c","src/core/retail_image.c")
            command=(*shlex.split(os.environ.get("HOST_CC","cc")),"-std=c11","-O1","-g",
                     "-Wall","-Wextra","-Werror","-Wpedantic","-fsanitize=address,undefined",
                     "-fno-sanitize-recover=all","-fno-pie","-no-pie","-ffunction-sections",
                     "-fdata-sections","-Iinclude",*sources,"-Wl,--gc-sections","-o",str(binary))
            subprocess.run(command,cwd=ROOT,check=True)
            result=subprocess.run((str(binary),),cwd=ROOT,env=env,capture_output=True,text=True)
            if not result.returncode:
                raise RuntimeError("Unsafe FAT cache mutation passed: "+name)
            if not any(word in result.stderr for word in ("Assertion","runtime error","AddressSanitizer")):
                raise RuntimeError("Mutation failed for an unexplained reason: "+name+"\n"+result.stderr)
            rejected.append(name)
    if core.read_text()!=original:
        raise RuntimeError("Production FAT cache changed during mutation checks")
    return dict(schema="kui.smash-menu.fresh-fat-mutations.v1",status="PASS",
                mutation_rejections=len(rejected),mutations=rejected,
                sanitizers=["address","undefined"],
                source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
                    ("src/loader/smash_fat_read_cache.c","include/kui/smash_fat_read_cache.h",
                     "tests/test_smash_fat_read_cache.c","tests/test_vmu_fs_admission_adversarial.c")})

def main():
    print(json.dumps(audit(),indent=2))

if __name__=="__main__":main()
