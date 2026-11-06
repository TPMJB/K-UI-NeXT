#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create and extract-verify the folder-copy VC2 readonly integration test."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

from check_vc2_ce_vmu_package import audit
from runtime_package import verify

ROOT=Path(__file__).resolve().parents[1]
MAIN='751677974d1ee3eb4128314d67a80d77d1ea082f'

def sha(data):return hashlib.sha256(data).hexdigest()
def wire(value):return (json.dumps(value,indent=2)+'\n').encode()
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build',type=Path,default=ROOT/'build/vc2-ce-vmu')
    ap.add_argument('--linked-proof',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args()
    assert not git('status','--porcelain'), 'Commit source before packaging'
    commit=git('rev-parse','HEAD').decode().strip();stamp=commit[:12]
    subprocess.run(['git','merge-base','--is-ancestor',MAIN,commit],cwd=ROOT,check=True)
    proof=audit(args.build,False)
    assert proof['compile_provenance']['commit']==commit
    assert not proof['compile_provenance']['source_dirty']
    linked=json.loads(args.linked_proof.read_text());assert linked['status']=='PASS'
    for name,digest in linked['artifacts_sha256'].items():
        assert sha((args.build/name).read_bytes())==digest, 'Stale linked proof '+name
    files={
        'KUI/runtime.kui':(args.build/'runtime.kui').read_bytes(),
        'KUI/apps/games/retail-physical.kui':(args.build/'retail-native-foreground.kui').read_bytes(),
        'KUI/apps/disc/native-disc.kui':(ROOT/'build/disc-native/native-disc.kui').read_bytes(),
        'LICENSE.txt':(ROOT/'LICENSE').read_bytes(),
        'source/change-from-main-7516779.patch':git('diff','--binary',MAIN,commit),
        'source/package_vc2_ce_vmu_test.py':Path(__file__).read_bytes(),
        'source/vc2-ce-vmu-readonly-test.md':(ROOT/'docs/vc2-ce-vmu-readonly-test.md').read_bytes(),
        'evidence/linked-package.json':wire(proof),
        'evidence/linked-transition.json':args.linked_proof.read_bytes(),
        'evidence/owner-source.json':(ROOT/'docs/evidence/vc2-ce-vmu-owner.json').read_bytes(),
        'evidence/runtime-geometry.json':(ROOT/'docs/evidence/vc2-ce-vmu-runtime-geometry.json').read_bytes(),
    }
    for name in ('KUI/runtime.kui','KUI/apps/games/retail-physical.kui',
                 'KUI/apps/disc/native-disc.kui'):
        assert verify(files[name])['build']==stamp, 'Mismatched install build '+name
    for relative in ('build/kui-runtime.elf','build/disc-native/entry.elf'):
        path=ROOT/relative
        provenance=json.loads(path.with_suffix('.compile.json').read_text())
        assert provenance['commit']==commit and not provenance['source_dirty']
        assert provenance['elf_sha256']==sha(path.read_bytes())
        files['evidence/'+path.stem+'.compile.json']=wire(provenance)
    files['README.txt']=f'''K-UI VC2 blank virtual-card test {stamp}

Copy this package's KUI folder onto the SD card, replacing the matching files.
There is no installer script to run.

This is a VC2-only Standard-reader test. It includes current main's tested
GD-Play and GD-ripper fixes. Use it with the same Sega Smash Pack SD image.

TEST
1. Remove physical VMUs for this run, leaving the controller connected.
2. Use Native/Default boot. Leave virtual VMU, background reading,
   CD music and RAM patches OFF in the launcher.
3. Launch Sega Smash Pack, continue past any outer no-card prompt, then
   select Virtua Cop 2. This test module supplies its own blank A2 inside VC2.
4. The test intentionally stops after its first completed virtual-card read
   and finalization, or on a fault/visit limit. Photograph BOTH cycling pages.
   Each page stays visible for eight seconds. Then power off.

If it stops on the ordinary GD STOP page, photograph that whole page instead.
That earlier failure screen is ONE page and does not cycle.
If it hangs without a diagnostic, report the last stage and SD LED state.

This test does not save to SD or use your existing virtual cards. The first
goal is detection and reads; durable saving is a subsequent integration step.
The reader retains its working VRAM fix and zero speculative read-ahead.
The complete 40-track TOC is preserved; heavily fragmented data files may
exceed this experiment's 48 map slots and are refused before game execution.

Host/machine checks passed; console acceptance has not yet been established.
The source patch applies to main {MAIN}; no retail game files are included.
'''.encode()
    files['BUILD.json']=wire(dict(commit=commit,main_included=MAIN,
        hardware_verified=False,persistent_writes=False,
        virtual_card='generated blank readonly A2',read_ahead_bytes=0,
        CE_reserved_bytes=49152,expected_checkpoint='first read finalization',
        working_memory_initialization='P2 reset after immutable code verification, before stack save',
        installed={name:dict(bytes=len(data),sha256=sha(data))
                   for name,data in files.items() if name.startswith('KUI/')}))
    files['SHA256SUMS.txt']=''.join(sha(data)+'  '+name+'\n'
        for name,data in sorted(files.items())).encode()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    temporary=args.output.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name,data in sorted(files.items()):archive.writestr(name,data)
    with zipfile.ZipFile(temporary) as archive:
        assert archive.testzip() is None
        with tempfile.TemporaryDirectory() as directory:
            archive.extractall(directory)
            for name,data in files.items():assert (Path(directory)/name).read_bytes()==data
    subprocess.run(['unzip','-t',str(temporary)],check=True,stdout=subprocess.DEVNULL)
    temporary.replace(args.output)
    print(json.dumps(dict(status='PASS',file=str(args.output),bytes=args.output.stat().st_size,
        sha256=sha(args.output.read_bytes()),commit=commit,extract_verified=True)))

if __name__=='__main__':main()
