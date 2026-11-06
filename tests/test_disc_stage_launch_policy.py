#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind normal physical-disc launch to the shared no-photo-hold policy.

Source controls prove the old unconditional pause is rejected. Optional actual
ELF checks retain the independent CPU/handoff audit and require the normal
linked stage to contain no pause implementation. No console time is inferred.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_disc_handoff import audit_elf as handoff_audit
from check_retail_calls import read_elf
from check_loader_layout import EH, SH, SYM, region
from test_global_launch_performance_policy import effective_flags, preprocess


def require(value, message):
    if not value:
        raise ValueError(message)


def source_contract(source, frames):
    defines, _ = effective_flags(('disc-physical', 'Makefile.disc-native', 'FLAGS',
                                  ('KUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES=' + str(frames),), ()))
    compiled = preprocess(source, defines)
    body = compiled.split('void kui_disc_stage_main(void) {', 1)[1].split(
        'void kui_disc_stage_returned(void)', 1)[0]
    calls = re.findall(r'\bretail_display_pause\(([^)]*)\)', body)
    require(calls == ([str(frames)] if frames else []),
            'Normal physical-disc launch acquired an unrequested timed hold')
    require(body.count('kui_disc_stage_enter();') == 1,
            'Physical-disc launch lost its original bootstrap handoff')
    require('kui_disc_launch_validate(payload,plan.payload_bytes,&plan)' in ''.join(body.split()),
            'Physical-disc launch lost complete source packet validation')
    return dict(photo_hold_frames=frames, pause_calls=len(calls))


def linked_contract(data, frames=0):
    handoff = handoff_audit(data)
    _, symbols = read_elf(data)
    # The handoff reader deliberately returns executable symbols only. This
    # policy witness is absolute, so read its real ELF metadata separately.
    header = EH.unpack_from(data)
    sections = [SH.unpack(region(data, header[6] + i * SH.size, SH.size, 'section'))
                for i in range(header[12])]
    marker = []
    for section in sections:
        if section[1] != 2:
            continue
        names = region(data, sections[section[6]][4], sections[section[6]][5], 'symbol names')
        for offset in range(section[4], section[4] + section[5], SYM.size):
            name, value, size, info, other, owner = SYM.unpack(region(data, offset, SYM.size, 'symbol'))
            end = names.find(b'\0', name)
            require(0 <= name < len(names) and end >= name, 'Invalid linked policy symbol')
            if names[name:end] == b'__kui_disc_handoff_pause_frames':
                marker.append((value, size, info, other, owner))
    require(marker == [(frames, 0, 0x10, 0, 0xfff1)],
            'Physical-disc linked stage has missing or wrong photo-hold policy')
    pause = [s for s in symbols if s.name == '_retail_display_pause']
    require(len(pause) == (1 if frames else 0),
            'Physical-disc linked stage retains an unrequested timed hold')
    return dict(stage_sha256=hashlib.sha256(data).hexdigest(), photo_hold_frames=frames,
                linked_pause_implementations=len(pause), original_handoff=handoff)


def audit(stage=None, expected_frames=0):
    source = (ROOT / 'src/loader/disc_stage.c').read_text()
    defaults, _ = effective_flags(('disc-physical', 'Makefile.disc-native', 'FLAGS', (), ()))
    require([d for d in defaults if d.startswith('-DKUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES=')] ==
            ['-DKUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES=0'],
            'Normal physical-disc recipe enabled a timed photo hold')
    normal, diagnostic = source_contract(source, 0), source_contract(source, 30)
    guarded = ('#if KUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES\n'
               '    retail_display_pause(KUI_RETAIL_NATIVE_HANDOFF_PAUSE_FRAMES);\n#endif')
    require(source.count(guarded) == 1, 'Missing unique physical-disc hold policy seam')
    old = source.replace(guarded, '    retail_display_pause(30u);')
    try:
        source_contract(old, 0)
    except ValueError:
        pass
    else:
        raise ValueError('Old unconditional physical-disc hold was accepted')
    report = dict(schema='kui.disc-stage-launch-policy.v1', status='PASS',
                  normal=normal, explicit_photo_diagnostic=diagnostic,
                  old_unconditional_hold_refused=True, hardware_timing_measured=False)
    if stage is not None:
        data = Path(stage).read_bytes()
        report['linked'] = linked_contract(data, expected_frames)
        # A changed declared policy must not pass against the same actual ELF.
        try:
            linked_contract(data, 30 if expected_frames == 0 else 0)
        except ValueError:
            report['wrong_linked_policy_refused'] = True
        else:
            raise ValueError('Wrong linked photo-hold policy was accepted')
    report['source_sha256'] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
        for name in ('src/loader/disc_stage.c', 'Makefile.disc-native',
                     'tests/test_disc_stage_launch_policy.py')}
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--expected-frames', type=int, default=0)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = audit(args.stage, args.expected_frames)
    output = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(output)
    print(output, end='')
