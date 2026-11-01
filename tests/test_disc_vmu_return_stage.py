#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the production physical-disc entry-return binder on shared P1/P2 RAM."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="disc-return-binder-") as directory:
    output=Path(directory)/"return-binder"
    # Exercise the existing installer's actual exit loop, not a second copy of
    # its checks. All surrounding high-stage/SH machinery has separate audits.
    source=(ROOT/"src/loader/vmu_native_passive_stage.inc").read_text()
    def function(name):
        start=source.index("static void "+name+"(")
        brace=source.index("{",start)
        end=brace+1
        depth=1
        while depth:
            depth+=(source[end]=="{")-(source[end]=="}")
            end+=1
        return source[start:end]
    # The early pair shares its literal across two entries. Exercise the
    # production writer as well as its production retarget loop.
    (Path(directory)/"disc-vmu-exit-writer-fixture.h").write_text(
        function("readonly_patch_head")+"\n"+
        function("readonly_patch_entry").replace("static void readonly_patch_entry(",
            "static void actual_patch_entry(")+"\n")
    marker=source.index("readonly_patch_entry(native_heap.release,e->release_hook);")
    start=source.index("for(unsigned i=0;i<native_exits.count;i++) {",marker)
    brace=source.index("{",start)
    end=brace+1
    depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}")
        end+=1
    (Path(directory)/"disc-vmu-exit-loop-fixture.h").write_text(
        "static void actual_installer_exit_loop(void) {\n"+source[start:end]+"\n}\n")
    subprocess.run([*shlex.split(os.environ.get("CC","cc")),"-std=c11","-O1","-g",
        "-Wall","-Wextra","-Werror","-Wpedantic","-fsanitize=undefined","-fno-sanitize-recover=all",
        "-fno-omit-frame-pointer","-fno-pie","-no-pie","-ffunction-sections","-fdata-sections",
        "-Wl,--gc-sections","-Iinclude","-I"+directory,"tests/test_disc_vmu_return_stage.c",
        "src/core/vmu_native_patch.c","src/core/disc_vmu_entry.c","-o",str(output)],cwd=ROOT,check=True)
    subprocess.run([str(output)],cwd=ROOT,check=True)
