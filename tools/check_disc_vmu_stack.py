#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Walk the physical-disc worker/support cross-ELF private-stack graph.

The serial borrower runs on the worker's stack. Its frame and callback into
the worker must be counted in the same path, even though separately linked.
Unknown callback sites, dynamic frames, recursion and missing frames refuse.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_disc_vmu_handoff import Image
from check_loader_layout import SYM
from check_retail_stack import load, NODE, base

ROOTS = ('kui_vmu_native_passive_arm', 'kui_vmu_native_passive_submit',
         'kui_vmu_native_passive_complete', 'kui_vmu_native_readonly_release_check',
         'kui_vmu_native_passive_report', 'kui_vmu_native_runtime_fault',
         'kui_vmu_native_trace_game', 'kui_vmu_native_passive_split_submit',
         'persistent_lease')
MARGIN = 336  #80B assembly/libgcc allowance,256B untouched guard border.


def linked_functions(image):
    names = set()
    for section in image.sections:
        if section[1] != 2:
            continue
        strings = image.sections[section[6]]
        table = image.data[strings[4]:strings[4]+strings[5]]
        for at in range(section[4], section[4]+section[5], SYM.size):
            n, _, _, info, _, owner = SYM.unpack_from(image.data, at)
            if info & 15 == 2 and owner and n:
                end = table.find(b'\0', n)
                if end < 0:
                    raise ValueError('Malformed function name')
                name = table[n:end].decode('ascii')
                names.add(base(name[1:] if name.startswith('_') else name))
    return names


