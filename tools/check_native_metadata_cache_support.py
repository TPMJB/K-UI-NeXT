#!/usr/bin/env python3
"""Reexecute focused metadata-cache support proofs; no hardware timing claims."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', type=Path, required=True)
    p.add_argument('--reader', type=Path, required=True)
    p.add_argument('--standard', type=Path, required=True)
    p.add_argument('--old-evidence', type=Path, required=True)
    p.add_argument('--startup', type=Path, required=True)
    p.add_argument('--objdump', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--split-controller', action='store_true',
                   help='Audit the NORMAL-only split-controller integration candidate')
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    sys.path[:0] = [str(a.repo/'tools'), str(a.repo/'tests')]
    from check_retail_instructions import audit
    from check_vmu_native_sdk_coherence import audit as sdk_audit
    from check_vmu_native_worker_gateway import read_elf, symbol_bytes, P2
    from check_loader_layout import inspect_elf
    from check_native_isolation_preservation import check_reader, check_standard, build_label
    from check_vmu_native_persistent import check_stacks, linked_stack_rows, _function_base, CachedGateway
    from check_retail_stack import load, base
    import check_vmu_native_background as background
    from check_vmu_native_readahead import private_stack
    from check_vmu_native_map_refusal import check as map_refusal
    from check_vmu_native_readonly import relocated

    def save(name, data):
        (a.output/name).write_text(json.dumps(data, indent=2)+'\n')

    current = build_label(a.reader)
    require(current == build_label(a.standard), 'reader and Standard build labels differ')
    save('reader-component-comparison.json', check_reader(a.reader, current))
    standard_comparison = check_standard(a.standard, current)
    save('standard-control-comparison.json', standard_comparison)
    instructions = {}
    for variant, directory in [('reader', a.reader), ('standard', a.standard)]:
        counts = {}
        for name in ('resident-sci', 'native-worker', 'stage', 'entry'):
            text = subprocess.check_output([a.objdump, '-d', str(directory/(name+'.elf'))], text=True)
            counts[name] = audit(name, text)
        instructions[variant] = dict(instructions_checked=counts, no_illegal_delay_slots_or_fpu_use=True)
        save('sdk-'+variant+'.json', sdk_audit(read_elf(directory/'native-worker.elf'),
                                             True, {'evolution1': a.startup}))
    save('instruction-audit.json', instructions)

    def reference(name, digest):
        data = (a.old_evidence/name).read_bytes()
        require(sha(data) == digest, 'old receipt changed: '+name)
        return json.loads(data)

    old_reader = reference('stack-reader.json', '54b4f5cc93a5b7e90b056ca6ea4e855fa4200a13379e5c4eacae7a73110f1a69')
    old_standard = reference('stack-standard.json', 'fd0cd98df0726056e25068dc4752e304389bb0070c2368c215869ff773f65b77')

    def su_rows(directory):
        worker = read_elf(directory/'native-worker.elf')
        resident = inspect_elf((directory/'resident-sci.elf').read_bytes(), 0x8c004000,
                              0x8c007ba0 if directory == a.reader else 0x8c007800)
        high_reports = sorted((directory/'native').rglob('*.su'))
        low_reports = sorted((directory/'resident/lto').glob('*.ltrans*.su'))
        high = linked_stack_rows(high_reports, worker[4])
        low = linked_stack_rows(low_reports, resident['symbols'])
        return high, low, dict(emitted_su_sha256={str(x.relative_to(directory)): sha(x.read_bytes())
                    for x in low_reports+high_reports},
                    retained_worker_frames=[dict(function=n, bytes=s) for n, s in high],
                    retained_low_frames=[dict(function=n, bytes=s) for n, s in low])

    # Standard is byte-for-byte the established implementation once the exact
    # metadata fields above are normalized. Its established graph had GCC LTO
    # truncation; do not pretend it can be reexecuted from that partial graph.
    high, low, bindings = su_rows(a.standard)
    require(sum(x[1] for x in high) == old_standard['worker_c_frame_sum'] and
            sum(x[1] for x in low) == old_standard['resident_callback_c_frame_sum'],
            'Standard retained compiler stack frames changed')
    lease = [s for n, s in low if _function_base(n) == 'kui_vmu_observe_sd_lease']
    require(lease == [old_standard['lease_only_frame_bytes']], 'Standard lease frame changed')
    standard = copy.deepcopy(old_standard)
    standard['provenance'] = dict(
        method='inherited established linked callgraph via strict whole-file normalized binary equality',
        current_callgraph_reexecuted=False,
        reason='The retained Standard LTO callgraph is incomplete; identical complete executable bytes bind the established bound.',
        prior_stack_receipt=dict(name='stack-standard.json', sha256=sha((a.old_evidence/'stack-standard.json').read_bytes())),
        comparison_receipt='standard-control-comparison.json',
        all_eight_components_strict_normalized_sha256_equal=True,
        current_retained_su_frame_sums_equal=True,
        frame_and_elf_bindings=bindings,
        current_artifacts={n:sha((a.standard/n).read_bytes()) for n in ('native-worker.elf', 'resident-sci.elf')})
    save('stack-standard.json', standard)

    # Rewalk actual current low/aux graphs and the linked division helper.
    low_image = read_elf(a.reader/'resident-sci.elf')
    reports = sorted(a.reader.glob('resident-sci.elf.ltrans*.ci'))
    if not reports:
        reports = sorted((a.reader/'resident/lto').glob('*.ci'))
    if '__kui_native_loader_timeline_resident' in low_image[4]:
        reports += [a.reader/'native/src/loader/vmu_native_passive.ci',
                    a.reader/'native/src/core/native_loader_timeline.ci',
                    a.reader/'native/src/core/vmu_native_profile.ci',
                    a.reader/'native/src/loader/minic.ci']
    frames, calls = load(reports, qualified=True)
    roots = dict(background.ROOTS, kui_retail_sdk_sci_dispatch=0)
    paths, callbacks, escapes = background.stack_paths(frames, calls, roots,
                                                      {('deliver','retail_async.c'):20})
    dis = subprocess.check_output([a.objdump, '-d', str(a.reader/'resident-sci.elf')], text=True)
    helper_at, helper_bytes = background.typed(low_image, '___udivsi3', 2)
    library = background.library_cfg(background.rows(dis), helper_at, helper_bytes)
    worst = max(v['bytes'] for v in paths.values())
    total = worst+library['stack_bytes']+background.EXTRA_MARGIN+background.GUARD+background.TOP_GAP
    require(total <= 352, 'current low private stack exceeds352 bytes')
    low_stack = dict(available_bytes=352, worst_total_bytes=total,
                     worst_c_and_entry_bytes=worst, linked_library=library,
                     additional_margin_bytes=background.EXTRA_MARGIN, guard_bytes=background.GUARD,
                     top_gap_bytes=background.TOP_GAP, paths=paths,
                     callback_sites=len(callbacks), nonreturning_boundaries=[list(x) for x in escapes])
    read_ahead = copy.deepcopy(old_reader['targeted_boundary_proof']['read_ahead'])
    # The current low/aux complete binary preservation was established above.
    # Preserve the exact ABI boundary proof and rerun its actual stack graphs.
    read_ahead['private_stack'] = private_stack(a.reader, low_image, self_test=True)
    read_ahead['preserved_boundary_provenance'] = dict(
        receipt='reader-component-comparison.json',
        source_receipt_sha256=sha((a.old_evidence/'stack-reader.json').read_bytes()),
        strict_low_and_auxiliary_binary_equality_after_label_normalization=True)
    refusal = map_refusal(a.reader/'resident-sci.elf', self_test=True, objdump=a.objdump)
    worker = read_elf(a.reader/'native-worker.elf')
    binary = (a.reader/'native-worker.bin').read_bytes()
    wire = (a.reader/'native-worker.reloc').read_bytes()
    # This explicit NORMAL-only integration build removes PROFILE capture
    # call roots. The independent stage/arm guards bind exact E1/NORMAL;
    # stack analysis below walks actual retained C and all authored callbacks.
    integration = 'split_controller' if a.split_controller else 'metadata_cache'
    require(worker[4].get('__kui_native_'+integration+'_isolation_worker') == (1,0,0,0xfff1) and
            read_elf(a.reader/'stage.elf')[4].get('__kui_native_'+integration+'_isolation_stage') == (1,0,0,0xfff1),
            'requires the exact paired integration markers: '+integration)
    if a.split_controller:
        require('__kui_native_metadata_cache_isolation_worker' not in worker[4] and
                '_native_metadata_publish' not in worker[4],
                'split candidate unexpectedly enables metadata-cache publication')
    require('_diagnostic_complete' not in worker[4] and '_diagnostic_pair_begin' not in worker[4],
            'unsupported PROFILE capture roots remain linked')
    closure_provenance = dict(method='PROFILE capture compiled out in explicit NORMAL-only integration control',
        paired_integration_markers=integration, absent_profile_capture_roots=True,
        actual_retained_callgraphs_walked=True)
    if not a.split_controller:
        closure_provenance['paired_metadata_cache_markers'] = True
    terminal_cases = 0
    for destination in (P2, 0xac38a880, 0xac700000):
        blob = relocated(binary, wire, destination)
        mapped = {name: record[0]+destination-P2 for name, record in worker[4].items()}
        for seed in range(160):
            for mask in range(16):
                CachedGateway(seed, mask, destination, blob, mapped, 'terminal').run().check()
                terminal_cases += 1
    reader = check_stacks(a.reader, worker, background_audit={'private_stack':low_stack},
                          terminal_proof=refusal, terminal_gateway_cases=terminal_cases,
                          read_ahead_proof=read_ahead)

    reader['normal_only_integration'] = dict(
        variant=integration,
        metadata_cache_publication_function_retained='_native_metadata_publish' in worker[4],
        profile_capture_roots_absent=True,
        actual_retained_worker_roots_walked=True,
        bss_stack_margin=worker[4]['__vmu_native_worker_stack_bottom'][0]-worker[4]['__vmu_native_worker_bss_end'][0])
    if a.split_controller:
        require('kui_vmu_native_passive_split_submit' in reader['callgraph']['paths'],
                'actual split submit callgraph root was not walked')
        reader['normal_only_integration']['actual_split_submit_root_walked'] = True
    reader['targeted_boundary_proof'] = dict(read_ahead=read_ahead,
        low_stack=low_stack, current_terminal_gateway_cases=terminal_cases,
        profile_capture=closure_provenance)
    save('stack-reader.json', reader)
    print(json.dumps(dict(build=current, instruction_audit=instructions,
                          reader_stack_bytes=reader['conservative_worker_bytes'],
                          standard_stack_bytes=standard['conservative_worker_bytes'],
                          low_stack_bytes=low_stack['worst_total_bytes'],
                          metadata_cache=reader['normal_only_integration']), indent=2))


if __name__ == '__main__':
    main()
