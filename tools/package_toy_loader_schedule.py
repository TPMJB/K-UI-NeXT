#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Emit B/Y/Z hardware ZIP only after independent linked admission."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

from toy_loader_schedule_audit import audit_schedule
from package_toy_loader_data_probe import pilot_legend, legend_markdown
from package_toy_loader_sci_next import data_legend
from package_toy_loader_trace import trace_legend

ROOT=Path(__file__).resolve().parents[1]
R_SHA='99e1c0ad84a916fa9e08cf68b494c2d0efe0748fe956e579841c3d7614e49037'
PROFILES=((3,'B','two-sector-control'),(1,'Y','one-sector-startup'),(2,'Z','bounded-async-startup'))


def digest(data):return hashlib.sha256(data).hexdigest()


def schedule_data_legend():
    schema=data_legend()
    schema['mode']={'0':'B/Y/Z: attribution with original payload callback'}
    schema['control_window']={'B_Y':'Observes original synchronous callbacks in both phases',
                              'Z':'Startup engine bypasses original synchronous callbacks; use SCHED engine and TRACE progress'}
    return schema


def legend():
    header=['magic','version','words','mode','tick_hz','frozen','accepted_requests','next_slot',
            'active_slot','after_accepted_PLAY','saturated','fingerprint_or_record_faults',
            'fingerprint_ticks_total','fingerprint_ticks_max','cleanup_ok','cleanup_failed']
    engine=['calls','irq_calls','call_blocks','irq_blocks','audio_claims','audio_pending',
            'audio_releases','data_resumes','errors','retries','token_yields','polled_blocks',
            'work_ticks_max','irq_ticks_max','active_token','card_owned']
    startup=['enabled','retired','quiesces','quiesce_failed']+['reserved_zero']*12
    fields=['sequence','token','LBA','sector_count','destination','sector_bytes','command','flags',
            'credited_bytes','status','error','first_sector_CRC32','first_credit_ticks',
            'completion_ticks','calls','max_call_ticks_before_fingerprint']
    words=header+['engine.'+x for x in engine]+['startup.'+x for x in startup]
    for i in range(8):words+=['record['+str(i)+'].'+x for x in fields]
    if len(words)!=176:raise ValueError('SCHED legend geometry differs')
    text=['# SCHED PAGE exact word legend','',
          'Read four words left to right per row. Page numbers and words are hexadecimal.','',
          '| Page | Row | Word 1 | Word 2 | Word 3 | Word 4 |','|---|---:|---|---|---|---|']
    for i in range(0,len(words),4):
        text.append('| '+f'{i//16:02X}'+' | '+str((i%16)//4+1)+' | '+' | '.join('`'+x+'`' for x in words[i:i+4])+' |')
    text+=['','Header: LSC1 `4C534331`, version1,176words (`B0`), mode3=B/1=Y/2=Z.',
           'CRC uses IEEE CRC32 like Python binascii.crc32; it covers only the first credited sector.',
           'Flags:1=CRC valid,2=first-credit timing valid,4=terminal timing valid,8=CHECK consumed,',
           '16=request after accepted PLAY,32=invalid call timing,64=CRC map refused,128=accepted ABORT,256=RESET/INIT.',
           'Header `active_slot` and missing timing fields use `FFFFFFFF` for unavailable, not a huge elapsed value.',
           'Record `status` equal to `FFFFFFFF` means FAILED.',
           'Sort records by sequence. Page positions are ring slots, not chronological order.',
           'Ticks/781250 gives seconds. Call max excludes the following fingerprint; TRACE includes it.',
           'Z engine/startup snapshots are taken before terminal quiesce. Cleanup flags describe quiesce afterward.',
           'Synchronous B/Y have zero engine/startup words. No report proves console playback quality.','']
    return '\n'.join(text).encode()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,default=ROOT.parent/'overnight-source/build/toy-u-reference')
    parser.add_argument('--out',type=Path,default=ROOT/'dist/K-UI-Toy-DATA-Scheduling-Tests.zip')
    args=parser.parse_args();out=args.out.resolve();out.parent.mkdir(parents=True,exist_ok=True)
    if subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True):
        raise ValueError('Commit the exact reviewed source before packaging')
    source=json.loads((ROOT/'build/schedule-tests/source.json').read_text())
    metadata={'format':1,'runtime_source_commit':source['runtime_source_commit'],
              'source_snapshot_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
              'profiles':[],'CDDA_algorithm_changed':False,'hardware_behavior_verified':False,
              'audio_or_video_improvement_proven':False}
    files={'TESTING.md':(ROOT/'docs/toy-data-schedule-test.md').read_bytes(),
           'evidence/sched-word-legend.md':legend(),
           'tools/check_toy_sector.py':(ROOT/'tools/check_toy_sector.py').read_bytes()}
    for label, schema in (('DATA', schedule_data_legend()), ('PILOT', pilot_legend()),
                          ('TRACE', trace_legend())):
        files['evidence/'+label.lower()+'-word-legend.json']=(json.dumps(schema,indent=2)+'\n').encode()
        files['evidence/'+label.lower()+'-word-legend.md']=legend_markdown(label,schema)
    r=ROOT/'build/schedule-r/retail-toy-pilot.kui'
    if digest(r.read_bytes())!=R_SHA:raise ValueError('R restoration is not byte exact')
    files['restore/R-private-write-through.kui']=r.read_bytes()
    # Host logs are copied by the build orchestration with known passing footers.
    required={'host-all.log':'38 Toy pilot regression suites passed',
              'host-trace.log':'Toy loader trace production differential, telemetry and report suites passed',
              'host-data.log':'Toy loader DATA probe production telemetry and report suites passed',
              'host-one.log':'DATA schedule production differential:',
              'host-report.log':'SCHED production report modes1/2/3:',
              'host-startup.log':'Startup DATA scheduling suites passed (independent audio profile)',
              'linked-tests.log':'OK'}
    for name,footer in required.items():
        data=(ROOT/'build/schedule-tests'/name).read_bytes()
        if footer.encode() not in data:raise ValueError('Missing passed test evidence: '+name)
        files['evidence/'+name]=data
    for mode,label,name in PROFILES:
        directory=ROOT/'build'/('schedule-'+label.lower())
        admission=audit_schedule(directory,args.baseline,mode)
        if admission.get('admitted') is not True:raise ValueError('Candidate not admitted: '+label)
        files['evidence/'+label+'-linked-admission.json']=(json.dumps(admission,indent=2,sort_keys=True)+'\n').encode()
        runtime=(directory/'retail-toy-pilot.kui').read_bytes()
        files['profiles/'+label+'-'+name+'.kui']=runtime
        metadata['profiles'].append({'label':label,'mode':mode,'build':admission['build'],
                                     'runtime_sha256':digest(runtime)})
        for path in sorted(directory.rglob('*')):
            if path.is_file() and (path.suffix in ('.elf','.bin','.map','.su') or
                                 path.name in ('build-config','toy_pilot_resident_symbols.h')):
                files['evidence/'+label+'/'+path.relative_to(directory).as_posix()]=path.read_bytes()
    for path in [ROOT/'LICENSE',ROOT/'THIRD_PARTY.md',*sorted((ROOT/'LICENSES').glob('*'))]:
        if path.is_file():files[path.relative_to(ROOT).as_posix()]=path.read_bytes()
    files['source-snapshot.tar']=subprocess.check_output(['git','archive','HEAD'],cwd=ROOT)
    files['build.json']=(json.dumps(metadata,indent=2,sort_keys=True)+'\n').encode()
    files['SHA256SUMS']=''.join(digest(data)+'  '+name+'\n' for name,data in sorted(files.items())).encode()
    temporary=out.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED,compresslevel=8) as archive:
        for name,data in sorted(files.items()):archive.writestr(name,data)
    with zipfile.ZipFile(temporary) as archive:
        if archive.testzip() is not None:raise ValueError('ZIP integrity failure')
        for line in archive.read('SHA256SUMS').decode().splitlines():
            h,name=line.split('  ',1)
            if digest(archive.read(name))!=h:raise ValueError('ZIP member checksum differs: '+name)
    temporary.replace(out)
    print(json.dumps({'file':str(out),'sha256':digest(out.read_bytes()),'bytes':out.stat().st_size,
                      'profiles':metadata['profiles'],'all_member_checksums_verified':True},indent=2))


if __name__=='__main__':main()
