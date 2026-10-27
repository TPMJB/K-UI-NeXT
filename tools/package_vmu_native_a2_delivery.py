#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify extracted installers and deliver separate audited A2 reader choices."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

from runtime_package import verify
from vmu_native_cache_seed import decode_seed, template_seed
from vmu_native_mode import decode_mode

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_personalize_vmu_native_persistent import pair


def require(condition,message):
    if not condition:raise ValueError(message)


def sha(data):return hashlib.sha256(data).hexdigest()


def archive_files(path):
    with zipfile.ZipFile(path) as archive:
        names=archive.namelist()
        require(len(set(names))==len(names) and archive.testzip() is None,'Invalid ZIP members')
        require(all(not Path(n).is_absolute() and '..' not in Path(n).parts for n in names),
                'Unsafe ZIP member')
        files={n:archive.read(n) for n in names}
    rows={name:digest for digest,name in
          (row.split('  ',1) for row in files['SHA256SUMS.txt'].decode().splitlines())}
    require(set(rows)==set(files)-{'SHA256SUMS.txt'} and
            all(sha(files[name])==digest for name,digest in rows.items()),'ZIP checksums differ')
    return files


def checksums(files):
    return ''.join(sha(value)+'  '+name+'\n' for name,value in sorted(files.items())).encode()


def smoke(files,label):
    manifest=json.loads(files['base/native-persistent-personalization.json'])
    choices=manifest['runtime_mode_choices']
    require(choices==({'normal':0,'profile':2,'hooks-only':4,'raw-chain':5,'evolution-trace':6}
                     if label=='Background' else
                     {'normal':0,'control':1,'profile':2,'lease-only':3,'hooks-only':4,'raw-chain':5,'evolution-trace':6}),
            'Reader-specific runtime choices differ')
    checks=[]
    with tempfile.TemporaryDirectory(prefix='kui-a2-'+label.lower()+'-') as directory:
        base=Path(directory);package=base/'extracted';card=base/'card';card.mkdir()
        for name,data in files.items():
            if name.startswith('source/'):continue
            path=package/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        profile=pair(card,'p0005',number='0005')
        destination=card/'KUI/apps/games/retail-boot.kui'
        destination.parent.mkdir(parents=True);destination.write_bytes(b'previous loader')
        def snapshot():
            return {p.relative_to(card).as_posix():p.read_bytes()
                    for p in (card/'KUI/vmu-test').rglob('*') if p.is_file()}
        before=snapshot()
        def run(game='evolution2',flag=None,expected=0,refused=False,idempotent=False):
            argv=[sys.executable,str(package/'personalize.py'),str(card),'--profile',
                  str(profile.relative_to(card))]+(['--game',game] if game else [])+([flag] if flag else [])
            old=destination.read_bytes()
            result=subprocess.run(argv,cwd=base,text=True,capture_output=True)
            require(result.returncode==(2 if refused else 0),result.stderr or 'Installer status differs')
            require(snapshot()==before,'Installer changed existing save pair')
            module=destination.read_bytes()
            if refused:
                require(module==old,'Refused mode replaced loader')
            else:
                require(verify(module)['build']==manifest['build'],'Installed build differs')
                offset=manifest['runtime_mode_module_offset']
                require(decode_mode(module[offset:offset+32])==expected,'Installed runtime mode differs')
                offset=manifest['profile_module_offset']
                require(module[offset:offset+4096]==profile.read_bytes(),'Installed profile differs')
                offset=manifest['cache_seed_module_offset']
                wire=module[offset:offset+1120]
                seed=decode_seed(wire)
                if game:
                    catalog=json.loads(files['native-cache/catalog.json'])
                    entry=next(g for g in catalog['games'] if g['slug']==game)
                    require(seed and seed['record_sha256']==entry['record_sha256'],'Installed game seed differs')
                else:
                    require(seed is None and wire==template_seed(),'Cold discovery seed is not canonical empty')
                if idempotent:require(module==old and result.stdout.startswith('Already installed'),
                                      'Repeated install was not idempotent')
            checks.append(dict(game=game,flag=flag or 'normal',refused=refused,idempotent=idempotent))
        run(game=None);run(game=None,idempotent=True)
        for game in ('ps2','skies','sa2','grandia2','evolution1','evolution2'):run(game)
        for flag,value in (('--control',1),('--profile-runtime',2),('--lease-only',3),
                           ('--hooks-only',4),('--raw-chain',5),('--evolution-trace',6)):
            run(flag=flag,expected=value,refused=label=='Background' and value in (1,3))
        run();run(idempotent=True)
        old=destination.read_bytes()
        result=subprocess.run([sys.executable,str(package/'personalize.py'),str(card),
                               '--control','--raw-chain'],cwd=base,capture_output=True)
        require(result.returncode==2 and destination.read_bytes()==old and snapshot()==before,
                'Conflicting options changed loader or save pair')
        checks.append(dict(conflicting_modes_refused=True))
    return dict(reader=label,extracted_cli_cases=len(checks),checks=checks,
                prepared_pair_unchanged=True,module_sha256=manifest['module_sha256'])


