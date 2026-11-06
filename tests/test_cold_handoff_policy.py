#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify shared native/CE photo holds without changing handoff contracts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_vmu_native_worker_gateway import read_elf
from test_global_launch_performance_policy import PROFILES, effective_flags, preprocess


def require(value, message):
    if not value:
        raise ValueError(message)


def source_contract(source, defines, frames=None):
    begin = source.index('#ifndef KUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES')
    end = source.index('extern void kui_retail_stage_sync(void);', begin)
    policy = source[begin:end]
    guard = '#if STEP_PAUSE_FRAMES\n    retail_display_pause(STEP_PAUSE_FRAMES);\n#endif'
    require(source.count(guard) == 2, 'Cold launch lost its two bounded photo-hold seams')
    probe = '#include "kui/retail_loader_layout.h"\n' + policy + '\n' + \
        'unsigned hold_frames=STEP_PAUSE_FRAMES;\nunsigned patch_bytes=ENTRY_PATCH_BYTES;\n' + \
        'void first_handoff(void){\n' + guard + '\n}\nvoid second_handoff(void){\n' + guard + '\n}\n'
    selected = list(defines)
    if frames is not None:
        selected = [d for d in selected if not d.startswith('-DKUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES=')]
        selected.append('-DKUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES=' + str(frames))
    expected = 0 if frames is None else frames
    compiled = preprocess(probe, selected)
    match = re.search(r'unsigned hold_frames\s*=\s*(\d+)[uUlL]*\s*;', compiled)
    patch = re.search(r'unsigned patch_bytes\s*=\s*(\d+)[uUlL]*\s*;', compiled)
    require(match and int(match.group(1)) == expected, 'Cold launch acquired an unrequested timed hold')
    ce = any(d in ('-DKUI_RETAIL_CE', '-DKUI_RETAIL_CE=1') for d in selected)
    require(patch and int(patch.group(1)) == (64 if ce else 128),
            'Photo policy changed the CE64/native128 entry-patch contract')
    calls = re.findall(r'retail_display_pause\((\d+)[uUlL]*\)', compiled)
    require(calls == ([str(expected)] * 2 if expected else []), 'Cold launch has an unexpected photo hold')
    return dict(photo_hold_frames=expected, entry_patch_bytes=int(patch.group(1)), pause_calls=len(calls))


def linked_contract(path, frames=0):
    image = read_elf(path)
    symbols = image[4]
    require(symbols.get('__kui_retail_handoff_pause_frames') == (frames, 0, 0, 0xfff1),
            'Linked cold stage has missing or wrong shared photo-hold policy')
    pause = [name for name in symbols if name.startswith('_retail_display_pause')]
    require(bool(pause) == bool(frames), 'Linked cold stage retains an unrequested timed hold')
    require(all(symbols.get(name, (0, 0, 0, 0))[2] == 2 for name in
                ('_kui_retail_stage_main', '_kui_retail_stage_relay')),
            'Linked cold stage lost a source-owned launch/relay function')
    return dict(stage_sha256=hashlib.sha256(image[0]).hexdigest(), photo_hold_frames=frames,
                linked_pause_implementations=len(pause), console_timing_measured=False)


def audit(stages=(), frames=0):
    source = (ROOT / 'src/loader/retail_stage.c').read_text()
    rows = []
    ce_defines = None
    for profile in PROFILES:
        if profile[0].startswith('disc-'):
            continue
        defines, _ = effective_flags(profile)
        # An effective ordinary recipe may not silently opt in to a photo hold.
        actual = [d for d in defines if d.startswith('-DKUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES=')]
        require(not actual or actual == ['-DKUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES=0'],
                'Normal recipe enabled a timed photo hold: ' + profile[0])
        normal, photo = source_contract(source, defines), source_contract(source, defines, 30)
        rows.append(dict(profile=profile[0], normal=normal, explicit_photo=photo))
        if profile[0] == 'ordinary-ce':
            ce_defines = defines
    require(ce_defines is not None, 'Ordinary CE effective recipe was not audited')
    seam = '#ifdef KUI_RETAIL_CE\n/* Place only the trampoline'
    require(source.count(seam) == 1, 'Missing unique CE policy seam')
    old = source.replace(seam, '#ifdef KUI_RETAIL_CE\n#undef STEP_PAUSE_FRAMES\n'
                         '#define STEP_PAUSE_FRAMES 120u\n/* Place only the trampoline')
    try:
        source_contract(old, ce_defines)
    except ValueError:
        pass
    else:
        raise ValueError('Old unconditional CE120-frame holds were accepted')
    changed_default = source.replace('#define KUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES 0\n',
                                     '#define KUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES 30\n')
    require(changed_default != source, 'Missing shared normal no-hold default')
    try:
        source_contract(changed_default, ce_defines)
    except ValueError:
        pass
    else:
        raise ValueError('Changed shared photo-hold default was accepted')
    return dict(schema='kui.cold-handoff-policy.v1', status='PASS', profiles=rows,
                old_CE_120_frame_control_refused=True,
                changed_shared_default_refused=True,
                linked={str(path): linked_contract(path, frames) for path in stages},
                source_sha256={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                    for name in ('src/loader/retail_stage.c', 'tests/test_cold_handoff_policy.py')},
                console_timing_measured=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, action='append', default=[])
    parser.add_argument('--expected-frames', type=int, default=0)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = audit(args.stage, args.expected_frames)
    output = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(output)
    print(output, end='')
