#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Collect the complete newly built ordinary/CE/CDDA cold-start rollout.

The final delivery composes these files with its authenticated preserved72KiB
cohort and its separately qualified Smash/GUI files. No historical adapter is
added merely because a package script knows its filename.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_shared_loader_performance import receipt, require, sha
from check_vmu_native_worker_gateway import read_elf
from runtime_package import envelope, flatten_elf, verify
from native_cdda_manifest import PROFILES, generate, runtime_binding

COMMON = {
    'physical': ('build/retail-physical', 'retail-physical.kui'),
    'ce': ('build/retail-ce', 'ce-probe.kui'),
    'standalone': ('build/native-standalone-general', 'retail-native-standalone.kui'),
    'zso': ('build/retail-zso', 'zso-boot.kui'),
    'lzo': ('build/retail-lzo', 'lzo-boot.kui'),
}
CE = ('armada', 'bam4', 'worms', 'wwp')
DEFAULT_BUILDS = {**{role: ROOT / folder for role, (folder, _) in COMMON.items()},
    **{'ce_' + profile: ROOT / ('build/' + profile + '-ce-stream') for profile in CE},
    **{'native_' + profile.replace('-', '_'): ROOT / ('build/native-compat-' + profile)
       for profile in PROFILES}}
INSTALL_TARGETS = frozenset(
    ['KUI/apps/games/' + name for _, name in COMMON.values()] +
    ['KUI/apps/games/ce-cdda-' + profile + '.kui' for profile in CE] +
    ['KUI/apps/games/' + name for _, _, old, new in PROFILES.values() for name in (old, new)])


