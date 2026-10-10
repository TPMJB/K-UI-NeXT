#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed linked admission for V/W/X cooked-DATA SCI experiments.

The published U artifact is the immutable reference. Its runtime bytes are
bound to every reference ELF before comparison. Low reader/CDDA instructions,
reservations and sources stay unchanged. New helpers and v2 observer functions
require independently reviewed instruction identities for the selected mode;
--candidate prints evidence and never grants hardware admission.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

import toy_loader_data_probe_audit as u
import toy_loader_trace_audit as trace
from check_loader_layout import inspect_elf, padded, region
from check_retail_instructions import audit as instruction_audit
from package_cdda_toy_pilot import embedded_blob, linked_symbol_sizes, stack_rows
from runtime_package import flatten_elf
from retail_package import inspect_retail
import retail_package as layout
from toy_pilot_cache_audit import Linked, audit_cache_layout

ROOT = Path(__file__).resolve().parents[1]
LOW, LOW_LIMIT, HIGH, HIGH_LIMIT = u.LOW, u.LOW_LIMIT, u.HIGH, u.HIGH_LIMIT
STAGE, STAGE_LIMIT, P2 = u.STAGE, u.STAGE_LIMIT, u.P2
U_BUILD = '52562af7aba3'
U_RUNTIME_SHA = 'a6505d0330a75c6d199a7e3420c8561d51da66a0f92edceefc27530f91adfff5'
REQUIRED_CONFIG = dict(u.REQUIRED_CONFIG)
CHANGED_FUNCTIONS = set(u.REVIEWED_DATA)
# Only manually reviewed linked bodies are admitted. Candidate mode emits
# evidence for review; it never populates these tables or accepts a new shape.
# Reviewed using GCC 15.2.0 target disassembly. Sample/metric/read retain
# U identities. Payload snapshots exact readonly diagnostic words around one
# original call (V) or the proved high helper (W/X). Initializers encode exact
# v2/mode flags; W/X begin freezes before phase-one work and freeze restores
# both callbacks before publishing helper counts once. Words is a frame-zero
# accessor. Helper bodies were reviewed across admission, decline and failure
# branches: W two 16-line OCBP loops, X one success-only 512-byte copy.
REVIEWED = {0: {'_data_probe_metric': '99918b342a22bc5eb3b48b77496a934ec3ae11962ee1c3471de86e0239e7e418',
     '_data_probe_payload': '5ff85808b88761df26e870e4728a0159e724a47790c71b471b22e225e5fc66de',
     '_data_probe_read': 'b0e1a424abf8a24f0a444f469f8470e461c8caa314a0508937d35777859869d8',
     '_data_probe_sample': 'ede5402906f5a7142a372c6637005f1590a4a6fa167b1b497d6375dcbaf5c2e9',
     '_kui_toy_loader_data_probe_begin': '8180b3c97330c6eb17cbc29fc6f4162590823e982de9549b1ffa153329b30461',
     '_kui_toy_loader_data_probe_end': 'f1ef968e1ded595a08030a18852665a624f47098a16e585a7d1606331e42eea1',
     '_kui_toy_loader_data_probe_freeze': '2419cd7e9bba3f3484cfc8c2b34778a6d0fdaf4dfa228f91968d89c7451e4927',
     '_kui_toy_loader_data_probe_words': 'd10acf90db580d5b0ff1cfa656a87be6d8a891a6efd22fb7d856fd66bd3a7073'},
 1: {'_data_probe_metric': '99918b342a22bc5eb3b48b77496a934ec3ae11962ee1c3471de86e0239e7e418',
     '_data_probe_payload': '4dde16fe8d206e0135e69d6e1e6a8d82f55b4879f9aa0646da507aba16c0dc20',
     '_data_probe_read': 'b0e1a424abf8a24f0a444f469f8470e461c8caa314a0508937d35777859869d8',
     '_data_probe_sample': 'ede5402906f5a7142a372c6637005f1590a4a6fa167b1b497d6375dcbaf5c2e9',
     '_kui_toy_loader_data_probe_begin': 'dc69fa9373f7912a201f38e7e7e265db973a9ea492b8a3d1e074d83d3ee3e943',
     '_kui_toy_loader_data_probe_end': 'e4bef92458576bbc51a7626a3d65b116e742c98ae2f9c382646cae745ea20c6e',
     '_kui_toy_loader_data_probe_freeze': '39b684496c8513e88724aec69fa3cc823108248fd224a08e81d3d2969372e067',
     '_kui_toy_loader_data_probe_words': 'c67eaff87f25a71352b54f9e26471bda3d8ece118b4f0799bfd4a058d19eb4c4',
     '_kui_toy_loader_payload_call': '17c12c3131e1ded241abe4af8ba1f15c26793e41b2d4d1f3d6390b2ef70cbb3d',
     '_kui_toy_loader_payload_control_counts': 'f58e9aaf1d0d4acc4c2725548937ec588a4f18684f87d2619697bb241efb3ca5'},
 2: {'_data_probe_metric': '99918b342a22bc5eb3b48b77496a934ec3ae11962ee1c3471de86e0239e7e418',
     '_data_probe_payload': '4dde16fe8d206e0135e69d6e1e6a8d82f55b4879f9aa0646da507aba16c0dc20',
     '_data_probe_read': 'b0e1a424abf8a24f0a444f469f8470e461c8caa314a0508937d35777859869d8',
     '_data_probe_sample': 'ede5402906f5a7142a372c6637005f1590a4a6fa167b1b497d6375dcbaf5c2e9',
     '_kui_toy_loader_data_probe_begin': '02a62c984f56d058bbd89f7c4acb151515983b18fc5db5259d93332ee9469b82',
     '_kui_toy_loader_data_probe_end': '7c7e6170ce6f28cf3e013e0c78796cc14ef2038be9856d0a41760bfaff3d1c2e',
     '_kui_toy_loader_data_probe_freeze': '6c18346cdeb4686d6444574bc9c64243207b0631e0acf00b110e84d69fe39fee',
     '_kui_toy_loader_data_probe_words': 'e11af02cf0ccd914c5c30cbdebbd3daf2388ef88c8fc8902057334802bfc8a90',
     '_kui_toy_loader_payload_call': '7fc8043367061a2bfbd807ff7dcd26736cb6fe6fb025307ec8c133bf8659f3b4',
     '_kui_toy_loader_payload_control_counts': 'f58e9aaf1d0d4acc4c2725548937ec588a4f18684f87d2619697bb241efb3ca5'}}
