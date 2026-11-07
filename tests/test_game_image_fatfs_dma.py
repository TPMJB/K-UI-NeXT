#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual FatFs read destination traces through the production SCI DMA gate.

An authored raw track covers all 32 source-offset residues, an odd starting
LBA, three converted sector strides, batched and allocation-failure reads,
and contiguous/fragmented files on both supported filesystems. No SD hardware
or elapsed-time effect is inferred from these host callbacks.
"""
import hashlib,json,os,pathlib,shlex,subprocess,sys,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
SOURCE='src/core/game_image.c'
ORIGINAL='a72faa150b6c91a1b26fa10b1b7ff9413c3be87f'
ORIGINAL_SHA256='8ca73620fcb221c913bff66935f3fc6fbb85c4095c28b8cb18b7822e60004b3d'
ORIGINAL_PATCH='tests/fixtures/game-image-before-dma-alignment.patch'
FATFS='.deps/fatfs/source'
def run(command,*,cwd=ROOT,**kwargs):
    p=subprocess.run(command,cwd=cwd,text=True,capture_output=True,**kwargs)
    if p.returncode:raise AssertionError(shlex.join(map(str,command))+'\n'+p.stdout+p.stderr)
    return p.stdout
def original_source(tmp):
    # Recover the exact old implementation without depending on Git history.
    original=tmp/SOURCE
    original.parent.mkdir(parents=True)
    original.write_bytes((ROOT/SOURCE).read_bytes())
    run(['git','apply','--no-index',str(ROOT/ORIGINAL_PATCH)],cwd=tmp)
    if hashlib.sha256(original.read_bytes()).hexdigest()!=ORIGINAL_SHA256:
        raise AssertionError('Pre-alignment game_image.c does not match the historical source')
    return original
def main():
    with tempfile.TemporaryDirectory(prefix='kui-fatfs-dma-') as temporary:
        tmp=pathlib.Path(temporary)
        config=(ROOT/'config/ffconf.h').read_text();assert config.count('#define FF_USE_MKFS 0')==1
        (tmp/'formatter-ffconf.h').write_text(config.replace('#define FF_USE_MKFS 0','#define FF_USE_MKFS 1'))
        bus=(ROOT/'src/loader/sci_sd_bus.c').read_text();at=bus.index('static uint32_t dma_address(')
        end=bus.index('\nstatic void purge_buffer',at);gate=bus[at:end]
        (tmp/'sci-dma-address.inc').write_text(gate)
        original=original_source(tmp)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
        flags=[*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
            '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-include',str(tmp/'formatter-ffconf.h'),'-Iinclude','-Itests','-I'+FATFS,'-I'+str(tmp)]
        rows=[]
        for label,source in [('old',original),('current',ROOT/SOURCE)]:
            binary=tmp/label
            run([*flags,'-DEXPECT_ALIGNED='+str(int(label=='current')),
                'tests/test_game_image_fatfs_dma.c',str(source),'src/core/zso.c','src/core/lzo1x.c','src/core/data.c',
                FATFS+'/ff.c',FATFS+'/ffunicode.c','-Wl,--wrap=malloc,--wrap=free','-o',str(binary)])
            for fs in ('fat32','exfat'):
                for fragmented in (False,True):
                    card=tmp/(label+'-'+fs+'-'+str(fragmented)+'.img')
                    output=run([str(binary),fs,str(card),str(int(fragmented))],env=env)
                    row=json.loads(next(line[7:] for line in output.splitlines() if line.startswith('RESULT ')))
                    rows.append(dict(implementation=label,filesystem=fs,fragmented=fragmented,**row))
        for fs in ('fat32','exfat'):
            for fragmented in (False,True):
                pair=[r for r in rows if r['filesystem']==fs and r['fragmented']==fragmented]
                assert len(pair)==2
                for key in ('cases','file_extents','file_read_callbacks','direct_blocks','all_payloads_equal'):
                    assert pair[0][key]==pair[1][key],(fs,fragmented,key,pair)
        report=dict(schema='kui.game-image.fatfs-dma-alignment.v1',status='PASS',cases=rows,
            old_source_commit=ORIGINAL,old_source_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
            actual_production_dma_address_function_sha256=hashlib.sha256(gate.encode()).hexdigest(),
            source_sha256={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in (
                SOURCE,'src/loader/sci_sd_bus.c','tests/test_game_image_fatfs_dma.c',
                'tests/test_game_image_fatfs_dma.py',ORIGINAL_PATCH,'tests/format_fatfs_fixture.c',
                FATFS+'/ff.c',FATFS+'/ffunicode.c','config/ffconf.h')},
            unchanged_callback_counts_and_payloads=True,console_timing_measured=False,
            scope='Production converter and FatFs read path; original SCI address-admission predicate executes with a host physical-address seam. Busy-channel and hardware timing are not modeled here.')
        if len(sys.argv)>1:pathlib.Path(sys.argv[1]).write_text(json.dumps(report,indent=2)+'\n')
        print('PASS real FatFs DMA alignment: old bytes pass but bulk pointers can refuse DMA; current batched/OOM pointers pass across all32 residues,3strides,both filesystems and fragmented files')
if __name__=='__main__':main()