def wire(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def cold(directory, *, native=False):
    from check_vc2_stage_cached_relay import audit as relay
    from test_smash_native_fast_crc import audit as crc
    path = directory / 'stage.elf'
    stage = read_elf(path)
    require(stage[4].get('__kui_retail_cold_fast_crc') == (1, 0, 0, 0xfff1),
            'Rollout stage lost its unconditional cold byte CRC: ' + str(directory))
    return dict(relay=relay(path, marker_name='__kui_retail_cold_instruction_cache'),
                byte_CRC=crc(path, relay='_kui_toy_pilot_base_relay' if native else
                            '_kui_retail_stage_relay'))


def linked_module(directory, commit, allow_dirty):
    entry = directory / 'entry.elf'
    provenance = receipt(entry, commit, allow_dirty)
    payload, memory = flatten_elf(entry.read_bytes())
    data = envelope(payload, memory, commit[:12])
    stage = (directory / 'stage.bin').read_bytes()
    require(payload[0x2000:] == stage, 'Entry does not embed its exact stage: ' + str(directory))
    image = read_elf(directory / 'stage.elf')
    segments = [s for s in image[2] if s[0] == 1 and s[4]]
    base = min(s[2] for s in segments)
    expected = bytearray(max(s[2] + s[4] for s in segments) - base)
    for s in segments:expected[s[2]-base:s[2]-base+s[4]] = image[0][s[1]:s[1]+s[4]]
    require(stage == expected, 'Stage binary differs from its ELF: ' + str(directory))
    return data, dict(compile=provenance, envelope=verify(data),
        artifacts_sha256={p.name: sha(p.read_bytes()) for p in directory.glob('*.elf')},
        stage_bin_sha256=sha(stage))


def collect(builds, commit, *, allow_dirty=False, runtime_elf=None):
    require(set(builds) == set(DEFAULT_BUILDS), 'Incomplete/unknown global rollout roles')
    builds = {role: Path(directory).resolve() for role, directory in builds.items()}
    files, records = {}, {}
    from check_retail_loader_layout import check_directory as retail_layout
    from check_zso_loader import check_directory as compressed_layout
    from package import NATIVE_RELEASE_FLAGS, validate_ordinary_ce
    from retail_package import inspect_retail
    for role, (_, filename) in COMMON.items():
        directory = builds[role]
        data, record = linked_module(directory, commit, allow_dirty)
        if role == 'physical':
            config = dict(token.split('=', 1) for token in
                          (directory / 'plain-layout-config').read_text().split())
            require(all(config.get(k) == v for k, v in NATIVE_RELEASE_FLAGS.items()) and
                    all(config.get(k, '0') == '0' for k in ('RETURN_TRACE', 'SKIES_READ_DIAGNOSTIC')),
                    'Ordinary physical loader lost its accepted native configuration')
            record['accepted_native_flags'] = NATIVE_RELEASE_FLAGS
            record['layout_and_stack'] = retail_layout(directory)
            require(read_elf(directory / 'stage.elf')[4].get('__kui_native_physical_stage') ==
                    (1, 0, 0, 0xfff1), 'Ordinary physical slot contains a special foreground variant')
        elif role == 'ce':
            validate_ordinary_ce(directory, data)
            record['layout_and_stack'] = retail_layout(directory, ce=True)
        elif role in ('zso', 'lzo'):
            record['layout_and_stack'] = compressed_layout(directory, 'lzo' if role == 'lzo' else 'lz4')
        else:
            symbols = read_elf(directory / 'stage.elf')[4]
            require(all(symbols.get(name) == (1, 0, 0, 0xfff1) for name in
                ('__kui_native_standalone_reader_stage', '__kui_native_standalone_structural_stage')) and
                '__kui_native_standalone_foreground_stage' not in symbols and
                '__kui_native_standalone_doa2_stage' not in symbols,
                'Standalone fallback is not the structural no-reservation reader')
            require(b'KUI NATIVE RAM MAP' not in data, 'A RAM-writing diagnostic occupied the normal fallback')
            record['no_virtual_VMU_or_cache_reservation'] = True
            inspect_retail(data)
        record['cold_stage'] = cold(directory)
        target = 'KUI/apps/games/' + filename
        files[target] = data
        record.update(install_targets=[target], module_sha256=sha(data))
        records[role] = record
    from check_armada_ce_excerpt import check as ce_audit
    for profile in CE:
        role = 'ce_' + profile
        directory = builds[role]
        data, record = linked_module(directory, commit, allow_dirty)
        audit = ce_audit(directory, stream=True, bam4=profile == 'bam4',
                         worms=profile == 'worms', wwp=profile == 'wwp')
        require(audit['title_profile'] == profile + '-us' and audit['stream_profile'] is True and
                audit['distributed_pcm_bytes'] == 0 and audit['distributed_audio_map_bytes'] == 4096,
                'CE/CDDA rollout changed the profile or distributed nonblank audio: ' + profile)
        record.update(layout_stack_ownership=audit, cold_stage=cold(directory))
        target = 'KUI/apps/games/ce-cdda-' + profile + '.kui'
        files[target] = data
        record.update(install_targets=[target], module_sha256=sha(data))
        records[role] = record
    native = {p: builds['native_' + p.replace('-', '_')] for p in PROFILES}
    header, manifest = generate(native, allow_dirty)
    require(manifest['source_commit'] == commit, 'Native CDDA manifest source differs from rollout')
    files['evidence/native-cdda-manifest.h'] = header.encode()
    files['evidence/native-cdda-manifest.json'] = wire(manifest)
    for profile, (_, filename, old_alias, new_alias) in PROFILES.items():
        role = 'native_' + profile.replace('-', '_')
        directory = native[profile]
        data, record = linked_module(directory, commit, allow_dirty)
        require(data == (directory / filename).read_bytes(), 'Native CDDA installed alias differs from source build')
        for alias in (old_alias, new_alias):files['KUI/apps/games/' + alias] = data
        record.update(cold_stage=cold(directory, native=True),
                      native_cohort=manifest['profiles'][profile],
                      install_targets=manifest['profiles'][profile]['aliases'], module_sha256=sha(data))
        records[role] = record
    require({name for name in files if name.startswith('KUI/')} == INSTALL_TARGETS,
            'The global rollout installation set is incomplete or contains extra modules')
    report = dict(schema='kui.global-loader-rollout.v1', status='PASS', source_commit=commit,
        source_dirty=any(r['compile']['provenance']['source_dirty'] for r in records.values()),
        profiles=records, installed_files=sorted(INSTALL_TARGETS),
        installed_sha256={name: sha(files[name]) for name in sorted(INSTALL_TARGETS)},
        full_historical_game_matrices_rerun=False, hardware_timing_measured=False,
        standard_background_disc_workers_rebuilt=False, optional_historical_adapters_added=False,
        existing_exact_CDDA_recognition_retained=True)
    if runtime_elf is not None:report['runtime_native_CDDA_binding'] = runtime_binding(runtime_elf, manifest)
    for role, directory in builds.items():
        files['evidence/global-loader-rollout/' + role + '-entry.compile.json'] = \
            (directory / 'entry.compile.json').read_bytes()
    files['evidence/global-loader-rollout.json'] = wire(report)
    return files, report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for role, default in DEFAULT_BUILDS.items():p.add_argument('--' + role.replace('_', '-'), type=Path, default=default)
    p.add_argument('--commit', required=True)
    p.add_argument('--runtime-elf', type=Path)
    p.add_argument('--allow-dirty', action='store_true')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    files, report = collect({role: getattr(a, role) for role in DEFAULT_BUILDS}, a.commit,
                           allow_dirty=a.allow_dirty, runtime_elf=a.runtime_elf)
    for name, body in files.items():
        path = a.output / name;path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(body)
    print(json.dumps(dict(status='PASS', installed_files=report['installed_files'], output=str(a.output))))


if __name__ == '__main__':main()