REVIEWED_OBJECTS = {
    0: {'_data_probe_report': 768, '_data_probe_scope': 36, '_data_probe_read_state': 112},
    1: {'_data_probe_report': 768, '_data_probe_scope': 36, '_data_probe_read_state': 112,
        '_payload_counts': 16},
    2: {'_data_probe_report': 768, '_data_probe_scope': 36, '_data_probe_read_state': 112,
        '_payload_counts': 16, '_payload_pio_scratch': 544},
}
FROZEN_SOURCES = {
    'src/loader/sci_sd_bus.c': 'a141af6127f6a73b27042dac6109a3c967ed2fa6243c5626afea1e3babea2a96',
    'src/loader/sci_sd_bus.h': 'f1b4d423b74498d4bba28fceeacd16648953fe7e6f0622572ad9014f130711e5',
    'src/loader/sd_reader.c': 'd590bf058eaf73b435dd1ba0b2e090b3c3d455d16c69f12697c3cb44b206c1da',
    'src/loader/retail_sd.c': '51a943a5c1f58b9e8e07b2aa0fcb58d46d680c77059b6b578ebad685f210cc40',
    'src/loader/retail_storage.c': '3d588b0db76f5a4af7c28e8f9eebe73f9cdc12253a4675b7c3551d689302c8a9',
    'src/loader/retail_resident.c': 'c8f9a66acc252f9320628306edb0864ca00002569fcd8d85f687346241511026',
    'src/loader/toy_pilot_resident.c': 'fa5ac09e1f6f1442b5d746e0df23c9d1ca7d14fe5c2d19dbe03ff82cfa8e3866',
    'src/loader/toy_pilot_worker.c': '6e6212cd1024691dfa67b6461f5d59eefc63fb1e5f1c5fd3226156edd783133c',
    'src/loader/toy_pilot_bus.c': 'e1298b0bd8226284c3601cdb89bcc5d83683d2fe6fb1e27e96c79ee26257a0a7',
    'src/loader/toy_pilot_lease.c': 'd6d03ff94c596ab778ba7a29bd90d0d752bc6845c28304953dee546bd3586e8b',
    'src/loader/toy_pilot_gd.c': 'b7614cd14af657ed5045a453177b3dd21617b787d60205666046e55eb5cd2f62',
    'src/loader/toy_pilot_pause.c': '327eb90f9bac0ddbd2da044514e26f9aabbe1ddd00416aeede5a259be8b0a2e9',
    'src/loader/toy_pilot_worker.S': '76a1fb395779a4238fed901ebadfd9fa373fd2e85479ad2b2fffec479905358e',
    'src/loader/toy_pilot_pause.S': '714775804d2883d39a3bebe5138f94101c030a379263862ccb69bee77df67a39',
    'src/loader/toy_pilot_boot.S': '9b6f6fef5edb6775df7548cb4f6f6d12aeb9eb8605ef8769fd8c1ff12d62640b',
    'src/loader/toy_loader_trace.c': 'e4c68ed60fac3b97d5b353b88263e5de9d51f54f329b03777516141a7f7c7dde',
    'src/loader/toy_loader_trace_report.c': 'ab6dad40137686e94efbd10478d2f7dd27b86cc7a6372866da86bd6b039d55ce',
    'src/core/toy_pilot.c': '43d672705a3746359c11496998304bfd20ff537366735dcd64ab8651d7dbff4d',
    'src/core/retail_image.c': '432e677b0f81fe358a7012551754d2efa36dbf57586dbc71479c573547479aba',
    'src/core/retail_gd.c': 'd6a46d3f9df4c2dbb51611d998b32479e07a183fa04740e5eb2c38ffd8661f9c',
    'src/core/hash.c': '451f915eb8d85d0c94ef2af91cca2e25604e2eee81072d316ca708982d14485f',
}
# Pinned function/frame tuples from the exact published U target build. Source
# line numbers and absolute checkout paths are irrelevant; static rows, their
# order, their names and their byte counts are part of the stack admission.
RETAINED_STACK_REPORTS = {'worker/src/core/hash.su': 'f43136e9635b1a3134cfb72eedf2ea4ae3cd5eaedb945781d4391a57d928bb9a',
 'worker/src/core/toy_pilot.su': '5a3e4b6994fcee5705056d476468902d39ed200f3a688bfd4bf62f3c72b881d7',
 'worker/src/loader/minic.su': 'aee234f393f54b72c561d21ae9bbc05079733746748724f86ed5b878440bb471',
 'worker/src/loader/retail_display.su': '91800ba927951c42adbd563d8c7a8928423541c0a5fab639dec8873e1225aa79',
 'worker/src/loader/toy_loader_trace.su': 'd27b751231d4c4f687afb04c2e09dc0c0bf10588c882d06a03ed71ced79ac887',
 'worker/src/loader/toy_loader_trace_report.su': '076699080d13d23c74b3e925e4418b86f74ff943f1afda986939358163048ba3',
 'worker/src/loader/toy_pilot_bus.su': 'abd277304ccba2e6d77cc7cf12289eda4686100028e94c413e0f2f0937105819',
 'worker/src/loader/toy_pilot_gd.su': '56e079e8fb758382b8356c684211a3ce81ce3dd05a0b3c35ee106ef17590079a',
 'worker/src/loader/toy_pilot_lease.su': 'a738234f7af9f0514b59b230c0f33d4140b136495ac2f92111c807c8f0690dfa',
 'worker/src/loader/toy_pilot_pause.su': 'cc35e46ef5a9b2d4ee15ce85ca37999b2b7c61a4cfd782bcc4f50f046791eba7',
 'worker/src/loader/toy_pilot_worker.su': '2555c765510575508f9e4ea917f578ac90baba3acc389a4b0fd640e16ada668e',
 'stage/src/core/ata.su': '178c891d8bd5d4b9c9d2c1014f86fffdb6fda981fad53e655deff7c0872ffcc8',
 'stage/src/core/hash.su': 'f43136e9635b1a3134cfb72eedf2ea4ae3cd5eaedb945781d4391a57d928bb9a',
 'stage/src/core/retail_image.su': '9b402a0b093fc0456f34270513010fa418b9906e4fc8c53f88449766d3142ce8',
 'stage/src/core/retail_observe.su': 'e78ec23dc9bfd3104497d7cee07130c0605fe8a5b1fb866e041651ef6e2ee1fa',
 'stage/src/dreamcast/ata_bus.su': 'd8c251cad54ee6ba63711d1b1eed8f32b9550496165da3f51a58bddf45950053',
 'stage/src/loader/minic.su': 'aee234f393f54b72c561d21ae9bbc05079733746748724f86ed5b878440bb471',
 'stage/src/loader/retail_display.su': '91800ba927951c42adbd563d8c7a8928423541c0a5fab639dec8873e1225aa79',
 'stage/src/loader/retail_sd.su': 'a68f39dfb6df67522668288144af464b84f61b8fabf9a2d82cbca7d700237087',
 'stage/src/loader/retail_storage.su': 'f0698dc22889dffac8e65e05975d865adfd118cecbf8289e0bb556bee63975a8',
 'stage/src/loader/sci_sd_bus.su': 'f49e78dd627082718590512dcbc23c091ea05ff27871a9154daded38de9ddf51',
 'stage/src/loader/sd_reader.su': '90bdad5b9fa7d63c702552638a50f94ead9115fb65a4f8f9c2180b1365df903c',
 'stage/src/loader/toy_pilot_stage.su': '8832b5a58b55fceb14a6b4ca98f42aaae25e11764145ed7c2e7f98bab8aaff65',
 'sci/lto/resident-sci.elf.ltrans0.ltrans.su': '25c09b873e662281c829863b4fc08d20844dbd4f9a95ca6bef3555cebb81ab96'}

