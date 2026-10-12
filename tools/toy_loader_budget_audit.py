#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Separate fail-closed admission for the four startup DATA budget profiles.

The older scheduling gate and its admitted B/Y/Z artifacts remain immutable.
Candidate mode emits linked review evidence and never fills acceptance tables.
The physical-block allowance and TMU step allowance are admission limits, not
hard execution deadlines: one finite stream step may cross the tick allowance.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

import toy_loader_schedule_audit as schedule
import toy_loader_trace_audit as trace
from check_loader_layout import inspect_elf
from check_retail_instructions import audit as instruction_audit
from package_cdda_toy_pilot import linked_symbol_sizes
from toy_pilot_cache_audit import Linked, audit_cache_layout, physical

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_Z_BASELINE = ROOT.parent / 'scheduling-source/build/schedule-z'
DEFAULT_U_BASELINE = ROOT.parent / 'overnight-source/build/toy-u-reference'
LOW, LOW_LIMIT, HIGH, HIGH_LIMIT = schedule.LOW, schedule.LOW_LIMIT, schedule.HIGH, schedule.HIGH_LIMIT
STAGE, STAGE_LIMIT, P2 = schedule.STAGE, schedule.STAGE_LIMIT, schedule.P2
Z_BUILD = '143d51bf7602'
Z_RUNTIME_SHA256 = 'f58f8dad7f51233f01088f68eb09fa78a7228aea3489ce8b62c12d691a77a1ef'
REQUIRED_CONFIG = {**schedule.REQUIRED_CONFIG, 'DATA_SCHEDULE_MODE': '2'}
PROFILES = {1: ('AA', 4, 1500), 2: ('AB', 12, 1500),
            3: ('AC', 4, 6000), 4: ('AD', 12, 6000)}
