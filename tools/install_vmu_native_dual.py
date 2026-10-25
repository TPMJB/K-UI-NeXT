#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install matching Games UI and generic native X/Y+A2 module; keep ordinary loaders."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent/'tools'))

from runtime_package import verify, MAX_BYTES

DESTINATION = Path('KUI/apps/games/retail-native-dual.kui')
GUI = Path('KUI/runtime.kui')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load_personalizer(package):
    spec = importlib.util.spec_from_file_location('dual_personalizer', package/'personalize.py')
    require(spec and spec.loader, 'Missing native personalizer')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def temporary_bytes(path, data):
    fd, name = tempfile.mkstemp(prefix='.native-dual-', dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, 'wb') as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        require(temporary.read_bytes() == data, 'Candidate temporary readback failed')
        return temporary
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def install(root, package, profile=None, profile_runtime=False, evolution_trace=False):
    root = Path(root).resolve(strict=True)
    package = Path(package).resolve(strict=True)
    p = load_personalizer(package)
    module, manifest, info = p.generic_module(package)
    metadata = json.loads(p.read_regular(package/'base/native-dual-runtime.json', package,
                                         maximum=4096).decode())
    gui = p.read_regular(package/'base'/GUI, package, maximum=MAX_BYTES+64)
    gui_info = verify(gui)
    descriptor = bytes.fromhex('4b4452314455414c010000002000000001000000020000000000060000000000')
    require(module[64:].count(descriptor) == 1, 'Module lacks its unique native X/Y capability')
    require(metadata.get('schema') == 'kui.native.dual-runtime.v1' and
            metadata.get('build') == gui_info['build'] == info['build'] == manifest['build'] and
            metadata.get('runtime_sha256') == sha(gui) and
            metadata.get('module_sha256') == sha(module) and
            metadata.get('runtime_bytes') == len(gui), 'Mismatched UI and native reader candidate')
    gui_path = root/GUI
    module_path = root/DESTINATION
    p.ordinary_path(gui_path.parent, root, directory=True)
    p.ordinary_path(module_path.parent, root, directory=True)
    old_gui = p.read_regular(gui_path, root, maximum=MAX_BYTES+64) if gui_path.exists() or gui_path.is_symlink() else None
    old_module = p.read_regular(module_path, root, maximum=MAX_BYTES+64) if module_path.exists() or module_path.is_symlink() else None
    require(type(profile_runtime) is bool and type(evolution_trace) is bool and
            not (profile_runtime and evolution_trace),
            'Select either runtime profiling or Evolution tracing')
    selected = p.select_profile(root, profile)
    runtime_mode = 6 if evolution_trace else 2 if profile_runtime else 0
    personalized = p.personalize_bytes(module, manifest, info, selected['wire'],
                                      runtime_mode=runtime_mode)
    # Stage both files before publication; the GUI becomes visible last.
    # Track each rename before fsync so every caught publication/readback
    # failure can restore this invocation's exact bytes.
    pending = []
    published = []
    try:
        for path, before, after in ((module_path, old_module, personalized),
                                    (gui_path, old_gui, gui)):
            if before != after:
                pending.append((path, before, after, temporary_bytes(path, after)))
        require(p.read_regular(selected['path'], root, expected=p.WIRE_BYTES) == selected['wire'] and
                p.read_regular(selected['path'].parent/'identity.bin', root, expected=16) == selected['identity'],
                'Selected profile/identity changed before installation')
        for path, before, after, temporary in pending:
            p.ordinary_path(path.parent, root, directory=True)
            current = p.read_regular(path, root, maximum=MAX_BYTES+64) if path.exists() or path.is_symlink() else None
            require(current == before, 'Installed file changed before publication: '+str(path))
            os.replace(temporary, path)
            published.append((path, before, after))
            sync_directory(path.parent)
        require(p.read_regular(gui_path, root, maximum=MAX_BYTES+64) == gui and
                p.read_regular(module_path, root, maximum=MAX_BYTES+64) == personalized,
                'Installed candidate readback differs')
    except BaseException as error:
        for path, before, after in reversed(published):
            try:
                # Do not replace bytes another process changed after our rename.
                if p.read_regular(path, root, maximum=MAX_BYTES+64) != after:
                    continue
                if before is None:
                    path.unlink()
                else:
                    rollback = temporary_bytes(path, before)
                    try:
                        os.replace(rollback, path)
                    finally:
                        rollback.unlink(missing_ok=True)
                sync_directory(path.parent)
            except (OSError, ValueError) as rollback_error:
                error.add_note('Rollback failed for '+str(path)+': '+str(rollback_error))
        raise
    finally:
        for _, _, _, temporary in pending:
            temporary.unlink(missing_ok=True)
    return dict(build=info['build'], runtime_sha256=sha(gui),
                module_sha256=sha(personalized), profile=str(selected['path'].relative_to(root)),
                runtime_mode=p.mode_codec().MODE_NAMES[runtime_mode] if p.MODE_FIELDS <= set(manifest) else 'normal',
                installed_files=[str(GUI), str(DESTINATION)],
                fresh_discovery=True, ordinary_loaders_unchanged=True)



if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--profile', type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--profile-runtime', action='store_true')
    mode.add_argument('--evolution-trace', action='store_true',
                      help='Use the existing exact Evolution 1 startup trace and capture')
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent,
                        help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        result = install(args.sd_root, args.package, args.profile, args.profile_runtime, args.evolution_trace)
        print('Installed native New (X) / Legacy (Y) and virtual A2 candidate '+result['build'])
        print('Prepared card files and existing game-launch settings were unchanged.')
        print(json.dumps(result, indent=2))
        if args.evolution_trace:
            print('Evolution 1 trace enabled; record startup and any complete repeating report cycle.')
        print('Safely eject, then use SCI / Background On / New (X) / CD music Off / RAM patches Off.')
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'Native X/Y installation refused: '+str(error)+'\n')