PROBE_MMIO_READS = dict(u.PROBE_MMIO_READS)
_signed = trace._signed
exact_function = trace.exact_function


def read_config(directory, mode=None):
    config = {}
    for line in (Path(directory) / 'build-config').read_text().splitlines():
        if '=' not in line:
            raise ValueError('Malformed SCI-next configuration')
        key, value = line.split('=', 1)
        if key in config:
            raise ValueError('Duplicate SCI-next configuration: ' + key)
        config[key] = value
    if any(config.get(key) != value for key, value in REQUIRED_CONFIG.items()):
        raise ValueError('SCI-next requires exact retained R plus DATA/TRACE')
    if set(config) != set(REQUIRED_CONFIG) | {'BUILD', 'DATA_PAYLOAD_MODE'}:
        raise ValueError('SCI-next contains an unreviewed configuration key')
    if config.get('DATA_PAYLOAD_MODE') not in ('0', '1', '2'):
        raise ValueError('SCI-next payload mode must be 0, 1 or 2')
    if mode is not None and config['DATA_PAYLOAD_MODE'] != str(mode):
        raise ValueError('SCI-next requested mode differs from actual linked configuration')
    if not re.fullmatch(r'[0-9a-f]{12}', config.get('BUILD', '')):
        raise ValueError('SCI-next requires a twelve-digit build identity')
    return config


def load_profile(directory, mode=None, reference=False):
    directory = Path(directory).resolve()
    config = u.read_config(directory) if reference else read_config(directory, mode)
    objdump = shutil.which('sh-elf-objdump')
    if not objdump:
        raise ValueError('SH objdump is required for linked SCI-next admission')
    images, dis = {}, {}
    for name, base, limit in (('resident-sci', LOW, LOW_LIMIT),
                             ('worker', HIGH, HIGH_LIMIT),
                             ('stage', STAGE, STAGE_LIMIT)):
        raw = (directory / (name + '.elf')).read_bytes()
        options = {'private_p2': True} if name != 'stage' else {}
        if name == 'worker':
            options.update(entry_symbol='_kui_toy_pilot_initialize', entry_at_base=False)
        image = inspect_elf(raw, base, limit, **options)
        image['symbol_sizes'] = linked_symbol_sizes(raw)
        image['readonly_ranges'] = u.readonly_ranges(raw)
        if (directory / (name + '.bin')).read_bytes() != image['payload']:
            raise ValueError(name + '.bin differs from actual ELF load bytes')
        images[name] = image
        dis[name] = subprocess.check_output([objdump, '-d', str(directory / (name + '.elf'))], text=True)
    return config, images, dis


def bind_runtime(directory, config, images, expected_sha=None):
    """The comparison ELFs must be the bytes embedded in the actual runtime."""
    directory = Path(directory)
    runtime = (directory / 'retail-toy-pilot.kui').read_bytes()
    digest = hashlib.sha256(runtime).hexdigest()
    if expected_sha is not None and digest != expected_sha:
        raise ValueError('SCI-next reference is not the exact published U runtime')
    envelope = inspect_retail(runtime)
    payload, memory = flatten_elf((directory / 'entry.elf').read_bytes())
    if (runtime[64:] != payload or envelope['memory_bytes'] != memory or
            envelope['build'] != config['BUILD']):
        raise ValueError('SCI-next runtime differs from entry ELF/build identity')
    stage = images['stage']
    embedded_blob(stage, '__toy_pilot_worker_blob_start', '__toy_pilot_worker_blob_end',
                  STAGE, images['worker']['payload'], 'SCI-next worker')
    resident = []
    for transport in ('scif', 'sci', 'ide', 'scia'):
        prefix = '__retail_resident_' + transport + '_blob_'
        proof = embedded_blob(stage, prefix + 'start', prefix + 'end', STAGE,
                              padded(images['resident-sci']['payload']), 'retained SCI ' + transport)
        resident.append((proof['address'], proof['bytes']))
    if len(set(resident)) != 1 or payload[layout.STAGE_BLOB_OFFSET:] != padded(stage['payload']):
        raise ValueError('SCI-next runtime does not embed the exact single low resident/stage')
    if (region(payload, layout.HEADER_OFFSET, layout.HEADER_BYTES, 'relocation header') !=
            layout.relocation_header(len(padded(stage['payload'])), low=True) or
            any(region(payload, layout.MAP_OFFSET, layout.MAP_BYTES, 'blank manifest'))):
        raise ValueError('SCI-next runtime/header/card manifest geometry changed')
    return {'sha256': digest, 'all_ELF_images_bound_to_runtime': True}


