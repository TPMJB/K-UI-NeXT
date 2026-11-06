#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real GUI Makefile graph across older bundle selections.

Only compiler/linker output and the unrelated build-receipt writer are tiny
authored boundaries. The production Makefile, force-build-id dependencies,
configuration updater and all bundle-selection prerequisites execute as-is.
This is a dependency test; it does not claim SH code or console execution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

COMPILER = r'''#!/usr/bin/env python3
import json,os,re,sys
from pathlib import Path
args=sys.argv[1:];out=Path(args[args.index('-o')+1]);out.parent.mkdir(parents=True,exist_ok=True)
event={'args':args,'output':str(out)}
if '-c' in args:
    source=Path(args[args.index('-c')+1])
    if source.name=='bundle.S':
        name=re.search(r'\.incbin\s+"([^"]+)"',source.read_text()).group(1)
        out.write_bytes(b'BUNDLE-OBJECT\0'+Path(name).read_bytes());event['bundle_compile']=True
    else:out.write_bytes(b'AUTHORED-C-OBJECT\0')
else:
    selected=[Path(p) for p in args if p.endswith('/runtime-module-bundle.o')]
    assert len(selected)<=1
    out.write_bytes(b'GUI-LINK\0'+(selected[0].read_bytes() if selected else b'NO-BUNDLE'))
    event['gui_link']=True
with open(os.environ['KUI_BUNDLE_MAKE_EVENTS'],'a') as f:f.write(json.dumps(event)+'\n')
'''


def sha(body):
    return hashlib.sha256(body).hexdigest()


