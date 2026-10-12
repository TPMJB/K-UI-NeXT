#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Separate fail-closed AM/AN SCI IRQ-progress admission over frozen AI.

Original tuning and older gates remain unchanged and validate the frozen AI
reference from its original source directory. Discovery never admits a new
candidate or fills independently reviewed pins. Geometry and all hard limits
remain those of the original admitted startup engine.
"""
import argparse
import copy
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

import toy_data_tuning_audit as tuning_parent

af_parent = tuning_parent.af_parent
budget_parent = tuning_parent.budget_parent
schedule = budget_parent.schedule
trace = budget_parent.trace
Linked = budget_parent.Linked
ROOT = Path(__file__).resolve().parents[1]
PARENT_ROOT = ROOT.parent / 'aeaf-tuning-source'
DEFAULT_AI_BASELINE = PARENT_ROOT / 'build/tuning-ai'
LOW, LOW_LIMIT, HIGH, HIGH_LIMIT = schedule.LOW, schedule.LOW_LIMIT, schedule.HIGH, schedule.HIGH_LIMIT
STAGE, STAGE_LIMIT, P2 = schedule.STAGE, schedule.STAGE_LIMIT, schedule.P2
AI_BUILD = '0251a9f05601'
AI_RUNTIME_SHA256 = '3121b5e2b6efb6a34a51cfdb5aecd6496362048859092e3fa4d3caeada85d26e'
REQUIRED_CONFIG = {**tuning_parent.REQUIRED_CONFIG, 'DATA_TUNING_PROFILE': '1'}
PROFILES = {1: ('AM', 24, 12000, 2048), 2: ('AN', 24, 12000, 1)}
FROZEN_GATE_SHA256 = {'check_loader_layout.py': '7551528fa4e86adc02903389f0b8c8266297ccbcb1d0984ddd04ac40cd473dea',
 'check_retail_instructions.py': '895788f228216a759318db11eb683a9c3b9516ead33fa8cc4448ce6e7309d348',
 'check_retail_loader_layout.py': '7b6a102856826fab03f8f317b81b1a8a694dbf2a355dd760f0d952913e18b356',
 'check_retail_stack.py': '4b388f7383d8daac171e4daeb61ca979f6c1f95799e198166d4f2da21aeb0f62',
 'package_cdda_calibration.py': 'a7ea0ac07319a317ba0f74f9b4710e4a365bf87a98723c6624d1c946c4e11c8c',
 'package_cdda_mixed.py': 'b3165bb7bff6225a32c10a79fd073fe4be4c1efa2d515b03b1cb6b10c90f8ef7',
 'package_cdda_preflight.py': 'a4e717005d50b2fa4bb40d5b1dc3a2be31fd7c9fc8c04400c0c7735f01e65188',
 'package_cdda_toy_pilot.py': '6f27e4f8b090a6934f9deede7b62e69adb7d13dab12e3fd487cd927cbf79258f',
 'retail_package.py': 'cbdeda2c5b6e9491d61512af29de34d4cb3c7f2be396b5c09adf2cca1c92f06f',
 'runtime_package.py': 'c885e08e5a167b02dcaa6884084d11ef4cad70161f423d0d8876d92330ecd0b5',
 'toy_ad_followup_audit.py': '84b2241a1d2350f392a3144f3d3eb5e4d3d08dbb6f35f29860ccf3caa313cf39',
 'toy_data_tuning_audit.py': '8d8952058379e0a0334228e2b1e6085f3fa2b7930671662bc0c55ed7bcaef5d0',
 'toy_loader_budget_audit.py': '10e2b58c9c81d9515ed5666eac55946325d8264ef1b21db0986c1f67290dd70e',
 'toy_loader_data_probe_audit.py': '4f092ff5098751f190a71373fdbc5389c4f104655de995206875d9db07c99882',
 'toy_loader_schedule_audit.py': 'bf2641ae40a475b296c3eae93eca0e5cac6186f05233e9bb18df2f45e5f957df',
 'toy_loader_sci_next_audit.py': '57b6b67b39b7292766e64e8c29cd71555a7629867aefb8fb7ccc255e5c8a48a9',
 'toy_loader_trace_audit.py': 'b3e5ab7ea3b189a0d60e56250aee986a028ae00ba0fb85dd448ced42cad4f932',
 'toy_pilot_cache_audit.py': '8f46d2a9acd0318ebea837e788b3f82f37692164a96ebdbbc0b4d3cdea26118d',
 'toy_pilot_pause_stack_audit.py': '2bb3ccafa2c2568ab4f505084ad4ae1038fc0d41be6314f817b6b793dcb6f266',
 'toy_pilot_scratch_audit.py': '11d642fb9c578dbe27d0f0aed5edaa30e8f9b03c05e8a52f4e9dd3f57c0688ad',
 'toy_shared_sci_audit.py': 'acc358cab79f3a0092295bf63d519b68d5040effae1393f7105750e471dcc788'}
REVIEW_SOURCE_FILES = (*tuning_parent.REVIEW_SOURCE_FILES,
    'src/loader/toy_pilot_sci_irq.S', 'include/kui/toy_loader_irq_progress.h',
    'src/loader/sci_sd_bus.c')
CHANGED_SOURCE_FILES = {'Makefile.toy_pilot', 'include/kui/toy_loader_ad_followup.h',
    'src/loader/toy_pilot_sci.c', 'src/loader/toy_pilot_sci.h',
    'src/loader/toy_pilot_sci_irq.S', 'include/kui/toy_loader_irq_progress.h'}
# Independently reviewed runtime877ef5e82f7f7484fb6d9589d83b643a2d7b62c1.
# AM changes only SCI priority immediates, the matching assembly rehook and
# report9→13. AN adds the emitted one-step boolean guard and report14. No new
# functions/objects or retained reader/audio/driver/cache/installer changes.
# All189 actual frames were checked; AN service alone falls44→40 bytes.
REVIEWED = {
    1: {'resident-sci': {}, 'stage': {}, 'worker': {
        '_hook': '84e5e605dc4a0aa6c9d005b99e5869a4c25344dffa6f418be24387b22d378d98',
        '_unhook': '74ed4fa28e9b8c042c2011000f4e6c1edddbd6239cfd8093b5ddf049d44a8959',
        '_kui_toy_pilot_sci_init': 'e31b7e151bec7f911c51a0a3cd93a432595cd5756589fb2a51543f3e2dcddfc6'}},
    2: {'resident-sci': {}, 'stage': {}, 'worker': {
        '_hook': '84e5e605dc4a0aa6c9d005b99e5869a4c25344dffa6f418be24387b22d378d98',
        '_unhook': '74ed4fa28e9b8c042c2011000f4e6c1edddbd6239cfd8093b5ddf049d44a8959',
        '_kui_toy_pilot_sci_init': '60ed8bc9d5cd04ecda936a3c39e7cd5974bfdd24a36f9a647d05f2b8e7b5c7b1',
        '_kui_toy_pilot_sci_service': 'a468fb3117a9dbfb9551a1794773241e0289f2db330e2d1929897e60a7ebb69f'}},
}
REVIEWED_STACK_ROWS = {profile: copy.deepcopy(tuning_parent.REVIEWED_STACK_ROWS[1])
                      for profile in PROFILES}
REVIEWED_STACK_ROWS[2]['worker/src/loader/toy_pilot_sci.su'] = \
    '1495eadad167189ea5c489be05c9df18bafb0435ac3587397bd569acf7ce0683'
REVIEWED_RUNTIME_SHA256 = {
    1: '5812f3a6534b9b642227d86a9b17fca850294c3c9ea44c704fdaa06e629b8bd8',
    2: 'a5c9e8a00d60ebd56399f6e83adfaefe7b1d2546da4e991d16306e7470dc4e2b',
}
REVIEWED_SOURCE_SHA256 = {
    'Makefile.toy_pilot': 'dd8025c7f796c56d3b83c76b3baf9e22805bacc05d7a39d34fa61cf6b3ee7691',
    'include/kui/toy_loader_schedule.h': '60b7ec6bb7df4357fca9d601b98496b76ee95b3e1015bec632974ee8bbeea93b',
    'include/kui/toy_loader_schedule_report.h': 'c59e3f751f760692100e21ba10869d68d32f02686ac64bd2d9e6c861ea9e4eff',
    'src/loader/toy_pilot_sci.c': 'ea599bde842a0064289bc7e2f915edc6d14a1617875274b9b49b46f5dfeddf2e',
    'src/loader/toy_pilot_sci.h': '581eace2da6621d53e4935cc33fa073b0b47153b561f37f944cb4fa017793c3a',
    'src/loader/toy_loader_schedule_report.c': '75e44977fa46e1c6262354a8f9dc36cb0d047acc32980d3900ef190ce4967b8b',
    'src/loader/toy_loader_trace.c': '9b16e64a1cf311bc99c26eed5e3e3d02c46ee5781f5bda041b8fcdd1e01d5e93',
    'src/loader/retail_resident.c': 'de823b3ba5d9f337be6a48dae223cf1842db9c044f124135b19c85f8a4845458',
    'src/loader/toy_pilot_gd.c': 'f00450c6c56e0ccfc61b5c46beb8efbb76698e893a7ff39920d72f7f0af9074f',
    'src/loader/toy_loader_trace_report.c': 'c787f102013956c3c64c55c26f4a1c5f7f9ca4894120ddfcbd49727a68ef9853',
    'src/loader/toy_pilot_worker.c': '0d3d341f971355c31a15cf1afd84f1230f6e4e779f41ff1b554d25f417f51863',
    'src/loader/toy_pilot_resident.c': 'a1e67d0fd91cef3187c6bc64ec0d4fc55f185d22a705debd66b74ab9fc5f14dc',
    'include/kui/toy_loader_ad_followup.h': 'dd36fc58065400cee70ea45f30e29ece32df0cf2ae1ca9c59eac89acd87e81b0',
    'include/kui/toy_loader_data_tuning.h': '491d35345fb593a2aea8a1bea38f57c5ea0d2ba564b8d4dedd16e6077d024481',
    'src/loader/toy_pilot_sci_irq.S': '4bb3f26261b891df160999de26e225076313d6a569bf6f3a1aeb91f15a93e0fb',
    'include/kui/toy_loader_irq_progress.h': '9282a09c9aedaabbd7a3b12faefa1b41bea6d59eb830723ff1c9bba80cdc6f03',
    'src/loader/sci_sd_bus.c': '98d21cbf25d37719c68fd89206c05edf99f5be188a8ac1ff4b3eb1c2ee5c4565',
}
REVIEWED_OBJECTS = {profile: copy.deepcopy(tuning_parent.REVIEWED_OBJECTS[1])
                    for profile in PROFILES}

def function_identity(code,name,build,*,retained=True):
    # The original immutable AF gate owns the exact ADv2 report-order anchor,
    # VBR/protocol relocation scopes and all opcode/literal contents checks.
    if code.base == HIGH and name == '___udivsi3':
        # GCC emits anonymous DIV1 steps in the34 bytes before this74-byte
        # function. AN relocates them together; exact BSR displacements do
        # not change, but the old generic external-target identity has no
        # symbol for that step. Bind both complete byte slices and the exact
        # targets−34,−32,−16. This helper has no referenced protocol/rodata.
        begin,end,rows=schedule.bounded_function(code,name)
        helper=bytes.fromhex('54345434543454345434543454340b0054345434244054342440543424400b005434')
        if (end-begin != 74 or code.image['symbol_sizes'].get(name) != 74 or
                code.data(begin-34,34) != helper or len(rows) != 37):
            raise ValueError('Exact GCC unsigned-division helper size/body changed')
        calls={0xe:-34,0x12:-32,0x1a:-34,0x1e:-32,0x34:-16,0x38:-16,0x3c:-16,0x40:-16}
        observed={}
        for at,op,mn,_ in rows:
            if code.half(at) != op:
                raise ValueError('Disassembly differs from actual GCC divider instruction')
            if (op & 0xf000 in (0x9000,0xd000) and mn in ('mov.w','mov.l') or
                    op & 0xf0ff in (0x400b,0x402b)):
                raise ValueError('GCC divider gained literal/indirect references')
            if op & 0xf000 in (0xa000,0xb000):
                target=at+4+trace._signed(op&0xfff,12)*2
                if op & 0xf000 != 0xb000 or target-begin != calls.get(at-begin):
                    raise ValueError('GCC divider external BSR target changed')
                observed[at-begin]=target-begin
        if observed != calls:
            raise ValueError('GCC divider exact owned-step call inventory changed')
        return hashlib.sha256(b'exact-GCC-udiv-and-owned-step\0'+helper+code.data(begin,74)).hexdigest()
    return tuning_parent.function_identity(code,name,build,retained=retained)


def changed_functions(profile, image_name):
    if profile not in PROFILES:
        raise ValueError('Unknown startup IRQ progress profile')
    if image_name != 'worker':
        return set()
    return {'_kui_toy_pilot_sci_init', '_hook', '_unhook'} | (
        {'_kui_toy_pilot_sci_service'} if profile == 2 else set())


def read_config(directory, profile=None):
    config = {}
    for line in (Path(directory) / 'build-config').read_text().splitlines():
        if '=' not in line:
            raise ValueError('Malformed startup IRQ progress configuration')
        key, value = line.split('=', 1)
        if key in config:
            raise ValueError('Duplicate startup IRQ progress configuration: ' + key)
        config[key] = value
    if 'STARTUP_IRQ_PROFILE' not in config:
        if profile not in (None, 0):
            raise ValueError('startup IRQ progress profile differs from linked configuration')
        return tuning_parent.read_config(directory, 1)
    if profile == 0:
        raise ValueError('Legacy profile0 must omit STARTUP_IRQ_PROFILE')
    if set(config) != set(REQUIRED_CONFIG) | {'BUILD', 'STARTUP_IRQ_PROFILE'}:
        raise ValueError('startup IRQ progress configuration has missing/unreviewed keys')
    if any(config.get(key) != value for key, value in REQUIRED_CONFIG.items()):
        raise ValueError('Startup IRQ profiles must retain complete AI reader/cache/audio configuration')
    if config['STARTUP_IRQ_PROFILE'] not in tuple(str(p) for p in PROFILES):
        raise ValueError('startup IRQ progress profile must be 1 or 2')
    if profile is not None and config['STARTUP_IRQ_PROFILE'] != str(profile):
        raise ValueError('startup IRQ progress profile differs from linked configuration')
    if not re.fullmatch(r'[0-9a-f]{12}', config.get('BUILD', '')):
        raise ValueError('startup IRQ progress build must be twelve hexadecimal digits')
    return config


def load_profile(directory, profile=None):
    directory = Path(directory).resolve()
    config = read_config(directory, profile)
    objdump = shutil.which('sh-elf-objdump')
    if not objdump:
        raise ValueError('SH objdump is required for startup IRQ progress admission')
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
            raise ValueError(name + '.bin differs from actual startup IRQ progress ELF bytes')
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
def _reference_ai_cached(directory, signature):
    del signature  # Its full content hashes invalidate the cache on any change.
    completed = subprocess.run([sys.executable,
        str(PARENT_ROOT / 'tools/toy_data_tuning_audit.py'), str(directory),
        '--profile', '1'], cwd=PARENT_ROOT, text=True, capture_output=True)
    if completed.returncode:
        raise ValueError('AI reference failed its original immutable gate: ' + completed.stderr.strip())
    proof = json.loads(completed.stdout)
    if (proof.get('admitted') is not True or proof.get('build') != AI_BUILD or
            proof.get('runtime', {}).get('sha256') != AI_RUNTIME_SHA256):
        raise ValueError('AI reference did not pass its original immutable gate')
    config, images, dis = tuning_parent.load_profile(directory, 1)
    return config, images, dis, proof


def reference_ai(directory=DEFAULT_AI_BASELINE):
    directory = Path(directory).resolve()
    audit_frozen_gates()
    # Always revalidate frozen source, even when artifact inspection is cached.
    tuning_parent.audit_sources(PARENT_ROOT)
    result = _reference_ai_cached(directory, _reference_signature(directory))
    return copy.deepcopy(result)


def _private_objects(image, base, limit):
    return {record['name']: record['bytes'] for record in image['sized_symbol_records']
            if record['type'] == 1 and record['bytes'] and
            base + P2 <= record['address'] < limit + P2}


def compare_retained(directory, config, images, dis, baseline=DEFAULT_AI_BASELINE, *, discover=False):
    old_config, old_images, old_dis, proof = reference_ai(baseline)
    profile = int(config['STARTUP_IRQ_PROFILE'])
    identities, differences, new_names, names_by_image, objects = {}, {}, {}, {}, {}
    for image_name, base, limit in (('resident-sci', LOW, LOW_LIMIT),
                                  ('worker', HIGH, HIGH_LIMIT), ('stage', STAGE, STAGE_LIMIT)):
        before_code = Linked(old_images[image_name], base, old_dis[image_name])
        code = Linked(images[image_name], base, dis[image_name])
        old_names = schedule.u.function_symbols((Path(baseline) / (image_name + '.elf')).read_bytes())
        names = schedule.u.function_symbols((Path(directory) / (image_name + '.elf')).read_bytes())
        if old_names - names:
            raise ValueError('startup IRQ progress removed retained AI functions: ' + ', '.join(sorted(old_names - names)))
        names_by_image[image_name] = names
        new_names[image_name] = sorted(names - old_names)
        identities[image_name], differences[image_name] = {}, {}
        for name in sorted(old_names - changed_functions(profile, image_name)):
            # Identical instruction bytes can still reference changed readonly
            # contents. Always validate the unchanged parent's exact protected
            # protocol/readonly scopes, retaining every opcode and literal.
            before = function_identity(before_code, name, old_config['BUILD'])
            after = function_identity(code, name, config['BUILD'])
            identities[image_name][name] = after
            if before != after:
                differences[image_name][name] = {'AI': before, 'candidate': after}
        old_objects = _private_objects(old_images[image_name], base, limit)
        actual_objects = _private_objects(images[image_name], base, limit)
        if actual_objects != old_objects:
            raise ValueError('startup IRQ progress changed exact retained private object inventory: ' + image_name)
        objects[image_name] = actual_objects
    if not discover and (any(differences.values()) or any(new_names.values())):
        raise ValueError('startup IRQ progress changed retained opcode/literal identity or gained functions: ' +
                         json.dumps({'mismatches': differences, 'new': new_names}, sort_keys=True))
    return {'baseline_build': AI_BUILD, 'baseline_runtime': proof['runtime'],
        'baseline_gate': 'frozen aeaf-tuning-source/tools/toy_data_tuning_audit.py, profile1',
        'baseline_admitted_by_original_gate': True,
        'U_audio_reference': proof['retained']['U_audio_reference'],
        'function_identities': identities, 'retained_mismatches': differences,
        'new_functions': new_names, 'functions': names_by_image,
        'exact_retained_private_object_inventories': objects}


def reviewed_shapes(config, images, dis, retained):
    profile = int(config['STARTUP_IRQ_PROFILE'])
    if not REVIEWED[profile] or not REVIEWED_STACK_ROWS[profile]:
        raise ValueError('Missing independently reviewed startup IRQ progress identities/frames')
    result = {}
    for image_name, base in (('resident-sci', LOW), ('worker', HIGH), ('stage', STAGE)):
        expected = REVIEWED[profile].get(image_name, {})
        if (set(expected) != changed_functions(profile, image_name) or
                retained['functions'][image_name] != set(expected) | set(retained['function_identities'][image_name])):
            raise ValueError('startup IRQ progress missing/unreviewed functions in ' + image_name)
        code = Linked(images[image_name], base, dis[image_name])
        actual = {name: function_identity(code, name, config['BUILD'], retained=False) for name in expected}
        if actual != expected:
            raise ValueError('Changed independently reviewed startup IRQ progress body: ' + image_name)
        result[image_name] = actual
    return result


def source_identities(root=ROOT):
    return {name: hashlib.sha256((Path(root) / name).read_bytes()).hexdigest() for name in REVIEW_SOURCE_FILES}


def audit_sources(root=ROOT):
    if set(REVIEWED_SOURCE_SHA256) != set(REVIEW_SOURCE_FILES):
        raise ValueError('Missing independently reviewed startup IRQ progress sources')
    observed = source_identities(root)
    if observed != REVIEWED_SOURCE_SHA256:
        raise ValueError('Changed independently reviewed startup IRQ progress source')
    retained = {name: expected for name, expected in tuning_parent.REVIEWED_SOURCE_SHA256.items()
                if name not in CHANGED_SOURCE_FILES}
    if any(observed[name] != expected for name, expected in retained.items()):
        raise ValueError('startup IRQ progress changed retained AI runtime source')
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
            raise ValueError('startup IRQ progress changed frozen low reader/CDDA source: ' + name)
        frozen[name] = expected
    return {'reviewed_irq_sha256': observed, 'retained_AI_source_sha256': retained,
            'frozen_low_CDDA_sha256': frozen, 'immutable_parent_gate_sha256': audit_frozen_gates()}


def reviewed_object_tables(profile):
    expected = REVIEWED_OBJECTS.get(profile)
    if (not isinstance(expected, dict) or set(expected) != {'worker', 'resident-sci'} or
            any(not isinstance(table, dict) or not table for table in expected.values()) or
            expected != tuning_parent.REVIEWED_OBJECTS[1]):
        raise ValueError('Missing/changed independently reviewed exact owned-object tables')
    return expected


def budget_metadata(profile):
    name,blocks,ticks,steps=PROFILES[profile]
    return {'name':name,'profile':profile+12,'credit_limit':blocks,
        'credit_unit':'verified 512-byte physical card blocks','tick_limit':ticks,
        'tick_hz':781250,'admission_microseconds':ticks*1000000/781250,
        'finite_step_limit':steps,'one_step_may_cross_tick_limit':True,
        'intervening_IRQ_and_one_step_work_count_toward_block_limit':True}


def audio_policy(profile):
    if profile not in PROFILES:
        raise ValueError('Unknown startup IRQ progress audio policy')
    return {'refill_quanta':4,'service_ticks':12500,'tick_hz':781250,
        'admission_microseconds':16000,'one_quantum_may_cross_tick_limit':True,
        'changed_from_AI':False,'raw_sector_reader_and_ring_queue_algorithm_retained':True}


class PriorityLinked(schedule.P2SharedLinked):
    """Reuse unchanged templates after checking the one allowed new opcode.

    Only rehook+0x4c may differ from the original priority1 template, and it
    must be the exact XOR immediate0x70. This view changes no bytes or symbols
    in the original artifact. All remaining register/width/opcode/literal and
    forwarded-event restoration checks run through the historical auditor.
    """
    def __init__(self, worker):
        super().__init__(worker)
        self.priority_instruction = self.code('_kui_toy_pilot_sci_rehook') + 0x4c
        if super().half(self.priority_instruction) != 0xca70:
            raise ValueError('Startup IRQ rehook must use exact XOR immediate0x70')

    def half(self, address):
        if budget_parent.physical(address) == getattr(self, 'priority_instruction', None):
            return 0xca10
        return super().half(address)


def audit_priority_bodies(worker, disassembly):
    """Allow exactly three C priority operands while retaining all other code."""
    config, images, dis, _ = reference_ai()
    before = Linked(images['worker'], HIGH, dis['worker'])
    code = Linked(worker, HIGH, disassembly)
    scopes = {'_hook': {0x1a: (0x8870, 0x8810), 0x2a: (0xcb70, 0xcb10)},
              '_unhook': {0x22: (0x8870, 0x8810)}}
    result = {}
    for name, offsets in scopes.items():
        begin, _, _ = schedule.bounded_function(code, name)
        normalized = copy.copy(code)
        payload = bytearray(code.image['payload'])
        replacements = {}
        for offset, (expected, original) in offsets.items():
            at = begin + offset
            if code.half(at) != expected:
                raise ValueError('Startup IRQ exact C priority opcode changed: ' + name)
            struct.pack_into('<H', payload, at - HIGH, original)
            replacements[at] = original
        normalized.image = {**code.image, 'payload': bytes(payload)}
        normalized.payload = bytes(payload)
        normalized.rows = [(at, replacements.get(at, op), mn, arg)
                           for at, op, mn, arg in code.rows]
        expected = function_identity(before, name, config['BUILD'])
        actual = function_identity(normalized, name, config['BUILD'])
        if actual != expected:
            raise ValueError('Startup IRQ C hook changed beyond the exact priority operands: ' + name)
        result[name] = {'changed_offsets': [hex(n) for n in offsets],
                        'all_other_opcodes_literals_and_registers_retained': True}
    return result


def audit_priority_irq(directory, worker, disassembly):
    forbidden = ('_kui_toy_pilot_sci_audio_acquire', '_kui_toy_pilot_sci_audio_release',
                 '_kui_toy_pilot_sci_audio_read')
    if any(name in worker['symbols'] for name in forbidden):
        raise ValueError('Startup IRQ progress gained an audio transport helper')
    symbols = worker['symbols']
    bottom, top = (symbols.get(name, 0) for name in
                  ('__toy_sci_irq_stack_bottom', '__toy_sci_irq_stack_top'))
    if (top - bottom != 2048 or bottom % 32 or top % 32 or
            not symbols['__toy_pilot_bss_begin'] <= bottom < top <= symbols['__toy_pilot_bss_end'] or
            not HIGH + P2 <= bottom < top <= HIGH_LIMIT + P2):
        raise ValueError('Startup IRQ lost its independent guarded2KiB P2 stack')
    linked = PriorityLinked(worker)
    vector = schedule.shared.irq_vectors(linked)
    publication = schedule.shared.vector_publisher(linked)
    low_rows = schedule.stack_rows(list((Path(directory) / 'sci/lto').glob('*.ltrans*.su')))
    high_rows = schedule.stack_rows(list((Path(directory) / 'worker').rglob('*.su')))
    units = {'toy_pilot_sci.su', 'sci_stream.su', 'retail_cursor.su', 'async_gd.su',
             'retail_gd.su', 'minic.su'}
    engine = [row for row in high_rows if Path(row['report']).name in units or
              row['function'] == 'kui_sci_sd_healthy']
    low_common = [row for row in low_rows if row['function'] not in
                  ('kui_retail_resident_init', 'kui_toy_pilot_read_raw')]
    used = sum(row['bytes'] for row in engine + low_common) + 44
    if used > 1968:
        raise ValueError(f'Startup IRQ participating-frame sum{used} exceeds1968')
    return {'present': True, 'priority': 7, 'IPRB_SCI_nibble': '0x70',
        'single_pump_per_IRQ': True, 'GD_entry_mask_retained': True,
        'C_priority_operands': audit_priority_bodies(worker, disassembly),
        'assembly_rehook': {'changed_offset': '0x4c', 'actual_opcode': '0xca70',
            'all_other_original_template_checks_retained': True},
        'original_game_VBR_and_SCI_nibble_restored_before_forwarding': True,
        'IRQ_P2_stack_bottom': hex(bottom), 'IRQ_P2_stack_top': hex(top),
        'integer_vector_handoff': vector, 'vector_cache_publication': publication,
        'independent_stacks': {'conservative_bytes': used, 'available_bytes': 1968,
            'assembly_allowance': 44, 'all_engine_units_and_shared_low_callbacks_counted': True},
        'CDDA_transport_helpers': False,
        'bounded_token_slice_and_possible_PIO_or_stop_waits_retained': True}


def audit_foreground_policy(worker, disassembly, profile):
    code = Linked(worker, HIGH, disassembly)
    name = '_kui_toy_pilot_sci_service'
    begin, _, rows = schedule.bounded_function(code, name)
    if profile == 1:
        # AM retains the complete admitted AI service; require its actual
        # finite2048-step literal as well as the retained identity comparison.
        loads = [(at, op) for at, op, mn, _ in rows if op & 0xff00 == 0x9000 and
                 mn == 'mov.w' and code.literal(at, 0, 2)[1] == 2048]
        if len(loads) != 1 or code.half(loads[0][0] + 2) != 0x3100:
            raise ValueError('AM lost its actual finite2048-step comparison')
    elif profile == 2:
        # With a one-step cap GCC removes the impossible second-step timer
        # check. r1 is a boolean: both the optional boundary pump and normal
        # work back edge set it to1, and the next tst/bf exits before work.
        # Preserve the exact actual branch/register and both helper targets.
        for offset, expected in {0x70: 0xe100, 0x74: 0x400b, 0x76: 0xe400,
                0x78: 0xe101, 0x9a: 0x2118, 0x9c: 0x8b3c, 0xa0: 0x410b,
                0xa2: 0xe401, 0xaa: None, 0xac: 0x420b, 0xb2: 0x8de3,
                0xb4: 0xe101}.items():
            if expected is not None and code.half(begin + offset) != expected:
                raise ValueError('AN lost its exact one-step branch/register guard')
        for offset, register, target in ((0x72, 0, '_data_work'),
                (0x9e, 1, '_data_work'), (0xaa, 2, '_kui_sci_stream_token_pending')):
            if code.literal(begin + offset, register)[1] != code.symbols[target]:
                raise ValueError('AN one-step guard helper target changed')
    else:
        raise ValueError('Unknown foreground IRQ-progress policy')
    return {'finite_step_limit': PROFILES[profile][3],
        'GD_entry_remains_BL_and_IMASK15_masked': True,
        'AN_one_step_guard_includes_not_ready_DMA_polls': profile == 2,
        'AN_second_step_timer_check_optimized_out': profile == 2,
        'exact_guard_branches_registers_and_helper_targets_checked': True}


def _legacy_ai_result(directory, *, discover=False):
    proof = copy.deepcopy(reference_ai(directory)[3])
    proof.update(startup_irq_profile=0, original_immutable_AI_gate=True)
    if discover:
        proof.update(admitted=False, independent_review_required=True)
    return proof


def audit_schema(profile, root=ROOT):
    if profile not in PROFILES:
        raise ValueError('Unknown startup IRQ progress schema profile')
    cc = shutil.which('cc') or shutil.which('gcc')
    if not cc:
        raise ValueError('Host preprocessor required for guarded startup IRQ progress schema')
    _, blocks, ticks, steps = PROFILES[profile]
    definitions = [f'-DKUI_TOY_PILOT_STARTUP_IRQ_PROFILE={profile}',
        '-DKUI_TOY_PILOT_DATA_TUNING_PROFILE=1',
        '-DKUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=2',
        '-DKUI_TOY_PILOT_DATA_BUDGET_PROFILE=4', '-DKUI_TOY_PILOT_DATA_SCHEDULE_MODE=2',
        '-DKUI_TOY_PILOT_PRIVATE_P2=1', '-DKUI_TOY_PILOT_LOADER_TRACE=1',
        '-DKUI_TOY_PILOT_DATA_PROBE=1', '-DKUI_TOY_PILOT_GD_FIXED_STEP=2']
    expected = {'KUI_TOY_SCHEDULE_REPORT_VERSION': 2, 'KUI_TOY_SCHEDULE_REPORT_WORDS': 192,
        'KUI_TOY_SCHEDULE_RECORD_WORDS': 18, 'KUI_TOY_SCHEDULE_RECORDS': 8,
        'KUI_TOY_SCI_SERVICE_BLOCKS': blocks, 'KUI_TOY_SCI_SERVICE_TICKS': ticks,
        'KUI_TOY_SCI_SERVICE_STEPS': steps, 'KUI_TOY_AD_BUDGET_REPORT_PROFILE': profile + 12,
        'KUI_TOY_TUNING_REPORT_PROFILE': 9,
        'KUI_TOY_IRQ_REPORT_PROFILE': profile + 12,
        'KUI_TOY_STARTUP_SCI_PRIORITY': 7, 'KUI_TOY_STARTUP_SCI_NIBBLE': 0x70,
        'KUI_TOY_STARTUP_SCI_LEVEL': 0x70,
        'KUI_TOY_AD_REFILL_QUANTA': 4, 'KUI_TOY_AD_REFILL_TICKS': 12500}
    source = '#include "kui/toy_loader_schedule_report.h"\n#include "toy_pilot_sci.h"\n'
    for name, value in expected.items():
        source += f'#if {name} != {value}u\n#error startup IRQ progress schema constant changed: {name}\n#endif\n'
    completed = subprocess.run([cc, '-E', '-x', 'c',
        '-I' + str(Path(root) / 'include'), '-I' + str(Path(root) / 'src/loader'),
        *definitions, '-'], input=source, text=True, capture_output=True)
    if completed.returncode:
        raise ValueError('startup IRQ progress schema constant/guard changed: ' + completed.stderr.strip())
    return {'magic': '0x4c534331', 'version': 2, 'words': 192,
        'header_words': 16, 'engine_words': 32, 'records': 8, 'record_words': 18,
        'record_start': 48, 'budget_stats_start': 36, 'budget_stats_words': 12,
        'reported_budget_profile': profile + 12, 'TRACE_words': 416,
        'record_words_16_17': ['IRQ verified-block delta', 'retry delta'],
        'publication_checksum': 'first credited sector CRC32',
        'TRACE_delivered_sector_unit': 'credited logical sector bytes, persistent across IRQ work'}


def candidate(builddir, baseline=DEFAULT_AI_BASELINE, profile=None):
    directory = Path(builddir).resolve()
    config = read_config(directory, profile)
    if 'STARTUP_IRQ_PROFILE' not in config:
        return _legacy_ai_result(directory, discover=True)
    config, images, dis = load_profile(directory, profile)
    profile = int(config['STARTUP_IRQ_PROFILE'])
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
        'profile': PROFILES[profile][0], 'startup_irq_profile': profile, 'data_tuning_profile': 1, 'followup_profile': 2, 'budget_profile': profile + 12,
        'budget': budget_metadata(profile), 'audio_policy': audio_policy(profile),
        'runtime': runtime, 'retained': retained, 'functions': evidence,
        'startup_IRQ': audit_priority_irq(directory, images['worker'], dis['worker']),
        'foreground_policy': audit_foreground_policy(images['worker'], dis['worker'], profile),
        'stack_rows': schedule.stack_inventory(directory), 'source_sha256': source_identities()}


def audit_irq_progress(builddir, baseline=DEFAULT_AI_BASELINE, profile=None):
    directory = Path(builddir).resolve()
    config = read_config(directory, profile)
    if 'STARTUP_IRQ_PROFILE' not in config:
        return _legacy_ai_result(directory)
    config, images, dis = load_profile(directory, profile)
    profile = int(config['STARTUP_IRQ_PROFILE'])
    runtime = schedule.previous.bind_runtime(directory, config, images)
    if not REVIEWED_RUNTIME_SHA256[profile] or runtime['sha256'] != REVIEWED_RUNTIME_SHA256[profile]:
        raise ValueError('startup IRQ progress runtime is not the independently reviewed immutable artifact')
    retained = compare_retained(directory, config, images, dis, baseline)
    reviewed = reviewed_shapes(config, images, dis, retained)
    inventory = schedule.stack_inventory(directory)
    if schedule.stack_signatures(inventory) != REVIEWED_STACK_ROWS[profile]:
        raise ValueError('startup IRQ progress emitted static stack inventory changed or missing')
    for name in images:
        try:
            budget_parent.instruction_audit(name, dis[name])
        except SystemExit as exc:
            raise ValueError(str(exc)) from exc
    low, worker, stage = (images[name] for name in ('resident-sci', 'worker', 'stage'))
    objects = {}
    for image_name, expected in reviewed_object_tables(profile).items():
        base, limit, prefix = ((HIGH, HIGH_LIMIT, '__toy_pilot') if image_name == 'worker'
                               else (LOW, LOW_LIMIT, '__retail_resident'))
        objects[image_name] = schedule.audit_owned_objects(images[image_name], expected, prefix, base, limit)
    retained.pop('functions')
    code = Linked(worker, HIGH, dis['worker'])
    runtime.update(bytes=(directory / 'retail-toy-pilot.kui').stat().st_size, build=config['BUILD'])
    return {'admitted': True, 'profile': PROFILES[profile][0], 'startup_irq_profile': profile, 'data_tuning_profile': 1, 'followup_profile': 2,
        'budget_profile': profile + 12, 'build': config['BUILD'], 'config': config,
        'budget': budget_metadata(profile), 'audio_policy': audio_policy(profile), 'runtime': runtime,
        'retained': retained, 'reviewed_functions': reviewed, 'sources': audit_sources(),
        'owned_objects': objects, 'schedule_schema': audit_schema(profile),
        'report_geometry': budget_parent.audit_report_geometry(worker),
        'data_routing': {'asynchronous_startup_cooked_sector_bytes': 2048,
            'retained_dispatch_identity': retained['function_identities']['worker']['_dispatch_body'],
            'raw_and_post_PLAY_reader': 'original synchronous low reader retained against AI, AF, AD, Z and U'},
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
        'startup_IRQ': audit_priority_irq(directory, worker, dis['worker']),
        'foreground_policy': audit_foreground_policy(worker, dis['worker'], profile),
        'hardware_behavior_verified': False, 'audio_or_video_improvement_proven': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builddir', type=Path)
    parser.add_argument('--baseline', type=Path, default=DEFAULT_AI_BASELINE)
    parser.add_argument('--profile', type=int, choices=(0, *PROFILES))
    parser.add_argument('--candidate', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = (candidate if args.candidate else audit_irq_progress)(args.builddir, args.baseline, args.profile)
    encoded = json.dumps(result, indent=2, sort_keys=True) + '\n'
    if args.output:
        args.output.write_text(encoded)
    else:
        print(encoded, end='')