# Independently reviewed final e680281d16 builds. SCI service emits exact
# block/step/time/token exit branches and only private saturating counters.
# SCHED guards slot<8 and uses72-byte records; its snapshots end at word48.
# TRACE's new cooked-sector crossings use shifts and retain the closed old
# passive call graph. All other low/audio/installer bodies retain Z/U identity.
# There is deliberately no bless/update acceptance option.
_REVIEWED_COMMON_WORKER = {
    '_kui_toy_loader_schedule_report_begin': 'b482adedacacdcf7832597fde0abd3b8de6e3e11a8d6db06cb9601cef2fced28',
    '_kui_toy_loader_schedule_report_end': '30a5c0658fda6303448b9ceac201dab535b7ac2e52e184b313f10428b1484643',
    '_kui_toy_loader_schedule_report_freeze': 'deb3bb37ae0b1380e8696f6a1730f4810cd22cf539dfa8a02d68d2d8ba2c8b3c',
    '_kui_toy_loader_trace_end': 'b51e107b4fe373e2c705105ef6e2faaedb9785f8463a0f346b49739c1455a10b',
    '_kui_toy_loader_trace_report_page': '4ba07ac530c4e4430a35dc1c96ffe14f767ec5e94e76051b64d4a8b129cb7252',
    '_kui_toy_pilot_sci_budget_snapshot': 'ba1a7a3364bd2fcf09d52bf5f88f6df9d3c96c923cca851defccf5d2fae1075f',
    '_schedule_counter_delta': 'da8607be8b9d6628721547c3d0c7d7545be4163c3792daffcef7c5452f000f04',
    '_schedule_initialize': '8612ccaa4e8858a6b2eb21b16d119f829da710a3897a8c8d1030912e0cc341e2',
    '_schedule_record': '52a9c6d179b7925a59ddde54e6236fe122a25fa348ae765a61ce31122b938eb9',
    '_sci_budget_add': '70f8c15fac9fbfa8ed6b0a6ba2f5b157c25131b95c4022ab2b6129d91d06a608',
}
REVIEWED = {
    1: {'resident-sci': {}, 'stage': {}, 'worker': {**_REVIEWED_COMMON_WORKER,
        '_kui_toy_loader_trace_terminal': '15aad82a37a42368c1e15165423a0060dad1c8361bd7b97250fa7392ef40cae3',
        '_kui_toy_pilot_sci_init': '216af1f9562b3984cc2719e1bea07b7045cd8e3c9605311e010d6f8e027da200',
        '_kui_toy_pilot_sci_service': '4d34fd88c5a2750a3eb58b2c86c429c80071150286343068d4e5688db68ca603'}},
    2: {'resident-sci': {}, 'stage': {}, 'worker': {**_REVIEWED_COMMON_WORKER,
        '_kui_toy_loader_trace_terminal': '22673cc4c35e54c2fb499ed55884997f3c2295ea8db8df2c410b607aa6a81315',
        '_kui_toy_pilot_sci_init': '1a4e446a6d1b3624ce665c6a32c172105b656e43bab8e714883a40580154ecc3',
        '_kui_toy_pilot_sci_service': '639f661715050a1ab3ed4c08f4bad2595ee53b6af75a7578318e276822860a07',
        '_data_work': '6625880f3677ae5bb177f54d98c4da3506960858d700107fc3b2d77099824839'}},
    3: {'resident-sci': {}, 'stage': {}, 'worker': {**_REVIEWED_COMMON_WORKER,
        '_kui_toy_loader_trace_terminal': '22673cc4c35e54c2fb499ed55884997f3c2295ea8db8df2c410b607aa6a81315',
        '_kui_toy_pilot_sci_init': '22f7b90ac80a24eaaafdd7537be5a8c78772a3975887ff0491c864562e123c46',
        '_kui_toy_pilot_sci_service': '5cbf60745c83b32a81b07cb2176fec734e546c264c59f5479b1ee578ba6dd713'}},
    4: {'resident-sci': {}, 'stage': {}, 'worker': {**_REVIEWED_COMMON_WORKER,
        '_kui_toy_loader_trace_terminal': '22673cc4c35e54c2fb499ed55884997f3c2295ea8db8df2c410b607aa6a81315',
        '_kui_toy_pilot_sci_init': '550984582117ccc93a9e7443318f1ad19859fba1da40414d749c8caa3230eb63',
        '_kui_toy_pilot_sci_service': 'c186cf3f7578e2b8884555a7b2cbda89e15fd440f6f61f0be4c69cf44d994cfd',
        '_data_work': '6625880f3677ae5bb177f54d98c4da3506960858d700107fc3b2d77099824839'}},
}
REVIEWED_OBJECTS = {profile: copy.deepcopy(schedule.REVIEWED_OBJECTS[2]) for profile in PROFILES}
for _objects in REVIEWED_OBJECTS.values():
    _objects['worker'].update({'_e': 1376, '_schedule_report': 768, '_schedule_visit': 56})
REVIEWED_STACK_ROWS = {profile: {**schedule.REVIEWED_STACK_ROWS[2],
    'worker/src/loader/toy_loader_schedule_report.su': '15721fe1db55a792d972f9d30f69afbf768feb17931caa8380725eaf2affda24',
    'worker/src/loader/toy_pilot_sci.su': '41222b11093a722d05ee419311fa04021077f1baebba479036638f8e214efe7c'} for profile in PROFILES}
