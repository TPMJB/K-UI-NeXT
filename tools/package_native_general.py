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
import re
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
# Each split-controller build states its prefetch mode in its stage banner.
PREFETCH = {'off': ('build/native-isolation-split-controller-general', 'K-UI-Native-General-',
                    b'PREFETCH OFF / SPLIT CONTROLLER ALL\0'),
            'on': ('build/native-split-controller-prefetch', 'K-UI-Native-General-Prefetch-',
                   b'PREFETCH ON / SPLIT CONTROLLER ALL\0'),
            'auto': ('build/native-split-controller-stream-guard', 'K-UI-Native-General-Prefetch-Auto-',
                     b'PREFETCH AUTO / SPLIT CONTROLLER ALL\0')}
PACKAGE_TOOLS = ('runtime_package.py', 'vmu_persist_profile.py', 'vmu_native_cache_seed.py', 'vmu_native_mode.py',
                 # The owner's PC tools: per-game card migration and the library sweep.
                 'kui-game-cards.py', 'explain_vmu_image.py', 'kui-library-startup-bundle.py',
                 'kui-native-startup-bundle.py', 'kui-library-startup-bundle-README.md',
                 'decode_native_ram_map.py')
# The standalone RAM-map test build (Makefile.native-standalone-ram-map)
# announces itself on the stage screen; the structural build never does.
RAM_MAP_BANNER = b'RAM MAP TEST - FREE PAGES MARKED\0'
STANDALONE_VARIANTS = {'structural': ('build/native-standalone-general', ''),
                       'ram-map': ('build/native-standalone-ram-map', 'RamMap-')}


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(*arguments):
    return subprocess.check_output(['git', '-C', str(ROOT), *arguments], text=True).strip()


def runtime_predecessors(paths):
    """Record only verified whole-runtime predecessors, never build-name wildcards."""
    result, seen = [], set()
    require(len(paths) <= 32, 'At most 32 runtime predecessors may be recorded')
    for path in paths:
        data = path.read_bytes()
        info = verify(data)
        require(re.fullmatch('[0-9a-f]{12}', info['build']), 'Runtime predecessor needs a source build ID')
        digest = sha(data)
        if digest not in seen:
            result.append(dict(build=info['build'], sha256=digest))
            seen.add(digest)
    return result


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
    parser.add_argument('--dual', type=Path, help='Default: the selected split-controller build')
    parser.add_argument('--prefetch', choices=sorted(PREFETCH), default='off',
                        help='Split-controller prefetch: off, on, or auto (paused while the game streams)')
    parser.add_argument('--standalone', type=Path, help='Default: the selected standalone build')
    parser.add_argument('--standalone-variant', choices=sorted(STANDALONE_VARIANTS), default='structural',
                        help='Standalone New reader: structural, or the console RAM-map test')
    parser.add_argument('--gui', type=Path, required=True, help='runtime.kui built from the same source commit')
    parser.add_argument('--accept-runtime-predecessor', type=Path, action='append', default=[],
                        help='Exact verified old runtime replaceable only with a matching four-file checkpoint')
    parser.add_argument('--evidence', type=Path, action='append', default=[], help='Evidence file to include')
    parser.add_argument('--readme', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Directory receiving the package folder and ZIP')
    args = parser.parse_args()
    if args.dual is None:
        args.dual = ROOT/PREFETCH[args.prefetch][0]/'retail-native-dual.kui'
    if args.standalone is None:
        args.standalone = ROOT/STANDALONE_VARIANTS[args.standalone_variant][0]/'retail-native-standalone.kui'
    ram_map = args.standalone_variant == 'ram-map'
    source_commit = git('rev-parse', 'HEAD')
    require(not git('status', '--porcelain', '--untracked-files=no'), 'Commit all source changes before packaging')
    build = source_commit[:12]
    dual, standalone, gui = (path.read_bytes() for path in (args.dual, args.standalone, args.gui))
    for name, data in (('dual', dual), ('standalone', standalone), ('gui', gui)):
        require(verify(data)['build'] == build, name+' module was not built from '+build)
    predecessors = runtime_predecessors(args.accept_runtime_predecessor)
    require(all(dual.count(banner) == (mode == args.prefetch) for mode, (_, _, banner) in PREFETCH.items()),
            'Split-controller module prefetch mode differs from --prefetch')
    require(standalone.count(RAM_MAP_BANNER) == ram_map and not dual.count(RAM_MAP_BANNER),
            'Standalone module variant differs from --standalone-variant')
    name = PREFETCH[args.prefetch][1]+STANDALONE_VARIANTS[args.standalone_variant][1]+build
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
                              virtual_a2=True, controller_tick_off=args.prefetch == 'off',
                              prefetch_enabled=args.prefetch != 'off', prefetch_mode=args.prefetch),
                    standalone=dict(build=build, sha256=sha(standalone), virtual_vmu=False,
                                    game_ram_reservation=False, ram_map_test=ram_map),
                    refuse_ordinary_sha256=[DOA2_PILOT_MODULE],
                    console_compatibility_verified=False)
    if predecessors:
        metadata['accepted_runtime_predecessors'] = predecessors
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
