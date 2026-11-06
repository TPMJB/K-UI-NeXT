#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit actual shared launch checksum helpers against generated image layouts."""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def real_fatfs_audit():
    """Reexecute established production launch fixtures without mkfs tools.

    Private FatFs formatting provides real disposable FAT32/exFAT volumes;
    this does not claim an independent fsck implementation was available.
    """
    from test_games_library_images import private_formatter
    from test_games_retail import CHECKSUM_CASES, make_retail_fixture
    from test_loader_probe_images import digest, partition_image
    environment={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'}
    cases=(*CHECKSUM_CASES,'valid','boot-tail','cooked-2048','cooked-boot-tail',
        'async-cooked-2048','ce-probe','ce-probe-cooked','ce-probe-async')
    rows=[]
    with tempfile.TemporaryDirectory(prefix='kui-launch-checksum-fatfs-') as directory:
        folder=Path(directory);binary=folder/'games-retail'
        plan=subprocess.check_output(['make','-n','-B','build/games-retail'],cwd=ROOT,text=True)
        commands=[shlex.split(line) for line in plan.splitlines() if 'tests/games_retail.c' in line and ' -o ' in line]
        assert len(commands)==1 and commands[0][-2:]==['-o','build/games-retail']
        command=commands[0];command[-1]=str(binary)
        inputs={name for name in command if name.endswith('.c') and (ROOT/name).is_file()}
        inputs.update(('tests/test_games_retail_checksum_batch.py','tests/test_games_retail.py',
            'tests/games_fixture.py','tests/game_format_fixture.py','tests/format_fatfs_fixture.c',
            'tests/test_games_library_images.py','tests/test_loader_probe_images.py','include/kui/game_image.h'))
        hashes={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sorted(inputs)}
        subprocess.run([*command,'-fno-pie','-no-pie'],cwd=ROOT,env=environment,check=True,capture_output=True,text=True)
        binary_hash=hashlib.sha256(binary.read_bytes()).hexdigest()
        formatter=private_formatter(folder)
        for kind in ('fat32','exfat'):
            volume=folder/(kind+'.fs');clean=folder/(kind+'.img')
            subprocess.run([str(formatter),kind,str(volume)],env=environment,check=True,capture_output=True,text=True)
            partition_image(volume,clean,kind)
            for case in cases:
                fixture=folder/'fixture'
                if fixture.exists():shutil.rmtree(fixture)
                make_retail_fixture(fixture,case)
                image=folder/'working.img';shutil.copyfile(clean,image)
                outputs=[]
                for operation in ('seed','check'):
                    result=subprocess.run([str(binary),str(image),str(fixture),operation,case],
                        cwd=ROOT,env=environment,capture_output=True,text=True,timeout=60)
                    if result.returncode:raise AssertionError(f'{kind}/{case}/{operation}: {result.stdout}{result.stderr}')
                    if operation=='seed':before=digest(image)
                    outputs.append(result.stdout)
                assert digest(image)==before,'Launch check modified the card: '+case
                output=outputs[-1]
                assert 'PASS retail preparation check '+case in output
                row=dict(filesystem=kind,case=case,card_unchanged=True)
                if case in CHECKSUM_CASES and not any(x in case for x in ('read-fail','short-read','cancel-','bad-')):
                    counts=re.search(r'Checksum direct reads PASS: IP (\d+) reads, exact 66537-byte boot (\d+) reads; max FatFs read (\d+) bytes',output)
                    assert counts,'Missing measured production FatFs counters'
                    ip,boot,largest=map(int,counts.groups())
                    assert ip==1 and boot==(0 if case.endswith('gdi-raw') else 3) and largest in (32768,37376,37632,39168)
                    row['actual_fatfs_calls']=dict(ip=ip,boot=boot,max_bytes=largest)
                rows.append(row)
        assert all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==value for name,value in hashes.items()),'Source changed during launcher fixture audit'
    return dict(status='PASS',cases=len(rows),filesystems=['fat32','exfat'],
        production_checksum_crc_and_fault_fixtures=True,ordinary_ce_raw_cooked_and_async=True,
        independent_fsck=False,results=rows,launcher_binary_sha256=binary_hash,source_sha256=hashes)

def audit(full=False):
    sources=('tests/test_games_retail_checksum_batch.c','src/core/game_image.c',
        'src/core/zso.c','src/core/lzo1x.c','src/core/hash.c','src/core/retail_image.c')
    with tempfile.TemporaryDirectory(prefix='kui-launch-checksum-batch-') as folder:
        binary=Path(folder)/'checksum'
        subprocess.run((*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-Wno-unused-function','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections',
            '-DKUI_VMU_GAME_CARDS=0','-Iinclude','-I.deps/fatfs/source','-Isrc/dreamcast',
            *sources,'-Wl,--gc-sections','-o',str(binary)),cwd=ROOT,check=True)
        output=subprocess.check_output((str(binary),),cwd=ROOT,text=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
    report=dict(schema='kui.launch.common-checksum-batch.v1',status='PASS',
        physical_layouts=6,container_labels=5,request_sizes=[1,2,15,16,17,33,257],tail_cases=2,
        maximum_logical_sectors_per_read=16,maximum_checksum_heap_bytes=32768,
        representative_257_sectors=dict(old_callbacks=257,new_callbacks=17),
        representative_33_sectors=dict(old_callbacks=33,new_callbacks=3),
        stack_fallback_bytes=2048,output=output,sanitizers=['address','undefined'],
        source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
            for name in (*sources,'src/apps/games_retail.c','include/kui/game_image.h',
                'tests/test_games_retail_checksum_batch.py')},
        limits='Actual private checksum helpers and shared reader; callbacks are not console timings. Full parser, launch route and physical SD CRC gates remain separate.')
    if full:report['real_fatfs']=real_fatfs_audit()
    return report

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--real-fatfs',action='store_true',help='also run50 existing launcher checksum/CE cases on real FAT32/exFAT')
    args=parser.parse_args()
    print(json.dumps(audit(full=args.real_fatfs),indent=2))