def audit_reference(directory):
    config, images, dis = load_profile(directory, reference=True)
    if config['BUILD'] != U_BUILD:
        raise ValueError('SCI-next reference must retain the published U build identity')
    binding = bind_runtime(directory, config, images, U_RUNTIME_SHA)
    code = Linked(images['worker'], HIGH, dis['worker'])
    trace.reviewed_identities(code, u.REVIEWED_DATA, 'published U DATA')
    u.reviewed_report_identities(code, config['BUILD'])
    trace.reviewed_identities(code, {n: h for n, h in trace.REVIEWED_TRACE.items()
                                   if n not in u.CHANGED_FUNCTIONS}, 'published U trace')
    trace.reviewed_identities(Linked(images['stage'], STAGE, dis['stage']),
                              trace.REVIEWED_STAGE, 'published U stage')
    return config, images, dis, binding


def audit_retained(directory, config, images, dis, baseline):
    old_config, old_images, old_dis, reference = audit_reference(baseline)
    old_low, low = old_images['resident-sci'], images['resident-sci']
    old_id, new_id = U_BUILD.encode(), config['BUILD'].encode()
    if old_low['payload'].count(old_id) != 1 or low['payload'].count(new_id) != 1:
        raise ValueError('SCI-next low build identity is not an exact single string')
    canonical = low['payload'].replace(new_id, old_id)
    if canonical != old_low['payload'] or low['memory_end'] != old_low['memory_end']:
        raise ValueError('SCI-next changed retained low bytes/reservations')
    before = {n: old_low['symbol_sizes'][n] for n in old_low['symbols']}
    after = {n: low['symbol_sizes'][n] for n in low['symbols']}
    if low['symbols'] != old_low['symbols'] or before != after:
        raise ValueError('SCI-next changed retained low symbols/object sizes')
    old_code = Linked(old_images['worker'], HIGH, old_dis['worker'])
    code = Linked(images['worker'], HIGH, dis['worker'])
    old_names = u.function_symbols((Path(baseline) / 'worker.elf').read_bytes())
    identities = {}
    for name in sorted(old_names - CHANGED_FUNCTIONS):
        before = u.scoped_normalized_function(old_code, name, U_BUILD)
        after = u.scoped_normalized_function(code, name, config['BUILD'])
        if before != after:
            raise ValueError('SCI-next changed retained worker/audio instructions: ' + name)
        identities[name] = after
    names = u.function_symbols((Path(directory) / 'worker.elf').read_bytes())
    mode = int(config['DATA_PAYLOAD_MODE'])
    if names != (old_names - CHANGED_FUNCTIONS) | set(REVIEWED[mode]):
        raise ValueError('SCI-next added an unreviewed/missing linked function')
    return {'baseline_build': U_BUILD, 'baseline_runtime': reference,
            'low_binary_identical_except_build_string': True,
            'low_canonical_sha256': hashlib.sha256(canonical).hexdigest(),
            'low_symbols_and_reservations_identical': True,
            'retained_audio_instructions_identical': True,
            'retained_worker_function_identities': identities}


def audit_frozen_sources(root=ROOT):
    if not FROZEN_SOURCES:
        raise ValueError('Missing independently pinned frozen SCI/CDDA sources')
    observed = {}
    for name, expected in FROZEN_SOURCES.items():
        digest = hashlib.sha256((Path(root) / name).read_bytes()).hexdigest()
        if digest != expected:
            raise ValueError('SCI-next changed frozen low/CDDA source: ' + name)
        observed[name] = digest
    return {'all_frozen_sources_identical_to_U': True, 'sha256': observed}


def audit_state(worker, mode):
    if not REVIEWED_OBJECTS[mode]:
        raise ValueError('Missing reviewed SCI-next private BSS ownership')
    owned = trace.owned_bss(worker, {**u.TRACE_OBJECTS, **REVIEWED_OBJECTS[mode]})
    if mode == 2 and worker['symbols'].get('_payload_pio_scratch', 1) & 31:
        raise ValueError('SCI-next programmed scratch must retain aligned complete object')
    return owned


def audit_schema(root=ROOT):
    """Admit all 192 uint32_t fields, including every reused reserved word."""
    from package_toy_loader_trace import _wire_fields
    source = (Path(root) / 'include/kui/toy_loader_data_probe.h').read_text()
    defines = {'KUI_TOY_LOADER_DATA_PROBE_WORDS': 192,
               'KUI_TOY_LOADER_DATA_PROBE_PHASE_WORDS': 88,
               'KUI_TOY_LOADER_DATA_PROBE_VERSION': 2}
    for name, value in defines.items():
        if re.findall(r'^#define\s+' + name + r'\s+(\d+)u\b', source, re.M) != [str(value)]:
            raise ValueError('SCI-next DATA constant changed: ' + name)
    for name, value in (('MAGIC', '0x4c445031'), ('DMA_ATTRIBUTION', '1'),
                        ('CACHED_PAYLOAD', '2'), ('PIO_PAYLOAD', '4')):
        if re.findall(r'^#define\s+KUI_TOY_LOADER_DATA_PROBE_' + name +
                      r'\s+UINT32_C\(([^)]+)\)', source, re.M) != [value]:
            raise ValueError('SCI-next DATA marker/feature changed: ' + name)
    metric = ['samples', 'ticks_total', 'ticks_max', 'ticks_min']
    types = {}
    for name in ('kui_toy_loader_data_probe_metric', 'kui_toy_loader_data_probe_phase'):
        types['struct ' + name] = _wire_fields(source, name, types, defines)
    phase = ['data_visits', 'read_calls', 'read_ok', 'read_failed',
             'payload_calls', 'payload_ok', 'payload_failed', 'payload_bytes',
             'no_payload_reads', 'invalid_intervals', 'reentrant_reads', 'reentrant_payload',
             'callback_conflicts', 'nonstandard_payload', 'dma_delta_invalid', 'dma_counter_wraps']
    for name in ('read_body', 'first_payload_gap', 'payload_body', 'inter_payload_gap', 'tail_gap'):
        phase.extend(name + '.' + field for field in metric)
    phase.extend(f'sr_buckets[{i}]' for i in range(32))
    phase.extend(f'worst_read[{i}]' for i in range(8))
    phase.extend(['dma_started', 'dma_payload_ok', 'pio_fallback', 'prestart_failed'])
    for name in ('dma_payload_body', 'pio_payload_body'):
        phase.extend(name + '.' + field for field in metric)
    header = ['magic', 'version', 'words', 'phase_words', 'tick_hz', 'frozen', 'saturated',
              'unmatched_end', 'nested_begin', 'feature_flags', 'payload_mode',
              'payload_attempts', 'payload_declines', 'payload_publications', 'payload_failed', 'reserved']
    expected = header + [f'phase[{i}].' + word for i in range(2) for word in phase]
    actual = _wire_fields(source, 'kui_toy_loader_data_probe_report', types, defines)
    if (types['struct kui_toy_loader_data_probe_metric'] != metric or
            types['struct kui_toy_loader_data_probe_phase'] != phase or actual != expected or len(actual) != 192):
        raise ValueError('SCI-next DATA report ABI changed')
    return {'version': 2, 'words': 192, 'phase_words': 88,
            'all_word_offsets_exact': True, 'feature_header_offsets': [9, 10, 11, 12, 13, 14]}


