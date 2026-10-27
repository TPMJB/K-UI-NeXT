#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Select native or Windows CE startup analysis from the original GDI/IP flag.

Keep this script beside kui-native-startup-bundle.py and
kui-wince-startup-bundle.py. Requires only Python3's standard library.
Reads original raw2352 GDI tracks without modifying them and never overwrites
the output. This prepares analysis evidence, not a patch or compatibility claim.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import zipfile

EXTRACTOR_VERSION = '1'


def load_helper(name):
    path = Path(__file__).resolve().with_name(f'kui-{name}-startup-bundle.py')
    spec = importlib.util.spec_from_file_location(f'kui_{name}_startup_bundle', path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    return helper


native = load_helper('native')
wince = load_helper('wince')


def extract(gdi):
    """Retry only when a validated IP header explicitly identifies Windows CE."""
    helper = native
    try:
        entries, report = native.extract(gdi)
    except native.WindowsCEProfileError:
        helper = wince
        entries, report = wince.extract(gdi)
    return helper, entries, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    version = f'K-UI startup analysis dispatcher v{EXTRACTOR_VERSION}'
    parser.add_argument('--version', action='version', version=version)
    parser.add_argument('gdi', type=Path, help='Original, unconverted raw2352 GDI')
    parser.add_argument('--output', type=Path, default=Path('Startup-analysis.zip'))
    args = parser.parse_args()
    print(version, flush=True)
    try:
        if os.path.lexists(args.output):
            raise ValueError(f'{args.output} already exists; choose a new --output filename')
        helper, entries, report = extract(args.gdi)
        helper.write_bundle(args.output, entries, report)
        family = 'Windows CE' if report['windows_ce'] else 'native'
        print(f'Created {args.output.resolve()} ({args.output.stat().st_size:,} bytes; {family} IP profile)')
        for warning in report.get('warnings', []):
            print(f'Note: {warning}')
        print('Upload this ZIP when requested for startup analysis. Source tracks were read only.')
    except (OSError, ValueError, zipfile.BadZipFile, UnicodeError) as error:
        parser.exit(1, f'Extraction stopped: {error}\n')


if __name__ == '__main__':
    main()
