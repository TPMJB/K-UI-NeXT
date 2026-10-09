#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package one explicit silent-source control with exact R restoration."""
import argparse
import json
from pathlib import Path
import subprocess
from zipfile import ZipFile

from check_loader_layout import inspect_elf, padded
from check_retail_instructions import audit as instruction_audit
from package_cdda_calibration import ROOT, build_config, git, sha, write_archive
from package_cdda_toy_pilot import (
    conservative_stack, embedded_blob, pilot_contract, pilot_low_stack,
    snapshot_legend, stack_rows,
)
import retail_package as layout
from retail_package import inspect_retail
from runtime_package import flatten_elf
from toy_pilot_cache_audit import _load_cache_profile, audit_cache_profiles

R_SHA = '99e1c0ad84a916fa9e08cf68b494c2d0efe0748fe956e579841c3d7614e49037'
FALLBACK_SHA = '161ea24d655b6531867c6704c57a57c1ee25668e534d2f56dc793a5cd73a80de'


def collect(build, retained, host_log, published_commit=None):
    if git('status', '--porcelain'):
        raise ValueError('Commit exact source before packaging')
    commit, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    expected = {
        'BUILD': commit[:12], 'PROFILE': '15', 'PILOT': '1', 'LOW': '1',
        'SLOTS': '64', 'SCI': '1', 'SCI_PACED': '1', 'SCI_FAULT': '1',
        'SCI_REUSE_TDRE': '0', 'GD_FIXED_STEP': '2', 'SHARED_SCI': '0',
        'ASYNC_CDDA': '0', 'PRIVATE_P2': '1', 'NATIVE_CACHE': '0',
        'SYNTHETIC_SOURCE': '1', 'OPT': '-Os -fno-tree-scev-cprop',
    }
    if build_config(build / 'build-config') != expected:
        raise ValueError('Wrong complete silent-source configuration')
    host = host_log.read_bytes()
    if b'Toy pilot regression suites passed' not in host or b'synthetic source control:' not in host:
        raise ValueError('Missing completed actual-worker and regression checks')
    pilot_contract((ROOT / 'include/kui/toy_pilot.h').read_bytes())
    legend = snapshot_legend()
    cache_proof = audit_cache_profiles(build, native_cache=False, synthetic_source=True)
    _, images, disassemblies = _load_cache_profile(
        build, native_cache=False, synthetic_source=True)
    entry = inspect_elf((build / 'entry.elf').read_bytes(), layout.EXEC_ADDRESS,
                        layout.EXEC_ADDRESS + layout.EXEC_MAX_BYTES)
    runtime = (build / 'retail-toy-pilot.kui').read_bytes()
    envelope = inspect_retail(runtime)
    payload, memory = flatten_elf((build / 'entry.elf').read_bytes())
    if envelope['build'] != commit[:12] or runtime[64:] != payload or envelope['memory_bytes'] != memory:
        raise ValueError('Source/runtime/entry identity differs')
    for name in ('resident-sci', 'worker', 'stage'):
        if (build / (name + '.bin')).read_bytes() != images[name]['payload']:
            raise ValueError('Linked binary differs: ' + name)
        instruction_audit(name, disassemblies[name])
    if payload[layout.STAGE_BLOB_OFFSET:] != padded(images['stage']['payload']):
        raise ValueError('Runtime embeds different stage bytes')
    embedded = {
        'worker': embedded_blob(images['stage'], '__toy_pilot_worker_blob_start',
            '__toy_pilot_worker_blob_end', layout.STAGE_ADDRESS,
            images['worker']['payload'], 'synthetic worker'),
        'resident': embedded_blob(images['stage'], '__retail_resident_sci_blob_start',
            '__retail_resident_sci_blob_end', layout.STAGE_ADDRESS,
            padded(images['resident-sci']['payload']), 'SCI resident'),
    }
    # The original reader remains linked but S never calls it for audio.
    # Conservatively retain its complete low callback stack allowance.
    low = pilot_low_stack(build, images['resident-sci']['symbols'], images['worker']['symbols'])
    worker = conservative_stack(build / 'worker', stack_bytes=8192, assembly_bytes=256)
    callback = stack_rows(list((build / 'sci/lto').glob('*.ltrans*.su')))
    callback_bytes = sum(row['bytes'] for row in callback if row['function'] != 'kui_retail_resident_init')
    worker['conservative_bytes'] += callback_bytes
    worker['callback_low_c_bytes'] = callback_bytes
    if worker['conservative_bytes'] > worker['available_bytes']:
        raise ValueError('Conservative worker/reader stack exceeds reservation')
    files = {'profiles/S-silent-source.kui': runtime,
             'README.md': (ROOT / 'docs/cdda-toy-synthetic-source-test.md').read_bytes(),
             'evidence/host-tests.txt': host,
             'evidence/cache-layout.json': (json.dumps(cache_proof, indent=2) + '\n').encode(),
             'evidence/build-config': (build / 'build-config').read_bytes(),
             'evidence/toy_pilot_resident_symbols.h': (build / 'toy_pilot_resident_symbols.h').read_bytes(),
             'source-snapshot.tar': subprocess.check_output(['git', 'archive', '--format=tar', 'HEAD'], cwd=ROOT),
             'LICENSE': (ROOT / 'LICENSE').read_bytes()}
    for path in sorted((ROOT / 'LICENSES').glob('*')):
        if path.is_file():
            files['LICENSES/' + path.name] = path.read_bytes()
    for path in sorted(build.rglob('*')):
        if path.is_file() and path.suffix in ('.elf', '.map', '.su'):
            data = path.read_bytes()
            if not data:
                raise ValueError('Empty emitted evidence: ' + str(path))
            files['evidence/build/' + path.relative_to(build).as_posix()] = data
    with ZipFile(retained) as z:
        r = z.read('profiles/R-private-write-through.kui')
        fallback = z.read('fallback/7b55156aafa2-retail-boot.kui')
        if sha(r) != R_SHA or inspect_retail(r)['build'] != '73e8363c10f7' or sha(fallback) != FALLBACK_SHA:
            raise ValueError('Retained restoration bytes differ')
        files['restore/R-private-write-through.kui'] = r
        files['restore/R-source-snapshot.tar'] = z.read('source-snapshot.tar')
        files['restore/R-build.json'] = z.read('build.json')
        files['restore/7b55156aafa2-retail-boot.kui'] = fallback
        files['restore/K-UI-Toy-Audio-Rollback.zip'] = z.read('fallback/K-UI-Toy-Audio-Rollback.zip')
    for name in ('docs/evidence/toy-cdda-transport-decision-2026-10-09.md',
                 'docs/evidence/toy-cdda-path-forward-2026-10-09.md'):
        files[name] = (ROOT / name).read_bytes()
    report = {
        'profile': 'S: diagnostic silent PCM16 source; no audio RAW card callbacks',
        'source_commit': commit, 'source_tree': tree, 'published_commit': published_commit,
        'runtime': envelope, 'runtime_sha256': sha(runtime), 'configuration': expected,
        'restoration': {'R': {'build': '73e8363c10f7', 'sha256': R_SHA},
                        'clean_audio': {'build': '7b55156aafa2', 'sha256': FALLBACK_SHA}},
        'snapshot': {'version': 8, 'worker_words': legend,
                     'S_raw_fields_mean': 'generated source requests and generation duration, not physical card reads'},
        'cache_layout': cache_proof, 'resident_stack': low, 'worker_stack': worker,
        'embedded': embedded, 'audio_or_video_improvement_proven': False,
        'archive_contains_game_files': False,
    }
    files['build.json'] = (json.dumps(report, indent=2, sort_keys=True) + '\n').encode()
    files['SHA256SUMS'] = ''.join(sha(data) + '  ' + name + '\n'
                                  for name, data in sorted(files.items())).encode()
    return files, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--retained-cache-zip', type=Path, required=True)
    parser.add_argument('--host-log', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--published-commit')
    args = parser.parse_args()
    files, report = collect(args.build_dir.resolve(), args.retained_cache_zip,
                            args.host_log, args.published_commit)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_archive(args.output, files)
    print(json.dumps({'output': str(args.output.resolve()), 'build': report['runtime']['build'],
                      'bytes': args.output.stat().st_size, 'sha256': sha(args.output.read_bytes()),
                      'resident_stack': report['resident_stack'], 'worker_stack': report['worker_stack']}, indent=2))


if __name__ == '__main__':
    main()
