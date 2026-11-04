#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Review typed console results; never turn absent evidence into zero cost.

Input is JSON, JSONL, or CSV. CSV dotted headings become nested dictionaries;
e.g. metrics.elapsed_ms.value, engagement.loader.observed. Numeric console
strings require hex:/0x or an explicit row radix (10 or 16). No dependencies.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import sys


class ResultError(ValueError):
    pass


def number(value, radix=None):
    """Decode only deliberately typed values. Missing and uint32 max differ."""
    if value is None or (isinstance(value, str) and value.strip().lower() in
                         ('', '-', 'n/a', 'unavailable', 'unknown')):
        return None
    if isinstance(value, bool):
        raise ResultError('boolean is not a numeric report value')
    if isinstance(value, (int, float)):
        if value < 0 or value != value or value == float('inf'):
            raise ResultError('numeric values must be finite and nonnegative')
        return value
    if not isinstance(value, str):
        raise ResultError('expected a number, typed string, or null')
    value = value.strip()
    try:
        if value.lower().startswith(('hex:', '0x')):
            digits = value[4:] if value.lower().startswith('hex:') else value[2:]
            if not re.fullmatch(r'[0-9a-fA-F]+', digits):
                raise ValueError()
            return int(digits, 16)
        if value.lower().startswith('dec:'):
            digits = value[4:]
            if not re.fullmatch(r'\d+(?:\.\d+)?', digits):
                raise ValueError()
            return number(float(digits)) if '.' in digits else int(digits, 10)
        if radix in (10, 16):
            pattern = r'\d+' if radix == 10 else r'[0-9a-fA-F]+'
            if not re.fullmatch(pattern, value):
                raise ValueError()
            return int(value, radix)
    except ValueError as exc:
        raise ResultError('invalid typed number: ' + value) from exc
    raise ResultError('ambiguous number; use hex:, dec:, 0x, or row radix: ' + value)


def count(value, radix=None):
    result = number(value, radix)
    if result is not None and (not isinstance(result, int) or result > 0xffffffff):
        raise ResultError('report counters must be unsigned 32-bit integers')
    return result


def known(value):
    return isinstance(value, str) and value.strip().lower() not in (
        '', '-', 'n/a', 'unknown', 'pending', 'unavailable')


def expand_csv(row):
    out = {}
    for heading, value in row.items():
        if heading is None:
            raise ResultError('CSV row has more cells than headings')
        if value in (None, ''):
            continue
        target = out
        parts = heading.strip().split('.')
        for part in parts[:-1]:
            if part in target and not isinstance(target[part], dict):
                raise ResultError('conflicting CSV dotted headings: ' + heading)
            target = target.setdefault(part, {})
        if parts[-1] in target:
            raise ResultError('duplicate CSV heading: ' + heading)
        target[parts[-1]] = value
    for field in ('schema_version', 'repeat', 'radix'):
        if field in out:
            try:
                out[field] = int(out[field], 10)
            except ValueError as exc:
                raise ResultError(field + ' must be decimal') from exc
    if 'observation_only' in out:
        flag = out['observation_only'].strip().lower()
        if flag not in ('true', 'false'):
            raise ResultError('observation_only must be true or false')
        out['observation_only'] = flag == 'true'
    if isinstance(out.get('required_activity'), str):
        out['required_activity'] = [x.strip() for x in out['required_activity'].split(';') if x.strip()]
    return out


def read_rows(path):
    with path.open(encoding='utf-8-sig', newline='') as handle:
        if path.suffix.lower() == '.csv':
            return [expand_csv(row) for row in csv.DictReader(handle)]
        if path.suffix.lower() == '.json':
            data = json.load(handle)
            if isinstance(data, dict):
                data = data.get('results')
            if not isinstance(data, list):
                raise ResultError('JSON must be an array or {"results": [...]}')
            return data
        rows = []
        for line, text in enumerate(handle, 1):
            if not text.strip():
                continue
            try:
                rows.append(json.loads(text))
            except json.JSONDecodeError as exc:
                raise ResultError('JSONL line ' + str(line) + ': ' + str(exc)) from exc
        return rows


WORKLOAD = ('game', 'revision', 'workload_id', 'scene', 'start_marker',
            'end_marker', 'save_seed', 'video_mode', 'repeat')
SETTINGS = ('media', 'format', 'reader', 'profile', 'prefetch', 'background',
            'vmu', 'cdda', 'vmu_slot', 'storage')
CONTRASTS = {
    'profile': {'profile'},
    'vmu': {'vmu', 'vmu_slot'},
    'format': {'format'},
    'reader': {'reader', 'profile', 'background'},
    'media': {'media', 'format', 'reader', 'profile', 'background', 'storage'},
    # Explicit end-to-end reference; cannot assign its delta to one feature.
    'system': {'media', 'format', 'reader', 'profile', 'background', 'vmu', 'vmu_slot', 'storage'},
}


