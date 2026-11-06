#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate exact GUI admission from the source-built native CDDA cohort."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct

from check_native_cdda_compat import check_directory
from runtime_package import flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
PROFILES = {
    'toy-raw': ('KUI_GAMES_CDDA_TOY_RAW', 'retail-toy-pilot.kui',
                'cdda-toy-raw.kui', 'cdda-native-toy-raw.kui'),
    'toy-cooked': ('KUI_GAMES_CDDA_TOY_COOKED', 'retail-toy-pilot.kui',
                   'cdda-toy-cooked.kui', 'cdda-native-toy-cooked.kui'),
    'mdk2': ('KUI_GAMES_CDDA_MDK2_RAW', 'retail-mdk2-cdda.kui',
             'retail-cdda-mdk2.kui', 'cdda-native-mdk2.kui'),
}


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def runtime_binding(runtime_elf, report):
    """Bind the generated exact identities to the actual linked GUI table."""
    from check_vmu_native_worker_gateway import read_elf, symbol_bytes
    image = read_elf(Path(runtime_elf), allow_tls=True)
    _, body, section = symbol_bytes(image, '_kui_games_cdda_manifest', 1)
    expected = b''.join(struct.pack('<III13s32s3x', index + 1,
        report['profiles'][profile]['payload_bytes'],
        int(report['profiles'][profile]['crc32'], 16),
        report['profiles'][profile]['build'].encode(),
        bytes.fromhex(report['profiles'][profile]['payload_sha256']))
        for index, profile in enumerate(PROFILES))
    require(body == expected and section[2] & 3 == 2,
            'Linked GUI native CDDA table differs from the generated exact cohort')
    return dict(status='PASS', profiles=list(PROFILES), exact_table_bytes=len(body),
                exact_table_sha256=sha(body), runtime_elf_sha256=sha(Path(runtime_elf).read_bytes()))


def generate(builds, allow_dirty=False):
    require(set(builds) == set(PROFILES), 'The manifest requires all three native CDDA profiles')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT))
    require(allow_dirty or not dirty, 'Generate the final manifest from committed source')
    records = {}
    rows = []
    for profile, (identity, filename, old_alias, new_alias) in PROFILES.items():
        directory = Path(builds[profile]).resolve()
        audit = check_directory(directory, profile)
        package = (directory / filename).read_bytes()
        info = verify(package)
        payload, memory = flatten_elf((directory / 'entry.elf').read_bytes())
        require(payload == package[64:] and memory == info['memory_bytes'] == len(payload),
                'Native CDDA module differs from its exact linked entry: ' + profile)
        provenance = {}
        for role, path in (('current', directory / 'entry.elf'),
                           ('reference', Path(audit['reference_directory']) / 'entry.elf')):
            receipt_path = path.with_suffix('.compile.json')
            receipt = json.loads(receipt_path.read_text())
            require(receipt.get('commit') == commit and
                    receipt.get('elf_sha256') == sha(path.read_bytes()) and
                    (allow_dirty or receipt.get('source_dirty') is False),
                    'Native CDDA source receipt differs from this cohort: ' + profile + '/' + role)
            provenance[role] = dict(receipt=receipt, receipt_sha256=sha(receipt_path.read_bytes()))
        require(info['build'] == commit[:12], 'Native CDDA display build differs from source')
        digest = sha(payload)
        records[profile] = dict(build=info['build'], payload_bytes=len(payload),
            payload_sha256=digest, module_sha256=sha(package), crc32=info['crc32'],
            aliases=['KUI/apps/games/' + name for name in (old_alias, new_alias)],
            source_provenance=provenance, linked_audit=audit,
            old_exact_admission_preserved=True, descriptor_admission_required=True,
            new_console_acceptance_claimed=False)
        rows.append('    {' + identity + ',' + str(len(payload)) + 'u,UINT32_C(0x' +
                    info['crc32'] + '),"' + info['build'] + '",{' +
                    ','.join('0x%02x' % byte for byte in bytes.fromhex(digest)) + '}},')
    header = '\n'.join([
        '/* SPDX-License-Identifier: GPL-3.0-only */',
        '/* Generated from source-built, independently paired native CDDA adapters. */',
        '#define KUI_NATIVE_CDDA_MANIFEST_VERSION 1',
        'static const struct kui_games_cdda_manifest_entry kui_games_cdda_manifest[3]={',
        *rows, '};', ''])
    report = dict(schema='kui.native.cdda-build-manifest.v1', source_commit=commit,
        source_dirty=dirty or any(p['receipt']['source_dirty'] for r in records.values()
                                  for p in r['source_provenance'].values()),
        header_sha256=sha(header.encode()), profiles=records,
        installed_module_self_authorization=False, routing_precedence_changed=False,
        playback_configuration_retuned=False)
    return header, report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for profile in PROFILES:
        p.add_argument('--' + profile, type=Path, default=ROOT / ('build/native-compat-' + profile))
    p.add_argument('--header', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--allow-dirty', action='store_true')
    a = p.parse_args()
    header, report = generate({profile: getattr(a, profile.replace('-', '_')) for profile in PROFILES},
                              a.allow_dirty)
    a.header.parent.mkdir(parents=True, exist_ok=True)
    a.report.parent.mkdir(parents=True, exist_ok=True)
    temporary = a.header.with_suffix(a.header.suffix + '.tmp')
    temporary.write_text(header)
    temporary.replace(a.header)
    a.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(status='PASS', build=report['source_commit'][:12],
                         header=str(a.header), profiles=list(report['profiles']))))


if __name__ == '__main__':
    main()
