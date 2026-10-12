#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Separate, fail-closed linked admission for four DATA factors over frozen AF.

The historical budget/scheduling gates are unchanged. The frozen AF reference
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

import toy_ad_followup_audit as af_parent

budget_parent = af_parent.budget_parent

schedule = budget_parent.schedule
trace = budget_parent.trace
Linked = budget_parent.Linked
ROOT = Path(__file__).resolve().parents[1]
PARENT_ROOT = ROOT.parent / 'ad-followup-source'
DEFAULT_AF_BASELINE = PARENT_ROOT / 'build/followup-af'
LOW, LOW_LIMIT, HIGH, HIGH_LIMIT = schedule.LOW, schedule.LOW_LIMIT, schedule.HIGH, schedule.HIGH_LIMIT
STAGE, STAGE_LIMIT, P2 = schedule.STAGE, schedule.STAGE_LIMIT, schedule.P2
AF_BUILD = '025646791d02'
AF_RUNTIME_SHA256 = '1fdc977d61d5ec50b0c66777a60db4dc9bc3cd73dbeecc5bda787af9e195d5f4'
REQUIRED_CONFIG = {**af_parent.REQUIRED_CONFIG, 'AD_FOLLOWUP_PROFILE': '2'}
# name, verified512-byte block cap, TMU tick cap, finite service-step cap
PROFILES = {1: ('AI',24,12000,2048), 2: ('AJ',24,9000,1024),
            3: ('AK',32,12000,2048), 4: ('AL',32,18000,2048)}
FROZEN_GATE_SHA256 = {
    'toy_loader_budget_audit.py': '10e2b58c9c81d9515ed5666eac55946325d8264ef1b21db0986c1f67290dd70e',
    'toy_loader_schedule_audit.py': 'bf2641ae40a475b296c3eae93eca0e5cac6186f05233e9bb18df2f45e5f957df',
    'toy_loader_trace_audit.py': 'b3e5ab7ea3b189a0d60e56250aee986a028ae00ba0fb85dd448ced42cad4f932',
    'toy_pilot_cache_audit.py': '8f46d2a9acd0318ebea837e788b3f82f37692164a96ebdbbc0b4d3cdea26118d',
    'check_loader_layout.py': '7551528fa4e86adc02903389f0b8c8266297ccbcb1d0984ddd04ac40cd473dea',
    'check_retail_instructions.py': '895788f228216a759318db11eb683a9c3b9516ead33fa8cc4448ce6e7309d348',
    'package_cdda_toy_pilot.py': '6f27e4f8b090a6934f9deede7b62e69adb7d13dab12e3fd487cd927cbf79258f',
}
FROZEN_GATE_SHA256['toy_ad_followup_audit.py'] = '84b2241a1d2350f392a3144f3d3eb5e4d3d08dbb6f35f29860ccf3caa313cf39'
REVIEW_SOURCE_FILES = (*af_parent.REVIEW_SOURCE_FILES,
                       'include/kui/toy_loader_data_tuning.h')
CHANGED_SOURCE_FILES = {'Makefile.toy_pilot', 'include/kui/toy_loader_ad_followup.h',
                       'src/loader/toy_pilot_sci.h', 'include/kui/toy_loader_data_tuning.h'}