REVIEWED_RUNTIME_SHA256 = {
    1: '930e8223fee1e1a6c74356591b65c9e789a0031b61b475c74cfbfc2c18c50797',
    2: '8969d401b96ab9560ec34ba027bc94f1d47f2efcee50f5e419e8c437d54b53c5',
    3: '22032b16e3dcdb808c9c43c25ea05c8a9d4bdca4bc9c4f7453ef14c99ed02d7a',
    4: '8f478c473f2574409a5a0d346237a1b09995d350d960f3150a8f3ebaf716a57a',
}
REVIEWED_SOURCE_SHA256 = {
    'Makefile.toy_pilot': '0e11cccf49da1cf5eae2a96b8dbcb09a7383c8464f169a367e9e5428e7bff51b',
    'include/kui/toy_loader_schedule.h': '202b6b8d36990dc7c871b4e48800d7187ecddb44b7d6218081ccd1b901c4fb0b',
    'include/kui/toy_loader_schedule_report.h': 'c59e3f751f760692100e21ba10869d68d32f02686ac64bd2d9e6c861ea9e4eff',
    'src/loader/toy_pilot_sci.c': '1b022c0b585fa19bc9985d9afe75c7279334df02af3401b1277b323591199ff8',
    'src/loader/toy_pilot_sci.h': 'ef11141bc1ac3a179bc7407b68be3f44a485cad8d3c8d60bb862464c4906f746',
    'src/loader/toy_loader_schedule_report.c': '75e44977fa46e1c6262354a8f9dc36cb0d047acc32980d3900ef190ce4967b8b',
    'src/loader/toy_loader_trace.c': '9b16e64a1cf311bc99c26eed5e3e3d02c46ee5781f5bda041b8fcdd1e01d5e93',
    'src/loader/retail_resident.c': 'de823b3ba5d9f337be6a48dae223cf1842db9c044f124135b19c85f8a4845458',
    'src/loader/toy_pilot_gd.c': 'f00450c6c56e0ccfc61b5c46beb8efbb76698e893a7ff39920d72f7f0af9074f',
    'src/loader/toy_loader_trace_report.c': 'c787f102013956c3c64c55c26f4a1c5f7f9ca4894120ddfcbd49727a68ef9853',
    'src/loader/toy_pilot_worker.c': '91d83437adf019b5a16fff5bb16adb56c12f186e0d62c9e668331844043458b8',
    'src/loader/toy_pilot_resident.c': 'a1e67d0fd91cef3187c6bc64ec0d4fc55f185d22a705debd66b74ab9fc5f14dc',
}
REVIEW_SOURCE_FILES = (
    'Makefile.toy_pilot', 'include/kui/toy_loader_schedule.h',
    'include/kui/toy_loader_schedule_report.h',
    'src/loader/toy_pilot_sci.c', 'src/loader/toy_pilot_sci.h',
    'src/loader/toy_loader_schedule_report.c', 'src/loader/toy_loader_trace.c',
    'src/loader/retail_resident.c', 'src/loader/toy_pilot_gd.c',
    'src/loader/toy_loader_trace_report.c', 'src/loader/toy_pilot_worker.c',
    'src/loader/toy_pilot_resident.c',
)
CHANGED_WORKER = {
    '_kui_toy_pilot_sci_init', '_kui_toy_pilot_sci_service',
    '_schedule_initialize', '_schedule_record',
    '_kui_toy_loader_schedule_report_begin', '_kui_toy_loader_schedule_report_end',
    '_kui_toy_loader_schedule_report_freeze', '_kui_toy_loader_trace_end',
    '_kui_toy_loader_trace_report_page', '_kui_toy_loader_trace_terminal',
}
# The e tail gains exactly 48 stats bytes and alignment may round the object.
# The actual final size and every owned object are independently pinned below.
RESIZED_OBJECTS = {'worker': {'_e', '_schedule_report', '_schedule_visit'}}


def changed_functions(profile, image_name):
    if image_name != 'worker':
        return set()
    return CHANGED_WORKER | ({'_data_work'} if profile in (2, 4) else set())


