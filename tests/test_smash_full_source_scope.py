#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual full-source SHA fallback's private cache wrapper."""
import argparse
import json
from pathlib import Path
import test_smash_native_cached_replay as replay


def audit(elf):
    # The fallback has the identical no-argument ABI and private stack as the
    # existing resolution proof. The existing machine adversary also writes
    # private BSS, a stricter publication demand than this SHA-only proof.
    replay.WRAPPER = '_kui_smash_native_menu_cached_full_source_owner'
    replay.HELPER = '_kui_smash_native_menu_full_source_owner'
    replay.MARKER = '__kui_smash_native_menu_cached_full_source_owner'
    result = replay.audit(elf)
    result['schema'] = 'kui.smash-native.full-source-scope.v1'
    result['purpose'] = 'Fresh complete-source owner SHA when the SDK proof covers a reclaimed prefix'
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = audit(args.elf)
    rendered = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.write_text(rendered)
    print(rendered, end='')