def check(directory):
    directory = Path(directory)
    report_sets = {'worker': sorted((directory/'native').rglob('*.ci')),
                   'support': sorted((directory/'support').rglob('*.ci'))}
    images = {'worker': Image((directory/'native-worker.elf').read_bytes()),
              'support': Image((directory/'support/support.elf').read_bytes())}
    frames, calls, origins = {}, {}, {}
    for domain, reports in report_sets.items():
        if not reports:
            raise ValueError('Missing emitted graph: '+domain)
        f, c = load(reports, qualified=True, scope=domain)
        live = linked_functions(images[domain])
        frames.update({name: value for name, value in f.items() if base(name) in live})
        calls.update(c)
        for report in reports:
            for title, label in NODE.findall(report.read_text()):
                pieces = label.split(r'\n')
                if len(pieces) >= 2:
                    origins[domain+':'+title] = Path(pieces[1].rsplit(':', 2)[0]).name

    # Audited read-only assembly leaf; no caller stack use or C calls.
    image = images['worker']
    trace = image.symbol('_kui_vmu_native_trace_irq')
    if image.read(trace, 14) != bytes.fromhex('420002243200011422000b000214'):
        raise ValueError('Exception-state leaf changed from reviewed zero-stack body')
    frames['worker:kui_vmu_native_trace_irq'] = 0
    calls['worker:kui_vmu_native_trace_irq'] = []
    by_base = {}
    for name in frames:
        by_base.setdefault((name.split(':', 1)[0], base(name)), []).append(name)

    def resolve(domain, function, owner=None):
        exact = domain+':'+function
        candidates = [exact] if exact in frames else by_base.get((domain, base(function)), [])
        if owner:
            candidates = [name for name in candidates if origins.get(name) == owner]
        if not candidates:
            raise ValueError('Missing emitted frame: '+domain+':'+function)
        return candidates

    def worker(*names):
        return [node for name in names for node in resolve('worker', name)]

    def support(*names, owner=None):
        return [node for name in names for node in resolve('support', name, owner)]

    def bus(*names):
        return support(*names, owner='sci_sd_bus.c')

    def indirect(node, label):
        domain, function = node.split(':', 1)[0], base(node)
        source = Path(label.split(':', 1)[0]).name
        if domain == 'support':
            if (function, source) != ('kui_disc_vmu_storage_loan', 'disc_vmu_storage.c'):
                raise ValueError('Unexpected support callback: '+node+' at '+label)
            # Every indirect call belongs to one of the immutable bound ops
            # or the one exact worker transaction; no caller-selected target.
            return support('current_sr', 'stream_active', 'acquire', 'release', 'healthy', 'faulted',
                           owner='disc_vmu_transport.c')+worker('kui_vmu_native_persistent_sd_transaction')
        fixed = {
            'vmu_durable.c': ({'durable_card_execute'}, ('persistent_lease',)),
            'vmu_emulation.c': ({'kui_vmu_emulation_handle'}, ('durable_card_read', 'durable_card_commit')),
            'vmu_native_heap.c': ({'read_is', 'kui_vmu_native_heap_lease_valid',
                                  'kui_vmu_native_heap_lease_valid_with_release'}, ('read_heap',)),
            'vmu_native_layout.c': ({'kui_vmu_native_layout_bind_checked'}, ('read_value',)),
            # prepare_protected_ranges supplies this same immutable worker
            # read_heap callback. Include arena identity sampling even though
            # the physical-disc installer only admits allocator leases.
            'vmu_native_lifetime.c': ({'kui_vmu_native_lifetime_release_safe_ranges',
                                     'kui_vmu_native_lifetime_prepare'}, ('read_heap',)),
            'vmu_native_live.c': ({'kui_vmu_native_live_check'}, ('boundary_read',)),
            'vmu_native_runtime.c': ({'kui_vmu_native_runtime_check'}, ('boundary_read',)),
            'vmu_store.c': ({'write_sync', 'kui_vmu_store_open', 'kui_vmu_store_read', 'kui_vmu_store_sync'},
                            ('durable_card_sector_read', 'durable_card_sector_write', 'durable_card_sector_sync')),
            'vmu_readonly_overlay.c': ({'kui_vmu_readonly_overlay_prepare', 'kui_vmu_readonly_overlay_complete'},
                                       ('persistent_admit', 'persistent_handler')),
        }
        if source in fixed and function in fixed[source][0]:
            return worker(*fixed[source][1])
        if (function, source) == ('persistent_lease', 'vmu_native_passive.c'):
            return support('kui_disc_vmu_support_sd_lease')
        if source == 'sd_reader.c':
            targets = {'transfer': ('transfer',), 'ticks': ('ticks',), 'release': ('select_card',),
                       'read_payload': ('transfer_block',), 'kui_loader_sd_read': ('select_card',)}
            if function in targets:
                return bus(*targets[function])
        if source == 'vmu_backing.c' and function in ('byte', 'wait_byte', 'kui_vmu_sd_identity_read'):
            return bus('transfer', 'ticks', 'select_card')
        if source == 'sci_sd_storage.c':
            # Include the optional block callback conservatively even though
            # native durable writes set it NULL. No callback is waived.
            return bus('transfer', 'ticks', 'select_card', 'transfer_block')
        raise ValueError('Unexpected worker callback: '+node+' at '+label)

    memo, active, indirect_sites = {}, set(), set()

    def depth(node):
        if node in memo:
            return memo[node]
        if node in active:
            raise ValueError('Recursive private-stack path: '+node)
        if node not in frames:
            # Compiler integer helpers are covered by the80B ABI allowance.
            if base(node) in ('__udivsi3', '__sdivsi3', '__udivdi3', '__umoddi3', '__moddi3'):
                return 0, [node+'(library allowance)']
            domain, function = node.split(':', 1)
            return max((depth(n) for n in resolve(domain, function)), key=lambda result: result[0])
        active.add(node)
        best = (0, [])
        for target, label in calls.get(node, []):
            targets = indirect(node, label) if target == '__indirect_call' else [target]
            if target == '__indirect_call':
                indirect_sites.add((node, label, tuple(targets)))
            for target in targets:
                result = depth(target)
                if result[0] > best[0]:
                    best = result
        active.remove(node)
        memo[node] = frames[node]+best[0], [f'{node}({frames[node]})']+best[1]
        return memo[node]

    paths = {}
    for root in ROOTS:
        result = max((depth(n) for n in resolve('worker', root)), key=lambda row: row[0])
        total = result[0]+MARGIN
        if total > 4096:
            raise ValueError(f'Private stack path {root} needs{total} >4096 bytes')
        paths[root] = dict(c_frame_bytes=result[0], conservative_bytes=total, worst_path=result[1])
    if ('support:kui_disc_vmu_storage_loan' not in memo or
            'worker:kui_vmu_native_persistent_sd_transaction' not in memo):
        raise ValueError('No complete worker→support→worker SD path was counted')
    return dict(schema='kui.physical-disc-vmu.stack.v1', worker_stack_bytes=4096,
                margin_bytes=MARGIN, worst_bytes=max(p['conservative_bytes'] for p in paths.values()),
                paths=paths, source_qualified_calls=True, indirect_sites=len(indirect_sites),
                artifact_sha256={domain: hashlib.sha256(image.data).hexdigest() for domain, image in images.items()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    try:
        report = check(args.directory)
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, f'Physical-disc VMU stack audit failed: {error}\n')
    if args.json:
        args.json.write_text(json.dumps(report, indent=2)+'\n')
    print(f'PASS physical-disc cross-ELF private stack: {report["worst_bytes"]}/4096 bytes '
          f'including{MARGIN}B margin; {report["indirect_sites"]} finite callback sites')


if __name__ == '__main__':
    main()