def audit_bindings(directory, low, code, mode):
    from toy_pilot_symbols import generate
    expected = generate(Path(directory) / 'resident-sci.elf', private_p2=True, data_probe=True)
    if (Path(directory) / 'toy_pilot_resident_symbols.h').read_text() != expected:
        raise ValueError('SCI-next generated header differs from exact typed low objects')
    begin, end = (low['symbols'].get('__retail_resident_bss_begin', 0),
                  low['symbols'].get('__retail_resident_bss_end', 0))
    for name, size, alignment in (('_diagnostic', 64, 4), ('_image', 544, 32),
                                  ('_card', 72, 4), ('_kui_retail_hook_sr', 4, 4),
                                  ('_kui_retail_native_caller', 8, 4)):
        at = low['symbols'].get(name, 0)
        if (low['symbol_sizes'].get(name) != size or at % alignment or
                not LOW + P2 <= begin <= at < at + size <= end <= LOW_LIMIT + P2):
            raise ValueError('SCI-next lost exact typed low P2 binding: ' + name)
    values = set()
    for name in REVIEWED[mode]:
        for at, opcode, mnemonic, _ in exact_function(code, name)[2]:
            if opcode & 0xF000 in (0xD000, 0x9000) and mnemonic in ('mov.l', 'mov.w'):
                values.add(code.literal(at, opcode >> 8 & 15, 4 if opcode & 0xF000 == 0xD000 else 2)[1])
    if not any(low['symbols']['_diagnostic'] + offset in values for offset in (0, 52, 56, 60)):
        raise ValueError('SCI-next lost readonly DMA attribution binding')
    if mode and low['symbols']['_image'] + 32 not in values:
        raise ValueError('SCI-next payload helper lost exact complete low image block')
    for name in ('_kui_retail_hook_sr', '_read_sectors', '_transfer_block'):
        if low['symbols'][name] not in values:
            raise ValueError('SCI-next lost exact retained callback/context binding: ' + name)
    return {'typed_generated_header_exact': True, 'diagnostic_bytes': 64,
            'readonly_counter_offsets': [52, 56, 60], 'image_bytes': 544,
            'image_block_offset': 32, 'image_block_bytes': 512,
            'image_P2_address': hex(low['symbols']['_image'] + 32),
            'original_read_block_callbacks_direct_and_exact': True,
            'cooked_DATA_only': True, 'raw_CDDA_route_unchanged': True}


