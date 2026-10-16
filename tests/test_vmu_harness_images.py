#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual native proof sequence through real FatFs on disposable FS images."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/vmu-harness/host-sequence'

def run(*arguments):
    result = subprocess.run([str(value) for value in arguments], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f'{arguments}: exit {result.returncode}\n{result.stdout}')
    return result.stdout

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').digest()

def main():
    for command in ('mkfs.fat', 'fsck.fat', 'mkfs.exfat', 'fsck.exfat'):
        if not shutil.which(command):
            raise SystemExit(f'Missing test prerequisite: {command}')
    with tempfile.TemporaryDirectory(prefix='kui-native-vmu-') as temp:
        base = Path(temp)
        for kind in ('fat32', 'exfat'):
            image = base / f'{kind}.img'
            with image.open('wb') as stream:
                stream.truncate(96 * 1024 * 1024)
            run('mkfs.fat', '-F', '32', image) if kind == 'fat32' else run('mkfs.exfat', image)
            for mode in ('create', 'verify', 'repeat', 'verify'):
                assert 'PASS native harness host sequence' in run(BINARY, image, mode)
            checker = 'fsck.fat' if kind == 'fat32' else 'fsck.exfat'
            run(checker, '-n', image)
            for corruption in ('bad-identity', 'short-journal'):
                bad = base / f'{kind}-{corruption}.img'
                shutil.copyfile(image, bad)
                run(BINARY, bad, corruption)
                before = digest(bad)
                run(BINARY, bad, 'refuse-verify')
                assert digest(bad) == before, 'refused pair changed storage'
                run(checker, '-n', bad)
            print(f'PASS {kind}: create, separate-process verify, repeat, identity/length refusal, fsck', flush=True)
        image = base / 'fat16-refusal.img'
        with image.open('wb') as stream:
            stream.truncate(32 * 1024 * 1024)
        run('mkfs.fat', '-F', '16', image)
        before = digest(image)
        run(BINARY, image, 'refuse-create')
        assert digest(image) == before, 'unsupported FAT16 changed storage'
        run('fsck.fat', '-n', image)
        print('PASS FAT16: refused without changing image', flush=True)

if __name__ == '__main__':
    main()
