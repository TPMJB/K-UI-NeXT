#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute private/default launcher admission with invented disc metadata."""
from pathlib import Path
import subprocess
import os
import shlex
import tempfile
from test_loader_probe_images import partition_image

root=Path(__file__).resolve().parents[1]
out=root/'build/toy-report-launcher-host'
out.mkdir(parents=True,exist_ok=True)
for enabled in (0,1):
    binary=out/str(enabled)
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=undefined','-fno-sanitize-recover=all','-Iinclude','-Iconfig','-I.deps/fatfs/source',
        '-DKUI_RETAIL_EXTENDED_MAP=1',f'-DKUI_TOY_REPORT_TEST={enabled}',
        'tests/test_toy_report_launcher.c','src/core/retail_image.c',
        'src/core/hash.c','src/core/data.c','-o',str(binary)],cwd=root,check=True)
    subprocess.run([str(binary)],cwd=root,check=True)

# Compile the actual production preparation function using its existing FAT
# fixture and complete host source list, instead of a duplicate mock launcher.
recipe=subprocess.check_output(['make','-n','build/games-retail'],cwd=root,text=True)
command=next(shlex.split(line) for line in recipe.splitlines()
             if 'tests/games_retail.c' in line and ' -o ' in line)
command[command.index('tests/games_retail.c')]='tests/test_toy_report_prepare.c'
command[command.index('-o')+1]=str(out/'prepare')
command.insert(1,'-DKUI_TOY_REPORT_TEST=1')
command.insert(2,'-DKUI_BUILD_ID="123456789abc"')
subprocess.run(command,cwd=root,check=True)
with tempfile.TemporaryDirectory(prefix='toy-report-prepare-') as temporary:
    directory=Path(temporary)
    for kind in ('fat32','exfat'):
        volume=directory/(kind+'.volume')
        with volume.open('wb') as stream:stream.truncate(96*1024*1024)
        mkfs=['mkfs.fat','-F','32',str(volume)] if kind=='fat32' else ['mkfs.exfat',str(volume)]
        subprocess.run(mkfs,check=True,stdout=subprocess.DEVNULL)
        image=directory/(kind+'.img');partition_image(volume,image,kind)
        result=subprocess.run([str(out/'prepare'),str(image)],cwd=root,check=True,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),text=True,capture_output=True)
        assert 'turn Emulated VMU Off' in result.stdout
        print(result.stdout,end='')
