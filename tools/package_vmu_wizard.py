#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the VMU import wizard UI without replacing native game modules."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

from runtime_package import verify

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = 'kui.vmu.wizard.delivery.v1'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def git(*arguments):
    return subprocess.check_output(['git', '-C', str(ROOT), *arguments], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--readme', type=Path, default=ROOT/'docs/vmu-import-wizard-test.md')
    parser.add_argument('--evidence', type=Path, action='append', default=[])
    args = parser.parse_args()
    source = git('rev-parse', 'HEAD')
    require(not git('status', '--porcelain', '--untracked-files=all'), 'Commit source changes before packaging')
    runtime = args.gui.read_bytes()
    build = verify(runtime)['build']
    require(build == source[:12], 'GUI build identifier differs from committed source')
    name = 'K-UI-VMU-Import-Wizard-'+build
    folder = args.output/name
    require(not folder.exists(), 'Output folder already exists: '+str(folder))
    (folder/'gui').mkdir(parents=True)
    (folder/'evidence').mkdir()
    (folder/'gui/runtime.kui').write_bytes(runtime)
    shutil.copyfile(ROOT/'tools/install_vmu_wizard.py', folder/'install.py')
    (folder/'README-FIRST.md').write_text(args.readme.read_text().replace('@BUILD@', build))
    shutil.copyfile(ROOT/'LICENSE', folder/'LICENSE')
    shutil.copyfile(ROOT/'THIRD_PARTY.md', folder/'THIRD_PARTY.md')
    used = set()
    for path in args.evidence:
        require(path.is_file() and path.name not in used, 'Missing or duplicate evidence file: '+str(path))
        used.add(path.name)
        shutil.copyfile(path, folder/'evidence'/path.name)
    metadata = dict(schema=SCHEMA, build=build, source_commit=source,
                    runtime_sha256=sha(runtime), required_dual_build='34dc7b5e15ee',
                    replaces=['KUI/runtime.kui'], console_compatibility_verified=False)
    (folder/'wizard.json').write_text(json.dumps(metadata, indent=2)+'\n')
    subprocess.run(['git', '-C', str(ROOT), 'bundle', 'create', str(folder/'source.bundle'), 'HEAD'],
                   check=True, capture_output=True)
    files = sorted(path for path in folder.rglob('*') if path.is_file())
    sums = ''.join(f'{sha(path.read_bytes())}  {path.relative_to(folder).as_posix()}\n' for path in files)
    (folder/'SHA256SUMS.txt').write_text(sums)
    archive = args.output/(name+'.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
        for path in sorted(path for path in folder.rglob('*') if path.is_file()):
            output.write(path, (Path(name)/path.relative_to(folder)).as_posix())
    require(archive.stat().st_size < 50 * 1024 * 1024, 'Package exceeds the 50 MiB delivery limit')
    print(json.dumps(dict(package=str(archive), bytes=archive.stat().st_size,
                         sha256=sha(archive.read_bytes()), **metadata), indent=2))


if __name__ == '__main__':
    main()