def package(standard,background,replays,steady,cache_lock,historical_baseline,output):
    builds={'Standard':standard,'Background':background};parts={};manifests={};sources={}
    for label,build in builds.items():
        files=archive_files(build/'K-UI-Native-Compatibility-Test.zip')
        manifests[label]=json.loads(files['base/native-persistent-personalization.json'])
        require(files['base/KUI/apps/games/retail-boot.kui']==(build/'retail-maple-observe.kui').read_bytes(),
                'ZIP module differs from actual build')
        report=json.loads(files['probe-report.json'])
        require(not report['source_provenance']['source_dirty'] and
                report['runtime']['build']==manifests[label]['build'],'Stale or dirty source')
        for name,data in files.items():
            if name.startswith('source/'):
                require(name not in sources or sources[name]==data,'Source checkpoints differ')
                sources[name]=data
        parts[label]=files
    require(manifests['Standard']['source_commit']==manifests['Background']['source_commit'],
            'Reader choices use different source commits')
    require(sources,'Missing recoverable source checkpoint')
    historical=json.loads((ROOT/'docs/evidence/native-a2-background-2026-10-24.json').read_bytes())['historical_fmv_baseline']
    require(sha(historical_baseline.read_bytes())==historical['archive_sha256'],
            'Historical accepted baseline archive differs')
    source_member='source/K-UI-SCI-e0e8a7546f9a.tar.gz'
    with zipfile.ZipFile(historical_baseline) as archive:
        module=archive.read(historical['module_member']);baseline_source=archive.read(source_member)
    require(sha(module)==historical['module_sha256'] and
            sha(baseline_source)=='e1deff17c28423680a8827dba5a806ca4f2d13495b15b6f0783d4c6df7972a89',
            'Historical module/source checkpoint differs')
    prefix='source/historical-edf20f0f11a3/'
    sources[prefix+'retail-boot.kui']=module
    sources[prefix+'K-UI-SCI-e0e8a7546f9a.tar.gz']=baseline_source
    sources[prefix+'provenance.json']=(json.dumps(historical,indent=2)+'\n').encode()
    require((standard/'native-worker.bin').read_bytes()==(background/'native-worker.bin').read_bytes() and
            (standard/'native-worker.elf').read_bytes()==(background/'native-worker.elf').read_bytes(),
            'Reader choices do not share the exact same optimized A2 worker')
    worker_sha=sha((standard/'native-worker.elf').read_bytes())
    worker_bin_sha=sha((standard/'native-worker.bin').read_bytes())
    evidence={}
    for game in ('ps2','skies','sa2','grandia2','evolution1','evolution2'):
        raw=(replays/f'record-replay-{game}-a1.json').read_bytes();report=json.loads(raw)
        require(report['worker_elf_sha256']==worker_sha and report['worker_binary_sha256']==worker_bin_sha
                and report['validated_callback_pairs']>=12 and len(report['negative_cases'])>=13 and
                report['physical_a1_replies_preserved'] and not report['original_bytes_embedded'],
                'Stale or incomplete SDK/A1 replay: '+game)
        evidence['evidence/record-replay-'+game+'-a1.json']=raw
    raw=steady.read_bytes();report=json.loads(raw)
    require(report['worker_elf_sha256']==worker_sha and
            {r['ccr'] for r in report['results']}=={'0x105','0x185'} and
            all(r['rounds']==r['submitted_callbacks']==r['completed_callbacks']==128 and
                r['held_lock_preserved_for_every_conflict'] for r in report['results']),
            'Stale or incomplete cache/lock replay')
    evidence['evidence/native-sdk-cache-steady.json']=raw
    raw=cache_lock.read_bytes();report=json.loads(raw)
    candidates=[row for row in report['results'] if row.get('stage')]
    require(len(report['results'])==14 and len(candidates)==6 and
            all(row['worker_elf_sha256']==worker_sha and
                bool(row['refusal'])==bool(row['ocbp_mutation']) for row in candidates),
            'Stale or incomplete cold-cache publication replay')
    evidence['evidence/native-sdk-cache-lock.json']=raw
    checks={label:smoke(files,label) for label,files in parts.items()}
    report=dict(schema='kui.native.a2-background.delivery.v1',build=manifests['Standard']['build'],
                source_commit=manifests['Standard']['source_commit'],worker_elf_sha256=worker_sha,
                worker_binary_sha256=worker_bin_sha,exact_same_worker_for_both_readers=True,
                installers=checks,console_execution_verified=False)
    files={'README-FIRST.md':(ROOT/'docs/vmu-native-a2-background-test.md').read_bytes(),
           'delivery-checks.json':(json.dumps(report,indent=2)+'\n').encode(),**evidence}
    for label,part in parts.items():
        selected={name:data for name,data in part.items()
                  if not name.startswith('source/') and name!='SHA256SUMS.txt'}
        selected['SHA256SUMS.txt']=checksums(selected)
        files.update({label+'/'+name:data for name,data in selected.items()})
    output.mkdir(parents=True,exist_ok=True);results=[]
    for full in (False,True):
        selected={**files,**(sources if full else {})}
        selected['SHA256SUMS.txt']=checksums(selected)
        path=output/('K-UI-A2-Background-'+report['build']+('-Full' if full else '')+'.zip')
        with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as archive:
            for name,data in selected.items():archive.writestr(name,data)
        require(archive_files(path)==selected,'Final ZIP byte comparison differs')
        results.append(dict(path=str(path.resolve()),bytes=path.stat().st_size,sha256=sha(path.read_bytes())))
    print(json.dumps(dict(build=report['build'],files=results,installers=checks),indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--standard',type=Path,required=True);p.add_argument('--background',type=Path,required=True)
    p.add_argument('--replays',type=Path,required=True);p.add_argument('--steady',type=Path,required=True)
    p.add_argument('--cache-lock',type=Path,required=True)
    p.add_argument('--historical-baseline',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    try:package(args.standard,args.background,args.replays,args.steady,args.cache_lock,args.historical_baseline,args.output)
    except (OSError,ValueError,KeyError,AssertionError,subprocess.CalledProcessError) as error:
        p.exit(2,f'A2/Background delivery refused: {error}\n')