def clock_status(clock, radix=None):
    if not isinstance(clock, dict):
        return 'unavailable'
    samples = count(clock.get('valid_samples'), radix)
    keys = ('bad_calls', 'bad_intervals')
    if not any(key in clock for key in keys):
        keys = ('clock_epoch', 'unmatched_ends', 'nested_begins')
    invalid = [count(clock.get(key), radix) for key in keys]
    if samples is None or any(value is None for value in invalid):
        return 'unavailable'
    saturated = count(clock.get('saturated', 0), radix)
    if saturated is None:
        return 'unavailable (saturation flag missing)'
    if samples == 0:
        return 'unavailable (zero valid samples)'
    all_bad = [count(clock[key], radix) for key in
               ('bad_calls', 'bad_intervals', 'clock_epoch', 'unmatched_ends', 'nested_begins')
               if key in clock]
    if any(invalid) or any(all_bad) or saturated:
        return 'invalid'
    return 'valid'


def assess(row):
    if not isinstance(row, dict):
        raise ResultError('each result must be an object')
    result = {'case_id': row.get('case_id', '?'), 'issues': [], 'activity': {},
              'clocks': {}, 'metrics': {}, 'eligible': False}
    issues = result['issues']
    if type(row.get('observation_only', False)) is not bool:
        issues.append('observation_only must be boolean')
    elif row.get('observation_only'):
        issues.append('observation-only: capture unarmed; no formal performance comparison')
    radix = row.get('radix')
    if radix not in (None, 10, 16):
        issues.append('radix must be 10 or 16')
        radix = None
    if row.get('schema_version') != 1:
        issues.append('schema_version must be 1')
    for key in ('case_id', *WORKLOAD, *SETTINGS):
        if key == 'repeat':
            if type(row.get(key)) is not int or row[key] < 1:
                issues.append('repeat must be a positive integer')
        elif key not in ('prefetch', 'background', 'cdda', 'vmu_slot', 'storage') and not known(row.get(key)):
            issues.append('missing ' + key)
    for key in ('prefetch', 'background', 'cdda'):
        if row.get(key) not in ('on', 'off', 'n/a'):
            issues.append(key + ' must be on, off, or n/a')
    if row.get('vmu') not in ('on', 'off', 'physical', 'unsupported'):
        issues.append('vmu must be on, off, physical, or unsupported')
    if row.get('media') not in ('sd', 'disc'):
        issues.append('media must be sd or disc')
    if 'storage' in row and not known(row['storage']):
        issues.append('missing storage')
    if row.get('vmu') in ('on', 'physical') and row.get('vmu_slot') not in ('A1', 'A2', 'B1', 'B2', 'C1', 'C2', 'D1', 'D2'):
        issues.append('active VMU requires an explicit vmu_slot')
    if row.get('media') == 'sd':
        for key in ('build_id', 'expected_build_id', 'expected_profile'):
            if not known(row.get(key)):
                issues.append('missing ' + key)
        for key in ('build_id', 'expected_build_id'):
            if known(row.get(key)) and not re.fullmatch(r'[0-9a-fA-F]{12}', row[key].strip()):
                issues.append(key + ' must be the exact 12 hexadecimal display characters')
        for observed, expected in (('build_id', 'expected_build_id'),
                                   ('profile', 'expected_profile')):
            if known(row.get(observed)) and known(row.get(expected)) and \
                    row[observed].strip().casefold() != row[expected].strip().casefold():
                issues.append(observed + ' does not match expected identity')
    required = row.get('required_activity', [])
    if not isinstance(required, list) or any(not known(x) for x in required):
        issues.append('required_activity must be an array of counter names')
        required = []
    required = set(required)
    if row.get('media') == 'sd':
        required.add('loader')
    if row.get('vmu') == 'on':
        required.add('vmu')
    if row.get('reference_case') and row.get('contrast') == 'profile':
        required.add('candidate')
    activity = row.get('engagement', {})
    if not isinstance(activity, dict):
        issues.append('engagement must be an object')
        activity = {}
    for name in sorted(required | set(activity)):
        evidence = activity.get(name, {})
        if not isinstance(evidence, dict):
            raise ResultError('engagement.' + name + ' must be an object')
        value = count(evidence.get('observed'), radix)
        minimum = count(evidence.get('minimum', 1), radix)
        if minimum is None or minimum < 1 or not known(evidence.get('counter')) or value is None:
            state = 'unavailable'
        else:
            state = 'engaged' if value >= minimum else 'unengaged'
        result['activity'][name] = state
        if name in required and state != 'engaged':
            issues.append(name + ' ' + state)
    clocks = row.get('clocks', {})
    metrics = row.get('metrics', {})
    if not isinstance(clocks, dict) or not isinstance(metrics, dict):
        raise ResultError('clocks and metrics must be objects')
    result['clocks'] = {name: clock_status(value, radix) for name, value in clocks.items()}
    for name, metric in metrics.items():
        if not isinstance(metric, dict):
            raise ResultError('metric ' + name + ' must contain value and unit')
        value = number(metric.get('value'), radix)
        unit = metric.get('unit')
        source = metric.get('source', 'report')
        if source not in ('phone', 'report'):
            raise ResultError('metric source must be phone or report')
        state = 'available' if value is not None and known(unit) else 'unavailable'
        if unit in ('s', 'ms', 'us', 'ns', 'ticks', 'cycles') and source != 'phone':
            clock = result['clocks'].get(metric.get('clock'), 'unavailable')
            if clock != 'valid':
                state = 'unavailable (clock ' + clock + ')'
        sentinels = metric.get('unavailable_values', [])
        if not isinstance(sentinels, list):
            raise ResultError('unavailable_values must be an array')
        if value is not None and value in [number(item, radix) for item in sentinels]:
            state = 'unavailable (sentinel)'
        result['metrics'][name] = {'value': value, 'unit': unit,
                                  'source': source, 'status': state}
    result['eligible'] = not issues
    return result