def audit_probe_graph(code, low, mode):
    """T's constant/control-flow walk, scoped to reviewed probe functions.

    Exact instruction identities close unknown pointer-derived RAM writes.
    This walk independently rejects constant MMIO writes, wrong widths,
    unknown callbacks, and branches into literals. Only the two unchanged
    exact low DATA callbacks are admitted as external call targets.
    """
    graph, reads = {}, set()
    allowed_reads = dict(PROBE_MMIO_READS)
    if mode == 1:
        allowed_reads[0xff00001c] = 4
    allowed = {code.symbols[name]: name for name in REVIEWED[mode]}
    primitives = {code.symbols[name]: name for name in ('_memcpy', '_memset')}
    low_callbacks = {low['symbols'][name]: name for name in ('_read_sectors', '_transfer_block')}
    allowed.update(low_callbacks)
    allowed.update(primitives)
    for name in (*tuple(REVIEWED[mode]), '_memcpy', '_memset'):
        begin, end, rows = exact_function(code, name)
        rowmap = {row[0]: row for row in rows}
        anchor, stack_low, stack_cells = 0xACFDE000, 0xACFDDC00, 272
        entry_state = [None] * (16 + stack_cells)
        entry_state[15] = anchor
        if name == '_kui_toy_loader_payload_call':
            entry_state[4] = low['symbols']['_transfer_block']
        incoming, pending, transfers = {begin: tuple(entry_state)}, [begin], set()

        def merge(at, state):
            if at not in rowmap:
                raise ValueError('Probe control flow escapes executable function: ' + name)
            old = incoming.get(at)
            if old is None:
                incoming[at] = tuple(state)
                pending.append(at)
            else:
                joined = tuple(a if a == b else None for a, b in zip(old, state))
                if joined != old:
                    incoming[at] = joined
                    pending.append(at)

        def effect(at, state):
            _, opcode, mnemonic, operands = rowmap[at]
            n, m = opcode >> 8 & 15, opcode >> 4 & 15
            operands = operands.split('!', 1)[0].strip()
            if mnemonic in ('ldc', 'ldc.l'):
                raise ValueError('SCI-next may not write interrupt/control registers: ' + name)
            if mnemonic in ('ocbp', 'ocbi', 'ocbwb', 'movca.l', 'pref') and not (
                    mode == 1 and name == '_kui_toy_loader_payload_call' and mnemonic == 'ocbp'):
                raise ValueError('SCI-next gained an out-of-scope cache operation: ' + name)
            address, width, store = None, None, False
            if opcode & 0xF00F in (0x6000, 0x6001, 0x6002):
                address, width = state[m], (1, 2, 4)[opcode & 15]
            elif opcode & 0xF00F in (0x6004, 0x6005, 0x6006):
                address, width = state[m], (1, 2, 4)[(opcode & 15) - 4]
            elif opcode & 0xF000 == 0x5000:
                address = None if state[m] is None else (state[m] + (opcode & 15) * 4) & 0xFFFFFFFF
                width = 4
            elif opcode & 0xF00F in (0x2000, 0x2001, 0x2002, 0x2004, 0x2005, 0x2006):
                width, store = (1, 2, 4)[(opcode & 15) % 4], True
                address = state[n]
                if opcode & 4 and address is not None:
                    address = (address - width) & 0xFFFFFFFF
            elif opcode & 0xF000 == 0x1000:
                width, store = 4, True
                address = None if state[n] is None else (state[n] + (opcode & 15) * 4) & 0xFFFFFFFF
            if address is not None and not 0x8C000000 <= address < 0x8D000000 and not 0xAC000000 <= address < 0xAD000000:
                if store:
                    raise ValueError('Probe wrote outside owned/guest main RAM: ' + hex(address))
                if allowed_reads.get(address) != width:
                    raise ValueError('Probe read an unreviewed MMIO address/width: ' + hex(address))
                reads.add((address, width))
            diagnostic = low['symbols']['_diagnostic']
            if store and address is not None and address < diagnostic + 64 and address + width > diagnostic:
                raise ValueError('SCI-next low diagnostic counters must remain readonly')
            stack_index = (16 + (address - stack_low) // 4
                           if address is not None and address % 4 == 0 and
                           stack_low <= address < stack_low + stack_cells * 4 else None)
            loaded = state[stack_index] if stack_index is not None and width == 4 else None
            if store and stack_index is not None:
                state[stack_index] = state[m] if width == 4 else None
            if opcode & 0xF00F in (0x2004, 0x2005, 0x2006):
                state[n] = address
            if opcode & 0xF000 in (0xD000, 0x9000) and mnemonic in ('mov.l', 'mov.w'):
                width = 4 if opcode & 0xF000 == 0xD000 else 2
                value = code.literal(at, n, width)[1]
                state[n] = (value if width == 4 else _signed(value, 16)) & 0xFFFFFFFF
            elif opcode & 0xF00F == 0x6003:
                state[n] = state[m]
            elif opcode & 0xF000 == 0xE000:
                state[n] = _signed(opcode & 255, 8) & 0xFFFFFFFF
            elif opcode & 0xF000 == 0x7000:
                state[n] = None if state[n] is None else (state[n] + _signed(opcode & 255, 8)) & 0xFFFFFFFF
            elif opcode & 0xF00F == 0x300C:
                state[n] = None if state[n] is None or state[m] is None else (state[n] + state[m]) & 0xFFFFFFFF
            elif mnemonic.startswith('mov.') and width is not None and not store:
                state[n] = loaded
            elif mnemonic not in ('cmp/eq', 'cmp/hs', 'cmp/hi', 'cmp/pl', 'cmp/pz', 'tst',
                                    'bt', 'bf', 'bt.s', 'bf.s', 'bra', 'bsr', 'jsr', 'jmp', 'rts', 'nop'):
                dest = re.search(r'(?:^|,)r(\d+)$', operands)
                if dest:
                    state[int(dest[1])] = None
            if '@r' in operands and '+' in operands:
                source = re.search(r'@r(\d+)\+', operands)
                if source:
                    source = int(source[1])
                    if not (mnemonic.startswith('mov.') and source == n):
                        state[source] = None if state[source] is None else (state[source] + (width or 4)) & 0xFFFFFFFF
            if mnemonic in ('sts.l', 'stc.l') and '@-r15' in operands:
                state[15] = None if state[15] is None else state[15] - 4
            return state

        while pending:
            at = pending.pop()
            state = list(incoming[at])
            _, opcode, mnemonic, _ = rowmap[at]
            if mnemonic in ('rte', 'trapa', 'bsrf', 'braf', 'jsr/n', 'jmp/n'):
                raise ValueError('Unreviewed probe transfer: ' + name)
            delayed = (opcode & 0xF000 in (0xA000, 0xB000) or
                       opcode & 0xFF00 in (0x8D00, 0x8F00) or
                       opcode & 0xF0FF in (0x400B, 0x402B) or opcode == 0x000B)
            if delayed:
                register = opcode >> 8 & 15
                target = state[register] if opcode & 0xF0FF in (0x400B, 0x402B) else None
                if at + 2 not in rowmap:
                    raise ValueError('Probe transfer has no executable delay slot')
                state = effect(at + 2, state)
                if opcode == 0x000B:
                    continue
                if opcode & 0xF000 in (0xA000, 0xB000):
                    target = at + 4 + _signed(opcode & 0xFFF, 12) * 2
                if opcode & 0xF000 == 0xB000 or opcode & 0xF0FF in (0x400B, 0x402B):
                    if target not in allowed:
                        raise ValueError('Probe gained an unknown/out-of-scope callback in ' + name +
                                         ' at ' + hex(at) + ': ' + str(target))
                    if (target == code.symbols.get('_kui_toy_loader_payload_call') and
                            state[4] != low['symbols']['_transfer_block']):
                        raise ValueError('SCI-next payload helper received an unproved original callback')
                    transfers.add(allowed[target])
                    if opcode & 0xF0FF == 0x402B:
                        continue
                    state[:8] = [None] * 8
                    merge(at + 4, state)
                elif opcode & 0xF000 == 0xA000:
                    merge(target, state)
                else:
                    merge(at + 4 + _signed(opcode & 255, 8) * 2, state)
                    merge(at + 4, state)
            elif opcode & 0xFF00 in (0x8900, 0x8B00):
                merge(at + 4 + _signed(opcode & 255, 8) * 2, state)
                merge(at + 2, state)
            else:
                merge(at + 2, effect(at, state))
        graph[name] = sorted(transfers)
    if reads != set(allowed_reads.items()):
        raise ValueError('DATA probe must retain the five exact passive clock reads')
    return {'closed_functions': graph, 'MMIO_reads': [{'address': hex(at), 'bytes': size}
            for at, size in sorted(reads)], 'MMIO_writes': False,
            'timer_configuration_writes': False, 'interrupt_control_register_writes': False,
            'low_diagnostic_writes': False,
            'low_storage_callbacks': sorted(low_callbacks.values()),
            'audio_callbacks': False, 'unknown_indirect_transfers': False}


