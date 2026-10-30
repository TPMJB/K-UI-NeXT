#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual normal capture -> damaged saved sector -> in-place repair -> Verify/Resume."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
from make_recovery_vectors import sector, edc, parity
from test_capture_compressed_images import digest

ROOT=Path(__file__).resolve().parents[1]
BINARY=ROOT/'build/capture-image'

def run(image, disc, *args, opts='', expected=0, layout='repair-small', extra=None):
    env=dict(os.environ,KUI_TEST_LAYOUT=layout,KUI_TEST_DISC_FILE=str(disc),KUI_TEST_OPTS=opts)
    if extra:env.update(extra)
    p=subprocess.run([str(BINARY),str(image),*args],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    assert p.returncode==expected,(args,expected,p.returncode,p.stdout[-5000:])
    return p.stdout

def optical(path,cd=False):
    with path.open('wb') as f:
        for start,count,data in (((150,240,True),(390,160,False),(550,240,True)) if cd else ((150,40,True),(340,4,False),(45150,40,True))):
            f.seek((start-150)*2352)
            for fad in range(start,start+count):
                b=sector(fad,fad) if data else bytearray((j*13+fad)&255 for j in range(2352))
                if fad==45150:
                    b[16:16+len(b'SEGA SEGAKATANA')]=b'SEGA SEGAKATANA';b[144:272]=b' '*128;b[144:144+len(b'REPAIR RECEIPT TEST!')]=b'REPAIR RECEIPT TEST!'
                    b[2064:2068]=edc(b[:2064]).to_bytes(4,'little')
                    parity(b,86,24,2,86,2076);parity(b,52,43,86,88,2248)
                f.write(b)

def main():
    with tempfile.TemporaryDirectory(prefix='kui-capture-repaired-') as temp:
        base=Path(temp);disc=base/'disc.raw';optical(disc)
        for fs in ('fat32','exfat'):
            seed=base/f'{fs}.img'
            with seed.open('wb') as f:f.truncate(96*1024*1024)
            subprocess.run(['mkfs.fat','-F','32',str(seed)] if fs=='fat32' else ['mkfs.exfat',str(seed)],check=True,stdout=subprocess.DEVNULL)
            run(seed,disc,'seed')
            for fmt in ('','cue'):
                image=base/f'{fs}-{fmt}.img';shutil.copyfile(seed,image)
                run(image,disc,'new',opts=fmt)
                before=base/f'{fs}-{fmt}-before';before.mkdir();run(image,disc,'export',str(before));folder=next(before.iterdir())
                expected={p.name:digest(p) for p in folder.glob('track*')}
                run(image,disc,'mutate','repair-target')
                run(image,disc,'repair-inplace')
                repaired=base/f'{fs}-{fmt}-repaired.img';shutil.copyfile(image,repaired)
                for action in ('verify','resume'):
                    saved=digest(image);out=run(image,disc,action)
                    assert 'Finalized in-place repair verified' in out and 'WRITES 0' in out
                    assert digest(image)==saved
                dest=base/f'{fs}-{fmt}-after';dest.mkdir();run(image,disc,'export',str(dest));result=next(dest.iterdir())
                assert {p.name:digest(p) for p in result.glob('track*')}==expected
                print(f'PASS {fs} repaired {fmt or "gdi"}: actual capture/repair -> normal Verify/Resume, exact bytes, zero writes',flush=True)
                for fault in ('repair-receipt','repair-receipt-delete','repair-receipt-path','repair-manifest','repair-checkpoints','repair-partial-checkpoint','gdi-internal' if fmt else 'gdi-cooked') + (('cue',) if fmt else ()):
                    damaged=base/f'{fs}-{fmt}-{fault}.img';shutil.copyfile(repaired,damaged)
                    run(damaged,disc,'mutate',fault)
                    saved=digest(damaged)
                    for action in ('verify','resume'):
                        out=run(damaged,disc,action,expected=1);assert 'WRITES 0' in out
                        assert digest(damaged)==saved
                    damaged.unlink()
                    print(f'PASS {fs} repaired {fmt or "gdi"}: {fault} refuses without writes',flush=True)
                image.unlink();repaired.unlink()
            # Named partial jobs deliberately have no published manifest or
            # descriptor. In-place completion uses its bound fallback names.
            import re
            for fmt in ('','cue'):
                image=base/f'{fs}-partial-{fmt}.img';shutil.copyfile(seed,image)
                named={'KUI_TEST_OUTPUT_ROOT':'/Games','KUI_TEST_GAME_NAMES':'1'}
                out=run(image,disc,'new','stop-small',opts=fmt,expected=3,extra=named)
                directory=re.search(r'job=(0:/[^\n]+)',out).group(1)
                selected=dict(named,KUI_TEST_JOB=directory,KUI_TEST_SELECTED_JOB=directory)
                run(image,disc,'repair-inplace',extra=selected)
                for action in ('verify','resume'):
                    saved=digest(image);out=run(image,disc,action,extra=selected)
                    assert 'Finalized in-place repair verified' in out and 'WRITES 0' in out and digest(image)==saved
                image.unlink();print(f'PASS {fs} named partial {fmt or "gdi"}: in-place completion -> normal Verify/Resume',flush=True)
            cd_disc=base/f'{fs}-cd.raw';optical(cd_disc,cd=True)
            image=base/f'{fs}-cd.img';shutil.copyfile(seed,image)
            run(image,cd_disc,'new',opts='cue',layout='cd-mixed')
            run(image,cd_disc,'mutate','repair-cd-target',layout='cd-mixed')
            run(image,cd_disc,'repair-inplace',layout='cd-mixed')
            for action in ('verify','resume'):
                saved=digest(image);out=run(image,cd_disc,action,layout='cd-mixed')
                assert 'Finalized in-place repair verified' in out and 'WRITES 0' in out and digest(image)==saved
            run(image,cd_disc,'mutate','cue',layout='cd-mixed');saved=digest(image)
            run(image,cd_disc,'verify',layout='cd-mixed',expected=1);assert digest(image)==saved
            image.unlink();print(f'PASS {fs} mixed CD: capture -> in-place repair -> normal Verify/Resume; changed CUE refused',flush=True)

if __name__=='__main__':main()
