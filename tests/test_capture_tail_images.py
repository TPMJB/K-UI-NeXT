#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual capture/FatFs tail policy, cancellation/fatal checkpoints and resume.

The adapter refuses every multi-sector or DMA request touching the last 150
sectors. A failure eight sectors into that region leaves exactly 142 sectors,
matching the owner's incomplete-tail case without manufacturing disc bytes.
"""
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import verify_dump


def main():
    subprocess.run(['make','build/capture-image'],cwd=ROOT,check=True)
    with tempfile.TemporaryDirectory(prefix='kui-capture-tail-') as folder:
        work=Path(folder);header=work/'ffconf.h'
        header.write_text((ROOT/'config/ffconf.h').read_text().replace(
            '#define FF_USE_MKFS 0','#define FF_USE_MKFS 1'))
        formatter=work/'format-image'
        subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-include',str(header),'-I.deps/fatfs/source','tests/format_test_image.c',
            '.deps/fatfs/source/ff.c','.deps/fatfs/source/ffunicode.c','-o',str(formatter)],
            cwd=ROOT,check=True)
        def run(image,*args,opts='dma',expected=0):
            env=dict(os.environ,KUI_TEST_OPTS=opts,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
            result=subprocess.run([str(ROOT/'build/capture-image'),str(image),*args],
                env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            assert result.returncode==expected,(args,result.returncode,result.stdout[-4000:])
            return result.stdout
        def export(image,name):
            destination=work/name;destination.mkdir();run(image,'export',str(destination))
            jobs=list(destination.iterdir());assert len(jobs)==1
            return jobs[0]
        def hashes(job):
            return {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in job.glob('track*')}
        for kind in ('fat32','exfat'):
            seed=work/f'{kind}-seed.img'
            subprocess.run([str(formatter),str(seed),'0' if kind=='fat32' else '1'],
                check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
            run(seed,'seed');baseline=None
            for opts in ('','dma','crc32,noend,dma'):
                label=opts.replace(',','-') or 'pio';image=work/f'{kind}-{label}.img'
                shutil.copyfile(seed,image);output=run(image,'new','tail-check',opts=opts)
                assert 'TAIL_READS 150' in output and 'single-sector PIO' in output
                job=export(image,f'{kind}-{label}-export');verify_dump.verify(job)
                actual=hashes(job)
                if baseline is None:baseline=actual
                assert actual==baseline
                assert (job/'track06.bin').stat().st_size==813*2352
            for fault,result in (('tail-stop',3),('tail-fatal',1)):
                image=work/f'{kind}-{fault}.img';shutil.copyfile(seed,image)
                output=run(image,'new',fault,expected=result)
                assert 'SAVED DATA VERIFIED' not in output
                partial=export(image,f'{kind}-{fault}-partial')
                assert not (partial/'manifest.json').exists() and not (partial/'disc.gdi').exists()
                assert (partial/'track06.bin').stat().st_size==(813-142)*2352
                output=run(image,'resume','tail-check')
                assert 'TAIL_READS 142' in output and 'SAVED DATA VERIFIED' in output
                job=export(image,f'{kind}-{fault}-resumed');verify_dump.verify(job)
                assert hashes(job)==baseline
            print(f'PASS {kind}: PIO/DMA/CRC tail bytes, clipped prefetch, all150 sectors, stop/fatal preserve and resume final142',flush=True)


if __name__=='__main__':main()
