#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FatFs report exports with a private formatter, fault injection and JSON/CRC verification."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]


def main():
    config=(ROOT/'config/ffconf.h').read_text()
    old='#define FF_USE_MKFS 0'
    assert config.count(old)==1
    with tempfile.TemporaryDirectory(prefix='kui-report-images-') as folder:
        work=Path(folder)
        header=work/'test-ffconf.h';header.write_text(config.replace(old,'#define FF_USE_MKFS 1'))
        binary=work/'problem-report-image'
        wrappers=['f_open','f_write','f_read','f_sync','f_close','f_rename']
        subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-include',str(header),'-Iinclude','-I.deps/fatfs/source',
            'tests/problem_report_image.c','src/apps/problem_report.c','src/core/data.c',
            'src/core/runtime_image.c','src/core/clock.c',
            '.deps/fatfs/source/ff.c','.deps/fatfs/source/ffunicode.c',
            *[f'-Wl,--wrap={name}' for name in wrappers],'-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary),str(work)],cwd=ROOT,check=True,timeout=120,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
        manifests=list(work.glob('*-manifest.json'));assert len(manifests)==4
        for manifest_path in manifests:
            manifest=json.loads(manifest_path.read_bytes())
            assert manifest['schema']=='kui.support-report.v1'
            assert manifest['version']=='3.0.0' and manifest['build']=='aabbccddeeff'
            assert manifest['clock_valid'] is False
            assert manifest['launch_reports_describe_preparation'] is True
            assert manifest['unavailable_optional_inputs']==0
            files=manifest['files'];names=[row['name'] for row in files]
            expected={'session-log.txt','module-headers.txt','last-launch-a.txt','last-launch-b.txt'}
            if 'REPORT-000001-' in manifest_path.name:expected.add('screenshot.bmp')
            assert len(names)==len(set(names)) and set(names)==expected
            prefix=manifest_path.name.removesuffix('manifest.json')
            for row in files:
                data=(work/(prefix+row['name'])).read_bytes()
                assert len(data)==row['bytes']
                assert f'{zlib.crc32(data):08x}'==row['crc32']
                for secret in (b'FTP PASSWORD SECRET',b'GAME EXECUTABLE SECRET',b'VMU SAVE SECRET',b'SETTINGS FLASH SECRET'):
                    assert secret not in data
            print(f'PASS independent JSON and per-file CRC verification: {manifest_path.name}',flush=True)


if __name__=='__main__':main()