# Independently reviewed runtime source0251a9f056f4f7af99031d391e3dbcf0a1c7fce4.
# Only the reported profile, block saturation, timer and finite-step constants
# change. All branch/call/return opcodes and positions match frozen AF. The
# literal1024/2048 bound is checked before work and its counter is saved across
# both helpers and incremented on each back edge, including a stopped clock.
# All189 actual compiler frames and complete object inventories retain AF.
REVIEWED = {
    1: {'resident-sci': {}, 'stage': {}, 'worker': {
        '_kui_toy_pilot_sci_init': '57e7b33753a65834124484854e9be91fb0353a3ac10e473fca74d4601e97f8c9',
        '_kui_toy_pilot_sci_service': '5ff8abebe25113705a5e9840f2c3d84471e4338045b838b84ae2d5d2e34e23a5'}},
    2: {'resident-sci': {}, 'stage': {}, 'worker': {
        '_kui_toy_pilot_sci_init': 'b9362cb9f1b29f0efcd50dce6da5014c3592bd8e63fa1b8199c8839b2dffd6ac',
        '_kui_toy_pilot_sci_service': 'a9b77d5744118b3386e04632c0283525a1e8b2bd5d9497f4ef5257f782e0cad1'}},
    3: {'resident-sci': {}, 'stage': {}, 'worker': {
        '_kui_toy_pilot_sci_init': '477d97c8fe04060b25e6d02bd21911ffa4f3f8f00431f68476d2092b4f2acb41',
        '_kui_toy_pilot_sci_service': '532582c5b85973f123a788a894bbdb6f44edc56e93920d2d4bd8c11370eee427',
        '_data_work': '02f2fb6f4d37eefcc02ba9e2a1332a224654d458141babcdbcadd5b25e71e6fe'}},
    4: {'resident-sci': {}, 'stage': {}, 'worker': {
        '_kui_toy_pilot_sci_init': '73e827ba22da5bd39656c7ba43afb494005e3d4bd9f2b7fbae0101e027e55f4a',
        '_kui_toy_pilot_sci_service': '91da07c11415603954cb518dbb767ad388819c9402ebfa94ada6d007b934172a',
        '_data_work': '02f2fb6f4d37eefcc02ba9e2a1332a224654d458141babcdbcadd5b25e71e6fe'}},
}
REVIEWED_STACK_ROWS = {profile: copy.deepcopy(af_parent.REVIEWED_STACK_ROWS[2]) for profile in PROFILES}
REVIEWED_RUNTIME_SHA256 = {
    1: '3121b5e2b6efb6a34a51cfdb5aecd6496362048859092e3fa4d3caeada85d26e',
    2: 'baea09af25b4b65950025514ece78495923e62751c34129a2051370e74f941da',
    3: 'd3df374ba3d920403239f35d79ae8b53fafaef9e8ba6f643471898b91b77e117',
    4: '81d94ef1cbad18bf682bed4828fab5b0b8bad952ffb34a848848c9cb545717b9',
}
REVIEWED_SOURCE_SHA256 = {
    'Makefile.toy_pilot': 'c745c36818e3ad5696d89a5a351a20185b36ef5626b7db9d220cd8ebb949419e',
    'include/kui/toy_loader_schedule.h': '60b7ec6bb7df4357fca9d601b98496b76ee95b3e1015bec632974ee8bbeea93b',
    'include/kui/toy_loader_schedule_report.h': 'c59e3f751f760692100e21ba10869d68d32f02686ac64bd2d9e6c861ea9e4eff',
    'src/loader/toy_pilot_sci.c': '97388ee120affacf39ea455f64ae21c035c02e09188f6ada1489db1d8b15bdcb',
    'src/loader/toy_pilot_sci.h': '565d16d2690530aa73c72df454680a4a8be66210591363081c3fcee6681fef93',
    'src/loader/toy_loader_schedule_report.c': '75e44977fa46e1c6262354a8f9dc36cb0d047acc32980d3900ef190ce4967b8b',
    'src/loader/toy_loader_trace.c': '9b16e64a1cf311bc99c26eed5e3e3d02c46ee5781f5bda041b8fcdd1e01d5e93',
    'src/loader/retail_resident.c': 'de823b3ba5d9f337be6a48dae223cf1842db9c044f124135b19c85f8a4845458',
    'src/loader/toy_pilot_gd.c': 'f00450c6c56e0ccfc61b5c46beb8efbb76698e893a7ff39920d72f7f0af9074f',
    'src/loader/toy_loader_trace_report.c': 'c787f102013956c3c64c55c26f4a1c5f7f9ca4894120ddfcbd49727a68ef9853',
    'src/loader/toy_pilot_worker.c': '0d3d341f971355c31a15cf1afd84f1230f6e4e779f41ff1b554d25f417f51863',
    'src/loader/toy_pilot_resident.c': 'a1e67d0fd91cef3187c6bc64ec0d4fc55f185d22a705debd66b74ab9fc5f14dc',
    'include/kui/toy_loader_ad_followup.h': '17a2dbd1bdcf0b066bd0fa7f5324fcc5969510599af729509cd351b91f18c9a1',
    'include/kui/toy_loader_data_tuning.h': '491d35345fb593a2aea8a1bea38f57c5ea0d2ba564b8d4dedd16e6077d024481',
}
# Geometry is inherited exactly from admitted AF; no resize/new object is an
# allowed experimental factor. Actual inventories are additionally compared.
REVIEWED_OBJECTS = {profile: copy.deepcopy(af_parent.REVIEWED_OBJECTS[2])
                    for profile in PROFILES}
