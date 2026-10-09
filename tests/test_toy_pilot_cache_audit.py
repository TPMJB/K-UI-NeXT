#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent negative ELF/publication fixtures and real P2 GD scratch flow."""
import argparse
import copy
from pathlib import Path
import os
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from check_loader_layout import inspect_elf
from toy_pilot_cache_audit import (audit_cache_profiles, audit_frame_helper,
    audit_hook_publications, publication_lines, _load_cache_profile, Linked,
    LOW, HIGH, _early_policy, audit_private_native_scratch,
    audit_heap_saved_state, audit_native_metadata, audit_masked_hold_scope,
    audit_stage_alias_publication)
from test_toy_pilot_p2_layout import fixture, change_program, change_section
LINKED_BUILD = None


def helper_fixture():
    base, size = 0x8CFD0100, 28
    code = struct.pack('<12HI', 0xD305, 0x2139, 0x2239, 0xE3E0, 0x2139,
                       0x01A3, 0x7120, 0x3122, 0x8BFB, 0x000B, 0x0009,
                       0x0009, 0xDFFFFFFF)
    return {'payload': code, 'symbols': {'helper': base},
            'symbol_sizes': {'helper': size}}, base


def publication_fixture():
    base, helper = 0x8CFD0100, 0x8CFD0140
    instructions = (0x0102, 0xE20F, 0x4208, 0x4208, 0x221B, 0x420E,
                    0x4F22, 0x2F16, 0x61F3, 0x62F3, 0x7208,
                    0xB013, 0x0009, 0x4F07, 0x4F26, 0x000B, 0x0009)
    image = {'payload': struct.pack('<17H', *instructions) + bytes(64 - 34),
             'symbols': {'hook': base, 'helper': helper},
             'symbol_sizes': {'hook': 34}}
    return image, base


def production_function(source, signature):
    begin = source.index(signature)
    brace = source.index('{', begin)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]


def run_p2_scratch_flow():
    resident = (ROOT / 'src/loader/toy_pilot_resident.c').read_text()
    mapping = production_function(resident, 'static uint8_t *toy_map(')
    dispatch = production_function(resident, 'int32_t kui_retail_resident_dispatch(')
    harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "kui/toy_pilot.h"
