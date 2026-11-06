#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify Swirl's source-specific GD request and imported Sleep contracts.

Only independently authored metadata is emitted; no original kernel bytes or
disassembly are extracted or written. Admission is not console compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zlib

from armada_ce_rom_audit import ROMInventory

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA256 = 'a19080f2e243c04e01a3a473c7a3caa36fb85a5a8fbb2163c8f7a1b2c8ed5ea8'
SOURCE_BYTES = 1828864


def audit(path):
    path = Path(path)
    if path.stat().st_size != SOURCE_BYTES:
        raise ValueError('Swirl GD wait requires the exact original kernel size')
    if hashlib.sha256(path.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError('Swirl GD wait requires the exact original kernel identity')
    evidence = json.loads((ROOT / 'docs/evidence/swirl-gd-request-busy-source.json').read_text())
    original = json.loads((ROOT / 'docs/evidence/vc2-gd-request-busy-source.json').read_text())
    rom = ROMInventory(path)
    module = rom.modules['wsegacd.dll']
    if module['nominal_virtual_base'] != 0x01cf0000:
        raise ValueError('Swirl WSEGACD nominal mapping changed')
    rows = evidence['windows'] + evidence['firmware_calls'] + [
        evidence['sleep_import']['thunk'], evidence['wait_import']['thunk']]
    for row in rows:
        data = rom.rva_read(module, int(row['rva'], 16), row['bytes'])
        if (hashlib.sha256(data).hexdigest() != row['sha256'] or
                f'{zlib.crc32(data):08x}' != row['crc32']):
            raise ValueError('Swirl GD wait source window changed: ' + row['rva'])
    # Independent import/export decoding, rather than a nominal address guess.
    symbols = [s for group in rom.imports(module) if group['dll'].lower() == 'coredll.dll'
               for s in group['symbols']]
    sleeping = [s for s in symbols if s.get('ordinal') == 496]
    if len(sleeping) != 1 or sleeping[0]['iat_rva'] != 0x6048:
        raise ValueError('Swirl native Sleep import changed')
    core = rom.modules['coredll.dll']
    exports = [s for s in rom.exports(core) if s['ordinal'] == 496]
    if len(exports) != 1 or exports[0]['name'] != 'Sleep' or exports[0]['rva'] != 0x8708:
        raise ValueError('Swirl native Sleep export changed')
    row = evidence['sleep_import']['native_export']
    data = rom.rva_read(core, int(row['rva'], 16), row['bytes'])
    if hashlib.sha256(data).hexdigest() != row['sha256']:
        raise ValueError('Swirl native Sleep body changed')
    if evidence['firmware_calls'] != original['firmware_calls']:
        raise ValueError('Swirl firmware call contracts differ from shared production profile')
    if evidence['request_calls'] != original['request_calls'] or len(evidence['request_calls']) != 18:
        raise ValueError('Swirl FN0 call contracts differ from shared production admission')
    if (evidence['sleep_import']['thunk'] != original['sleep_import']['thunk'] or
            evidence['sleep_import']['native_export'] != original['sleep_import']['native_export']):
        raise ValueError('Swirl native Sleep differs from the shared production contract')
    return dict(schema='kui.swirl.gd-wait-source-check.v1', status='PASS',
                kernel_sha256=SOURCE_SHA256, kernel_bytes=SOURCE_BYTES,
                original_bytes_or_disassembly_included=False,
                source_windows_checked=len(rows) + 1, firmware_call_windows=33,
                exact_fn0_sites_checked=18, native_sleep_ordinal=496,
                wsegacd_nominal_base='0x01cf0000', sleep_thunk_rva='0x581c',
                sleep_iat_rva='0x6048', native_sleep_export_rva='0x8708',
                production_gd_admission_reusable=True, console_compatibility_verified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kernel', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(audit(args.kernel), indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, 'Swirl GD source audit failed: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