def function_identity(code,name,build,*,retained=True):
    # The original immutable AF gate owns the exact ADv2 report-order anchor,
    # VBR/protocol relocation scopes and all opcode/literal contents checks.
    return af_parent.function_identity(code,name,build,retained=retained)


def changed_functions(profile, image_name):
    if profile not in PROFILES:
        raise ValueError('Unknown AF DATA tuning profile')
    if image_name != 'worker':
        return set()
    return {'_kui_toy_pilot_sci_init','_kui_toy_pilot_sci_service'} | (
        {'_data_work'} if profile in (3,4) else set())


def read_config(directory, profile=None):
    config = {}
    for line in (Path(directory) / 'build-config').read_text().splitlines():
        if '=' not in line:
            raise ValueError('Malformed AF DATA tuning configuration')
        key, value = line.split('=', 1)
        if key in config:
            raise ValueError('Duplicate AF DATA tuning configuration: ' + key)
        config[key] = value
    if set(config) != set(REQUIRED_CONFIG) | {'BUILD', 'DATA_TUNING_PROFILE'}:
        raise ValueError('AF DATA tuning configuration has missing/unreviewed keys')
    if any(config.get(key) != value for key, value in REQUIRED_CONFIG.items()):
        raise ValueError('AF DATA tunings must retain complete AF reader/cache/audio configuration')
    if config['DATA_TUNING_PROFILE'] not in tuple(str(p) for p in PROFILES):
        raise ValueError('AF DATA tuning profile must be 1, 2, 3 or 4')
    if profile is not None and config['DATA_TUNING_PROFILE'] != str(profile):
        raise ValueError('AF DATA tuning profile differs from linked configuration')
    if not re.fullmatch(r'[0-9a-f]{12}', config.get('BUILD', '')):
        raise ValueError('AF DATA tuning build must be twelve hexadecimal digits')
    return config


def load_profile(directory, profile=None):
    directory = Path(directory).resolve()
    config = read_config(directory, profile)
    objdump = shutil.which('sh-elf-objdump')
    if not objdump:
        raise ValueError('SH objdump is required for AF DATA tuning admission')
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
            raise ValueError(name + '.bin differs from actual AF DATA tuning ELF bytes')
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
def _reference_af_cached(directory, signature):
    del signature  # Its full content hashes invalidate the cache on any change.
    completed = subprocess.run([sys.executable,
        str(PARENT_ROOT / 'tools/toy_ad_followup_audit.py'), str(directory),
        '--profile', '2'], cwd=PARENT_ROOT, text=True, capture_output=True)
    if completed.returncode:
        raise ValueError('AF reference failed its original immutable gate: ' + completed.stderr.strip())
    proof = json.loads(completed.stdout)
    if (proof.get('admitted') is not True or proof.get('build') != AF_BUILD or
            proof.get('runtime', {}).get('sha256') != AF_RUNTIME_SHA256):
        raise ValueError('AF reference did not pass its original immutable gate')
    config, images, dis = af_parent.load_profile(directory, 2)
    return config, images, dis, proof


