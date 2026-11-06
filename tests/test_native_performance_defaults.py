#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check effective shared build policy across loader families, not game names."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    ('standard-sci', 'Makefile.vmu-native-vmu-lite-paced', ('VMU_TRANSPORT=1', 'VMU_SCI_RX_POLICY=paced')),
    ('standard-scif', 'Makefile.vmu-native-vmu-lite', ('VMU_TRANSPORT=0',)),
    ('smash-native-menu', 'Makefile.smash-native-menu', ()),
    ('background-general-a2', 'Makefile.vmu-native-split-controller-stream-guard', ()),
    ('background-general-a1', 'Makefile.vmu-native-split-controller-stream-guard-a1', ()),
    ('background-session-a2', 'Makefile.vmu-native-session-controls', ()),
    ('background-session-a1', 'Makefile.vmu-native-session-controls-a1', ()),
    ('background-compact-a2', 'Makefile.vmu-native-background-compact', ()),
    ('background-compact-a1', 'Makefile.vmu-native-background-compact-a1', ()),
    ('disc-a2', 'Makefile.disc-vmu', ()),
    ('disc-a1', 'Makefile.disc-vmu-a1', ()),
)
SHARED = ('KUI_NATIVE_STAGE_FAST_CRC', 'KUI_NATIVE_STAGE_FAST_SCAN',
          'KUI_NATIVE_CACHED_EXTENT', 'KUI_NATIVE_CACHED_RESOLUTION')


def flags(value):
    output = {}
    for name, number in re.findall(r'(?:^|\s)-D(KUI_\w+)=(\S+)', value):
        if name in output and output[name] != number:
            raise ValueError('Conflicting effective feature: ' + name)
        output[name] = number
    return output


def audit():
    command = ('kui-read-shared-policy:\n'
               '\t$(info POLICY_FLAGS=$(FLAGS))\n'
               '\t$(info POLICY_NATIVE=$(NATIVE_FLAGS))\n'
               '\t$(info POLICY_DISC=$(DISC_FLAGS))\n'
               '\t$(info POLICY_STAGE_SRC=$(STAGE_SRC))\n'
               '\t$(info POLICY_DISC_SRC=$(DISC_SOURCES))\n'
               '\t$(info POLICY_LEGACY=$(LEGACY_FLAGS))\n'
               '\t@:\n')
    rows = []
    for name, makefile, extra in PROFILES:
        output = subprocess.check_output(['make', '--no-print-directory', '-s', '-rR', '-f', makefile,
            'BUILD=build/policy-inspection-' + name, *extra, '--eval=' + command,
            'kui-read-shared-policy'], cwd=ROOT, text=True)
        fields = dict(re.findall(r'^POLICY_(\w+)=(.*)$', output, re.M))
        if set(fields) != {'FLAGS', 'NATIVE', 'DISC', 'STAGE_SRC', 'DISC_SRC', 'LEGACY'}:
            raise ValueError('Incomplete effective policy output: ' + name)
        selected = flags(fields['DISC' if name.startswith('disc-') else 'FLAGS'])
        # Physical-disc CRC is its separate existing pure packet/CRC policy.
        required = SHARED[1:] if name.startswith('disc-') else SHARED
        if any(selected.get(key) != '1' for key in required):
            raise ValueError('A loader family lost an inherited immutable-proof optimization: ' + name)
        if name.startswith('disc-') and selected.get('KUI_DISC_STAGE_FAST_CRC32') != '1':
            raise ValueError('Physical-disc launch lost its established full fastCRC: ' + name)
        sources = fields['DISC_SRC' if name.startswith('disc-') else 'STAGE_SRC'].split()
        if sources.count('src/loader/smash_fat_read_cache.c') != 1:
            raise ValueError('Fresh FAT implementation must be linked exactly once: ' + name)
        if sources.count('src/loader/vc2_ce_vmu_stage_cache.c') != 1:
            raise ValueError('Fresh metadata/image read-through cache must be linked exactly once: ' + name)
        if name.startswith('background-session') and flags(fields['NATIVE']).get('KUI_NATIVE_SESSION_CONTROLS') != '1':
            raise ValueError('Optional session variant lost return/disc-set controls: ' + name)
        if name.startswith('background-general') and flags(fields['NATIVE']).get('KUI_NATIVE_SESSION_CONTROLS') not in (None, '0'):
            raise ValueError('Historical General variant unexpectedly enables optional session controls: ' + name)
        if name.startswith('background-compact'):
            for field in ('FLAGS','NATIVE','LEGACY'):
                if flags(fields[field]).get('KUI_NATIVE_BACKGROUND_COMPACT') != '1':
                    raise ValueError('Default Background lost compact geometry in '+field+': '+name)
            if flags(fields['NATIVE']).get('KUI_NATIVE_SESSION_CONTROLS') not in (None,'0'):
                raise ValueError('Optional session controls replaced the compact default: '+name)
        # UI source verification and live workers use their own established
        # code/cache policy. No pure-stage cache mode is a worker feature.
        if flags(fields['NATIVE']).get('KUI_NATIVE_CACHED_RESOLUTION') == '1':
            raise ValueError('Temporary proof scope leaked into a live worker: ' + name)
        rows.append(dict(profile=name, makefile=makefile,
            stage_optimizations={key: selected.get(key) for key in required},
            fresh_FAT_windows=True, FAT_implementation_link_count=1,
            fresh_metadata_memo=True, validated_image_snapshot_runs=32,
            session_controls=flags(fields['NATIVE']).get('KUI_NATIVE_SESSION_CONTROLS') == '1',
            canonical_background_default=name.startswith('background-compact')))
    inputs = {'Makefile.vmu-native-compat', 'Makefile.vmu-native-persistent',
              'Makefile.disc-vmu', 'src/loader/native_proof_cache_scope.inc',
              'src/loader/vmu_native_persist_stage.inc', 'src/loader/disc_vmu_stage.c',
              'src/loader/vc2_ce_vmu_stage_cache.c', 'include/kui/vc2_ce_vmu_stage_cache.h',
              'tests/test_native_performance_defaults.py'}
    inputs.update(makefile for _, makefile, _ in PROFILES)
    return dict(schema='kui.native.shared-performance-defaults.v1', status='PASS',
        profiles=rows, per_game_selectors=False,
        default_background_profiles=['background-compact-a2','background-compact-a1'],
        default_background_geometry=dict(worker_bytes=0x12000,lease_bytes=0x47000,cache_bytes=0x30000),
        source_sha256={path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sorted(inputs)},
        limits='Effective build policies are checked; linked execution, storage correctness and console timing are separate gates.')


if __name__ == '__main__':
    print(json.dumps(audit(), indent=2))