class Fixture:
    def __init__(self, directory, makefile):
        self.root = directory
        self.makefile = directory / 'Makefile.dc'
        self.makefile.write_bytes(makefile)
        for name in ('config', 'src', 'include', 'resources', 'third_party', 'firmware'):
            (directory / name).symlink_to(ROOT / name, target_is_directory=True)
        tools = directory / 'tools'; tools.mkdir()
        for source in (ROOT / 'tools').iterdir():
            if source.name != 'record_build.py':
                (tools / source.name).symlink_to(source, target_is_directory=source.is_dir())
        (tools / 'record_build.py').write_text('# Dependency test: receipt writer is outside this graph.\n')
        kos = directory / 'kos'; kos.mkdir(); (kos / 'Makefile.rules').write_text('')
        binaries = directory / 'bin'; binaries.mkdir()
        compiler = binaries / 'kos-cc'; compiler.write_text(COMPILER); compiler.chmod(0o755)
        self.log = directory / 'events.jsonl'
        self.env = dict(os.environ, KOS_BASE=str(kos),
                        PATH=str(binaries)+os.pathsep+os.environ['PATH'],
                        KUI_BUNDLE_MAKE_EVENTS=str(self.log))
        for name in ('ff.c', 'ffunicode.c', 'ffconf.h'):
            p = directory / '.deps/fatfs/source' / name
            p.parent.mkdir(parents=True, exist_ok=True); p.write_text('')
        # Obtain the complete production object list without editing or
        # substituting the Makefile's dependency graph.
        database = subprocess.run(self.command('a', '-p', '-n'), cwd=directory,
            env=self.env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        line = re.search(r'^RUNTIME_OBJECTS\s*:=\s*(.*)$', database.stdout, re.M)
        if not line:
            raise AssertionError('Production runtime object list missing: '+database.stderr)
        for name in line.group(1).split():
            p = directory / name; p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b'UNRELATED-OBJECT\0')
        for name in ('splash_pixels.inc', 'boot_splash_pixels.inc', 'startup_ogg.inc'):
            p = directory / 'build' / name; p.write_text('')
        self.write_bundle('a', b'COMPONENT-A')
        self.write_bundle('b', b'COMPONENT-B')

    def command(self, bundle, *extra):
        selected = str(self.root / bundle) if bundle else ''
        return ['make', '--no-print-directory', '-s', '-f', str(self.makefile),
                'build/kui-runtime.elf', 'BUILD_ID=0123456789ab',
                'KUI_SMASH_LAUNCH_TIMING=1', 'RUNTIME_MODULE_BUNDLE='+selected, *extra]

    def write_bundle(self, name, body, *, older=False, assembly_suffix=''):
        directory = self.root / name; directory.mkdir(exist_ok=True)
        (directory / 'bundle.bin').write_bytes(body)
        (directory / 'bundle.S').write_text('.incbin "'+str(directory / 'bundle.bin')+'"\n'+assembly_suffix)
        if older:
            stamp = (self.root / 'build/dc/runtime-module-bundle.o').stat().st_mtime-100.0
            for p in directory.iterdir(): os.utime(p, (stamp, stamp))

    def run(self, bundle):
        result = subprocess.run(self.command(bundle), cwd=self.root, env=self.env,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            raise AssertionError('Actual production Makefile failed:\n'+result.stdout)
        return (self.root / 'build/kui-runtime.elf').read_bytes()

    def compilations(self):
        events = [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []
        return sum(e.get('bundle_compile', False) for e in events)


def audit(makefile, *, expect_old_failure=False):
    with tempfile.TemporaryDirectory(prefix='kui-production-bundle-make-') as temporary:
        fixture = Fixture(Path(temporary), makefile)
        initial = fixture.run('a')
        assert initial == b'GUI-LINK\0BUNDLE-OBJECT\0COMPONENT-A'
        first_count = fixture.compilations()
        # Both inputs at the newly chosen path predate the current object.
        fixture.write_bundle('b', b'COMPONENT-B', older=True)
        selected_b = fixture.run('b')
        if expect_old_failure:
            assert selected_b == initial, 'Old Makefile must reproduce the stale selected object'
            assert fixture.compilations() == first_count
        else:
            assert selected_b == b'GUI-LINK\0BUNDLE-OBJECT\0COMPONENT-B'
            assert fixture.compilations() == first_count+1
        # Reusing the same selected bundle must not rebuild its object.
        count = fixture.compilations()
        assert fixture.run('b') == selected_b
        assert fixture.compilations() == count
        # Content changes cannot hide behind restored/older timestamps.
        fixture.write_bundle('b', b'COMPONENT-B-UPDATED', older=True)
        content_b = fixture.run('b')
        if expect_old_failure:
            assert content_b == initial and fixture.compilations() == count
        else:
            assert content_b == b'GUI-LINK\0BUNDLE-OBJECT\0COMPONENT-B-UPDATED'
            assert fixture.compilations() == count+1
        count = fixture.compilations()
        fixture.write_bundle('b', b'COMPONENT-B-UPDATED', older=True, assembly_suffix='# changed assembly input\n')
        assert fixture.run('b') == content_b
        assert fixture.compilations() == count+(0 if expect_old_failure else 1)
        # Keep the production force-build-id edges: Off already relinks the
        # old GUI correctly. This is preservation, not a claimed old bug.
        assert fixture.run('') == b'GUI-LINK\0NO-BUNDLE'
        assert fixture.run('b') == content_b
        return dict(status='OLD_DEFECT_REPRODUCED' if expect_old_failure else 'PASS',
                    production_makefile_sha256=sha(makefile),
                    older_selected_path=True, older_changed_binary=True,
                    older_changed_assembly=True, unchanged_object_reused=True,
                    on_off_on_preserved=True, force_build_id_edges_retained=True,
                    production_repository_files_mutated=False,
                    compiler_and_receipt_boundaries_authored=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--negative-makefile', type=Path)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    report = dict(schema='kui.runtime-module-bundle.make-inputs.v1', status='PASS',
                  current=audit((ROOT / 'Makefile.dc').read_bytes()))
    if args.negative_makefile:
        report['old_negative'] = audit(args.negative_makefile.read_bytes(), expect_old_failure=True)
    text = json.dumps(report, indent=2)+'\n'
    if args.json: args.json.write_text(text)
    print(text, end='')


if __name__ == '__main__': main()
