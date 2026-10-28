#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Explicit new repair binding after unreadable original identity samples."""
from pathlib import Path
import shutil
import tempfile
from test_images import run
from test_legacy_import_images import fixture
from make_recovery_vectors import sector,mode2_sector
ROOT=Path(__file__).resolve().parents[1]
CASES=('clean','damaged-end','partial','prefix-corrupt','sha-mismatch','source-unstable','wrong-disc','fatal','cancel','crc-only','no-hd-prefix','cd-audio','cd-data','cd-mode2','cd-silence','cd-repeated','cd-fixed-silence-mismatch','cd-anchor-silence')
def main():
    with tempfile.TemporaryDirectory(prefix='kui-saved-identity-') as temp:
        base=Path(temp)
        for kind in ('fat32','exfat'):
            seed=base/f'{kind}-seed.img'
            with seed.open('wb') as f:f.truncate(96*1024*1024)
            run('mkfs.fat','-F','32',str(seed)) if kind=='fat32' else run('mkfs.exfat',str(seed))
            for case in CASES:
                folder=base/f'{kind}-{case}'
                if case.startswith('cd-'):
                    folder.mkdir()
                    for i,start in ((1,150),(2,154)):
                        data_track=i==1 and case in ('cd-data','cd-mode2')
                        name=f'{i:02d}.{"bin" if data_track else "raw"}'
                        if data_track:data=b''.join((mode2_sector if case=='cd-mode2' else sector)(fad,fad) for fad in range(start,start+4))
                        elif case=='cd-silence':data=bytes(4*2352)
                        elif case=='cd-repeated':data=bytes(j&255 for j in range(2352))*4
                        else:data=bytes((j*31+i)&255 for j in range(4*2352))
                        if case=='cd-fixed-silence-mismatch' and i==1:data=bytes(2352)+data[2352:]
                        (folder/('optical'+name)).write_bytes(data);(folder/('track'+name)).write_bytes(data)
                else:fixture(folder,'clean')
                if case=='partial':
                    p=folder/'track03.bin';p.write_bytes(p.read_bytes()[:20*2352])
                if case=='prefix-corrupt':
                    p=folder/'track03.bin';data=bytearray(p.read_bytes());data[90]^=1;p.write_bytes(data)
                (folder/'alternate.bin').write_bytes(sector(45169 if case=='wrong-disc' else 45151,1))
                image=base/f'{kind}-{case}.img';shutil.copyfile(seed,image)
                assert 'PASS saved-anchor identity' in run(str(ROOT/'build/saved-identity-image'),str(image),str(folder),case)
                run('fsck.fat' if kind=='fat32' else 'fsck.exfat','-n',str(image));image.unlink();shutil.rmtree(folder)
                print(f'PASS {kind} saved identity: {case}',flush=True)
if __name__=='__main__':main()
