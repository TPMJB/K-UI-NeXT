#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the general split controller, structural New reader and Games UI.

The split-controller module is packaged generically, exactly as the audited
personalizer expects; the installer binds it to the owner's prepared card.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from runtime_package import verify
from vmu_persist_profile import WIRE_BYTES, template_profile
import vmu_native_mode

SCHEMA = 'kui.native.general.delivery.v1'
PERSONALIZATION = 'kui.native.persistent.personalization.v1'
# The persistent modes the 6be split controller manifest advertised.
MODE_CHOICES = {'normal': 0, 'profile': 2, 'hooks-only': 4, 'raw-chain': 5, 'evolution-trace': 6}
# DOA2 pilot 921318c3d9cb installed into retail-boot.kui. Its own installer
# needs the 8adf UI to restore it, so install refuses while it is present.
DOA2_PILOT_MODULE = 'a871670e929866a994b69f2772424b6163f4cd474a64c423f2cb70cfb4634e78'
PACKAGE_TOOLS = ('runtime_package.py', 'vmu_persist_profile.py', 'vmu_native_cache_seed.py', 'vmu_native_mode.py')


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(*arguments):
    return subprocess.check_output(['git', '-C', str(ROOT), *arguments], text=True).strip()


def personalization_manifest(module, source_commit):
    info = verify(module)
    template = template_profile()
    offset = module.find(template)
    require(offset >= 64 and not offset & 3 and module.find(template, offset+1) == -1,
            'Split-controller module needs exactly one aligned profile template')
    mode_offset = vmu_native_mode.find_mode(module)
    mode = vmu_native_mode.template_mode()
    require(mode_offset+len(mode) <= offset or offset+WIRE_BYTES <= mode_offset,
            'Runtime mode overlaps the profile template')
    require(info['build'] == source_commit[:12], 'Module build differs from the source commit')
    return dict(schema=PERSONALIZATION, source_commit=source_commit, build=info['build'],
                module_sha256=sha(module), module_bytes=len(module), storage_writes_enabled=True,
                profile_module_offset=offset, profile_bytes=WIRE_BYTES, profile_sha256=sha(template),
                runtime_mode_module_offset=mode_offset, runtime_mode_bytes=len(mode),
                runtime_mode_sha256=sha(mode), runtime_mode_choices=MODE_CHOICES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dual', type=Path, default=ROOT/'build/native-isolation-split-controller-general/retail-native-dual.kui')
    parser.add_argument('--standalone', type=Path, default=ROOT/'build/native-standalone-general/retail-native-standalone.kui')
    parser.add_argument('--gui', type=Path, required=True, help='runtime.kui built from the same source commit')
    parser.add_argument('--evidence', type=Path, action='append', default=[], help='Evidence file to include')
    parser.add_argument('--readme', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Directory receiving the package folder and ZIP')
    args = parser.parse_args()
    source_commit = git('rev-parse', 'HEAD')
    require(not git('status', '--porcelain', '--untracked-files=no'), 'Commit all source changes before packaging')
    build = source_commit[:12]
    dual, standalone, gui = (path.read_bytes() for path in (args.dual, args.standalone, args.gui))
    for name, data in (('dual', dual), ('standalone', standalone), ('gui', gui)):
        require(verify(data)['build'] == build, name+' module was not built from '+build)
    name = 'K-UI-Native-General-'+build
    folder = args.output/name
    require(not folder.exists(), 'Output folder already exists: '+str(folder))
    (folder/'dual/base/KUI/apps/games').mkdir(parents=True)
    (folder/'standalone').mkdir()
    (folder/'gui').mkdir()
    (folder/'tools').mkdir()
    (folder/'evidence').mkdir()
    (folder/'dual/base/KUI/apps/games/retail-boot.kui').write_bytes(dual)
    manifest = personalization_manifest(dual, source_commit)
    (folder/'dual/base/native-persistent-personalization.json').write_text(json.dumps(manifest, indent=2)+'\n')
    (folder/'standalone/retail-native-standalone.kui').write_bytes(standalone)
    (folder/'gui/runtime.kui').write_bytes(gui)
    shutil.copyfile(ROOT/'tools/install_native_general.py', folder/'install.py')
    shutil.copyfile(ROOT/'tools/personalize_vmu_native_persistent.py', folder/'personalize.py')
    for tool in PACKAGE_TOOLS:
        shutil.copyfile(ROOT/'tools'/tool, folder/'tools'/tool)
    for path in args.evidence:
        shutil.copyfile(path, folder/'evidence'/path.name)
    shutil.copyfile(args.readme, folder/'README-FIRST.md')
    shutil.copyfile(ROOT/'LICENSE', folder/'LICENSE')
    shutil.copyfile(ROOT/'THIRD_PARTY.md', folder/'THIRD_PARTY.md')
    metadata = dict(schema=SCHEMA, build=build, source_commit=source_commit,
                    gui=dict(build=build, sha256=sha(gui)),
                    dual=dict(build=build, sha256=sha(dual), split_controller_general=True,
                              virtual_a2=True, controller_tick_off=True, prefetch_enabled=False),
                    standalone=dict(build=build, sha256=sha(standalone), virtual_vmu=False,
                                    game_ram_reservation=False),
                    refuse_ordinary_sha256=[DOA2_PILOT_MODULE],
                    console_compatibility_verified=False)
    (folder/'general.json').write_text(json.dumps(metadata, indent=2)+'\n')
    subprocess.run(['git', '-C', str(ROOT), 'bundle', 'create', str(folder/'source.bundle'), 'HEAD'],
                   check=True, capture_output=True)
    files = sorted(path for path in folder.rglob('*') if path.is_file())
    sums = ''.join(f'{sha(path.read_bytes())}  {path.relative_to(folder).as_posix()}\n' for path in files)
    (folder/'SHA256SUMS.txt').write_text(sums)
    archive = args.output/(name+'.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
        for path in sorted(path for path in folder.rglob('*') if path.is_file()):
            output.write(path, (Path(name)/path.relative_to(folder)).as_posix())
    print(json.dumps(dict(package=str(archive), bytes=archive.stat().st_size,
                          sha256=sha(archive.read_bytes()), **metadata), indent=2))


if __name__ == '__main__':
    main()