def function_identity(code, name, build, *, retained=True):
    """Canonicalize two exact historical vector/protocol relocation operands.

    VBR points 0x100 before the independently checked SCI vector object, so
    generic object-interior matching mistakenly assigns that address to e.
    GET_VERS includes a binary 0x02 byte before NUL and is not a plain string.
    Only the checked original operand at its exact instruction offset gains a
    logical identity here. All opcodes and every other literal remain strict.
    The older scheduling gate and its global symbol bindings are untouched.
    """
    rules = {'_hook': (0x4, 7, 'historical_SCI_vector_base'),
             '_unhook': (0x10, 1, 'historical_SCI_vector_base'),
             '_kui_retail_gd_dispatch': (0x712, 5, 'historical_GD_GET_VERS')}
    if code.base != HIGH or name not in rules:
        return schedule.function_identity(code, name, build, retained=retained)
    offset, register, label = rules[name]
    begin, _, _ = schedule.bounded_function(code, name)
    if (code.half(begin + offset) & 0xFF00) != (0xD000 | register << 8):
        raise ValueError('Historical scoped relocation opcode/register changed: ' + name)
    _, value = code.literal(begin + offset, register, 4)
    if label == 'historical_SCI_vector_base':
        if value != code.symbols['_kui_toy_pilot_sci_vectors'] - 0x100:
            raise ValueError('Historical SCI handoff lost exact vectors-minus-0x100 VBR base')
        size = 0
    else:
        expected = bytes.fromhex('4744432056657273696f6e20312e313020313939392d30332d33310200')
        if (not any(a <= value < value + len(expected) <= b for a, b in code.image.get('readonly_ranges', [])) or
                code.data(value, len(expected)) != expected):
            raise ValueError('Historical high GD GET_VERS protocol bytes changed')
        size = len(expected)
    tag = '_budget_reviewed_' + label
    if any(r['name'] == tag for r in code.image.get('sized_symbol_records', [])):
        raise ValueError('Ambiguous scoped relocation review anchor')
    scoped = copy.copy(code)
    scoped.image = {**code.image, 'sized_symbol_records': [*code.image.get('sized_symbol_records', []),
        {'name': tag, 'address': value, 'bytes': size, 'type': 1, 'occurrence': 0}]}
    return schedule.function_identity(scoped, name, build, retained=retained)


def read_config(directory, profile=None):
    config = {}
    for line in (Path(directory) / 'build-config').read_text().splitlines():
        if '=' not in line:
            raise ValueError('Malformed DATA budget configuration')
        key, value = line.split('=', 1)
        if key in config:
            raise ValueError('Duplicate DATA budget configuration: ' + key)
        config[key] = value
    if set(config) != set(REQUIRED_CONFIG) | {'BUILD', 'DATA_BUDGET_PROFILE'}:
        raise ValueError('DATA budget configuration has missing/unreviewed keys')
    if any(config.get(key) != value for key, value in REQUIRED_CONFIG.items()):
        raise ValueError('DATA budgets must retain startup mode 2 and exact synchronous CDDA/cache configuration')
    if config['DATA_BUDGET_PROFILE'] not in tuple(str(p) for p in PROFILES):
        raise ValueError('DATA budget profile must be 1, 2, 3 or 4')
    if profile is not None and config['DATA_BUDGET_PROFILE'] != str(profile):
        raise ValueError('DATA budget profile differs from linked configuration')
    if not re.fullmatch(r'[0-9a-f]{12}', config.get('BUILD', '')):
        raise ValueError('DATA budget build must be twelve hexadecimal digits')
    return config


def load_profile(directory, profile=None):
    directory = Path(directory).resolve()
    config = read_config(directory, profile)
    objdump = shutil.which('sh-elf-objdump')
    if not objdump:
        raise ValueError('SH objdump is required for DATA budget admission')
    images, dis = {}, {}
    for name, base, limit in (('resident-sci', LOW, LOW_LIMIT),
                             ('worker', HIGH, HIGH_LIMIT), ('stage', STAGE, STAGE_LIMIT)):
        raw = (directory / (name + '.elf')).read_bytes()
        if name != 'stage':
            schedule.reject_ambiguous_symbols(raw)
        options = {'private_p2': True} if name != 'stage' else {}
        if name == 'worker':
            options.update(entry_symbol='_kui_toy_pilot_initialize', entry_at_base=False)
        image = inspect_elf(raw, base, limit, **options)
        image['symbol_sizes'] = linked_symbol_sizes(raw)
        image['readonly_ranges'] = schedule.u.readonly_ranges(raw)
        image['sized_symbol_records'] = schedule.sized_symbol_records(raw)
        if (directory / (name + '.bin')).read_bytes() != image['payload']:
            raise ValueError(name + '.bin differs from actual DATA budget ELF bytes')
        images[name] = image
        dis[name] = subprocess.check_output([objdump, '-d', str(directory / (name + '.elf'))], text=True)
    schedule.attach_linked_symbols(images)
    return config, images, dis


