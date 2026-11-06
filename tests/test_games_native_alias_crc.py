#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual FatFs raw cold source-read budget across advisory alias publication."""
import hashlib,json,os,re,shlex,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def audit():
    from test_games_library_images import private_formatter
    from test_games_retail import make_retail_fixture
    from test_loader_probe_images import partition_image
    source=(ROOT/'src/apps/games_retail.c').read_text()
    policy='optional_crc_to_stage=published && source_proof.complete && !required_boot_crc;'
    assert source.count(policy)==1
    old=source.replace(policy,'optional_crc_to_stage=false && published && source_proof.complete && !required_boot_crc;')
    plan=subprocess.check_output(['make','-n','-B','build/games-retail'],cwd=ROOT,text=True)
    commands=[shlex.split(l) for l in plan.splitlines() if 'tests/games_retail.c' in l and ' -o ' in l];assert len(commands)==1
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'};rows=[]
    with tempfile.TemporaryDirectory(prefix='kui-alias-crc-') as folder:
        d=Path(folder);old_path=d/'games_retail_old.c';old_path.write_text(old);binaries={}
        for label in ('new','old'):
            cmd=commands[0].copy();cmd[cmd.index('tests/games_retail.c')]='tests/test_games_native_alias_crc.c'
            if label=='old':cmd[cmd.index('src/apps/games_retail.c')]=str(old_path)
            exe=d/label;cmd[cmd.index('-o')+1]=str(exe)
            cmd+=['-Isrc/apps','-fno-pie','-no-pie','-Wl,--wrap=kui_vmu_native_resolution_cache_build','-DOLD_POLICY='+str(int(label=='old'))]
            built=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True);assert not built.returncode,built.stderr;binaries[label]=exe
        fmt=private_formatter(d)
        for fs in ('fat32','exfat'):
            volume=d/(fs+'.volume');clean=d/(fs+'.clean')
            subprocess.run([str(fmt),fs,str(volume)],env=env,check=True,capture_output=True,text=True);partition_image(volume,clean,fs)
            for scenario in ('published','failed-alias','off','cooked'):
                case='native-standard-cooked-report' if scenario=='cooked' else 'native-standard-report'
                for label,exe in binaries.items():
                    fixture=d/(fs+scenario+label);make_retail_fixture(fixture,case,0x47000)
                    card=d/(fs+scenario+label+'.img');shutil.copyfile(clean,card)
                    for op in ('seed','check'):
                        ran=subprocess.run([str(exe),str(card),str(fixture),op,case,scenario],cwd=ROOT,env=env,capture_output=True,text=True)
                        assert not ran.returncode,ran.stdout+ran.stderr
                    found=re.search(r'ALIAS_CRC source=(\d+) crc=(\d+) reads=(\d+) flags=([0-9a-f]+) publishes=(\d+) off=(\d+) failed=(\d+)',ran.stdout);assert found
                    source_count,crc,reads=map(int,found.group(1,2,3))
                    expected_crc=int(scenario not in ('off','cooked') and (label=='old' or scenario=='failed-alias'))
                    assert (source_count,crc,reads)==(1,expected_crc,1+expected_crc)
                    rows.append(dict(filesystem=fs,scenario=scenario,implementation=label,full_source_passes=reads,fresh_sdk_source_reads=source_count,extra_crc_passes=crc,boot_crc_required=bool(int(found.group(4),16)&4),fresh_stage_length_sha_replay_and_changed_source_miss=True))
    return dict(schema='kui.gui.native-cold-alias-crc.v1',result='PASS',cases=rows,source_sha256=hashlib.sha256(source.encode()).hexdigest(),old_policy_control_sha256=hashlib.sha256(old.encode()).hexdigest(),sdk_discovery='Explicit pure adversary after actual synthetic discovery refusal; source read/publication/epoch/CRC/fallback/FatFs are production.',console_performance=False)
if __name__=='__main__':print(json.dumps(audit(),indent=2))