#include "kui/toy_pilot_boot.h"
#include "kui/toy_pilot_scratch.h"
#define TOP 0x8cfd8000u
#define SP 0xacfd7d00u
#define PHYSICAL (SP-0x20000000u)
#define BEGIN 0x8cfcffe0u
#define END 0x8cfe0000u
static uint8_t ram[END-BEGIN];
static uint32_t toy_scratch;
static struct kui_retail_gd service;
volatile struct kui_toy_pilot_boot_control kui_toy_pilot_boot_control;
static uint32_t kui_retail_native_caller[2],kui_retail_hook_source;
static uint32_t map_calls,last_map;
static uint8_t *map_guest(void *unused,uint32_t address,uint32_t bytes,int writing) {
    (void)unused;(void)writing;++map_calls;last_map=address;
    assert(address>=0x8c000000u && address<0x8d000000u);
    if(address<BEGIN || address>=END || bytes>END-address) return 0;
    return ram+address-BEGIN;
}
static int32_t kui_toy_pilot_base_dispatch(uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    return kui_retail_gd_dispatch(&service,a,b,c,d);
}
/* MAPPING */
static int32_t adapter(struct kui_retail_gd *s,uint32_t a,uint32_t b,uint32_t d,uintptr_t base) {
    (void)base;return kui_retail_gd_dispatch(s,a,b,0,d);
}
/* DISPATCH */
static int extent(void *u,uint32_t a,uint32_t b,uint32_t c) {
    (void)u;(void)a;(void)b;(void)c;return 0;
}
static int read_sectors(void *u,uint32_t a,uint32_t b,uint32_t c,void *d) {
    (void)u;(void)a;(void)b;(void)c;(void)d;assert(0);return -1;
}
static int32_t check(uint32_t ptr,uint32_t pr,uint32_t sp) {
    kui_retail_native_caller[0]=pr;kui_retail_native_caller[1]=sp;
    int32_t result=kui_retail_resident_dispatch(130u,ptr,0,KUI_GD_CHECK);
    assert(!toy_scratch);return result;
}
int main(void) {
    const union kui_retail_slot tracks[]={
        {.track={.start_lba=0,.end_lba=8,.control=4}},
        {.track={.start_lba=16,.end_lba=24}},
        {.track={.start_lba=45000,.end_lba=60000,.control=4}}};
    const struct kui_gd_ops ops={0,toy_map,extent,read_sectors};
    assert(!kui_retail_gd_init(&service,tracks,3,&ops,0x8c008000u,0x8d000000u));
    assert((uintptr_t)adapter<=UINT32_MAX);
    kui_toy_pilot_boot_control=(struct kui_toy_pilot_boot_control){
        .status=KUI_TOY_BOOT_INSTALLED,.worker_end=TOP,.gd_dispatch=(uintptr_t)adapter};
    service.token=129;service.command=KUI_RETAIL_GD_PLAY;
    memset(ram,0xa5,sizeof(ram));
    assert(check(SP,0x8c0bd374u,SP)==KUI_GD_FAILED);
    assert(map_calls==1 && last_map==PHYSICAL);
    uint32_t error;memcpy(&error,ram+PHYSICAL-BEGIN,4);assert(error==5);
    for(unsigned i=0;i<sizeof(ram);i++)
        if(i<PHYSICAL-BEGIN || i>=PHYSICAL-BEGIN+16) assert(ram[i]==0xa5);
    assert(!toy_map(0,PHYSICAL,16,1));
    const uint32_t denied[]={PHYSICAL,0xacfcffe0u,0xacfd0000u,
        TOP-8192+0x20000000u,TOP-16+0x20000000u};
    uint8_t before[sizeof(ram)];memcpy(before,ram,sizeof(ram));
    for(unsigned i=0;i<sizeof(denied)/sizeof(*denied);i++) {
        unsigned old=map_calls;
        assert(check(denied[i],0x8c0bd374u,denied[i])==KUI_GD_FAILED);
        assert(map_calls==old && !memcmp(before,ram,sizeof(ram)));
    }
    assert(check(SP,0x8c0bd376u,SP)==KUI_GD_FAILED);
    assert(check(SP,0x8c0bd374u,SP+4)==KUI_GD_FAILED);
    assert(!memcmp(before,ram,sizeof(ram)));
    return 0;
}
'''.replace('/* MAPPING */', mapping).replace('/* DISPATCH */', dispatch)
    with tempfile.TemporaryDirectory(prefix='kui-p2-scratch-') as tmp:
        work = Path(tmp)
        source, binary = work / 'scratch.c', work / 'scratch'
        source.write_text(harness)
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-fno-pie', '-no-pie', '-DKUI_TOY_PILOT_PRIVATE_P2=1',
            '-I' + str(ROOT / 'include'), str(source),
            str(ROOT / 'src/core/retail_gd.c'), '-o', str(binary)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        subprocess.run([str(binary)], check=True, capture_output=True, text=True)


class CacheAuditTests(unittest.TestCase):
    def test_compact_P2_ELF(self):
        data = fixture()
        image = inspect_elf(data, 0x8C004000, 0x8C007800, private_p2=True)
        self.assertEqual(len(image['payload']), 48)

    def test_wrong_alias_executable_and_LMA_are_rejected(self):
        changes = ((change_program, 1, {'pa': 0xAC004020}),
                   (change_program, 1, {'flags': 7}),
                   (change_section, 2, {'address': 0x8C004020}),
                   (change_section, 1, {'address': 0xAC004000}))
        for change, index, values in changes:
            with self.subTest(values=values), self.assertRaises(ValueError):
                data = fixture();change(data, index, **values)
                inspect_elf(data, 0x8C004000, 0x8C007800, private_p2=True)

    def test_exact_compiled_helper_covers_all_word_aligned_frames(self):
        image, base = helper_fixture()
        result = audit_frame_helper(image, base, 'helper')
        self.assertEqual(result['compiled_alignment_cases'], 128)
        self.assertEqual(publication_lines(0xAC01001C, 52),
                         (0x8C010000, 0x8C010020, 0x8C010040))

    def test_frame_publication_material_mutations_rejected(self):
        image, base = helper_fixture()
        for offset, opcode in ((0, 0x0009), (2, 0x0009), (8, 0x0009),
                               (10, 0x0193), (12, 0x7140), (16, 0x8BFC),
                               (20, 0x6003)):
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                broken = copy.deepcopy(image);blob = bytearray(broken['payload'])
                struct.pack_into('<H', blob, offset, opcode);broken['payload'] = bytes(blob)
                audit_frame_helper(broken, base, 'helper')
        with self.assertRaises(ValueError):
            broken = copy.deepcopy(image);blob = bytearray(broken['payload'])
            struct.pack_into('<I', blob, 24, 0xFFFFFFFF);broken['payload'] = bytes(blob)
            audit_frame_helper(broken, base, 'helper')

    def test_missing_publication_and_wrong_SP_span_rejected(self):
        image, base = publication_fixture()
        self.assertEqual(len(audit_hook_publications(image, base, '', 'helper',
            {'hook': (8,)})['hook']), 1)
        for offset, opcode in ((22, 0x0009), (20, 0x7204), (10, 0x0009), (14, 0x0009)):
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                broken = copy.deepcopy(image);blob = bytearray(broken['payload'])
                struct.pack_into('<H', blob, offset, opcode);broken['payload'] = bytes(blob)
                audit_hook_publications(broken, base, '', 'helper', {'hook': (8,)})

    def test_real_core_canonicalizes_P2_and_preserves_exclusion(self):
        run_p2_scratch_flow()

    def test_linked_gate_mutations(self):
        if LINKED_BUILD is None:
            self.skipTest('linked image mutations require --build-dir')
        native, images, dis = _load_cache_profile(LINKED_BUILD)
        low = Linked(images['resident-sci'], LOW, dis['resident-sci'])
        worker = Linked(images['worker'], HIGH, dis['worker'])
        stage = Linked(images['stage'], 0x8CE00000, dis['stage'])
        mutations = []
        def opcode(code, name, expected):
            begin, end = code.bounds(name)
            hits = [at for at, _, _, _ in code.rows if begin <= at < end and
                    at not in code.literals and code.half(at) == expected]
            self.assertEqual(len(hits), 1)
            return hits[0]
        def add(image, base, address, value, width, check, label):
            mutations.append((image, base, address, value, width, check, label))
        scratch_check = lambda image: audit_private_native_scratch(
            image, images['worker'], dis['resident-sci'])
        # The narrowly added decoder must execute XOR's actual operand;
        # replacing XOR #1 with XOR #0 changes P2 admission and is rejected.
        add('resident-sci', LOW, opcode(low, '_kui_retail_resident_dispatch', 0xCA01),
            0xCA00, 2, scratch_check, 'P2 alias boolean XOR changed')
        add('resident-sci', LOW, opcode(low, '_toy_map', 0x3510), 0x3550, 2,
            scratch_check, 'protected owner exclusion bypass')
        add('resident-sci', LOW, opcode(low, '_toy_map', 0x8810), 0x8814, 2,
            scratch_check, 'scratch extent enlarged')
        add('resident-sci', LOW, opcode(low, '_kui_toy_pilot_heap_hook', 0x4A0E),
            9, 2, lambda image: audit_heap_saved_state(image, dis['resident-sci']),
            'post-native first-save mask omitted')
        add('resident-sci', LOW, opcode(low, '_kui_toy_pilot_heap_hook', 0x7234),
            0x7230, 2, lambda image: audit_heap_saved_state(image, dis['resident-sci']),
            'heap live publication misses MAC word')
        add('resident-sci', LOW, opcode(low, '_kui_toy_pilot_heap_hook', 0x4F16),
            9, 2, lambda image: audit_heap_saved_state(image, dis['resident-sci']),
            'MAC restore omitted')
        metadata = lambda image: audit_native_metadata(image, dis['worker'])
        site = worker.find('_kui_toy_pilot_bus_publish', (0x21A1, 0x71F4, 0x01A3))
        add('worker', HIGH, site + 4, 9, 2, metadata, 'producer publication omitted')
        add('worker', HIGH, opcode(worker, '_kui_toy_pilot_lease_allocate', 0x02A3),
            9, 2, metadata, 'allocator record publication omitted')
        add('worker', HIGH, opcode(worker, '_kui_toy_pilot_lease_allocate', 0x1273),
            0x1203, 2, metadata, 'allocator active commit omitted')
        revoke = worker.find('_kui_toy_pilot_worker_revoke', (None, 0x4F22, 0x410B))
        literal = worker.literal(revoke, 1)[0]
        add('worker', HIGH, literal, 0x8C06AA0E, 4,
            lambda image: audit_masked_hold_scope(image, dis['worker']),
            'HOLD callee replaced with native SDK target')
        hold = worker.find('_kui_toy_pilot_am_init_hook', (None, 0x400B, 0x0009, 0x4F07))
        add('worker', HIGH, hold + 6,
            9, 2, lambda image: audit_masked_hold_scope(image, dis['worker']),
            'HOLD exact SR pop omitted')
        stage_check = lambda image: audit_stage_alias_publication(image, dis['stage'])
        resume = stage.symbols['_kui_retail_game_resume']
        add('stage', stage.base, resume + 12, 9, 2, stage_check,
            'whole RAM handoff purge omitted')
        install = stage.find('_kui_toy_pilot_stage_install', (None, 0x558D, 0x5493, 0x4A0B))
        add('stage', stage.base, install + 2, 0x558A, 2, stage_check,
            'worker pre-purge stops at BSS instead of private stack end')
        memcmp = stage.find('_kui_toy_pilot_stage_install',
            (None, 0x66B3, 0x5493, None, None, 0x400B, 0x342C))
        literal = stage.literal(memcmp + 6, 2)[0]
        add('stage', stage.base, literal, 0, 4, stage_check,
            'verification refills P1 writable state')
        policy = stage.symbols['_kui_retail_stage_relay']
        original = [(at, op) for at, op, _, _ in stage.rows if at >= policy and
                    at not in stage.literals and op & 0xFF00 == (0x9300 if native else 0x9200)
                    and stage.literal(at, 3 if native else 2, 2)[1] == 0x105]
        self.assertEqual(len(original), 1)
        literal = stage.literal(original[0][0], 3 if native else 2, 2)[0]
        add('stage', stage.base, literal, 0x101, 2,
            lambda image: _early_policy(image, dis['stage'], native),
            'original native cache-word admission weakened')
        for name, base, address, value, width, check, label in mutations:
            with self.subTest(mutation=label), self.assertRaises(ValueError):
                broken = copy.deepcopy(images[name])
                payload = bytearray(broken['payload'])
                payload[address-base:address-base+width] = value.to_bytes(width, 'little')
                broken['payload'] = bytes(payload)
                check(broken)
        self.assertEqual(len(mutations), 15)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    args = parser.parse_args()
    LINKED_BUILD = args.build_dir
    result = unittest.TextTestRunner(verbosity=1).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(CacheAuditTests))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.build_dir:
        print(__import__('json').dumps(audit_cache_profiles(args.build_dir), sort_keys=True))
    print('Toy cache audit: negative alias/LMA/publication fixtures and real P2 GD scratch pass')
