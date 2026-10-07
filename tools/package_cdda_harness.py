#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the isolated CDDA hardware test; never include game audio."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile
from runtime_package import flatten_elf,verify

ROOT=Path(__file__).resolve().parents[1]
def git(*args):
    return subprocess.check_output(['git',*args],cwd=ROOT,text=True).strip()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    parser.add_argument('--source-commit',help='Published commit with the identical verified source tree')
    args=parser.parse_args()
    if git('status','--porcelain'): parser.error('Commit reviewed source before packaging')
    checkpoint=git('rev-parse','HEAD')
    commit=args.source_commit or checkpoint
    if len(commit)!=40 or any(c not in '0123456789abcdef' for c in commit): parser.error('Invalid source commit')
    build=ROOT/'build/cdda'
    runtime=(build/'cdda-harness.kui').read_bytes()
    info=verify(runtime)
    if info['build']!=commit[:12]: parser.error('Runtime build ID differs from source commit')
    payload,memory=flatten_elf((build/'cdda-harness.elf').read_bytes())
    if runtime[64:]!=payload or memory!=0x200000: parser.error('ELF/envelope/layout mismatch')
    marker=bytes.fromhex('4b554953424f4f540100000003000000fcffffff')
    if payload.count(marker)!=1: parser.error('Runtime must contain one unpatched bootstrap marker')
    fixture=(build/'fixture/stereo.raw').read_bytes()
    expected=json.loads((build/'fixture/stereo.json').read_text())
    if hashlib.sha256(fixture).hexdigest()!=expected['sha256']: parser.error('Fixture checksum mismatch')
    metadata={'source_commit':commit,'source_tree':git('rev-parse','HEAD^{tree}'),
        'local_checkpoint':checkpoint,'source_dirty':False,'hardware_tested':False,
        'source_publication':'published' if args.source_commit else 'local source patch included; public push pending',
        'source_patch':'source-patch.mbox',
        'base_source_tree':'b32ef194830db7bbfe3fda43b598c92b66d868db',
        'reader_support':'standalone SCI homebrew harness only',**info,
        'compiler':subprocess.check_output(['sh-elf-gcc','--version'],text=True).splitlines()[0],
        'runtime_sha256':hashlib.sha256(runtime).hexdigest(),
        'elf_sha256':hashlib.sha256((build/'cdda-harness.elf').read_bytes()).hexdigest(),
        'sound_ring_bytes':65536,'private_stack_bytes':65536,
        'sample_path':'/KUI/tests/cdda/track14.raw','sample_included':False,
        'expected_sample_sha256':'ae3d955fc817f433b4c5273581398215e3be5cf2cfd1dee2ec62aaaa677cf338'}
    files={'KUI/runtime.kui':runtime,'KUI/tests/cdda/stereo.raw':fixture,
        'README.md':(ROOT/'docs/cdda-harness-test.md').read_bytes(),
        'build.json':(json.dumps(metadata,indent=2)+'\n').encode(),
        'fixture.json':(json.dumps(expected,indent=2)+'\n').encode(),
        'evidence/memory-and-source-audit.md':(ROOT/'docs/evidence/cdda-harness-memory-2026-10-07.md').read_bytes(),
        'evidence/cdda-harness.elf':(build/'cdda-harness.elf').read_bytes(),
        'evidence/cdda-harness.map':(build/'cdda-harness.map').read_bytes(),
        'source-patch.mbox':subprocess.check_output(['git','format-patch',
            '057f0e13c0c9e4e4e6f7c3365a47002467470277..HEAD','--stdout'],cwd=ROOT),
        'base-source-url.txt':b'https://github.com/TPMJB/K-UI-NeXT/tree/4be17b8d5383a6065e922f53362224405ed9d1e8\n'}
    if args.source_commit:
        files['source-url.txt']=('https://github.com/TPMJB/K-UI-NeXT/tree/'+commit+'\n').encode()
    for path in (ROOT/'LICENSES').iterdir():
        if path.is_file(): files['LICENSES/'+path.name]=path.read_bytes()
    files['LICENSE']= (ROOT/'LICENSE').read_bytes()
    files['THIRD_PARTY.md']=(ROOT/'THIRD_PARTY.md').read_bytes()
    # Preserve exact independently licensed inputs/configuration used to build.
    for name in ['ff.c','ff.h','ffconf.h','ffunicode.c','diskio.h']:
        files['source-fatfs/'+name]=(build/'fatfs'/name).read_bytes()
    for path in build.rglob('*.su'):
        files['evidence/stack/'+str(path.relative_to(build))]=path.read_bytes()
    files['SHA256SUMS']=(''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n'
        for name,data in sorted(files.items()))).encode()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(args.output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name,data in files.items(): archive.writestr(name,data)
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip(): parser.error('ZIP integrity check failed')
        for name,data in files.items():
            if archive.read(name)!=data: parser.error('ZIP content mismatch: '+name)
    print(json.dumps({'file':str(args.output.resolve()),'bytes':args.output.stat().st_size,
        'build':info['build'],'sha256':hashlib.sha256(args.output.read_bytes()).hexdigest()},indent=2))
if __name__=='__main__': main()