def compare(row, baseline, result, reference):
    contrast = row.get('contrast')
    if contrast not in CONTRASTS:
        return {'status': 'refused', 'reason': 'declare contrast: ' + ', '.join(CONTRASTS)}
    different = [key for key in (*WORKLOAD, *SETTINGS)
                 if row.get(key) != baseline.get(key) and key not in CONTRASTS[contrast]]
    if different:
        return {'status': 'refused', 'reason': 'unmatched ' + ', '.join(different)}
    changed = [key for key in CONTRASTS[contrast] if row.get(key) != baseline.get(key)]
    if not changed:
        return {'status': 'refused', 'reason': 'declared contrast did not change'}
    if contrast != 'system' and row.get(contrast) == baseline.get(contrast):
        return {'status': 'refused', 'reason': 'primary contrast did not change: ' + contrast}
    if contrast == 'system' and {row.get('media'), baseline.get('media')} != {'disc', 'sd'}:
        return {'status': 'refused', 'reason': 'system contrast requires a disc/SD pair'}
    if not result['eligible'] or not reference['eligible']:
        return {'status': 'refused', 'reason': 'identity/activity evidence incomplete'}
    deltas = {}
    for name in sorted(set(result['metrics']) & set(reference['metrics'])):
        new, old = result['metrics'][name], reference['metrics'][name]
        if new['status'] != 'available' or old['status'] != 'available':
            continue
        if new['unit'] != old['unit'] or new['source'] != old['source']:
            continue
        delta = new['value'] - old['value']
        deltas[name] = {'reference': old['value'], 'candidate': new['value'],
                        'delta': delta, 'unit': new['unit'],
                        'change_pct': 100.0 * delta / old['value'] if old['value'] else None}
    return {'status': 'matched', 'contrast': contrast, 'changed': sorted(changed),
            'metrics': deltas}


def analyze(rows):
    if not rows:
        raise ResultError('no result rows')
    by_id, results = {}, []
    for row in rows:
        result = assess(row)
        case_id = row.get('case_id')
        if case_id in by_id:
            raise ResultError('duplicate case_id: ' + str(case_id))
        by_id[case_id] = (row, result)
        results.append(result)
    for row, result in by_id.values():
        reference_id = row.get('reference_case')
        if reference_id:
            if reference_id not in by_id:
                result['comparison'] = {'status': 'refused', 'reason': 'missing reference_case'}
            elif reference_id == row.get('case_id'):
                result['comparison'] = {'status': 'refused', 'reason': 'self reference'}
            else:
                baseline, reference = by_id[reference_id]
                result['comparison'] = compare(row, baseline, result, reference)
    return results


def markdown(results):
    lines = ['Console evidence review', '',
             'Deltas describe matched captures; they do not establish frame rate or CPU usage.', '']
    for result in results:
        lines.append(str(result['case_id']) + ': ' + ('eligible' if result['eligible'] else 'incomplete'))
        for issue in result['issues']:
            lines.append('- ' + issue)
        for name, state in result['clocks'].items():
            lines.append('- clock ' + name + ': ' + state)
        for name, metric in result['metrics'].items():
            if metric['status'] != 'available':
                lines.append('- ' + name + ': ' + metric['status'])
        comparison = result.get('comparison')
        if comparison:
            if comparison['status'] == 'refused':
                lines.append('- comparison refused: ' + comparison['reason'])
            else:
                lines.append('- matched ' + comparison['contrast'] + ' contrast: ' +
                             ', '.join(comparison['changed']))
                if not comparison['metrics']:
                    lines.append('- no comparable available metrics')
                for name, values in comparison['metrics'].items():
                    percent = ('; change unavailable (zero reference)' if values['change_pct'] is None
                               else '; change {:+.2f}%'.format(values['change_pct']))
                    lines.append('- {}: {} -> {} {}; delta {:+g}{}'.format(
                        name, values['reference'], values['candidate'], values['unit'],
                        values['delta'], percent))
        lines.append('')
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    parser.add_argument('--json', action='store_true', help='emit structured review')
    args = parser.parse_args(argv)
    try:
        results = analyze(read_rows(args.results))
    except (OSError, ValueError, TypeError) as exc:
        parser.exit(2, 'Performance results refused: ' + str(exc) + '\n')
    print(json.dumps({'results': results}, indent=2) if args.json else markdown(results))
    return 1 if any(not row['eligible'] or row.get('comparison', {}).get('status') == 'refused'
                    for row in results) else 0


if __name__ == '__main__':
    sys.exit(main())