def static_rows(report):
    """Parse one required nonempty report, including temporary mutation copies."""
    report = Path(report)
    if not report.is_file() or not report.read_bytes():
        raise ValueError('Missing or empty SCI-next static stack evidence: ' + str(report))
    result = []
    for line in report.read_text().splitlines():
        fields = line.split('\t')
        if len(fields) != 3 or fields[2] != 'static' or not fields[1].isdigit():
            raise ValueError('Nonstatic or malformed SCI-next stack evidence: ' + str(report))
        result.append({'function': fields[0].rsplit(':', 1)[-1],
                       'bytes': int(fields[1]), 'report': str(report)})
    if not result:
        raise ValueError('Missing or empty SCI-next static stack evidence: ' + str(report))
    return result


def audit_stack_completeness(directory, baseline):
    """The retained graph cannot lose frames by dropping or emptying a .su."""
    directory, baseline = Path(directory), Path(baseline)
    probe_reports = {'worker/src/loader/toy_loader_data_probe.su',
                     'worker/src/loader/toy_loader_payload_control.su'}
    candidate_reports = {path.relative_to(directory).as_posix()
                         for group in ('worker', 'stage', 'sci/lto')
                         for path in (directory / group).rglob('*.su')}
    if candidate_reports != set(RETAINED_STACK_REPORTS) | probe_reports:
        raise ValueError('Missing or unreviewed retained SCI-next stack report set')
    baseline_reports = {path.relative_to(baseline).as_posix()
                        for group in ('worker', 'stage', 'sci/lto')
                        for path in (baseline / group).rglob('*.su')}
    if baseline_reports != set(RETAINED_STACK_REPORTS) | {'worker/src/loader/toy_loader_data_probe.su'}:
        raise ValueError('Missing or unreviewed U reference stack report set')
    count = 0
    for name, expected in RETAINED_STACK_REPORTS.items():
        for root in (baseline, directory):
            rows = static_rows(root / name)
            tuples = [[row['function'], row['bytes']] for row in rows]
            actual = hashlib.sha256(json.dumps(tuples, separators=(',', ':')).encode()).hexdigest()
            if actual != expected:
                raise ValueError('Changed retained SCI-next static stack evidence: ' + name)
            if root == directory:
                count += len(rows)
    for name in probe_reports:
        static_rows(directory / name)
    return {'all_retained_reports_present_and_nonempty': True,
            'retained_function_frames_identical_to_published_U': True,
            'retained_report_count': len(RETAINED_STACK_REPORTS),
            'retained_static_row_count': count}


def audit_frames(directory, worker, disassembly, mode):
    reports = [Path(directory) / 'worker/src/loader/toy_loader_data_probe.su',
               Path(directory) / 'worker/src/loader/toy_loader_payload_control.su']
    rows = [row for path in reports for row in static_rows(path)]
    emitted = {name[1:] for name in REVIEWED[mode]}
    unlinked = {'kui_toy_loader_data_probe_word_count'}
    if mode == 0:
        unlinked.update({'kui_toy_loader_payload_call', 'kui_toy_loader_payload_control_counts'})
    if (len(rows) != len(emitted | unlinked) or
            {row['function'] for row in rows} != emitted | unlinked):
        raise ValueError('Missing or unreviewed SCI-next static stack evidence')
    code = Linked(worker, HIGH, disassembly)
    for row in rows:
        if row['function'] in unlinked:
            if row['bytes'] or '_' + row['function'] in worker['symbols']:
                raise ValueError('Unexpected linked dropped SCI-next accessor/frame')
            continue
        _, _, instructions = exact_function(code, '_' + row['function'])
        frame, stop_after = 0, None
        for index, (_, opcode, mnemonic, _) in enumerate(instructions):
            if stop_after is not None and index > stop_after:
                break
            if opcode & 0xFF0F == 0x2F06 or opcode == 0x4F22:
                frame += 4
            elif opcode & 0xFF00 == 0x7F00:
                frame -= _signed(opcode & 255, 8)
            if stop_after is None and mnemonic in ('bt', 'bf', 'bt.s', 'bf.s', 'bra', 'bsr', 'jsr', 'jmp', 'rts'):
                stop_after = index + int(mnemonic in ('bt.s', 'bf.s', 'bra', 'bsr', 'jsr', 'jmp', 'rts'))
        if row['bytes'] != frame:
            raise ValueError('SCI-next .su differs from actual linked frame: ' + row['function'])
    return {'emitted_functions': len(emitted), 'actual_linked_frames_verified': True,
            'frames': {row['function']: row['bytes'] for row in rows}}


def audit_helper(code, low, mode):
    """Exact reviewed bodies make these branch-specific ownership claims."""
    if mode == 0:
        if any(name in code.symbols for name in ('_kui_toy_loader_payload_call', '_payload_counts', '_payload_pio_scratch')):
            raise ValueError('Attribution-only V unexpectedly linked a payload experiment')
        return {'mode': 0, 'original_payload_direct': True, 'extra_cache_operations': False}
    trace.reviewed_identities(code,
        {name: REVIEWED[mode][name] for name in
            ('_kui_toy_loader_payload_call', '_kui_toy_loader_payload_control_counts')},
        'SCI-next payload helper')
    rows = exact_function(code, '_kui_toy_loader_payload_call')[2]
    cache_ops = [(at, opcode, mnemonic) for at, opcode, mnemonic, _ in rows
                 if mnemonic in ('ocbp', 'ocbi', 'ocbwb')]
    if mode == 1:
        if len(cache_ops) != 2 or any(mnemonic != 'ocbp' for _, _, mnemonic in cache_ops):
            raise ValueError('Cached W must retain exact pre/post OCBP loops only')
        return {'mode': 1, 'exact_low_image_block_only': True, 'block_bytes': 512,
                'P1_alias_same_physical_destination': True, 'CCR_readonly_mask': '0x105',
                'CCR_required_bits': '0x101', 'pre_and_post_exact_line_count': 16,
                'publication_on_success_and_failure': True, 'original_called_once': True,
                'raw_CDDA_route_unchanged': True}
    if cache_ops:
        raise ValueError('Programmed X must not add cache instructions')
    scratch = code.symbols.get('_payload_pio_scratch', 0)
    values = []
    for at, opcode, mnemonic, _ in rows:
        if opcode & 0xF000 == 0xD000 and mnemonic == 'mov.l':
            values.append(code.literal(at, opcode >> 8 & 15)[1])
    if scratch + 1 not in values:
        raise ValueError('Programmed X lost its exact intentionally unaligned private P2 destination')
    return {'mode': 2, 'exact_low_image_block_only': True, 'block_bytes': 512,
            'private_P2_scratch_bytes': 544, 'scratch_receive_offset': 1,
            'DMA_admission_declined_by_original_unaligned_address_check': True,
            'copy_only_after_original_success': True, 'copy_bytes': 512,
            'original_called_once': True, 'raw_CDDA_route_unchanged': True}


