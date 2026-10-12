#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Separate, fail-closed linked admission for four factors over frozen AD.

The historical budget/scheduling gates are unchanged. The frozen AD reference
is admitted by its original gate from its original source directory, never by
running that gate against this modified source tree. Candidate discovery only
emits evidence; there is deliberately no command that fills review tables.
"""
import argparse
import copy
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

import toy_loader_budget_audit as budget_parent

schedule = budget_parent.schedule
trace = budget_parent.trace
Linked = budget_parent.Linked
ROOT = Path(__file__).resolve().parents[1]
PARENT_ROOT = ROOT.parent / 'budget-source'
DEFAULT_AD_BASELINE = PARENT_ROOT / 'build/budget-ad'
LOW, LOW_LIMIT, HIGH, HIGH_LIMIT = schedule.LOW, schedule.LOW_LIMIT, schedule.HIGH, schedule.HIGH_LIMIT
STAGE, STAGE_LIMIT, P2 = schedule.STAGE, schedule.STAGE_LIMIT, schedule.P2
AD_BUILD = 'e680281d1604'
AD_RUNTIME_SHA256 = '8f478c473f2574409a5a0d346237a1b09995d350d960f3150a8f3ebaf716a57a'
REQUIRED_CONFIG = {**budget_parent.REQUIRED_CONFIG, 'DATA_BUDGET_PROFILE': '4'}
# name, physical DATA block cap, DATA TMU cap, audio refill quanta, audio TMU cap
PROFILES = {1: ('AE', 24, 6000, 4, 12500), 2: ('AF', 24, 12000, 4, 12500),
            3: ('AG', 12, 6000, 2, 12500), 4: ('AH', 12, 6000, 4, 6250)}
FROZEN_GATE_SHA256 = {
    'toy_loader_budget_audit.py': '10e2b58c9c81d9515ed5666eac55946325d8264ef1b21db0986c1f67290dd70e',
    'toy_loader_schedule_audit.py': 'bf2641ae40a475b296c3eae93eca0e5cac6186f05233e9bb18df2f45e5f957df',
    'toy_loader_trace_audit.py': 'b3e5ab7ea3b189a0d60e56250aee986a028ae00ba0fb85dd448ced42cad4f932',
    'toy_pilot_cache_audit.py': '8f46d2a9acd0318ebea837e788b3f82f37692164a96ebdbbc0b4d3cdea26118d',
    'check_loader_layout.py': '7551528fa4e86adc02903389f0b8c8266297ccbcb1d0984ddd04ac40cd473dea',
    'check_retail_instructions.py': '895788f228216a759318db11eb683a9c3b9516ead33fa8cc4448ce6e7309d348',
    'package_cdda_toy_pilot.py': '6f27e4f8b090a6934f9deede7b62e69adb7d13dab12e3fd487cd927cbf79258f',
}
REVIEW_SOURCE_FILES = (*budget_parent.REVIEW_SOURCE_FILES,
                       'include/kui/toy_loader_ad_followup.h')
CHANGED_SOURCE_FILES = {'Makefile.toy_pilot', 'include/kui/toy_loader_schedule.h',
    'src/loader/toy_pilot_sci.c', 'src/loader/toy_pilot_sci.h',
    'src/loader/toy_pilot_worker.c', 'include/kui/toy_loader_ad_followup.h'}
# Independently reviewed final runtime source025646791da0e40858122ed88a5098104cba445f.
# SCI changes are exact quota/profile immediates and AF's two tick literals.
# AH changes only the audio timer admission literal; AG's two-quantum loop
# uses the original call/literal classes, finite timer check and audio helpers.
# AG's compiler frame shrinks252->248 bytes. All189 emitted frames were checked.
REVIEWED = {
    1: {'resident-sci': {}, 'stage': {}, 'worker': {
        '_kui_toy_pilot_sci_init': '9188eb8c76bb067c1c520c5857be8f1d408cb90bc98dbe9abd7b298a0f2c8e5c',
        '_kui_toy_pilot_sci_service': 'e6847461ffab2855123cb8e27ba771e99da1250d849a8b33c68e7264baf6b2fe',
        '_data_work': '9b7b84ae3e5feb1ef9206f3ee358e69468e999f49d6330c75f60dd6dbb024fde'}},
    2: {'resident-sci': {}, 'stage': {}, 'worker': {
        '_kui_toy_pilot_sci_init': '9cff8ee5bb994c988555dc8e72ccc3766b711400236094ac744c6a02a679bab0',
        '_kui_toy_pilot_sci_service': '1899cbefc56b0c7f4c2295e884c63d09c26944392220ca7c65a118d118dface8',
        '_data_work': '9b7b84ae3e5feb1ef9206f3ee358e69468e999f49d6330c75f60dd6dbb024fde'}},
    3: {'resident-sci': {}, 'stage': {}, 'worker': {
        '_kui_toy_pilot_sci_init': 'a99d89a15e06d0a81dc1cf4cf601bd3d7a5e1612c75abf8ad8e83dd47d7f6d8f',
        '_kui_toy_pilot_worker_step': 'f4276a58252643c9d012c655b6c226b0b361457bac978c95fad4b926ab5d4ddf'}},
    4: {'resident-sci': {}, 'stage': {}, 'worker': {
        '_kui_toy_pilot_sci_init': '37469801b94429e0525ed5c4da98604fa26a0038eba6360c9ec65814180a2a67',
        '_kui_toy_pilot_worker_step': '1969fe52ce66ada59ff879d70b4629b224b390b55d392620967a55fc3605d592'}},
}
REVIEWED_STACK_ROWS = {profile: copy.deepcopy(budget_parent.REVIEWED_STACK_ROWS[4]) for profile in PROFILES}
REVIEWED_STACK_ROWS[3]['worker/src/loader/toy_pilot_worker.su'] = '2b4e55eb0811938969e4bc22399fc1daa4920c13e37ffc15324a2478d355f811'
REVIEWED_RUNTIME_SHA256 = {
    1: '037697fca88aacec5927784c24091a553918f44f68814d1bafb614e7b4d769f8',
    2: '1fdc977d61d5ec50b0c66777a60db4dc9bc3cd73dbeecc5bda787af9e195d5f4',
    3: '40f756a7d88557b22364a0166798bb7b395ba367f99434c0a49e3cd4b21073fa',
    4: 'cd7f9372166273deb8bce33afbd4161420461430c2435dcb06fc18aa835806cb',
}
REVIEWED_SOURCE_SHA256 = {
    'Makefile.toy_pilot': 'e71bcef12427abcd990e048937f4c5f1f1be79bcc97d48234bc3fb44e470689c',
    'include/kui/toy_loader_schedule.h': '60b7ec6bb7df4357fca9d601b98496b76ee95b3e1015bec632974ee8bbeea93b',
    'include/kui/toy_loader_schedule_report.h': 'c59e3f751f760692100e21ba10869d68d32f02686ac64bd2d9e6c861ea9e4eff',
    'src/loader/toy_pilot_sci.c': '97388ee120affacf39ea455f64ae21c035c02e09188f6ada1489db1d8b15bdcb',
    'src/loader/toy_pilot_sci.h': '2dab3f19ca9b8fb4ff35d892aee525f7c94cbf49bac2dcf378ace12812543aa1',
    'src/loader/toy_loader_schedule_report.c': '75e44977fa46e1c6262354a8f9dc36cb0d047acc32980d3900ef190ce4967b8b',
    'src/loader/toy_loader_trace.c': '9b16e64a1cf311bc99c26eed5e3e3d02c46ee5781f5bda041b8fcdd1e01d5e93',
    'src/loader/retail_resident.c': 'de823b3ba5d9f337be6a48dae223cf1842db9c044f124135b19c85f8a4845458',
    'src/loader/toy_pilot_gd.c': 'f00450c6c56e0ccfc61b5c46beb8efbb76698e893a7ff39920d72f7f0af9074f',
    'src/loader/toy_loader_trace_report.c': 'c787f102013956c3c64c55c26f4a1c5f7f9ca4894120ddfcbd49727a68ef9853',
    'src/loader/toy_pilot_worker.c': '0d3d341f971355c31a15cf1afd84f1230f6e4e779f41ff1b554d25f417f51863',
    'src/loader/toy_pilot_resident.c': 'a1e67d0fd91cef3187c6bc64ec0d4fc55f185d22a705debd66b74ab9fc5f14dc',
    'include/kui/toy_loader_ad_followup.h': '84adf2b997752a9ae47a82de86df93dc7a683db239fefe1e18a16b5af74cca91',
}
# Geometry is inherited exactly from admitted AD; no resize/new object is an
# allowed experimental factor. Actual inventories are additionally compared.
REVIEWED_OBJECTS = {profile: copy.deepcopy(budget_parent.REVIEWED_OBJECTS[4])
                    for profile in PROFILES}
def function_identity(code, name, build, *, retained=True):
    """Retain the AD v2 terminal without applying historical U v1 offsets.

    Its only anonymous binary literal is the exact four-word report order.
    AG moves that array with the unchanged glyph object. Check the original
    load/register, readonly bounds, contents and glyph-minus16 anchor before
    assigning this one operand an identity. Every other opcode, literal and
    transfer stays in the unchanged parent canonicalizer.
    """
    if code.base != HIGH or name != '_kui_toy_loader_trace_terminal':
        return budget_parent.function_identity(code, name, build, retained=retained)
    begin, _, _ = schedule.bounded_function(code, name)
    if code.half(begin + 0x20) & 0xFF00 != 0xD500:
        raise ValueError('AD v2 terminal report-order opcode/register changed')
    _, value = code.literal(begin + 0x20, 5, 4)
    expected = bytes.fromhex('03000000020000000000000001000000')
    if (value != code.symbols.get('_glyphs', 0) - len(expected) or
            not any(a <= value < value + len(expected) <= b
                    for a, b in code.image.get('readonly_ranges', [])) or
            code.data(value, len(expected)) != expected):
        raise ValueError('AD v2 terminal exact readonly report-order array changed')
    scoped = copy.copy(code)
    scoped.image = {**code.image, 'sized_symbol_records': [*code.image['sized_symbol_records'],
        {'name': '__reviewed_AD_v2_report_order', 'address': value,
         'bytes': len(expected), 'type': 1, 'occurrence': 0}]}
    return budget_parent.function_identity(scoped, name, build, retained=False)


def changed_functions(profile, image_name):
    if profile not in PROFILES:
        raise ValueError('Unknown AD follow-up profile')
    if image_name != 'worker':
        return set()
    return {'_kui_toy_pilot_sci_init'} | (
        {'_kui_toy_pilot_sci_service', '_data_work'} if profile in (1, 2)
        else {'_kui_toy_pilot_worker_step'})


def read_config(directory, profile=None):
    config = {}
    for line in (Path(directory) / 'build-config').read_text().splitlines():
        if '=' not in line:
            raise ValueError('Malformed AD follow-up configuration')
        key, value = line.split('=', 1)
        if key in config:
            raise ValueError('Duplicate AD follow-up configuration: ' + key)
        config[key] = value
    if set(config) != set(REQUIRED_CONFIG) | {'BUILD', 'AD_FOLLOWUP_PROFILE'}:
        raise ValueError('AD follow-up configuration has missing/unreviewed keys')
    if any(config.get(key) != value for key, value in REQUIRED_CONFIG.items()):
        raise ValueError('AD follow-ups must retain complete AD reader/cache/audio configuration')
    if config['AD_FOLLOWUP_PROFILE'] not in tuple(str(p) for p in PROFILES):
        raise ValueError('AD follow-up profile must be 1, 2, 3 or 4')
    if profile is not None and config['AD_FOLLOWUP_PROFILE'] != str(profile):
        raise ValueError('AD follow-up profile differs from linked configuration')
    if not re.fullmatch(r'[0-9a-f]{12}', config.get('BUILD', '')):
        raise ValueError('AD follow-up build must be twelve hexadecimal digits')
    return config


def load_profile(directory, profile=None):
    directory = Path(directory).resolve()
    config = read_config(directory, profile)
    objdump = shutil.which('sh-elf-objdump')
    if not objdump:
        raise ValueError('SH objdump is required for AD follow-up admission')
    images, dis = {}, {}
    for name, base, limit in (('resident-sci', LOW, LOW_LIMIT),
                             ('worker', HIGH, HIGH_LIMIT), ('stage', STAGE, STAGE_LIMIT)):
        raw = (directory / (name + '.elf')).read_bytes()
        if name != 'stage':
            schedule.reject_ambiguous_symbols(raw)
        options = {'private_p2': True} if name != 'stage' else {}
        if name == 'worker':
            options.update(entry_symbol='_kui_toy_pilot_initialize', entry_at_base=False)
        image = budget_parent.inspect_elf(raw, base, limit, **options)
        image['symbol_sizes'] = budget_parent.linked_symbol_sizes(raw)
        image['readonly_ranges'] = schedule.u.readonly_ranges(raw)
        image['sized_symbol_records'] = schedule.sized_symbol_records(raw)
        if (directory / (name + '.bin')).read_bytes() != image['payload']:
            raise ValueError(name + '.bin differs from actual AD follow-up ELF bytes')
        images[name] = image
        dis[name] = subprocess.check_output([objdump, '-d', str(directory / (name + '.elf'))], text=True)
    schedule.attach_linked_symbols(images)
    return config, images, dis


def audit_frozen_gates(root=ROOT, parent_root=PARENT_ROOT):
    observed = {}
    for name, expected in FROZEN_GATE_SHA256.items():
        for directory in (Path(root), Path(parent_root)):
            actual = hashlib.sha256((directory / 'tools' / name).read_bytes()).hexdigest()
            if actual != expected:
                raise ValueError('Changed immutable parent admission tool: ' + name)
        observed[name] = expected
    return observed


def _reference_signature(directory):
    directory = Path(directory)
    names = ['build-config', 'retail-toy-pilot.kui', 'entry.elf',
             'toy_pilot_resident_symbols.h']
    names += [name + suffix for name in ('resident-sci', 'worker', 'stage')
              for suffix in ('.elf', '.bin')]
    names += sorted(path.relative_to(directory).as_posix()
                    for group in ('worker', 'stage', 'sci/lto')
                    for path in (directory / group).rglob('*.su'))
    return tuple((name, hashlib.sha256((directory / name).read_bytes()).hexdigest())
                 for name in names)


@lru_cache(maxsize=4)
def _reference_ad_cached(directory, signature):
    del signature  # Its full content hashes invalidate the cache on any change.
    completed = subprocess.run([sys.executable,
        str(PARENT_ROOT / 'tools/toy_loader_budget_audit.py'), str(directory),
        '--profile', '4'], cwd=PARENT_ROOT, text=True, capture_output=True)
    if completed.returncode:
        raise ValueError('AD reference failed its original immutable gate: ' + completed.stderr.strip())
    proof = json.loads(completed.stdout)
    if (proof.get('admitted') is not True or proof.get('build') != AD_BUILD or
            proof.get('runtime', {}).get('sha256') != AD_RUNTIME_SHA256):
        raise ValueError('AD reference did not pass its original immutable gate')
    config, images, dis = budget_parent.load_profile(directory, 4)
    return config, images, dis, proof


def reference_ad(directory=DEFAULT_AD_BASELINE):
    directory = Path(directory).resolve()
    audit_frozen_gates()
    # Always revalidate frozen source, even when artifact inspection is cached.
    budget_parent.audit_sources(PARENT_ROOT)
    result = _reference_ad_cached(directory, _reference_signature(directory))
    return copy.deepcopy(result)


def _private_objects(image, base, limit):
    return {record['name']: record['bytes'] for record in image['sized_symbol_records']
            if record['type'] == 1 and record['bytes'] and
            base + P2 <= record['address'] < limit + P2}


def compare_retained(directory, config, images, dis, baseline=DEFAULT_AD_BASELINE, *, discover=False):
    old_config, old_images, old_dis, proof = reference_ad(baseline)
    profile = int(config['AD_FOLLOWUP_PROFILE'])
    identities, differences, new_names, names_by_image, objects = {}, {}, {}, {}, {}
    for image_name, base, limit in (('resident-sci', LOW, LOW_LIMIT),
                                  ('worker', HIGH, HIGH_LIMIT), ('stage', STAGE, STAGE_LIMIT)):
        before_code = Linked(old_images[image_name], base, old_dis[image_name])
        code = Linked(images[image_name], base, dis[image_name])
        old_names = schedule.u.function_symbols((Path(baseline) / (image_name + '.elf')).read_bytes())
        names = schedule.u.function_symbols((Path(directory) / (image_name + '.elf')).read_bytes())
        if old_names - names:
            raise ValueError('AD follow-up removed retained AD functions: ' + ', '.join(sorted(old_names - names)))
        names_by_image[image_name] = names
        new_names[image_name] = sorted(names - old_names)
        identities[image_name], differences[image_name] = {}, {}
        for name in sorted(old_names - changed_functions(profile, image_name)):
            old_begin, old_end, _ = schedule.bounded_function(before_code, name)
            begin, end, _ = schedule.bounded_function(code, name)
            old_bytes = before_code.data(old_begin, old_end - old_begin)
            new_bytes = code.data(begin, end - begin)
            if old_bytes == new_bytes:
                before = after = hashlib.sha256(b'byte-identical-function\0' + old_bytes).hexdigest()
            else:
                before = function_identity(before_code, name, old_config['BUILD'])
                after = function_identity(code, name, config['BUILD'])
            identities[image_name][name] = after
            if before != after:
                differences[image_name][name] = {'AD': before, 'candidate': after}
        old_objects = _private_objects(old_images[image_name], base, limit)
        actual_objects = _private_objects(images[image_name], base, limit)
        if actual_objects != old_objects:
            raise ValueError('AD follow-up changed exact retained private object inventory: ' + image_name)
        objects[image_name] = actual_objects
    if not discover and (any(differences.values()) or any(new_names.values())):
        raise ValueError('AD follow-up changed retained opcode/literal identity or gained functions: ' +
                         json.dumps({'mismatches': differences, 'new': new_names}, sort_keys=True))
    return {'baseline_build': AD_BUILD, 'baseline_runtime': proof['runtime'],
        'baseline_gate': 'frozen budget-source/tools/toy_loader_budget_audit.py, profile4',
        'baseline_admitted_by_original_gate': True,
        'U_audio_reference': proof['retained']['U_audio_reference'],
        'function_identities': identities, 'retained_mismatches': differences,
        'new_functions': new_names, 'functions': names_by_image,
        'exact_retained_private_object_inventories': objects}


def reviewed_shapes(config, images, dis, retained):
    profile = int(config['AD_FOLLOWUP_PROFILE'])
    if not REVIEWED[profile] or not REVIEWED_STACK_ROWS[profile]:
        raise ValueError('Missing independently reviewed AD follow-up identities/frames')
    result = {}
    for image_name, base in (('resident-sci', LOW), ('worker', HIGH), ('stage', STAGE)):
        expected = REVIEWED[profile].get(image_name, {})
        if (set(expected) != changed_functions(profile, image_name) or
                retained['functions'][image_name] != set(expected) | set(retained['function_identities'][image_name])):
            raise ValueError('AD follow-up missing/unreviewed functions in ' + image_name)
        code = Linked(images[image_name], base, dis[image_name])
        actual = {name: function_identity(code, name, config['BUILD'], retained=False) for name in expected}
        if actual != expected:
            raise ValueError('Changed independently reviewed AD follow-up body: ' + image_name)
        result[image_name] = actual
    return result


def source_identities(root=ROOT):
    return {name: hashlib.sha256((Path(root) / name).read_bytes()).hexdigest() for name in REVIEW_SOURCE_FILES}


def audit_sources(root=ROOT):
    if set(REVIEWED_SOURCE_SHA256) != set(REVIEW_SOURCE_FILES):
        raise ValueError('Missing independently reviewed AD follow-up sources')
    observed = source_identities(root)
    if observed != REVIEWED_SOURCE_SHA256:
        raise ValueError('Changed independently reviewed AD follow-up source')
    retained = {name: expected for name, expected in budget_parent.REVIEWED_SOURCE_SHA256.items()
                if name not in CHANGED_SOURCE_FILES}
    if any(observed[name] != expected for name, expected in retained.items()):
        raise ValueError('AD follow-up changed retained AD runtime source')
    frozen = {}
    for name, expected in schedule.FROZEN_SOURCES.items():
        if name == 'src/loader/toy_loader_trace.c':
            continue
        raw = (Path(root) / name).read_bytes()
        if name == 'src/loader/sci_sd_bus.c':
            addition = re.compile(rb'^#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 2\n/\* Startup-only typed export\.[\s\S]*?^#endif\n', re.M)
            matches = list(addition.finditer(raw))
            if (len(matches) != 1 or
                    b'bool __attribute__((noipa)) kui_toy_pilot_startup_sci_healthy(void)' not in matches[0].group()):
                raise ValueError('Missing/ambiguous historical startup health export')
            raw = addition.sub(b'', raw)
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError('AD follow-up changed frozen low reader/CDDA source: ' + name)
        frozen[name] = expected
    return {'reviewed_followup_sha256': observed, 'retained_AD_source_sha256': retained,
            'frozen_low_CDDA_sha256': frozen, 'immutable_parent_gate_sha256': audit_frozen_gates()}


def budget_metadata(profile):
    name, blocks, ticks, _, _ = PROFILES[profile]
    return {'name': name, 'profile': profile + 4, 'credit_limit': blocks,
        'credit_unit': 'verified 512-byte physical card blocks', 'tick_limit': ticks,
        'tick_hz': 781250, 'admission_microseconds': ticks * 1000000 / 781250,
        'finite_step_limit': 1024, 'one_step_may_cross_tick_limit': True,
        'intervening_IRQ_and_one_step_work_count_toward_block_limit': True}


def audio_policy(profile):
    _, _, _, quanta, ticks = PROFILES[profile]
    return {'refill_quanta': quanta, 'service_ticks': ticks, 'tick_hz': 781250,
        'admission_microseconds': ticks * 1000000 / 781250,
        'one_quantum_may_cross_tick_limit': True,
        'changed_from_AD': profile in (3, 4),
        'raw_sector_reader_and_ring_queue_algorithm_retained': True}


def audit_schema(profile, root=ROOT):
    if profile not in PROFILES:
        raise ValueError('Unknown AD follow-up schema profile')
    cc = shutil.which('cc') or shutil.which('gcc')
    if not cc:
        raise ValueError('Host preprocessor required for guarded AD follow-up schema')
    _, blocks, ticks, quanta, audio_ticks = PROFILES[profile]
    definitions = [f'-DKUI_TOY_PILOT_AD_FOLLOWUP_PROFILE={profile}',
        '-DKUI_TOY_PILOT_DATA_BUDGET_PROFILE=4', '-DKUI_TOY_PILOT_DATA_SCHEDULE_MODE=2',
        '-DKUI_TOY_PILOT_PRIVATE_P2=1', '-DKUI_TOY_PILOT_LOADER_TRACE=1',
        '-DKUI_TOY_PILOT_DATA_PROBE=1', '-DKUI_TOY_PILOT_GD_FIXED_STEP=2']
    expected = {'KUI_TOY_SCHEDULE_REPORT_VERSION': 2, 'KUI_TOY_SCHEDULE_REPORT_WORDS': 192,
        'KUI_TOY_SCHEDULE_RECORD_WORDS': 18, 'KUI_TOY_SCHEDULE_RECORDS': 8,
        'KUI_TOY_SCI_SERVICE_BLOCKS': blocks, 'KUI_TOY_SCI_SERVICE_TICKS': ticks,
        'KUI_TOY_SCI_SERVICE_STEPS': 1024, 'KUI_TOY_AD_BUDGET_REPORT_PROFILE': profile + 4,
        'KUI_TOY_AD_REFILL_QUANTA': quanta, 'KUI_TOY_AD_REFILL_TICKS': audio_ticks}
    source = '#include "kui/toy_loader_schedule_report.h"\n#include "toy_pilot_sci.h"\n'
    for name, value in expected.items():
        source += f'#if {name} != {value}u\n#error AD followup schema constant changed: {name}\n#endif\n'
    completed = subprocess.run([cc, '-E', '-x', 'c',
        '-I' + str(Path(root) / 'include'), '-I' + str(Path(root) / 'src/loader'),
        *definitions, '-'], input=source, text=True, capture_output=True)
    if completed.returncode:
        raise ValueError('AD follow-up schema constant/guard changed: ' + completed.stderr.strip())
    return {'magic': '0x4c534331', 'version': 2, 'words': 192,
        'header_words': 16, 'engine_words': 32, 'records': 8, 'record_words': 18,
        'record_start': 48, 'budget_stats_start': 36, 'budget_stats_words': 12,
        'reported_budget_profile': profile + 4, 'TRACE_words': 416,
        'record_words_16_17': ['IRQ verified-block delta', 'retry delta'],
        'publication_checksum': 'first credited sector CRC32',
        'TRACE_delivered_sector_unit': 'credited logical sector bytes, persistent across IRQ work'}


def candidate(builddir, baseline=DEFAULT_AD_BASELINE, profile=None):
    directory = Path(builddir).resolve()
    config, images, dis = load_profile(directory, profile)
    profile = int(config['AD_FOLLOWUP_PROFILE'])
    retained = compare_retained(directory, config, images, dis, baseline, discover=True)
    evidence = {}
    for image_name, base in (('resident-sci', LOW), ('worker', HIGH), ('stage', STAGE)):
        code = Linked(images[image_name], base, dis[image_name])
        names = (set(retained['new_functions'][image_name]) |
                 set(retained['retained_mismatches'][image_name]) | changed_functions(profile, image_name))
        evidence[image_name] = {}
        for name in sorted(names & retained['functions'][image_name]):
            begin, end, rows = schedule.bounded_function(code, name)
            evidence[image_name][name] = {'identity': function_identity(code, name, config['BUILD'], retained=False),
                'bytes': end - begin, 'instructions': [[hex(at), hex(op), mn, arg] for at, op, mn, arg in rows],
                'literals': [{'instruction': hex(at), 'value': hex(code.literal(at, op >> 8 & 15,
                    4 if op & 0xF000 == 0xD000 else 2)[1])} for at, op, mn, _ in rows
                    if op & 0xF000 in (0xD000, 0x9000) and mn in ('mov.l', 'mov.w')]}
    retained.pop('functions')
    runtime = schedule.previous.bind_runtime(directory, config, images)
    runtime.update(bytes=(directory / 'retail-toy-pilot.kui').stat().st_size, build=config['BUILD'])
    return {'admitted': False, 'independent_review_required': True, 'config': config,
        'profile': PROFILES[profile][0], 'followup_profile': profile, 'budget_profile': profile + 4,
        'budget': budget_metadata(profile), 'audio_policy': audio_policy(profile),
        'runtime': runtime, 'retained': retained, 'functions': evidence,
        'stack_rows': schedule.stack_inventory(directory), 'source_sha256': source_identities()}


def audit_followup(builddir, baseline=DEFAULT_AD_BASELINE, profile=None):
    directory = Path(builddir).resolve()
    config, images, dis = load_profile(directory, profile)
    profile = int(config['AD_FOLLOWUP_PROFILE'])
    runtime = schedule.previous.bind_runtime(directory, config, images)
    if not REVIEWED_RUNTIME_SHA256[profile] or runtime['sha256'] != REVIEWED_RUNTIME_SHA256[profile]:
        raise ValueError('AD follow-up runtime is not the independently reviewed immutable artifact')
    retained = compare_retained(directory, config, images, dis, baseline)
    reviewed = reviewed_shapes(config, images, dis, retained)
    inventory = schedule.stack_inventory(directory)
    if schedule.stack_signatures(inventory) != REVIEWED_STACK_ROWS[profile]:
        raise ValueError('AD follow-up emitted static stack inventory changed or missing')
    for name in images:
        try:
            budget_parent.instruction_audit(name, dis[name])
        except SystemExit as exc:
            raise ValueError(str(exc)) from exc
    low, worker, stage = (images[name] for name in ('resident-sci', 'worker', 'stage'))
    objects = {}
    for image_name, expected in REVIEWED_OBJECTS[profile].items():
        base, limit, prefix = ((HIGH, HIGH_LIMIT, '__toy_pilot') if image_name == 'worker'
                               else (LOW, LOW_LIMIT, '__retail_resident'))
        objects[image_name] = schedule.audit_owned_objects(images[image_name], expected, prefix, base, limit)
    retained.pop('functions')
    code = Linked(worker, HIGH, dis['worker'])
    runtime.update(bytes=(directory / 'retail-toy-pilot.kui').stat().st_size, build=config['BUILD'])
    return {'admitted': True, 'profile': PROFILES[profile][0], 'followup_profile': profile,
        'budget_profile': profile + 4, 'build': config['BUILD'], 'config': config,
        'budget': budget_metadata(profile), 'audio_policy': audio_policy(profile), 'runtime': runtime,
        'retained': retained, 'reviewed_functions': reviewed, 'sources': audit_sources(),
        'owned_objects': objects, 'schedule_schema': audit_schema(profile),
        'report_geometry': budget_parent.audit_report_geometry(worker),
        'data_routing': {'asynchronous_startup_cooked_sector_bytes': 2048,
            'retained_dispatch_identity': retained['function_identities']['worker']['_dispatch_body'],
            'raw_and_post_PLAY_reader': 'original synchronous low reader retained against AD, Z and U'},
        'TRACE_graph': budget_parent.audit_budget_trace_graph(code),
        'retained_DATA_probe': {'schema': schedule.previous.audit_schema(),
            'bindings': schedule.previous.audit_bindings(directory, low, code, 0),
            'passive_callgraph': schedule.previous.audit_probe_graph(code, low, 0)},
        'exports': trace.audit_exports(low, worker),
        'bridges': schedule.audit_schedule_bridges(worker, dis['worker'], 2),
        'publication': trace.audit_boot_publication(low, worker, stage, dis['resident-sci'], dis['stage']),
        'cache_audio_heap': budget_parent.audit_cache_layout(low, worker, stage,
            dis['resident-sci'], dis['worker'], dis['stage']),
        'stacks': schedule.audit_schedule_stacks(directory, images, dis),
        'emitted_frames': schedule.audit_emitted_frames(directory, images, dis, inventory),
        'startup_IRQ': schedule.audit_startup_irq(directory, worker, 2),
        'hardware_behavior_verified': False, 'audio_or_video_improvement_proven': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builddir', type=Path)
    parser.add_argument('--baseline', type=Path, default=DEFAULT_AD_BASELINE)
    parser.add_argument('--profile', type=int, choices=tuple(PROFILES))
    parser.add_argument('--candidate', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = (candidate if args.candidate else audit_followup)(args.builddir, args.baseline, args.profile)
    encoded = json.dumps(result, indent=2, sort_keys=True) + '\n'
    if args.output:
        args.output.write_text(encoded)
    else:
        print(encoded, end='')