def reference_z(directory=DEFAULT_Z_BASELINE):
    """Bind historical Z, including its metadata, without current-source gates."""
    directory = Path(directory).resolve()
    config, images, dis = schedule.load_profile(directory, 2)
    if config['BUILD'] != Z_BUILD:
        raise ValueError('DATA budget reference must be immutable historical Z')
    binding = schedule.previous.bind_runtime(directory, config, images)
    if binding['sha256'] != Z_RUNTIME_SHA256:
        raise ValueError('DATA budget reference is not immutable historical Z runtime')
    retained = schedule.compare_retained(directory, config, images, dis, DEFAULT_U_BASELINE)
    schedule.reviewed_shapes(config, images, dis, retained)
    if schedule.stack_signatures(schedule.stack_inventory(directory)) != schedule.REVIEWED_STACK_ROWS[2]:
        raise ValueError('Historical Z stack evidence changed or missing')
    for name, expected in schedule.REVIEWED_OBJECTS[2].items():
        base, limit, prefix = ((HIGH, HIGH_LIMIT, '__toy_pilot') if name == 'worker'
                               else (LOW, LOW_LIMIT, '__retail_resident'))
        schedule.audit_owned_objects(images[name], expected, prefix, base, limit)
    return config, images, dis, binding


def compare_retained(directory, config, images, dis, baseline=DEFAULT_Z_BASELINE, *, discover=False):
    old_config, old_images, old_dis, binding = reference_z(baseline)
    profile = int(config['DATA_BUDGET_PROFILE'])
    names_by_image, identities, differences, new_names = {}, {}, {}, {}
    for image_name, base in (('resident-sci', LOW), ('worker', HIGH), ('stage', STAGE)):
        before_code = Linked(old_images[image_name], base, old_dis[image_name])
        code = Linked(images[image_name], base, dis[image_name])
        old_names = schedule.u.function_symbols((Path(baseline) / (image_name + '.elf')).read_bytes())
        names = schedule.u.function_symbols((Path(directory) / (image_name + '.elf')).read_bytes())
        missing = old_names - names
        if missing:
            raise ValueError('DATA budget removed historical Z functions: ' + ', '.join(sorted(missing)))
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
                differences[image_name][name] = {'Z': before, 'candidate': after}
        for name, size in old_images[image_name]['symbol_sizes'].items():
            at = old_images[image_name]['symbols'].get(name, 0)
            limit = HIGH_LIMIT if image_name == 'worker' else LOW_LIMIT
            if size and base + P2 <= at < limit + P2:
                if name not in RESIZED_OBJECTS.get(image_name, set()) and images[image_name]['symbol_sizes'].get(name) != size:
                    raise ValueError('DATA budget changed retained private object size: ' + name)
    if not discover and any(differences.values()):
        raise ValueError('DATA budget changed retained opcode/literal identity: ' + json.dumps(differences, sort_keys=True))
    return {'baseline_build': Z_BUILD, 'baseline_runtime': binding,
            'U_audio_reference': {'build': schedule.previous.U_BUILD, 'sha256': schedule.previous.U_RUNTIME_SHA},
            'function_identities': identities, 'retained_mismatches': differences,
            'new_functions': new_names, 'functions': names_by_image}


def reviewed_shapes(config, images, dis, retained):
    profile = int(config['DATA_BUDGET_PROFILE'])
    if not REVIEWED[profile] or not REVIEWED_OBJECTS[profile] or not REVIEWED_STACK_ROWS[profile]:
        raise ValueError('Missing independently reviewed DATA budget identities/objects/frames')
    result = {}
    for image_name, base in (('resident-sci', LOW), ('worker', HIGH), ('stage', STAGE)):
        code = Linked(images[image_name], base, dis[image_name])
        expected = REVIEWED[profile].get(image_name, {})
        if retained['functions'][image_name] != set(expected) | set(retained['function_identities'][image_name]):
            raise ValueError('DATA budget gained missing/unreviewed functions in ' + image_name)
        actual = {name: function_identity(code, name, config['BUILD'], retained=False) for name in expected}
        if actual != expected:
            raise ValueError('Changed independently reviewed DATA budget body: ' + image_name)
        result[image_name] = actual
    return result


