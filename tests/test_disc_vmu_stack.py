#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse missing, enlarged, recursive and unbound cross-ELF stack graphs."""
from pathlib import Path
import re
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from check_disc_vmu_stack import check


def main():
    if len(sys.argv) != 2:
        raise SystemExit('Usage: test_disc_vmu_stack.py <build-directory>')
    original = Path(sys.argv[1]).resolve()
    report = check(original)
    cases = 0
    with tempfile.TemporaryDirectory(prefix='disc-vmu-stack-') as temp:
        work = Path(temp)
        for path in [original/'native-worker.elf', original/'support/support.elf',
                     *original.glob('native/**/*.ci'), *original.glob('support/**/*.ci')]:
            destination = work/path.relative_to(original)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)

        def refuses(path, transform, expected):
            nonlocal cases
            text = path.read_text()
            changed = transform(text)
            assert text != changed
            path.write_text(changed)
            try:
                check(work)
            except ValueError as error:
                assert expected in str(error), str(error)
                cases += 1
            else:
                raise AssertionError('Corrupted stack evidence was admitted')
            finally:
                path.write_text(text)

        support = work/'support/src/loader/disc_vmu_storage.ci'
        refuses(support, lambda t: re.sub(
            r'(label: "kui_disc_vmu_storage_loan\\n[^"\n]*?\\n)(\d+)( bytes \(static\))',
            lambda m: m[1]+str(int(m[2])+4096)+m[3], t), 'needs')
        transport = work/'support/src/loader/disc_vmu_transport.ci'
        refuses(transport, lambda t: t.replace('0 bytes (static)', '0 bytes (dynamic)', 1), 'Dynamic stack')
        extra = ('edge: { sourcename: "kui_disc_vmu_storage_loan" '
                 'targetname: "__indirect_call" label: "unreviewed.c:1:1" }\n')
        refuses(support, lambda t: t+extra, 'Unexpected support callback')
        native = work/'native/src/loader/vmu_native_passive.ci'
        recursion = ('edge: { sourcename: "kui_vmu_native_passive_submit" '
                     'targetname: "kui_vmu_native_passive_submit" label: "vmu_native_passive.c:1:1" }\n')
        refuses(native, lambda t: t+recursion, 'Recursive private-stack path')
        lifetime = work/'native/src/core/vmu_native_lifetime.ci'
        refuses(lifetime, lambda t: t.replace(
            'targetname: "__indirect_call" label: "src/core/vmu_native_lifetime.c:',
            'targetname: "__indirect_call" label: "unreviewed.c:'), 'Unexpected worker callback')
    print(f'PASS physical-disc stack evidence mutations: {cases} refused; '
          f'original cross-ELF depth {report["worst_bytes"]}/4096 bytes')


if __name__ == '__main__':
    main()
