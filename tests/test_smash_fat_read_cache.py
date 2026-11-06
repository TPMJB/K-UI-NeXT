#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the actual fresh FAT cache and filesystem admission under sanitizers."""
import os
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def audit():
    with tempfile.TemporaryDirectory(prefix="kui-smash-fat-cache-") as folder:
        env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0","UBSAN_OPTIONS":"halt_on_error=1"}
        outputs=[];source_names=set()
        for fixture in ("test_smash_fat_read_cache","test_smash_fat_read_cache_exfat"):
            binary=Path(folder)/fixture
            sources=("tests/"+fixture+".c","src/loader/smash_fat_read_cache.c",
                     "src/core/vmu_fs_admission.c","src/core/vmu_persist_profile.c",
                     "src/core/hash.c","src/core/retail_image.c")
            command=(*shlex.split(os.environ.get("HOST_CC","cc")),"-std=c11","-O1","-g",
                     "-Wall","-Wextra","-Werror","-Wpedantic","-fsanitize=address,undefined",
                     "-fno-sanitize-recover=all","-fno-pie","-no-pie","-ffunction-sections",
                     "-fdata-sections","-Iinclude",*sources,"-Wl,--gc-sections","-o",str(binary))
            subprocess.run(command,cwd=ROOT,check=True)
            outputs.append(subprocess.check_output((str(binary),),cwd=ROOT,env=env,text=True))
            source_names.update(sources)
        output=''.join(outputs)
        counts=[dict(copies=int(copies),requested_sectors=int(requested),old_sessions=int(old),
                     single_reads=int(single),bounded_window_sessions=int(windows))
                for copies,requested,old,single,windows in re.findall(
                    r'FAT(\d+) collect: (\d+) requested sectors, (\d+) old sessions -> (\d+) single \+ (\d+) bounded windows;',output)]
        counts=[next(row for row in counts if row['copies']==copies) for copies in (1,2)]
        assert [(row['old_sessions'],row['single_reads']+row['bounded_window_sessions'])
                for row in counts]==[(528,48),(1041,81)]
        assert 'crosslinks, mirror mutation, CRC prefixes, failed stops, epochs and write invalidation passed' in output
        assert 'Fresh exFAT cache: exact old/new collect/admit answers and ownership;' in output
        return dict(schema='kui.smash-menu.fresh-fat-cache.v1',status='PASS',
                    sanitizers=['address','undefined'],full_fresh_admission=True,
                    identical_logical_requests_and_answers=True,session_baseline=counts,
                    failed_stops_and_crc_suffixes_refused=True,epoch_and_write_invalidation=True,
                    fat32_and_exfat=True,
                    source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sorted(source_names)},
                    output=output)


def main():
    print(json.dumps(audit(),indent=2))

if __name__=="__main__":main()