def source_identities(root=ROOT):
    return {name: hashlib.sha256((Path(root) / name).read_bytes()).hexdigest() for name in REVIEW_SOURCE_FILES}


def audit_sources(root=ROOT):
    if set(REVIEWED_SOURCE_SHA256) != set(REVIEW_SOURCE_FILES):
        raise ValueError('Missing independently reviewed DATA budget sources')
    observed = source_identities(root)
    if observed != REVIEWED_SOURCE_SHA256:
        raise ValueError('Changed independently reviewed DATA budget source')
    frozen = {}
    for name, expected in schedule.FROZEN_SOURCES.items():
        if name == 'src/loader/toy_loader_trace.c':
            continue
        raw = (Path(root) / name).read_bytes()
        if name == 'src/loader/sci_sd_bus.c':
            addition = re.compile(rb'^#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 2\n/\* Startup-only typed export\.[\s\S]*?^#endif\n', re.M)
            matches = list(addition.finditer(raw))
            if len(matches) != 1 or b'bool __attribute__((noipa)) kui_toy_pilot_startup_sci_healthy(void)' not in matches[0].group():
                raise ValueError('Missing/ambiguous historical startup health export')
            raw = addition.sub(b'', raw)
        actual = hashlib.sha256(raw).hexdigest()
        if actual != expected:
            raise ValueError('DATA budget changed frozen low reader/CDDA source: ' + name)
        frozen[name] = actual
    return {'reviewed_budget_sha256': observed, 'frozen_low_CDDA_sha256': frozen}


def audit_budget_schema(profile, root=ROOT):
    if profile not in PROFILES:
        raise ValueError('Unknown DATA budget schema profile')
    cc = shutil.which('cc') or shutil.which('gcc')
    if not cc:
        raise ValueError('Host preprocessor required to resolve guarded budget schema')
    definitions = [f'-DKUI_TOY_PILOT_DATA_BUDGET_PROFILE={profile}',
        '-DKUI_TOY_PILOT_DATA_SCHEDULE_MODE=2', '-DKUI_TOY_PILOT_PRIVATE_P2=1',
        '-DKUI_TOY_PILOT_LOADER_TRACE=1', '-DKUI_TOY_PILOT_DATA_PROBE=1',
        '-DKUI_TOY_PILOT_GD_FIXED_STEP=2']
    source = '#include "kui/toy_loader_schedule_report.h"\n#include "toy_pilot_sci.h"\n'
    output = subprocess.check_output([cc, '-dM', '-E', '-x', 'c',
        '-I' + str(Path(root) / 'include'), '-I' + str(Path(root) / 'src/loader'),
        *definitions, '-'], input=source, text=True, stderr=subprocess.STDOUT)
    _, blocks, ticks = PROFILES[profile]
    expected = {'KUI_TOY_SCHEDULE_REPORT_VERSION': 2, 'KUI_TOY_SCHEDULE_REPORT_WORDS': 192,
        'KUI_TOY_SCHEDULE_RECORD_WORDS': 18, 'KUI_TOY_SCHEDULE_RECORDS': 8,
        'KUI_TOY_SCI_SERVICE_BLOCKS': blocks, 'KUI_TOY_SCI_SERVICE_TICKS': ticks,
        'KUI_TOY_SCI_SERVICE_STEPS': 1024}
    for name, value in expected.items():
        if re.findall(r'^#define\s+' + name + r'\s+(\d+)u\b', output, re.M) != [str(value)]:
            raise ValueError('DATA budget schema constant changed: ' + name)
    return {'magic': '0x4c534331', 'version': 2, 'words': 192,
        'header_words': 16, 'engine_words': 32, 'records': 8, 'record_words': 18,
        'record_start': 48, 'budget_stats_start': 36, 'budget_stats_words': 12,
        'record_words_16_17': ['IRQ verified-block delta', 'retry delta'],
        'publication_checksum': 'first credited sector CRC32', 'TRACE_words': 416,
        'TRACE_delivered_sector_unit': 'credited logical sector bytes, persistent across IRQ work'}