def reference_af(directory=DEFAULT_AF_BASELINE):
    directory = Path(directory).resolve()
    audit_frozen_gates()
    # Always revalidate frozen source, even when artifact inspection is cached.
    af_parent.audit_sources(PARENT_ROOT)
    result = _reference_af_cached(directory, _reference_signature(directory))
    return copy.deepcopy(result)


def _private_objects(image, base, limit):
    return {record['name']: record['bytes'] for record in image['sized_symbol_records']
            if record['type'] == 1 and record['bytes'] and
            base + P2 <= record['address'] < limit + P2}


def compare_retained(directory, config, images, dis, baseline=DEFAULT_AF_BASELINE, *, discover=False):
    old_config, old_images, old_dis, proof = reference_af(baseline)
    profile = int(config['DATA_TUNING_PROFILE'])
    identities, differences, new_names, names_by_image, objects = {}, {}, {}, {}, {}
    for image_name, base, limit in (('resident-sci', LOW, LOW_LIMIT),
                                  ('worker', HIGH, HIGH_LIMIT), ('stage', STAGE, STAGE_LIMIT)):
        before_code = Linked(old_images[image_name], base, old_dis[image_name])
        code = Linked(images[image_name], base, dis[image_name])
        old_names = schedule.u.function_symbols((Path(baseline) / (image_name + '.elf')).read_bytes())
        names = schedule.u.function_symbols((Path(directory) / (image_name + '.elf')).read_bytes())
        if old_names - names:
            raise ValueError('AF DATA tuning removed retained AF functions: ' + ', '.join(sorted(old_names - names)))
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
                differences[image_name][name] = {'AF': before, 'candidate': after}
        old_objects = _private_objects(old_images[image_name], base, limit)
        actual_objects = _private_objects(images[image_name], base, limit)
        if actual_objects != old_objects:
            raise ValueError('AF DATA tuning changed exact retained private object inventory: ' + image_name)
        objects[image_name] = actual_objects
    if not discover and (any(differences.values()) or any(new_names.values())):
        raise ValueError('AF DATA tuning changed retained opcode/literal identity or gained functions: ' +
                         json.dumps({'mismatches': differences, 'new': new_names}, sort_keys=True))
    return {'baseline_build': AF_BUILD, 'baseline_runtime': proof['runtime'],
        'baseline_gate': 'frozen ad-followup-source/tools/toy_ad_followup_audit.py, profile2',
        'baseline_admitted_by_original_gate': True,
        'U_audio_reference': proof['retained']['U_audio_reference'],
        'function_identities': identities, 'retained_mismatches': differences,
        'new_functions': new_names, 'functions': names_by_image,
        'exact_retained_private_object_inventories': objects}


def reviewed_shapes(config, images, dis, retained):
    profile = int(config['DATA_TUNING_PROFILE'])
    if not REVIEWED[profile] or not REVIEWED_STACK_ROWS[profile]:
        raise ValueError('Missing independently reviewed AF DATA tuning identities/frames')
    result = {}
    for image_name, base in (('resident-sci', LOW), ('worker', HIGH), ('stage', STAGE)):
        expected = REVIEWED[profile].get(image_name, {})
        if (set(expected) != changed_functions(profile, image_name) or
                retained['functions'][image_name] != set(expected) | set(retained['function_identities'][image_name])):
            raise ValueError('AF DATA tuning missing/unreviewed functions in ' + image_name)
        code = Linked(images[image_name], base, dis[image_name])
        actual = {name: function_identity(code, name, config['BUILD'], retained=False) for name in expected}
        if actual != expected:
            raise ValueError('Changed independently reviewed AF DATA tuning body: ' + image_name)
        result[image_name] = actual
    return result


def source_identities(root=ROOT):
    return {name: hashlib.sha256((Path(root) / name).read_bytes()).hexdigest() for name in REVIEW_SOURCE_FILES}