def audit_sci_next(builddir, baseline=None, mode=None):
    directory = Path(builddir).resolve()
    baseline = Path(baseline or ROOT / 'build/toy-u-reference').resolve()
    config, images, dis = load_profile(directory, mode)
    mode = int(config['DATA_PAYLOAD_MODE'])
    if not REVIEWED[mode]:
        raise ValueError('Missing independently reviewed SCI-next linked identities')
    for name in images:
        try:
            instruction_audit(name, dis[name])
        except SystemExit as exc:
            raise ValueError(str(exc)) from exc
    low, worker, stage = (images[name] for name in ('resident-sci', 'worker', 'stage'))
    code = Linked(worker, HIGH, dis['worker'])
    identities = trace.reviewed_identities(code, REVIEWED[mode], 'SCI-next')
    retained = audit_retained(directory, config, images, dis, baseline)
    completeness = audit_stack_completeness(directory, baseline)
    frames = audit_frames(directory, worker, dis['worker'], mode)
    stacks = trace.audit_stacks(directory, low, worker, dis['worker'])
    stacks['SCI_next_actual_frames'] = frames
    stacks['retained_evidence_completeness'] = completeness
    return {'profile': 'toy-loader-sci-next-R', 'build': config['BUILD'], 'mode': mode,
            'runtime': bind_runtime(directory, config, images),
            'frozen_sources': audit_frozen_sources(), 'retained': retained,
            'exports': trace.audit_exports(low, worker),
            'retained_trace': {'report_words': 416, 'passive_callgraph': trace.audit_trace_graph(code)},
            'DATA_probe': {'version': 2, 'report_words': 192, 'phase_words': 88,
                           'report_magic': '0x4c445031', 'tick_hz': 781250,
                           'reviewed_linked_identities': identities,
                           'bindings': audit_bindings(directory, low, code, mode),
                           'wire_schema': audit_schema(),
                           'state': audit_state(worker, mode),
                           'passive_callgraph': audit_probe_graph(code, low, mode)},
            'bridges': trace.audit_bridges(worker, dis['worker']),
            'publication': trace.audit_boot_publication(low, worker, stage, dis['resident-sci'], dis['stage']),
            'stage_linked_identities': trace.reviewed_identities(Linked(stage, STAGE, dis['stage']), trace.REVIEWED_STAGE, 'stage publication'),
            'adapter': u.audit_adapter(worker, dis['worker']), 'stacks': stacks,
            'payload_helper': audit_helper(code, low, mode),
            'retained_cache_audio_heap_proofs': audit_cache_layout(low, worker, stage, dis['resident-sci'], dis['worker'], dis['stage']),
            'hardware_behavior_verified': False, 'audio_or_video_improvement_proven': False}


def review_candidate(builddir, baseline=None, mode=None):
    directory = Path(builddir).resolve()
    baseline = Path(baseline or ROOT / 'build/toy-u-reference').resolve()
    config, images, dis = load_profile(directory, mode)
    old_config, old_images, old_dis, binding = audit_reference(baseline)
    code = Linked(images['worker'], HIGH, dis['worker'])
    old_code = Linked(old_images['worker'], HIGH, old_dis['worker'])
    old_names = u.function_symbols((baseline / 'worker.elf').read_bytes())
    names = (u.function_symbols((directory / 'worker.elf').read_bytes()) - old_names) | CHANGED_FUNCTIONS
    evidence = {}
    for name in sorted(names):
        begin, end, rows = exact_function(code, name)
        literals = []
        for at, opcode, mnemonic, _ in rows:
            if opcode & 0xF000 in (0xD000, 0x9000) and mnemonic in ('mov.l', 'mov.w'):
                location, value = code.literal(at, opcode >> 8 & 15, 4 if opcode & 0xF000 == 0xD000 else 2)
                literals.append({'instruction': hex(at), 'address': hex(location), 'value': hex(value)})
        evidence[name] = {'bytes': end - begin,
                         'candidate_normalized_sha256': trace.normalized_function(code, name),
                         'instructions': [[hex(at), hex(opcode), mnemonic, operands] for at, opcode, mnemonic, operands in rows],
                         'literals': literals}
    mismatches = {}
    for name in sorted(old_names - CHANGED_FUNCTIONS):
        before = u.scoped_normalized_function(old_code, name, U_BUILD)
        after = u.scoped_normalized_function(code, name, config['BUILD'])
        if before != after:
            mismatches[name] = {'U': before, 'candidate': after}
    return {'admitted': False, 'independent_review_required': True, 'config': config,
            'functions': evidence, 'retained_mismatches': mismatches,
            'baseline_runtime': binding,
            'BSS_objects': {name: {'address': hex(at), 'bytes': images['worker']['symbol_sizes'].get(name)}
                            for name, at in images['worker']['symbols'].items()
                            if HIGH + P2 <= at < HIGH_LIMIT + P2 and images['worker']['symbol_sizes'].get(name)},
            'stack_rows': stack_rows(list((directory / 'worker').rglob('*.su')))}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builddir', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--mode', type=int, choices=(0, 1, 2))
    parser.add_argument('--candidate', action='store_true')
    args = parser.parse_args()
    operation = review_candidate if args.candidate else audit_sci_next
    print(json.dumps(operation(args.builddir, args.baseline, args.mode), indent=2, sort_keys=True))