def budget_metadata(profile):
    name, blocks, ticks = PROFILES[profile]
    return {'name': name, 'credit_limit': blocks, 'credit_unit': 'verified 512-byte physical card blocks',
        'tick_limit': ticks, 'tick_hz': 781250, 'admission_microseconds': ticks * 1000000 / 781250,
        'finite_step_limit': 1024, 'one_step_may_cross_tick_limit': True,
        'intervening_IRQ_and_one_step_work_count_toward_block_limit': True}


def audit_report_geometry(worker):
    """Bind the reviewed record indexing to actual private linked storage."""
    symbols, sizes = worker['symbols'], worker['symbol_sizes']
    begin, end = (symbols.get(name, 0) for name in ('__toy_pilot_bss_begin', '__toy_pilot_bss_end'))
    if not HIGH + P2 <= begin <= end <= HIGH_LIMIT + P2:
        raise ValueError('DATA budget reports escaped the high P2 lease')
    required = {'_schedule_report': 192 * 4, '_schedule_visit': 56,
                '_report': 416 * 4, '_trace_words': 416 * 4, '_request': 96}
    for name, size in required.items():
        at = symbols.get(name, 0)
        if sizes.get(name) != size or not begin <= at < at + size <= end:
            raise ValueError('DATA budget report lost exact private geometry: ' + name)
    # schedule_record() rejects slot>=8 before returning a pointer; the
    # reviewed bodies write only record words0..17. The last legal record's
    # final word ends exactly at the emitted 768-byte object boundary.
    last_record_word = 48 + (8 - 1) * 18 + 17
    if (last_record_word + 1) * 4 != sizes['_schedule_report']:
        raise ValueError('DATA budget chronology indexing exceeds linked report')
    return {'P2_owned_report_bytes': 768, 'last_legal_record_word': last_record_word,
        'record_index_guard': 'slot < 8', 'record_stride_words': 18,
        'budget_snapshot_interval_words': [36, 48],
        'chronology_interval_words': [48, 192], 'TRACE_storage_bytes': 1664,
        'geometry_closed_by_reviewed_source_and_emitted_function_identities': True}


def candidate(builddir, baseline=DEFAULT_Z_BASELINE, profile=None):
    directory = Path(builddir).resolve()
    config, images, dis = load_profile(directory, profile)
    profile = int(config['DATA_BUDGET_PROFILE'])
    retained = compare_retained(directory, config, images, dis, baseline, discover=True)
    evidence = {}
    for image_name, base in (('resident-sci', LOW), ('worker', HIGH), ('stage', STAGE)):
        code = Linked(images[image_name], base, dis[image_name])
        selected = (set(retained['new_functions'][image_name]) |
                    set(retained['retained_mismatches'][image_name]) | changed_functions(profile, image_name))
        evidence[image_name] = {}
        for name in sorted(selected & retained['functions'][image_name]):
            begin, end, rows = schedule.bounded_function(code, name)
            evidence[image_name][name] = {
                'identity': function_identity(code, name, config['BUILD'], retained=False),
                'bytes': end - begin,
                'instructions': [[hex(at), hex(op), mn, arg] for at, op, mn, arg in rows],
                'literals': [{'instruction': hex(at), 'value': hex(code.literal(at, op >> 8 & 15,
                     4 if op & 0xF000 == 0xD000 else 2)[1])}
                     for at, op, mn, _ in rows if op & 0xF000 in (0xD000, 0x9000) and mn in ('mov.l', 'mov.w')]}
    retained.pop('functions')
    return {'admitted': False, 'independent_review_required': True, 'config': config,
        'budget': budget_metadata(profile), 'runtime': schedule.previous.bind_runtime(directory, config, images),
        'retained': retained, 'functions': evidence, 'stack_rows': schedule.stack_inventory(directory),
        'source_sha256': source_identities(),
        'BSS_objects': {image_name: {name: {'address': hex(at), 'bytes': image['symbol_sizes'].get(name)}
            for name, at in image['symbols'].items() if base + P2 <= at < limit + P2 and image['symbol_sizes'].get(name)}
            for image_name, image, base, limit in (('resident-sci', images['resident-sci'], LOW, LOW_LIMIT),
                                                  ('worker', images['worker'], HIGH, HIGH_LIMIT))}}


