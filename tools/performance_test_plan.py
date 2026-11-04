#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate unmeasured console result forms from the package's real profiles.

The expected identity is copied from the built module manifest. Observed IDs,
counter values, clocks and timings remain empty. These are plans, not evidence.
"""
import argparse
import csv
import io
import json
from pathlib import Path
import re


SUFFIX = {'T1': 'd0', 'T2': 'd1', 'T3': 'd2', 'C1': 'e0', 'C2': 'e1',
          'N0': 'b2', 'N1': 'b0', 'N2': 'b1', 'N3': 'b4', 'S0': 'b5', 'S1': 'b3'}
NATIVE = {'N0', 'N1', 'N2', 'N3', 'S0', 'S1'}


def _module(profiles, name, image_format='raw'):
    info = profiles[name]
    modules = info.get('modules', [])
    if name.startswith('T'):
        modules = [item for item in modules
                   if item.get('member', '').endswith('-' + image_format + '.kui')]
    if len(modules) != 1:
        raise ValueError('Expected one module for ' + name + '/' + image_format)
    return modules[0]


def _validate(profiles):
    if not isinstance(profiles, dict):
        raise ValueError('profiles must be a mapping from profile name to module metadata')
    if set(SUFFIX) - set(profiles):
        raise ValueError('Missing required package profiles: ' +
                         ', '.join(sorted(set(SUFFIX) - set(profiles or {}))))
    prefixes = set()
    for name, suffix in SUFFIX.items():
        info = profiles[name]
        if not isinstance(info, dict) or not info.get('folder') or not info.get('modules'):
            raise ValueError('Missing folder/modules for ' + name)
        for module in info['modules']:
            build_id = module.get('build_id', '')
            if not re.fullmatch(r'[0-9a-fA-F]{12}', build_id) or build_id[-2:].lower() != suffix:
                raise ValueError('Wrong module build ID for ' + name)
            if not module.get('member'):
                raise ValueError('Missing module member for ' + name)
            prefixes.add(build_id[:10].lower())
        _module(profiles, name)
    if len(prefixes) != 1:
        raise ValueError('Package profiles do not share one source build prefix')


def _proof(counter):
    return {'counter': counter, 'observed': None, 'minimum': 1}


def _clock():
    return {'valid_samples': None, 'bad_calls': None,
            'bad_intervals': None, 'saturated': None}


def _metric(unit, source='report', clock=None):
    result = {'value': None, 'unit': unit, 'source': source}
    if clock:
        result['clock'] = clock
    return result


def make_cases(profiles):
    """Return 35 staged plans (34 if no cooked T1 module was built).

    Input is build.json's ``profiles`` mapping: name -> folder/modules list,
    with each module carrying ``member`` and its actual twelve-hex ``build_id``.
    """
    _validate(profiles)
    rows = []

    def add(game, scene, profile='DISC', vmu='off', image_format='raw', stage=1,
            optional=False, reference=None, contrast=None, capture='terminal'):
        slug = {'Toy Commander': 'toy', 'Armada': 'armada', 'Power Stone 2': 'ps2'}[game]
        case_id = '-'.join((slug, scene, profile.lower(), image_format, vmu, 'r1'))
        native = profile in NATIVE
        disc = profile == 'DISC'
        slot = 'A1' if vmu == 'physical' else 'A2' if vmu == 'on' else 'none'
        row = dict(schema_version=1, radix=16, case_id=case_id, stage=stage, optional=optional,
            game=game, revision='UNKNOWN', workload_id=slug + '-' + scene,
            scene=scene, start_marker='UNKNOWN', end_marker='UNKNOWN',
            save_seed='UNKNOWN', video_mode='UNKNOWN', repeat=1,
            media='disc' if disc else 'sd', storage='GD-ROM' if disc else 'SCI',
            format='disc' if disc else image_format,
            reader='GD-ROM' if disc else 'Standard' if profile.startswith('S') else
                   'New-standalone' if profile == 'N0' else 'New-general' if native else
                   'Toy-native-CDDA' if profile.startswith('T') else 'WinCE-background',
            profile=profile, expected_profile=None if disc else profile,
            build_id=None, expected_build_id=None, folder=None, module_member=None,
            prefetch='off', background='n/a' if disc else 'off' if profile.startswith(('T', 'S')) else 'on',
            vmu=vmu, vmu_slot=slot, cdda='off' if game == 'Power Stone 2' else 'on',
            capture_mode=capture, observation_only=capture == 'phone-unarmed',
            required_activity=[], engagement={}, clocks={}, metrics={},
            observations={'video': None, 'audio': None, 'input': None,
                          'save_before': None, 'save_after_reboot': None, 'notes': None})
        if not disc:
            info = profiles[profile]
            module = _module(profiles, profile, image_format)
            row.update(expected_build_id=module['build_id'], folder=info['folder'],
                       module_member=module['member'])
            row['required_activity'].append('loader')
            row['engagement']['loader'] = _proof(
                'Positive served DATA progress; use cumulative/prior evidence when the quiet window has zero demand'
                if native else 'DATA requests/completions or existing served-sector counter')
        if reference:
            row.update(reference_case=reference['case_id'], contrast=contrast)
        if scene.startswith('intro') or scene == 'movie':
            row['metrics']['scene_elapsed_ms'] = _metric('ms', 'phone')
            row['instructions'] = 'Do not skip. Fill the same visible first-frame and end-frame markers; record the full chosen movie.'
        elif scene == 'quiet':
            row['instructions'] = 'Same settled fight/checkpoint and simple input sequence; no new streaming is required in this quiet window.'
        elif scene == 'save-reboot':
            row['instructions'] = 'Load the same prepared test save; make one named change, save, power off, reboot and verify that changed value.'
            row['metrics']['save_ack_ms'] = _metric('ms', 'phone')
            row['required_activity'].append('save_readback')
            row['engagement']['save_readback'] = _proof('Enter 1 only after the named changed value is verified after a full cold reboot')
        else:
            row['instructions'] = 'Choose a reproducible later scene with actual CDDA source progress and demanded DATA.'
        if vmu == 'on':
            if not profiles[profile].get('virtual_vmu_supported') or profiles[profile].get('virtual_slot') != 'A2':
                raise ValueError(profile + ' does not admit the planned A2 VMU run')
            row['required_activity'].append('vmu')
            row['engagement']['vmu'] = _proof(
                'Positive VMU save/read/commit evidence on admitted A2; VMU menu On alone is insufficient')
        if profile in ('T2', 'T3'):
            row['required_activity'].append('candidate')
            row['engagement']['candidate'] = _proof(
                'VISITS STEPS WAITPOLLS YIELDS / YIELDS' if profile == 'T2' else
                'PROFILE CRC32SKIPS PLAYS SAT / CRC32SKIPS')
        if profile.startswith('T'):
            row['report_seconds'] = 40
            row['clocks']['phase0'] = _clock()
            row['metrics']['phase0_data_ticks_max'] = _metric('ticks', clock='phase0')
        elif profile.startswith('C'):
            row['report_seconds'] = 35
            if profile == 'C2':
                row['required_activity'].append('candidate')
                row['engagement']['candidate'] = _proof('VERSION MODE PROFILE-ID GD IRQ / GD; MODE must be 2')
                row['clocks']['cycles'] = _clock()
                row['metrics']['gd_cycles_max'] = _metric('cycles', clock='cycles')
        if native and capture == 'perf-quiet':
            if profile not in ('N1', 'N2'):
                raise ValueError('PERF capture is not supplied by ' + profile)
            row['report_seconds'] = 30
            row['required_activity'].append('capture')
            row['engagement']['capture'] = _proof('VMU FULL PREFETCH POLLS / POLLS, after arming in quiet gameplay or save')
            row['required_activity'].extend(('snapshot_begin', 'snapshot_end', 'sdk_route'))
            row['engagement']['snapshot_begin'] = _proof('SNAP BEGIN END SDK / BEGIN')
            row['engagement']['snapshot_end'] = _proof('SNAP BEGIN END SDK / END')
            row['engagement']['sdk_route'] = _proof('Admitted SDK/owner route plus matching module identity and positive prior served-demand evidence; SDK cookie alone is insufficient')
            if profile == 'N2':
                row['engagement']['candidate'] = _proof('FULL SUB / N; positive full-submit calls')
            else:
                row['engagement']['candidate'] = _proof('VMU FULL PREFETCH POLLS / POLLS; positive captured polls')
            for metric in ('split_submit', 'full_submit', 'fast_complete', 'full_complete'):
                row['clocks'][metric] = _clock()
                row['metrics'][metric + '_ticks_max'] = _metric('ticks', clock=metric)
            for metric in ('data_irq_blocks', 'data_call_blocks', 'vmu_read_delta', 'vmu_commit_delta'):
                row['metrics'][metric] = _metric('count')
            row['instructions'] += ' Arm L+R+B here, never in a movie. Record the bounded window and all four PERF pages.'
        elif native and capture == 'phone-unarmed':
            row['instructions'] += ' Leave PERF unarmed: B can skip movies. Record launch identity and phone footage; this row is observation-only.'
        elif native:
            row['report_seconds'] = 25
            row['instructions'] += ' Use only the ordinary report supplied by this profile; do not request unsupported PERF capture.'
        if scene == 'save-reboot' and vmu == 'on':
            row['required_activity'].append('save_commit')
            row['engagement']['save_commit'] = _proof('Positive durable commit/write acknowledgment for this named save, not an unrelated startup checkpoint')
        if scene == 'cdda-active':
            row['required_activity'].extend(('play', 'cdda'))
            row['engagement']['play'] = _proof('Accepted CDDA PLAY count')
            row['engagement']['cdda'] = _proof('Positive raw audio bytes/source progress; accepted PLAY alone is insufficient')
        rows.append(row)
        return row

    # Fixed disc references. A second Toy disc intro can be added after choosing
    # the actual second-scene markers; it is not silently mixed with intro1.
    disc_toy = add('Toy Commander', 'intro1', image_format='disc')
    disc_armada = add('Armada', 'intro', image_format='disc')
    disc_ps2_movie = add('Power Stone 2', 'movie', image_format='disc', stage=2)
    disc_ps2_quiet = add('Power Stone 2', 'quiet', image_format='disc', stage=2)
    disc_ps2_save = add('Power Stone 2', 'save-reboot', vmu='physical', image_format='disc', stage=3)
    toy = {}
    for scene in ('intro1', 'intro2'):
        control = add('Toy Commander', scene, 'T1', reference=disc_toy if scene == 'intro1' else None,
                      contrast='media')
        toy[scene] = control
        for profile in ('T2', 'T3'):
            add('Toy Commander', scene, profile, reference=control, contrast='profile')
    if any(module.get('member', '').endswith('-cooked.kui') for module in profiles['T1']['modules']):
        add('Toy Commander', 'intro1', 'T1', image_format='cooked', optional=True,
            reference=toy['intro1'], contrast='format')
    armada = add('Armada', 'intro', 'C1', reference=disc_armada, contrast='media')
    add('Armada', 'intro', 'C2', reference=armada, contrast='profile')
    movies = {}
    for profile in ('N0', 'N3', 'N1', 'N2', 'S0', 'S1'):
        movies[profile] = add('Power Stone 2', 'movie', profile,
            vmu='on' if profile == 'S1' else 'off', stage=2,
            reference=disc_ps2_movie, contrast='system', capture='phone-unarmed')
    quiet = {}
    for profile, vmu in (('N0', 'off'), ('N3', 'off'), ('N1', 'off'), ('N2', 'off'),
                         ('S0', 'off'), ('N3', 'on'), ('N1', 'on'), ('N2', 'on'), ('S1', 'on')):
        if profile == 'N0':
            reference, contrast = disc_ps2_quiet, 'media'
        elif profile in ('N3', 'S0') and vmu == 'off':
            reference, contrast = quiet['N0', 'off'], 'reader'
        elif profile == 'N1' and vmu == 'off':
            reference, contrast = quiet['N3', 'off'], 'profile'
        elif profile == 'N2':
            reference, contrast = quiet['N1', vmu], 'profile'
        elif profile == 'S1':
            reference, contrast = quiet['N1', 'on'], 'reader'
        else:
            reference, contrast = quiet[profile, 'off'], 'vmu'
        quiet[profile, vmu] = add('Power Stone 2', 'quiet', profile, vmu=vmu,
            stage=3 if vmu == 'on' else 2, reference=reference, contrast=contrast,
            capture='perf-quiet' if profile in ('N1', 'N2') else 'terminal')
    save_n1 = add('Power Stone 2', 'save-reboot', 'N1', vmu='on', stage=3,
        reference=disc_ps2_save, contrast='system', capture='perf-quiet')
    add('Power Stone 2', 'save-reboot', 'N2', vmu='on', stage=3,
        reference=save_n1, contrast='profile', capture='perf-quiet')
    add('Power Stone 2', 'save-reboot', 'S1', vmu='on', stage=3,
        reference=save_n1, contrast='reader')
    add('Toy Commander', 'cdda-active', 'T1', stage=4, optional=True)
    cdda_armada = add('Armada', 'cdda-active', 'C1', stage=4, optional=True)
    add('Armada', 'cdda-active', 'C2', stage=4, optional=True,
        reference=cdda_armada, contrast='profile')
    return sorted(rows, key=lambda row: row['stage'])


def _flatten(value, prefix='', result=None):
    result = {} if result is None else result
    for key, item in value.items():
        name = prefix + key
        if isinstance(item, dict):
            _flatten(item, name + '.', result)
        elif isinstance(item, list):
            result[name] = ';'.join(item)
        elif isinstance(item, bool):
            result[name] = 'true' if item else 'false'
        else:
            result[name] = '' if item is None else item
    return result


def csv_bytes(rows):
    """Return UTF-8 CSV, preserving null observations as genuinely empty cells."""
    flat = [_flatten(row) for row in rows]
    headings = list(dict.fromkeys(key for row in flat for key in row))
    output = io.StringIO(newline='')
    writer = csv.DictWriter(output, headings)
    writer.writeheader()
    writer.writerows(flat)
    return output.getvalue().encode('utf-8')


def jsonl_bytes(rows):
    return ''.join(json.dumps(row, sort_keys=True) + '\n' for row in rows).encode('utf-8')


def case_guide(rows):
    """Return a compact Markdown index for the package, not a result summary."""
    lines = ['# Planned console cases', '',
        'These rows are unmeasured. Observed IDs, counters and times are blank.',
        'Fill UNKNOWN revision, scene markers, save seed and display mode consistently within each pair.',
        'Start at stage 1; optional rows and later stages answer specific follow-up questions.', '',
        '| Case | Stage | Profile / expected ID | VMU | Capture | Reference |',
        '| --- | --- | --- | --- | --- | --- |']
    for row in rows:
        lines.append('| {}{} | {} | {} / {} | {} {} | {} | {} |'.format(
            row['case_id'], ' (optional)' if row['optional'] else '', row['stage'],
            row['profile'], row['expected_build_id'] or 'original disc', row['vmu'],
            row['vmu_slot'], row['capture_mode'], row.get('reference_case', 'none')))
    lines.extend(('', 'Native movies: PERF unarmed, phone observations only; no L+R+B in a movie.',
        'N1/N2 quiet/save: L+R+B at the fixed settled checkpoint, then record all four pages for 30 seconds.',
        'Toy: all five pages for 40 seconds. Armada: all five pages for 35 seconds.',
        'N0/N3/S0/S1: use the ordinary supplied report, not unsupported PERF capture.',
        'N0 SD0 = DMA, PIO, CMD18 starts, kept; SD4 = overrun, CRC, token, foreign;',
        'N0 SD8 = repaired, receive-ahead, deferred. These are existing SCI counters, not CPU times.',
        'Quiet-window DATA can be zero. Use positive prior/cumulative served demand and real route admission;',
        'an SDK cookie or menu selection alone is not evidence that the proposed path served a request.',
        'S0 is Standard + VMU Off. S1 is Standard + VMU On A2. N1/N2/N3 On target A2 only.',
        'Keep the same game revision, image, display, logical save seed and scene markers within every pair.',
        'A profile contrast retains its real candidate gate; no observed value is supplied by this plan.', ''))
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path, help='package build.json')
    parser.add_argument('output', type=Path, help='new output directory')
    args = parser.parse_args(argv)
    rows = make_cases(json.loads(args.manifest.read_text())['profiles'])
    args.output.mkdir(parents=True, exist_ok=True)
    for name, data in (('performance-results-template.csv', csv_bytes(rows)),
                       ('performance-results-template.jsonl', jsonl_bytes(rows)),
                       ('performance-case-guide.md', case_guide(rows).encode('utf-8'))):
        (args.output / name).write_bytes(data)
    print('Wrote ' + str(len(rows)) + ' unmeasured cases')


if __name__ == '__main__':
    main()
