#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the GUI's exact Standard-module allowlist from its linked cohort.

This runs while building software, never while launching an installed file.
The final delivery must additionally run the complete linked family audit;
this manifest preserves strict whole-code recognition without handwritten
payload sizes, offsets or per-game/build hash entries in GUI source.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from check_loader_layout import inspect_elf
from check_native_vmu_only import sci_rx_selection
from check_vmu_native_persistent import check_source
from check_vmu_native_worker_gateway import read_elf
from native_runtime_layout import VMU_LITE, layout_for
from package_native_vmu_only import bind_profile
from runtime_package import flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
ONE = (1, 0, 0, 0xfff1)
ALIAS = 'retail-native-vmu-lite-paced.kui'


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def generate(build, allow_dirty=False):
    build = Path(build).resolve()
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT))
    require(allow_dirty or not dirty, 'Generate the final manifest from committed source')
    module, personalization = bind_profile(build, commit, ALIAS)
    metadata = verify(module)
    provenance = check_source(build, metadata)
    require(provenance['commit'] == commit and (allow_dirty or not provenance['source_dirty']),
            'Standard module is not a matching clean cohort build')
    payload, memory = flatten_elf((build / 'entry.elf').read_bytes())
    require(payload == module[64:] and memory == len(payload), 'Standard module differs from linked entry')
    stage, worker, low = (read_elf(build / name) for name in
                          ('stage.elf', 'native-worker.elf', 'resident-sci.elf'))
    require(all(layout_for(image[4]) is VMU_LITE for image in (stage, worker, low)),
            'Standard manifest requires the exact72KiB VMU-lite family')
    require(stage[4].get('__kui_native_vmu_only_stage') == ONE and
            stage[4].get('__kui_native_vmu_only_transport_stage') == ONE and
            worker[4].get('__kui_native_virtual_slot_worker') == (2, 0, 0, 0xfff1) and
            stage[4].get('__kui_native_virtual_slot_stage') == (2, 0, 0, 0xfff1) and
            sci_rx_selection(stage[4], low[4]) == 'paced',
            'Standard manifest requires paired pacedSCI and virtualA2')
    require('__smash_native_menu_seed' not in stage[4] and '__smash_native_child_seed' not in stage[4] and
            '_native_background_boot_region' not in stage[4],
            'A selected-title or Background module cannot become Standard')
    require(low[4]['_manifest'][1] == 252 + 64 * 12,
            'Standard module changed its64-slot retained manifest ABI')
    inspect_elf(low[0], 0x8c004000, 0x8c007800)
    offset = personalization['profile_module_offset'] - 64
    require(0 <= offset <= len(payload) - 4096 and not offset & 3,
            'Standard personalization span is outside its exact payload')
    normalized = payload[:offset] + bytes(4096) + payload[offset + 4096:]
    report = dict(schema='kui.native.standard-build-manifest.v1',
        source_commit=commit, source_dirty=dirty or provenance['source_dirty'],
        build=metadata['build'], profile='paced-SCI-VMU-lite-A2',
        payload_bytes=len(payload), memory_bytes=memory, profile_payload_offset=offset,
        normalized_payload_sha256=sha(normalized), module_sha256=sha(module),
        module_bytes=len(module), profile_bytes=4096,
        provenance=provenance, launch_mutable_spans=[[offset, 4096]],
        build_labels_normalized=False, unknown_installed_modules_refused=True,
        artifacts_sha256={name: sha((build / name).read_bytes()) for name in
            ('entry.elf', 'stage.elf', 'native-worker.elf', 'resident-sci.elf', ALIAS)},
        full_linked_family_audit_required_for_delivery=True)
    digest = bytes.fromhex(report['normalized_payload_sha256'])
    lines = ['/* SPDX-License-Identifier: GPL-3.0-only */',
             '/* Generated build-time allowlist; never populate this from installed SD files. */',
             '#define KUI_NATIVE_STANDARD_MANIFEST_VERSION 1',
             f'#define KUI_NATIVE_STANDARD_MANIFEST_BUILD "{metadata["build"]}"',
             f'#define KUI_NATIVE_STANDARD_MANIFEST_PAYLOAD_BYTES {len(payload)}u',
             f'#define KUI_NATIVE_STANDARD_MANIFEST_PROFILE_OFFSET {offset}u',
             'static const uint8_t kui_native_standard_manifest_sha256[32]={',
             '    ' + ','.join('0x%02x' % byte for byte in digest[:16]) + ',',
             '    ' + ','.join('0x%02x' % byte for byte in digest[16:]) + '};', '']
    return '\n'.join(lines), report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--header', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--allow-dirty', action='store_true')
    args = parser.parse_args()
    header, report = generate(args.build, args.allow_dirty)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    # One complete source header is made visible only after all cohort checks.
    temporary = args.header.with_suffix(args.header.suffix + '.tmp')
    temporary.write_text(header)
    temporary.replace(args.header)
    report['header_sha256'] = sha(header.encode())
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(status='PASS', build=report['build'], header=str(args.header),
        module_sha256=report['module_sha256'], normalized_payload_sha256=report['normalized_payload_sha256'])))


if __name__ == '__main__':
    main()