def audit_sources(root=ROOT):
    if set(REVIEWED_SOURCE_SHA256) != set(REVIEW_SOURCE_FILES):
        raise ValueError('Missing independently reviewed AF DATA tuning sources')
    observed = source_identities(root)
    if observed != REVIEWED_SOURCE_SHA256:
        raise ValueError('Changed independently reviewed AF DATA tuning source')
    retained = {name: expected for name, expected in af_parent.REVIEWED_SOURCE_SHA256.items()
                if name not in CHANGED_SOURCE_FILES}
    if any(observed[name] != expected for name, expected in retained.items()):
        raise ValueError('AF DATA tuning changed retained AF runtime source')
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
            raise ValueError('AF DATA tuning changed frozen low reader/CDDA source: ' + name)
        frozen[name] = expected
    return {'reviewed_tuning_sha256': observed, 'retained_AF_source_sha256': retained,
            'frozen_low_CDDA_sha256': frozen, 'immutable_parent_gate_sha256': audit_frozen_gates()}


def budget_metadata(profile):
    name,blocks,ticks,steps=PROFILES[profile]
    return {'name':name,'profile':profile+8,'credit_limit':blocks,
        'credit_unit':'verified 512-byte physical card blocks','tick_limit':ticks,
        'tick_hz':781250,'admission_microseconds':ticks*1000000/781250,
        'finite_step_limit':steps,'one_step_may_cross_tick_limit':True,
        'intervening_IRQ_and_one_step_work_count_toward_block_limit':True}


def audio_policy(profile):
    if profile not in PROFILES:
        raise ValueError('Unknown DATA tuning audio policy')
    return {'refill_quanta':4,'service_ticks':12500,'tick_hz':781250,
        'admission_microseconds':16000,'one_quantum_may_cross_tick_limit':True,
        'changed_from_AF':False,'raw_sector_reader_and_ring_queue_algorithm_retained':True}


def audit_schema(profile, root=ROOT):
    if profile not in PROFILES:
        raise ValueError('Unknown DATA tuning schema profile')
    cc = shutil.which('cc') or shutil.which('gcc')
    if not cc:
        raise ValueError('Host preprocessor required for guarded AF DATA tuning schema')
    _, blocks, ticks, steps = PROFILES[profile]
    definitions = [f'-DKUI_TOY_PILOT_DATA_TUNING_PROFILE={profile}',
        '-DKUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=2',
        '-DKUI_TOY_PILOT_DATA_BUDGET_PROFILE=4', '-DKUI_TOY_PILOT_DATA_SCHEDULE_MODE=2',
        '-DKUI_TOY_PILOT_PRIVATE_P2=1', '-DKUI_TOY_PILOT_LOADER_TRACE=1',
        '-DKUI_TOY_PILOT_DATA_PROBE=1', '-DKUI_TOY_PILOT_GD_FIXED_STEP=2']
    expected = {'KUI_TOY_SCHEDULE_REPORT_VERSION': 2, 'KUI_TOY_SCHEDULE_REPORT_WORDS': 192,
        'KUI_TOY_SCHEDULE_RECORD_WORDS': 18, 'KUI_TOY_SCHEDULE_RECORDS': 8,
        'KUI_TOY_SCI_SERVICE_BLOCKS': blocks, 'KUI_TOY_SCI_SERVICE_TICKS': ticks,
        'KUI_TOY_SCI_SERVICE_STEPS': steps, 'KUI_TOY_AD_BUDGET_REPORT_PROFILE': profile + 8,
        'KUI_TOY_TUNING_REPORT_PROFILE': profile + 8,
        'KUI_TOY_AD_REFILL_QUANTA': 4, 'KUI_TOY_AD_REFILL_TICKS': 12500}
    source = '#include "kui/toy_loader_schedule_report.h"\n#include "toy_pilot_sci.h"\n'
    for name, value in expected.items():
        source += f'#if {name} != {value}u\n#error DATA tuning schema constant changed: {name}\n#endif\n'
    completed = subprocess.run([cc, '-E', '-x', 'c',
        '-I' + str(Path(root) / 'include'), '-I' + str(Path(root) / 'src/loader'),
        *definitions, '-'], input=source, text=True, capture_output=True)
    if completed.returncode:
        raise ValueError('AF DATA tuning schema constant/guard changed: ' + completed.stderr.strip())
    return {'magic': '0x4c534331', 'version': 2, 'words': 192,
        'header_words': 16, 'engine_words': 32, 'records': 8, 'record_words': 18,
        'record_start': 48, 'budget_stats_start': 36, 'budget_stats_words': 12,
        'reported_budget_profile': profile + 8, 'TRACE_words': 416,
        'record_words_16_17': ['IRQ verified-block delta', 'retry delta'],
        'publication_checksum': 'first credited sector CRC32',
        'TRACE_delivered_sector_unit': 'credited logical sector bytes, persistent across IRQ work'}


