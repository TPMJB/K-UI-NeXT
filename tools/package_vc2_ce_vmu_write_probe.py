#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the bounded VC2 WRITE-phase test without promising persistent saves."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

from check_vc2_ce_vmu_package import audit
from package_vc2_ce_vmu_late_test import ROOT, MAIN, git, installed_module, paired_helper, sha, wire

# This is historical evidence for the prior hardware-tested readonly build,
# not a startup proof for the newly compiled WRITE-phase experiment.
PRIOR_PROOF_SHA256='36b6dee9173322b9af1e69e47aa6e384039e138beb9a019f4416276b971ffcf2'
PRIOR_COMMIT='ceceb502a2c702313db1fdee3c6fb2c423096040'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,default=ROOT/'build/vc2-ce-vmu-write-probe')
    parser.add_argument('--prior-linked-proof',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,action='append',default=[])
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    assert args.output.suffix=='.zip','Use a ZIP output filename'
    assert not git('status','--porcelain'),'Commit source before packaging'
    commit=git('rev-parse','HEAD').decode().strip();stamp=commit[:12]
    subprocess.run(['git','merge-base','--is-ancestor',MAIN,commit],cwd=ROOT,check=True)
    proof=audit(args.build)
    assert proof['status']=='PASS' and proof.get('late_loader'),'Late loader audit did not pass'
    probe=proof['helper'].get('write_probe')
    assert probe and not probe['durable_commit_ack_enabled'] and not probe['storage_services_linked'],\
        'Expected only the no-storage WRITE-phase experiment'
    assert proof['capability']['flags']==2 and proof['capability']['slots']==48
    assert not proof['persistent_writes'] and not proof['hardware_verified']
    prior_bytes=args.prior_linked_proof.read_bytes()
    assert sha(prior_bytes)==PRIOR_PROOF_SHA256,'Unexpected historical baseline receipt'
    prior=json.loads(prior_bytes)
    assert prior['status']=='PASS' and prior['hardware_verified'] is False
    files={
        'LICENSE.txt':(ROOT/'LICENSE').read_bytes(),
        'source/change-from-main-a2b23b3.patch':git('diff','--binary',MAIN,commit),
        'source/package_vc2_ce_vmu_write_probe.py':Path(__file__).read_bytes(),
        'evidence/current-linked-package.json':wire(proof),
        'evidence/PRIOR-ceceb502-linked-transition.json':prior_bytes,
        'evidence/owner-source.json':(ROOT/'docs/evidence/vc2-ce-vmu-owner.json').read_bytes(),
        'evidence/runtime-geometry.json':(ROOT/'docs/evidence/vc2-ce-vmu-runtime-geometry.json').read_bytes(),
    }
    for item in args.evidence:
        target='evidence/'+item.name
        assert target not in files,'Duplicate evidence basename'
        files[target]=item.read_bytes()
    for target,module,elf,label in (
        ('KUI/runtime.kui',args.build/'runtime.kui',ROOT/'build/kui-runtime.elf','runtime'),
        ('KUI/apps/games/retail-physical.kui',args.build/'retail-native-foreground.kui',
         args.build/'entry.elf','late-reader'),
        ('KUI/apps/disc/native-disc.kui',ROOT/'build/disc-native/native-disc.kui',
         ROOT/'build/disc-native/entry.elf','disc'),
    ):
        data,provenance=installed_module(module,elf,commit)
        files[target]=data;files['evidence/'+label+'.compile.json']=wire(provenance)
    helper=(args.build/'helper/prepared.bin').read_bytes()
    pair=paired_helper(files['KUI/apps/games/retail-physical.kui'],helper)
    files['KUI/apps/games/vc2-vmu-helper.bin']=helper
    files['README.txt']=f'''K-UI VC2 WRITE-phase test {stamp}

Copy this package's KUI folder over the existing SD installation. Install ALL
FOUR files together: runtime.kui, apps/games/retail-physical.kui,
apps/disc/native-disc.kui and apps/games/vc2-vmu-helper.bin.

This includes main {MAIN}. Use the same Sega Smash Pack SD image.

TEST
1. Remove physical VMUs; leave the controller connected.
2. Use Native/Default boot. Leave virtual VMU, background reader, CD music
   and RAM patches OFF in the launcher.
3. Launch Sega Smash Pack, pass its outer no-card prompt if shown, then VC2.
4. Photograph ALL THREE cycling pages headed VC2 WRITE 1 OF 3 STATUS,
   VC2 WRITE 2 OF 3 CONTEXT and VC2 WRITE 3 OF 3 STAGED BLOCK. Then power off.

EXPECTED CHECKPOINT
VC2 may deliver four 128-byte WRITE phases into one private 512-byte stage.
Each phase ACK means receipt only. The test stops BEFORE acknowledging the
final commit request. Page3 should show MASK 0000000F, four phase ACKs,
PENDING 1 and DURABLE 0. It also records block, staged CRC and packet data.
Page3's PRIVATE CAP is backend staging capacity; guest RX CAP is on page2.

This is a volatile staging test, not a persistent save build. It writes no
SD sector, opens no existing virtual card and never acknowledges durable
commit. Powering off discards the staged bytes. Saving to SD comes next.

If an earlier VC2 STOP - THREE PAGES screen appears, photograph all three
numbered pages. For another early launch screen, photograph it once. If no
diagnostic appears, report the last visible stage and SD LED activity.

WHAT CHANGED
The previous readonly build reached the first CMD12 WRITE request. This build
uses the existing bounded WRITE protocol to accept phases0..3, repeats the
live request/owner checks, and withholds final commit acknowledgement. It keeps
the checked late helper loading, game track map, TOC and no-read-ahead route.

VALIDATION SCOPE
The current package audit binds actual ELF ranges, helper CRCs, bounded
callback closure, stacks, integer instructions and module provenance. The
enclosed PRIOR-ceceb502 startup receipt is historical evidence for the old
readonly baseline only; it is not a machine-startup proof for this new build.
This experiment has not yet been verified on the console. No retail game
files are included. The source patch applies to the main commit above.
'''.encode()
    files['BUILD.json']=wire(dict(
        commit=commit,main_included=MAIN,purpose='VC2 volatile WRITE-phase staging probe',
        hardware_verified=False,persistent_writes=False,durable_commit_ack_enabled=False,
        virtual_card='generated blank 200-block A2; one volatile staged write',
        read_ahead_bytes=0,map_slots=48,capability_flags=2,helper=pair,
        expected_checkpoint='complete matching phase4 request before final ACK',
        expected_staging_mask=15,expected_pending=1,expected_durable_commits=0,
        prior_readonly_baseline=PRIOR_COMMIT,prior_startup_receipt_sha256=PRIOR_PROOF_SHA256,
        current_startup_machine_proof=False,
        installed={name:dict(bytes=len(data),sha256=sha(data))
                   for name,data in files.items() if name.startswith('KUI/')},
    ))
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
                          sha256=sha(args.output.read_bytes()),commit=commit,
                          main_included=MAIN,helper=pair,extract_verified=True,
                          persistent_writes=False,durable_commit_ack_enabled=False)))


if __name__=='__main__':main()
