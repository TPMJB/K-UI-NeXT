#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual launcher/FatFs cooked advisory hint versus old full-proof control."""
import hashlib,json,os,re,shlex,shutil,subprocess,tempfile,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def audit():
    from test_games_library_images import private_formatter
    from test_games_retail import make_retail_fixture
    from test_loader_probe_images import partition_image
    production=(ROOT/'src/apps/games_retail.c').read_text()
    # Freeze the exact former production decisions. Reconstruct only those
    # three decisions in a separate C translation unit; all other source,
    # validators, callbacks, FatFs and detached physical reads are identical.
    replacements=(
      ('const bool hint_allowed=emulation_enabled &&\n        (!required_boot_crc || (cooked_boot && !patched && !cdda && !vmu_package)) &&',
       'const bool hint_allowed=emulation_enabled && !required_boot_crc &&'),
      ('if(cache==NATIVE_CACHE_READY && hint_allowed && !indexed_hint &&\n       (!required_boot_crc || mapping_hint)) {',
       'if(cache==NATIVE_CACHE_READY && hint_allowed && !indexed_hint) {'),
      ('if((mapping_hint || optional_crc_to_stage) && !required_boot_crc) {','if(mapping_hint || optional_crc_to_stage) {'),
    )
    control=production
    for new,old in replacements:
        assert control.count(new)==1
        control=control.replace(new,old)
    plan=subprocess.check_output(['make','-n','-B','build/games-retail'],cwd=ROOT,text=True)
    command=[shlex.split(line) for line in plan.splitlines() if 'tests/games_retail.c' in line and ' -o ' in line]
    assert len(command)==1
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'}
    rows=[]
    with tempfile.TemporaryDirectory(prefix='kui-cooked-hint-') as folder:
        directory=Path(folder);old_source=directory/'games_retail_old.c';old_source.write_text(control)
        binaries={}
        for label in ('new','old'):
            cmd=command[0].copy();cmd[cmd.index('tests/games_retail.c')]='tests/test_games_cooked_native_hint.c'
            if label=='old':cmd[cmd.index('src/apps/games_retail.c')]=str(old_source)
            binary=directory/label;cmd[cmd.index('-o')+1]=str(binary)
            cmd+=['-Isrc/apps','-fno-pie','-no-pie','-DEXPECT_SOURCE='+str(int(label=='old')),
                  '-DEXPECT_CRC='+str(int(label=='new')),'-DEXPECT_READS=1']
            subprocess.run(cmd,cwd=ROOT,env=env,check=True,capture_output=True,text=True);binaries[label]=binary
        formatter=private_formatter(directory)
        for kind in ('fat32','exfat'):
            volume=directory/(kind+'.volume');clean=directory/(kind+'.clean')
            subprocess.run([str(formatter),kind,str(volume)],env=env,check=True,capture_output=True,text=True)
            partition_image(volume,clean,kind)
            for changed in (False,True):
                expected=[]
                for label,binary in binaries.items():
                    fixture=directory/(kind+label+str(changed));make_retail_fixture(fixture,'native-standard-cooked-report',0x47000)
                    if changed:
                        track=fixture/'track03.bin';data=bytearray(track.read_bytes());data[21*2048+100]^=1;track.write_bytes(data)
                    card=directory/(kind+label+str(changed)+'.img');shutil.copyfile(clean,card)
                    for operation in ('seed','check'):
                        ran=subprocess.run([str(binary),str(card),str(fixture),operation,'native-standard-cooked-report'],cwd=ROOT,env=env,capture_output=True,text=True)
                        assert ran.returncode==0,ran.stdout+ran.stderr
                    match=re.search(r'POLICY source=(\d+) crc=(\d+) reads=(\d+) bootcrc=([0-9a-f]+) flags=([0-9a-f]+)',ran.stdout)
                    assert match;source,crc,reads=map(int,match.group(1,2,3));expected.append(match.group(4))
                    assert (source,crc,reads)==((0,1,1) if label=='new' else (1,0,1))
                    assert int(match.group(5),16)&4
                    assert int(match.group(4),16)==zlib.crc32((fixture/'track03.bin').read_bytes()[21*2048:21*2048+3001])
                    rows.append(dict(filesystem=kind,source_changed=changed,implementation=label,full_gui_proof_reads=source,fresh_crc_passes=crc,source_read_calls=reads,boot_crc32=match.group(4),mandatory_crc_retained=True,detached_physical_read_verified=not changed))
                assert expected[0]==expected[1]
    return dict(schema='kui.gui.cooked-native-hint.v1',result='PASS',cases=rows,
        source_sha256=hashlib.sha256(production.encode()).hexdigest(),old_policy_control_sha256=hashlib.sha256(control.encode()).hexdigest(),
        console_performance=False,scope='Actual C launcher and real FatFs; synthetic advisory record is deliberately not SDK source authority. Fresh mandatory CRC and detached raw/cooked data checks retained.')
if __name__=='__main__':
    print(json.dumps(audit(),indent=2))