def candidate(builddir, baseline=DEFAULT_AF_BASELINE, profile=None):
    directory = Path(builddir).resolve()
    config, images, dis = load_profile(directory, profile)
    profile = int(config['DATA_TUNING_PROFILE'])
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
        'profile': PROFILES[profile][0], 'data_tuning_profile': profile, 'followup_profile': 2, 'budget_profile': profile + 8,
        'budget': budget_metadata(profile), 'audio_policy': audio_policy(profile),
        'runtime': runtime, 'retained': retained, 'functions': evidence,
        'stack_rows': schedule.stack_inventory(directory), 'source_sha256': source_identities()}


def audit_tuning(builddir, baseline=DEFAULT_AF_BASELINE, profile=None):
    directory = Path(builddir).resolve()
    config, images, dis = load_profile(directory, profile)
    profile = int(config['DATA_TUNING_PROFILE'])
    runtime = schedule.previous.bind_runtime(directory, config, images)
    if not REVIEWED_RUNTIME_SHA256[profile] or runtime['sha256'] != REVIEWED_RUNTIME_SHA256[profile]:
        raise ValueError('AF DATA tuning runtime is not the independently reviewed immutable artifact')
    retained = compare_retained(directory, config, images, dis, baseline)
    reviewed = reviewed_shapes(config, images, dis, retained)
    inventory = schedule.stack_inventory(directory)
    if schedule.stack_signatures(inventory) != REVIEWED_STACK_ROWS[profile]:
        raise ValueError('AF DATA tuning emitted static stack inventory changed or missing')
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
    return {'admitted': True, 'profile': PROFILES[profile][0], 'data_tuning_profile': profile, 'followup_profile': 2,
        'budget_profile': profile + 8, 'build': config['BUILD'], 'config': config,
        'budget': budget_metadata(profile), 'audio_policy': audio_policy(profile), 'runtime': runtime,
        'retained': retained, 'reviewed_functions': reviewed, 'sources': audit_sources(),
        'owned_objects': objects, 'schedule_schema': audit_schema(profile),
        'report_geometry': budget_parent.audit_report_geometry(worker),
        'data_routing': {'asynchronous_startup_cooked_sector_bytes': 2048,
            'retained_dispatch_identity': retained['function_identities']['worker']['_dispatch_body'],
            'raw_and_post_PLAY_reader': 'original synchronous low reader retained against AF, AD, Z and U'},
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
    parser.add_argument('--baseline', type=Path, default=DEFAULT_AF_BASELINE)
    parser.add_argument('--profile', type=int, choices=tuple(PROFILES))
    parser.add_argument('--candidate', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = (candidate if args.candidate else audit_tuning)(args.builddir, args.baseline, args.profile)
    encoded = json.dumps(result, indent=2, sort_keys=True) + '\n'
    if args.output:
        args.output.write_text(encoded)
    else:
        print(encoded, end='')