def audit_budget(builddir, baseline=DEFAULT_Z_BASELINE, profile=None):
    directory = Path(builddir).resolve()
    config, images, dis = load_profile(directory, profile)
    profile = int(config['DATA_BUDGET_PROFILE'])
    runtime = schedule.previous.bind_runtime(directory, config, images)
    if not REVIEWED_RUNTIME_SHA256[profile] or runtime['sha256'] != REVIEWED_RUNTIME_SHA256[profile]:
        raise ValueError('DATA budget runtime is not the independently reviewed immutable artifact')
    retained = compare_retained(directory, config, images, dis, baseline)
    reviewed = reviewed_shapes(config, images, dis, retained)
    inventory = schedule.stack_inventory(directory)
    if schedule.stack_signatures(inventory) != REVIEWED_STACK_ROWS[profile]:
        raise ValueError('DATA budget emitted static stack inventory changed or missing')
    for name in images:
        try:
            instruction_audit(name, dis[name])
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
    return {'admitted': True, 'profile': PROFILES[profile][0], 'budget_profile': profile,
        'build': config['BUILD'], 'config': config, 'budget': budget_metadata(profile), 'runtime': runtime,
        'retained': retained, 'reviewed_functions': reviewed, 'sources': audit_sources(),
        'owned_objects': objects, 'schedule_schema': audit_budget_schema(profile),
        'report_geometry': audit_report_geometry(worker),
        'data_routing': {'asynchronous_startup_cooked_sector_bytes': 2048,
            'retained_dispatch_identity': retained['function_identities']['worker']['_dispatch_body'],
            'raw_and_post_PLAY_reader': 'original synchronous low reader retained against Z and U'},
        'TRACE_graph': audit_budget_trace_graph(code),
        'retained_DATA_probe': {'schema': schedule.previous.audit_schema(),
            'bindings': schedule.previous.audit_bindings(directory, low, code, 0),
            'passive_callgraph': schedule.previous.audit_probe_graph(code, low, 0)},
        'exports': trace.audit_exports(low, worker),
        'bridges': schedule.audit_schedule_bridges(worker, dis['worker'], 2),
        'publication': trace.audit_boot_publication(low, worker, stage, dis['resident-sci'], dis['stage']),
        'cache_audio_heap': audit_cache_layout(low, worker, stage, dis['resident-sci'], dis['worker'], dis['stage']),
        'stacks': schedule.audit_schedule_stacks(directory, images, dis),
        'emitted_frames': schedule.audit_emitted_frames(directory, images, dis, inventory),
        'startup_IRQ': schedule.audit_startup_irq(directory, worker, 2),
        'hardware_behavior_verified': False, 'audio_or_video_improvement_proven': False}


def audit_budget_trace_graph(code):
    # Async startup routing is cooked 2048-byte sectors only. The new
    # persistent-credit observer uses shifts and must retain the original
    # closed passive graph: no divider or storage callbacks are admitted.
    return trace.audit_trace_graph(code)




if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builddir', type=Path)
    parser.add_argument('--baseline', type=Path, default=DEFAULT_Z_BASELINE)
    parser.add_argument('--profile', type=int, choices=tuple(PROFILES))
    parser.add_argument('--candidate', action='store_true')
    args = parser.parse_args()
    print(json.dumps((candidate if args.candidate else audit_budget)(args.builddir, args.baseline, args.profile), indent=2, sort_keys=True))
