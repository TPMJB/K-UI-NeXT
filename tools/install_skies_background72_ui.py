#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install or restore only this package's GUI in a new independent namespace."""
import argparse
import importlib.util
import json
from pathlib import Path

SCHEMA = 'kui.skies-background72-ui.delivery.v1'
BACKUP_SCHEMA = 'kui.skies-background72-ui.backup.v1'
STATE_SCHEMA = 'kui.skies-background72-ui.state.v1'
PARENT = Path('KUI/skies-background72-ui')
BACKUP, STATE = PARENT/'backup', PARENT/'state.json'
DESTINATION = Path('KUI/runtime.kui')


def load_ui(package):
    path = Path(package)/'tools/install_skies_launch_ui.py'
    if not path.is_file() or path.is_symlink():
        raise ValueError('Missing ordinary packaged GUI installer core')
    spec = importlib.util.spec_from_file_location('background72_private_ui', path)
    if not spec or not spec.loader:
        raise ValueError('Cannot load packaged GUI installer core')
    ui = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ui)
    ui.SCHEMA, ui.BACKUP_SCHEMA, ui.STATE_SCHEMA = SCHEMA, BACKUP_SCHEMA, STATE_SCHEMA
    ui.PARENT, ui.BACKUP, ui.STATE, ui.DESTINATION = PARENT, BACKUP, STATE, DESTINATION
    return ui


def install(root, package, restore=False):
    package = Path(package).resolve(strict=True)
    return load_ui(package).install(root, package, restore)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--restore', action='store_true')
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        print(json.dumps(install(args.sd_root, args.package, args.restore), indent=2))
        print('Exact pre-package GUI restored.' if args.restore else 'Matching compact Background GUI installed.')
        print('Loaders, card data, settings and earlier test receipts are unchanged. Safely eject before booting.')
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'Compact Background GUI installation refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
