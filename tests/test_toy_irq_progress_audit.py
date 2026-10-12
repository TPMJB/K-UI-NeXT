#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed configuration, source and actual-target startup IRQ progress checks."""
import copy
import hashlib
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import toy_irq_progress_audit as audit


class ConfigurationTests(unittest.TestCase):
    def config(self, profile=1):
        return {**audit.REQUIRED_CONFIG, 'BUILD': '012345abcdef',
                'STARTUP_IRQ_PROFILE': str(profile)}

    def write(self, config, extra=''):
        area = tempfile.TemporaryDirectory()
        path = Path(area.name)
        (path / 'build-config').write_text(''.join(f'{k}={v}\n' for k, v in config.items()) + extra)
        return area, path

    def test_two_profiles_exact_limits_and_units(self):
        for profile,(name,blocks,ticks,steps) in audit.PROFILES.items():
            area,path=self.write(self.config(profile))
            with self.subTest(profile=profile),area:
                self.assertEqual(audit.read_config(path,profile)['STARTUP_IRQ_PROFILE'],str(profile))
                budget,audio=audit.budget_metadata(profile),audit.audio_policy(profile)
                self.assertEqual((budget['name'],budget['profile'],budget['credit_limit'],budget['tick_limit'],budget['finite_step_limit']),
                                 (name,profile+12,blocks,ticks,steps))
                self.assertEqual(budget['credit_unit'],'verified 512-byte physical card blocks')
                self.assertEqual((audio['refill_quanta'],audio['service_ticks']),(4,12500))
                self.assertFalse(audio['changed_from_AI'])
                self.assertEqual(budget['tick_hz'],781250)
                self.assertTrue(budget['one_step_may_cross_tick_limit'])
                self.assertTrue(audio['one_quantum_may_cross_tick_limit'])

    def test_unreviewed_modes_routes_and_extra_keys_refused(self):
        mutations = [('STARTUP_IRQ_PROFILE', '0'), ('STARTUP_IRQ_PROFILE', '3'), ('DATA_TUNING_PROFILE', '2'),
            ('AD_FOLLOWUP_PROFILE','1'), ('AD_FOLLOWUP_PROFILE','3'),
            ('DATA_BUDGET_PROFILE', '1'), ('DATA_SCHEDULE_MODE', '1'),
            ('DATA_SCHEDULE_MODE', '3'), ('DATA_PAYLOAD_MODE', '1'),
            ('SHARED_SCI', '1'), ('ASYNC_CDDA', '1'), ('GD_FIXED_STEP', '1'),
            ('PRIVATE_P2', '0'), ('NATIVE_CACHE', '1'), ('SCI_REUSE_TDRE', '1'),
            ('LOADER_TRACE', '0'), ('DATA_PROBE', '0'), ('BUILD', 'abcdef'), ('EXTRA', '1')]
        for key, value in mutations:
            config = self.config(); config[key] = value
            area, path = self.write(config)
            with self.subTest(key=key, value=value), area, self.assertRaises(ValueError):
                audit.read_config(path)

    def test_duplicate_missing_and_wrong_profile_refused(self):
        config = self.config(); config.pop('DATA_PROBE')
        area, path = self.write(config)
        with area, self.assertRaisesRegex(ValueError, 'missing/unreviewed'):
            audit.read_config(path)
        area, path = self.write(self.config(), 'STARTUP_IRQ_PROFILE=1\n')
        with area, self.assertRaisesRegex(ValueError, 'Duplicate'):
            audit.read_config(path)
        area, path = self.write(self.config())
        with area, self.assertRaisesRegex(ValueError, 'differs'):
            audit.read_config(path, 2)

    def test_only_exact_experimental_functions_can_change(self):
        for profile in audit.PROFILES:
            self.assertEqual(audit.changed_functions(profile,'resident-sci'),set())
            self.assertEqual(audit.changed_functions(profile,'stage'),set())
            self.assertEqual(audit.changed_functions(profile,'worker'),
                {'_kui_toy_pilot_sci_init','_hook','_unhook'} |
                ({'_kui_toy_pilot_sci_service'} if profile == 2 else set()))
            for name in ('_dispatch_body','_kui_toy_pilot_worker_initialize','_fill_step',
                         '_kui_toy_pilot_sci_irq','_kui_toy_pilot_worker_step'):
                self.assertNotIn(name,audit.changed_functions(profile,'worker'))


class SourceAndSchemaTests(unittest.TestCase):
    def test_all_guarded_schema_profiles(self):
        for profile in audit.PROFILES:
            with self.subTest(profile=profile):
                schema = audit.audit_schema(profile)
                self.assertEqual((schema['version'], schema['words'], schema['record_words']), (2, 192, 18))
                self.assertEqual(schema['record_start'] + schema['records'] * schema['record_words'], schema['words'])
                self.assertEqual(schema['budget_stats_start'] + schema['budget_stats_words'], schema['record_start'])
                self.assertEqual(schema['reported_budget_profile'], profile + 12)
                self.assertEqual(schema['TRACE_words'], 416)

    def test_schema_and_independent_factor_constant_mutations_refused(self):
        changes=(
            ('include/kui/toy_loader_schedule_report.h','REPORT_WORDS 192u','REPORT_WORDS 208u',1),
            ('include/kui/toy_loader_schedule_report.h','RECORD_WORDS 18u','RECORD_WORDS 17u',1),
            ('include/kui/toy_loader_irq_progress.h','SCI_NIBBLE 0x70','SCI_NIBBLE 0x10',1),
            ('include/kui/toy_loader_irq_progress.h','SCI_LEVEL 0x0070u','SCI_LEVEL 0x0010u',1),
            ('include/kui/toy_loader_irq_progress.h','SCI_PRIORITY 7','SCI_PRIORITY 6',1),
            ('include/kui/toy_loader_irq_progress.h','(12u + KUI_TOY_PILOT_STARTUP_IRQ_PROFILE)',
             '(13u + KUI_TOY_PILOT_STARTUP_IRQ_PROFILE)',1),
            ('src/loader/toy_pilot_sci.h','SERVICE_STEPS 1u','SERVICE_STEPS 2u',2),
            ('include/kui/toy_loader_data_tuning.h','TUNING_STEPS 2048u','TUNING_STEPS 0u',1),
            ('include/kui/toy_loader_data_tuning.h','TUNING_TICKS 12000u','TUNING_TICKS 12001u',1),
            ('include/kui/toy_loader_ad_followup.h','REFILL_QUANTA 4u','REFILL_QUANTA 5u',1),
            ('include/kui/toy_loader_ad_followup.h','REFILL_TICKS 12500u','REFILL_TICKS 12501u',1))
        for name,before,after,profile in changes:
            with self.subTest(name=name,profile=profile),tempfile.TemporaryDirectory() as area:
                root=Path(area);shutil.copytree(ROOT/'include',root/'include')
                (root/'src/loader').mkdir(parents=True)
                for source in (ROOT/'src/loader').glob('*.h'):
                    shutil.copy2(source,root/'src/loader'/source.name)
                target=root/name;original=target.read_text();self.assertIn(before,original)
                target.write_text(original.replace(before,after))
                with self.assertRaisesRegex(ValueError,'schema constant'):
                    audit.audit_schema(profile,root)

    def test_source_discovery_never_grants_review(self):
        observed = audit.source_identities()
        self.assertEqual(set(observed), set(audit.REVIEW_SOURCE_FILES))
        with patch.dict(audit.REVIEWED_SOURCE_SHA256, {}, clear=True), \
             self.assertRaisesRegex(ValueError, 'Missing independently reviewed'):
            audit.audit_sources()

    def test_all_historical_runtime_sources_are_covered(self):
        self.assertTrue(set(audit.budget_parent.REVIEW_SOURCE_FILES) <= set(audit.REVIEW_SOURCE_FILES))
        self.assertIn('include/kui/toy_loader_ad_followup.h', audit.REVIEW_SOURCE_FILES)
        self.assertIn('include/kui/toy_loader_data_tuning.h', audit.REVIEW_SOURCE_FILES)
        self.assertTrue(set(audit.af_parent.REVIEW_SOURCE_FILES)<=set(audit.REVIEW_SOURCE_FILES))
        for name in ('src/loader/retail_resident.c', 'src/loader/toy_pilot_gd.c',
                     'src/loader/toy_loader_trace_report.c', 'src/loader/toy_pilot_worker.c',
                     'src/loader/toy_pilot_resident.c'):
            self.assertIn(name, audit.REVIEW_SOURCE_FILES)

    def test_changed_reviewed_source_refused(self):
        with tempfile.TemporaryDirectory() as area:
            root = Path(area); (root / 'source.c').write_text('original\n')
            digest = hashlib.sha256(b'original\n').hexdigest()
            with patch.object(audit, 'REVIEW_SOURCE_FILES', ('source.c',)), \
                 patch.dict(audit.REVIEWED_SOURCE_SHA256, {'source.c': digest}, clear=True), \
                 patch.dict(audit.tuning_parent.REVIEWED_SOURCE_SHA256, {}, clear=True), \
                 patch.dict(audit.schedule.FROZEN_SOURCES, {}, clear=True), \
                 patch.object(audit, 'audit_frozen_gates', return_value={}):
                self.assertEqual(audit.audit_sources(root)['reviewed_irq_sha256'], {'source.c': digest})
                (root / 'source.c').write_text('changed\n')
                with self.assertRaisesRegex(ValueError, 'Changed independently reviewed'):
                    audit.audit_sources(root)

    def test_full_SCI_bus_pin_rejects_mutation_inside_guarded_health_export(self):
        name='src/loader/sci_sd_bus.c'
        self.assertIn(name,audit.REVIEW_SOURCE_FILES)
        expected=audit.REVIEWED_SOURCE_SHA256[name]
        with tempfile.TemporaryDirectory() as area:
            root=Path(area);target=root/name;target.parent.mkdir(parents=True)
            target.write_bytes((ROOT/name).read_bytes())
            with patch.object(audit,'REVIEW_SOURCE_FILES',(name,)), \
                    patch.dict(audit.REVIEWED_SOURCE_SHA256,{name:expected},clear=True), \
                    patch.dict(audit.tuning_parent.REVIEWED_SOURCE_SHA256,{},clear=True), \
                    patch.dict(audit.schedule.FROZEN_SOURCES,{},clear=True), \
                    patch.object(audit,'audit_frozen_gates',return_value={}):
                self.assertEqual(audit.audit_sources(root)['reviewed_irq_sha256'][name],expected)
                original=target.read_text()
                before='kui_toy_pilot_startup_sci_healthy(void) {\n    return port.acquired && !port.fault;'
                self.assertIn(before,original)
                target.write_text(original.replace(before,
                    'kui_toy_pilot_startup_sci_healthy(void) {\n    return true;'))
                with self.assertRaisesRegex(ValueError,'Changed independently reviewed'):
                    audit.audit_sources(root)

    def test_original_gate_tools_are_unchanged(self):
        self.assertEqual(audit.audit_frozen_gates(), audit.FROZEN_GATE_SHA256)
        with tempfile.TemporaryDirectory() as area:
            root = Path(area); (root / 'tools').mkdir()
            for name in audit.FROZEN_GATE_SHA256:
                shutil.copy2(ROOT / 'tools' / name, root / 'tools' / name)
            target = root / 'tools/toy_data_tuning_audit.py'
            target.write_bytes(target.read_bytes() + b'\n# changed\n')
            with self.assertRaisesRegex(ValueError, 'immutable parent admission tool'):
                audit.audit_frozen_gates(root)

    def test_empty_review_tables_always_refuse_admission(self):
        for profile in audit.PROFILES:
            with self.subTest(profile=profile), patch.dict(audit.REVIEWED, {profile: {}}), \
                 self.assertRaisesRegex(ValueError, 'Missing independently reviewed'):
                audit.reviewed_shapes({'STARTUP_IRQ_PROFILE': str(profile)}, {}, {}, {})


class TargetLinkedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('sh-elf-objdump'):
            raise unittest.SkipTest('Target objdump required for emitted mutation fixtures')
        cls.paths = {p: ROOT / 'build' / ('irq-' + values[0].lower())
                     for p, values in audit.PROFILES.items()}
        if not all((path / 'retail-toy-pilot.kui').is_file() for path in cls.paths.values()):
            raise unittest.SkipTest('Build all four final startup IRQ progress profiles before linked fixtures')
        cls.profiles = {p: audit.load_profile(path, p) for p, path in cls.paths.items()}

    def test_candidate_mode_is_unadmitted_and_cannot_fill_review_tables(self):
        before = copy.deepcopy((audit.REVIEWED, audit.REVIEWED_STACK_ROWS, audit.REVIEWED_RUNTIME_SHA256,
                                audit.REVIEWED_SOURCE_SHA256, audit.REVIEWED_OBJECTS))
        for profile, path in self.paths.items():
            with self.subTest(profile=profile):
                result = audit.candidate(path, profile=profile)
                self.assertFalse(result['admitted']); self.assertTrue(result['independent_review_required'])
                self.assertTrue(result['retained']['baseline_admitted_by_original_gate'])
                self.assertEqual(result['retained']['baseline_runtime']['sha256'], audit.AI_RUNTIME_SHA256)
        self.assertEqual(before, (audit.REVIEWED, audit.REVIEWED_STACK_ROWS, audit.REVIEWED_RUNTIME_SHA256,
                                  audit.REVIEWED_SOURCE_SHA256, audit.REVIEWED_OBJECTS))

    def test_final_candidates_require_independent_runtime_review(self):
        for profile, path in self.paths.items():
            with self.subTest(profile=profile):
                if not audit.REVIEWED_RUNTIME_SHA256[profile]:
                    with self.assertRaisesRegex(ValueError, 'independently reviewed immutable'):
                        audit.audit_irq_progress(path, profile=profile)
                else:
                    proof = audit.audit_irq_progress(path, profile=profile)
                    self.assertTrue(proof['admitted'])
                    self.assertEqual(proof['runtime']['sha256'], audit.REVIEWED_RUNTIME_SHA256[profile])
                    self.assertEqual(proof['schedule_schema']['reported_budget_profile'], profile + 12)
                    self.assertFalse(proof['hardware_behavior_verified'])
                    for name, limit in [('low',1232), ('high_GD',4000), ('audio_worker',8096)]:
                        self.assertLessEqual(proof['stacks']['conservative_sums'][name]['bytes'], limit)
                    self.assertEqual(len(proof['emitted_frames']), 189)

    def test_changed_runtime_pin_refused(self):
        for profile, path in self.paths.items():
            with self.subTest(profile=profile), patch.dict(audit.REVIEWED_RUNTIME_SHA256, {profile: '0'*64}), \
                 self.assertRaisesRegex(ValueError, 'immutable artifact'):
                audit.audit_irq_progress(path, profile=profile)

    def test_original_AI_reference_bytes_and_stack_cache_cannot_change(self):
        # The unchanged historical stack reader requires report paths within
        # its frozen source tree. Mutate an isolated temporary sibling copy,
        # never the admitted AD directory or its actual build artifacts.
        with tempfile.TemporaryDirectory(dir=audit.PARENT_ROOT/'build',
                                         prefix='irq-reference-test-') as area:
            path=Path(area); original=audit.DEFAULT_AI_BASELINE
            for name,_ in audit._reference_signature(original):
                target=path/name;target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(original/name,target)
            self.assertEqual(audit.reference_ai(path)[3]['runtime']['sha256'],audit.AI_RUNTIME_SHA256)
            runtime=path/'retail-toy-pilot.kui';saved=runtime.read_bytes()
            changed=bytearray(saved);changed[-1]^=1;runtime.write_bytes(changed)
            with self.assertRaisesRegex(ValueError,'original immutable gate'):
                audit.reference_ai(path)
            runtime.write_bytes(saved)
            stack=path/'worker/src/loader/toy_pilot_worker.su';stack_saved=stack.read_bytes()
            stack.write_text('')
            with self.assertRaisesRegex(ValueError,'original immutable gate'):
                audit.reference_ai(path)
            stack.write_bytes(stack_saved)
            header=path/'toy_pilot_resident_symbols.h'
            header.write_bytes(header.read_bytes()+b'\n/* changed */\n')
            with self.assertRaisesRegex(ValueError,'original immutable gate'):
                audit.reference_ai(path)

    def test_each_reviewed_body_instruction_mutation_refused(self):
        for profile, (config, images, dis) in self.profiles.items():
            if not audit.REVIEWED[profile]:
                continue
            retained = audit.compare_retained(self.paths[profile], config, images, dis)
            for image_name, names in audit.REVIEWED[profile].items():
                for name in names:
                    changed = copy.deepcopy(images)
                    base = {'resident-sci':audit.LOW,'worker':audit.HIGH,'stage':audit.STAGE}[image_name]
                    at = audit.budget_parent.physical(changed[image_name]['symbols'][name]) - base
                    payload = bytearray(changed[image_name]['payload']); payload[at] ^= 1
                    changed[image_name]['payload'] = bytes(payload)
                    with self.subTest(profile=profile, function=name), self.assertRaises(ValueError):
                        audit.reviewed_shapes(config, changed, dis, retained)

    def test_compiled_finite_step_guard_cannot_be_disabled(self):
        for profile,(config,images,dis) in self.profiles.items():
            code=audit.Linked(images['worker'],audit.HIGH,dis['worker'])
            begin,_,rows=audit.schedule.bounded_function(code,'_kui_toy_pilot_sci_service')
            changed=copy.deepcopy(images['worker']);payload=bytearray(changed['payload'])
            if profile==1:
                loads=[code.literal(at,0,2)[0] for at,op,mn,_ in rows
                       if op&0xff00==0x9000 and mn=='mov.w' and code.literal(at,0,2)[1]==2048]
                self.assertEqual(len(loads),1)
                struct.pack_into('<H',payload,loads[0]-audit.HIGH,0xFFFF)
            else:
                self.assertEqual(code.half(begin+0xb4),0xe101)
                struct.pack_into('<H',payload,begin+0xb4-audit.HIGH,0xe100)
            changed['payload']=bytes(payload)
            with self.subTest(profile=profile),self.assertRaises(ValueError):
                audit.audit_foreground_policy(changed,dis['worker'],profile)

    def test_profile0_only_admits_original_AI_and_candidates_never_admit(self):
        proof=audit.audit_irq_progress(audit.DEFAULT_AI_BASELINE,profile=0)
        self.assertTrue(proof['admitted'])
        self.assertTrue(proof['original_immutable_AI_gate'])
        self.assertEqual(proof['startup_irq_profile'],0)
        self.assertEqual(proof['runtime']['sha256'],audit.AI_RUNTIME_SHA256)
        discovered=audit.candidate(audit.DEFAULT_AI_BASELINE,profile=0)
        self.assertFalse(discovered['admitted'])
        self.assertEqual(discovered['runtime']['sha256'],audit.AI_RUNTIME_SHA256)
        with self.assertRaises(ValueError):
            audit.audit_irq_progress(self.paths[1],profile=0)

    def test_C_priority_change_scope_rejects_old_nibble_masks_and_other_opcodes(self):
        for profile,(_,images,dis) in self.profiles.items():
            worker=images['worker'];code=audit.Linked(worker,audit.HIGH,dis['worker'])
            self.assertEqual(set(audit.audit_priority_bodies(worker,dis['worker'])),{'_hook','_unhook'})
            for name,offset,opcode in [('_hook',0x1a,0x8810),('_hook',0x2a,0xcb10),
                    ('_unhook',0x22,0x8810),('_hook',0x18,0xc9e0),
                    ('_hook',0x2c,0x2311),('_unhook',0x30,0x2712)]:
                begin,_,_=audit.schedule.bounded_function(code,name)
                changed=copy.deepcopy(worker);payload=bytearray(changed['payload'])
                struct.pack_into('<H',payload,begin+offset-audit.HIGH,opcode)
                changed['payload']=bytes(payload)
                with self.subTest(profile=profile,function=name,offset=offset),self.assertRaises(ValueError):
                    audit.audit_priority_bodies(changed,dis['worker'])

    def test_assembly_priority_scope_retains_exact_register_mask_and_return(self):
        for profile,(_,images,dis) in self.profiles.items():
            worker=images['worker'];begin=worker['symbols']['_kui_toy_pilot_sci_rehook']
            self.assertEqual(audit.PriorityLinked(worker).half(begin+0x4c),0xca10)
            for offset,opcode in [(0x4c,0xca10),(0x4c,0xca60),(0x4e,0xc9e0),
                    (0x50,0x6311),(0x54,0x2131),(0x58,0x64f6),(0x5e,0x000b)]:
                changed=copy.deepcopy(worker);payload=bytearray(changed['payload'])
                struct.pack_into('<H',payload,begin+offset-audit.HIGH,opcode)
                changed['payload']=bytes(payload)
                with self.subTest(profile=profile,offset=offset,opcode=opcode),self.assertRaises(ValueError):
                    audit.audit_priority_irq(self.paths[profile],changed,dis['worker'])

    def test_direct_PriorityLinked_scope_refuses_ca10_ca71_and_adjacent_mutations(self):
        for profile,(_,images,_) in self.profiles.items():
            worker=images['worker'];begin=worker['symbols']['_kui_toy_pilot_sci_rehook']
            for offset,opcode in [(0x4c,0xca10),(0x4c,0xca71),(0x4e,0xc9e0),
                    (0x50,0x6311),(0x54,0x2131)]:
                changed=copy.deepcopy(worker);payload=bytearray(changed['payload'])
                struct.pack_into('<H',payload,begin+offset-audit.HIGH,opcode)
                changed['payload']=bytes(payload)
                with self.subTest(profile=profile,offset=offset,opcode=opcode),self.assertRaises(ValueError):
                    linked=audit.PriorityLinked(changed)
                    audit.schedule.shared.irq_vectors(linked)

    def test_C_priority_scopes_reject_mutations_with_synchronized_disassembly(self):
        for profile,(_,images,dis) in self.profiles.items():
            worker=images['worker'];code=audit.Linked(worker,audit.HIGH,dis['worker'])
            for name,offset,opcode,mn,arg in [
                    ('_hook',0x1a,0x8810,'cmp/eq','#16,r0'),
                    ('_hook',0x2a,0xcb10,'or','#16,r0'),
                    ('_unhook',0x22,0x8810,'cmp/eq','#16,r0'),
                    ('_hook',0x18,0xc9e0,'and','#224,r0'),
                    ('_hook',0x2c,0x2311,'mov.w','r1,@r3'),
                    ('_unhook',0x30,0x2712,'mov.l','r1,@r7')]:
                begin,_,_=audit.schedule.bounded_function(code,name);at=begin+offset
                changed=copy.deepcopy(worker);payload=bytearray(changed['payload'])
                struct.pack_into('<H',payload,at-audit.HIGH,opcode);changed['payload']=bytes(payload)
                lines=dis['worker'].splitlines();matches=0
                for index,line in enumerate(lines):
                    if line.lstrip().startswith(f'{at:x}:'):
                        lines[index]=f'{at:x}:\t{opcode&255:02x} {opcode>>8:02x}\t{mn}\t{arg}'
                        matches+=1
                self.assertEqual(matches,1);synchronized='\n'.join(lines)+'\n'
                checked=audit.Linked(changed,audit.HIGH,synchronized)
                self.assertEqual(next(op for address,op,_,_ in checked.rows if address==at),opcode)
                with self.subTest(profile=profile,function=name,offset=offset),self.assertRaises(ValueError):
                    audit.audit_priority_bodies(changed,synchronized)

    def test_missing_empty_or_changed_review_object_tables_cannot_admit(self):
        for profile,path in self.paths.items():
            original=copy.deepcopy(audit.REVIEWED_OBJECTS[profile])
            variants=[{}, {'worker':original['worker']},
                {'resident-sci':original['resident-sci']},
                {'worker':{},'resident-sci':original['resident-sci']}]
            missing=copy.deepcopy(original);missing['worker'].pop('_e');variants.append(missing)
            resized=copy.deepcopy(original);resized['worker']['_e']+=4;variants.append(resized)
            for number,table in enumerate(variants):
                with self.subTest(profile=profile,variant=number), \
                        patch.dict(audit.REVIEWED_OBJECTS,{profile:table}), \
                        self.assertRaisesRegex(ValueError,'owned-object tables'):
                    audit.audit_irq_progress(path,profile=profile)

    def test_retained_comparison_checks_pointer_stable_protocol_and_readonly_contents(self):
        config,images,dis=self.profiles[1]
        old_config,old_images,old_dis,_=audit.reference_ai()
        code=audit.Linked(images['worker'],audit.HIGH,dis['worker'])
        before=audit.Linked(old_images['worker'],audit.HIGH,old_dis['worker'])
        name='_kui_retail_gd_dispatch'
        begin,end,_=audit.schedule.bounded_function(code,name)
        old_begin,old_end,_=audit.schedule.bounded_function(before,name)
        self.assertEqual(code.data(begin,end-begin),before.data(old_begin,old_end-old_begin))
        _,value=code.literal(begin+0x712,5)
        terminal,_,_=audit.schedule.bounded_function(code,'_kui_toy_loader_trace_terminal')
        _,order=code.literal(terminal+0x20,5)
        for address in (value,value+26,value+27,order,order+15):
            changed=copy.deepcopy(images);payload=bytearray(changed['worker']['payload'])
            payload[address-audit.HIGH]^=1;changed['worker']['payload']=bytes(payload)
            with self.subTest(readonly_address=address),self.assertRaises(ValueError):
                audit.compare_retained(self.paths[1],config,changed,dis)

    def test_exact_GCC_divider_exception_binds_body_helper_and_BSR_target(self):
        old_config,old_images,old_dis,_=audit.reference_ai()
        name='___udivsi3';before=audit.Linked(old_images['worker'],audit.HIGH,old_dis['worker'])
        expected=audit.function_identity(before,name,old_config['BUILD'])
        for profile,(config,images,dis) in self.profiles.items():
            code=audit.Linked(images['worker'],audit.HIGH,dis['worker'])
            begin,_,rows=audit.schedule.bounded_function(code,name)
            self.assertEqual(audit.function_identity(code,name,config['BUILD']),expected)
            call=next(at for at,op,_,_ in rows if op&0xf000==0xb000)
            for address in (begin,begin-34,begin-32,begin-16,call):
                changed=copy.deepcopy(images);payload=bytearray(changed['worker']['payload'])
                payload[address-audit.HIGH]^=1;changed['worker']['payload']=bytes(payload)
                with self.subTest(profile=profile,address=address),self.assertRaises(ValueError):
                    audit.compare_retained(self.paths[profile],config,changed,dis)

    def test_AM_foreground_service_must_remain_complete_original_AI(self):
        config,images,dis=self.profiles[1];changed=copy.deepcopy(images)
        at=images['worker']['symbols']['_kui_toy_pilot_sci_service']-audit.HIGH
        payload=bytearray(changed['worker']['payload']);payload[at]^=1
        changed['worker']['payload']=bytes(payload)
        with self.assertRaises(ValueError):
            audit.compare_retained(self.paths[1],config,changed,dis)

    def test_retained_low_raw_driver_and_installer_mutations_refused(self):
        config, images, dis = self.profiles[1]
        names = {'resident-sci': ('_read_sectors', '_kui_toy_pilot_read_raw'),
                 'worker': ('_kui_toy_pilot_worker_initialize', '_fill_step'),
                 'stage': ('_kui_toy_pilot_stage_install',)}
        for image_name, symbols in names.items():
            for name in symbols:
                self.assertIn(name, images[image_name]['symbols'])
                changed = copy.deepcopy(images)
                base = {'resident-sci':audit.LOW,'worker':audit.HIGH,'stage':audit.STAGE}[image_name]
                at = audit.budget_parent.physical(changed[image_name]['symbols'][name]) - base
                payload = bytearray(changed[image_name]['payload']); payload[at] ^= 1
                changed[image_name]['payload'] = bytes(payload)
                with self.subTest(image=image_name,function=name), self.assertRaises(ValueError):
                    audit.compare_retained(self.paths[1], config, changed, dis)

    def test_audio_worker_cannot_change_in_any_tuning_profile(self):
        for profile in audit.PROFILES:
            config,images,dis=self.profiles[profile];changed=copy.deepcopy(images)
            at=audit.budget_parent.physical(changed['worker']['symbols']['_kui_toy_pilot_worker_step'])-audit.HIGH
            payload=bytearray(changed['worker']['payload']);payload[at]^=1;changed['worker']['payload']=bytes(payload)
            with self.subTest(profile=profile),self.assertRaises(ValueError):
                audit.compare_retained(self.paths[profile],config,changed,dis)

    def test_DATA_work_and_IRQ_pump_cannot_change(self):
        for profile in audit.PROFILES:
            config,images,dis=self.profiles[profile]
            for name in ('_data_work','_kui_toy_pilot_sci_irq'):
                changed=copy.deepcopy(images)
                at=audit.budget_parent.physical(changed['worker']['symbols'][name])-audit.HIGH
                payload=bytearray(changed['worker']['payload']);payload[at]^=1;changed['worker']['payload']=bytes(payload)
                with self.subTest(profile=profile,function=name),self.assertRaises(ValueError):
                    audit.compare_retained(self.paths[profile],config,changed,dis)

    def test_exact_private_objects_and_extra_object_refused(self):
        config, images, dis = self.profiles[1]
        for name in ('_e', '_schedule_report', '_schedule_visit'):
            changed = copy.deepcopy(images)
            record = next(r for r in changed['worker']['sized_symbol_records'] if r['name']==name)
            record['bytes'] += 4
            with self.subTest(object=name), self.assertRaisesRegex(ValueError, 'object inventory'):
                audit.compare_retained(self.paths[1], config, changed, dis)
        changed = copy.deepcopy(images)
        changed['worker']['sized_symbol_records'].append({'name':'_unreviewed_object','bytes':4,'type':1,
            'address':audit.HIGH+audit.P2,'occurrence':0})
        with self.assertRaisesRegex(ValueError, 'object inventory'):
            audit.compare_retained(self.paths[1], config, changed, dis)

    def test_IRQ_guard_alias_and_actual_frame_mutations_refused(self):
        profile=1; _,images,dis=self.profiles[profile]
        for symbol,delta in [('__toy_sci_irq_stack_top',32),('__toy_sci_irq_stack_bottom',-audit.P2)]:
            changed=copy.deepcopy(images['worker']);changed['symbols'][symbol]+=delta
            with self.subTest(symbol=symbol),self.assertRaises(ValueError):
                audit.audit_priority_irq(self.paths[profile],changed,dis['worker'])
        inventory=audit.schedule.stack_inventory(self.paths[profile])
        self.assertEqual(len(audit.schedule.audit_emitted_frames(self.paths[profile],images,dis,inventory)),189)
        changed=copy.deepcopy(inventory)
        report='worker/src/loader/toy_pilot_worker.su'
        row=next(r for r in changed[report] if r[0]=='kui_toy_pilot_worker_step');row[1]+=4
        with self.assertRaisesRegex(ValueError,'actual linked frame'):
            audit.schedule.audit_emitted_frames(self.paths[profile],images,dis,changed)

    def test_scoped_VBR_and_binary_protocol_remain_strict(self):
        config,images,dis=self.profiles[1];code=audit.Linked(images['worker'],audit.HIGH,dis['worker'])
        for name,offset,register in [('_hook',4,7),('_unhook',16,1)]:
            begin,_,_=audit.schedule.bounded_function(code,name);pool=code.literal(begin+offset,register)[0]
            changed=copy.deepcopy(images['worker']);payload=bytearray(changed['payload'])
            struct.pack_into('<I',payload,pool-audit.HIGH,changed['symbols']['_kui_toy_pilot_sci_vectors']-0x100+4)
            changed['payload']=bytes(payload)
            with self.subTest(function=name),self.assertRaises(ValueError):
                audit.function_identity(audit.Linked(changed,audit.HIGH,dis['worker']),name,config['BUILD'])
        begin,_,_=audit.schedule.bounded_function(code,'_kui_retail_gd_dispatch')
        value=code.literal(begin+0x712,5)[1]
        protocol=bytes.fromhex('4744432056657273696f6e20312e313020313939392d30332d33310200')
        for offset in (0,len(protocol)-2,len(protocol)-1):
            changed=copy.deepcopy(images['worker']);payload=bytearray(changed['payload'])
            payload[value-audit.HIGH+offset]^=1;changed['payload']=bytes(payload)
            with self.subTest(protocol_byte=offset),self.assertRaisesRegex(ValueError,'protocol bytes'):
                audit.function_identity(audit.Linked(changed,audit.HIGH,dis['worker']),
                                        '_kui_retail_gd_dispatch',config['BUILD'])

    def test_AD_v2_terminal_relocation_preserves_exact_identity(self):
        old,images,dis,_=audit.reference_ai()
        before=audit.Linked(images['worker'],audit.HIGH,dis['worker'])
        expected=audit.function_identity(before,'_kui_toy_loader_trace_terminal',old['BUILD'])
        for profile,(config,images,dis) in self.profiles.items():
            with self.subTest(profile=profile):
                actual=audit.function_identity(audit.Linked(images['worker'],audit.HIGH,dis['worker']),
                    '_kui_toy_loader_trace_terminal',config['BUILD'])
                self.assertEqual(actual,expected)

    def test_AD_v2_terminal_scoped_order_and_opcode_mutations_refused(self):
        config,images,dis=self.profiles[2]
        code=audit.Linked(images['worker'],audit.HIGH,dis['worker'])
        name='_kui_toy_loader_trace_terminal';begin,_,_=audit.schedule.bounded_function(code,name)
        pool,value=code.literal(begin+0x20,5)
        for mutation in ['register','pool_target','array_word','array_last_byte','readonly_bounds']:
            changed=copy.deepcopy(images['worker']);payload=bytearray(changed['payload'])
            if mutation=='register':payload[begin+0x20-audit.HIGH+1]^=1
            elif mutation=='pool_target':struct.pack_into('<I',payload,pool-audit.HIGH,value+4)
            elif mutation=='array_word':payload[value-audit.HIGH]^=1
            elif mutation=='array_last_byte':payload[value-audit.HIGH+15]^=1
            else:changed['readonly_ranges']=[]
            changed['payload']=bytes(payload)
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):
                audit.function_identity(audit.Linked(changed,audit.HIGH,dis['worker']),name,config['BUILD'])
        # Synchronize the decoded row too: the explicit scope must reject a
        # changed register even without the separate stale-disassembly check.
        changed=copy.deepcopy(images['worker']);payload=bytearray(changed['payload'])
        payload[begin+0x20-audit.HIGH+1]^=1;changed['payload']=bytes(payload)
        mutated=audit.Linked(changed,audit.HIGH,dis['worker'])
        mutated.rows=[(at,mutated.half(at),mn,arg) if at==begin+0x20 else (at,op,mn,arg)
                      for at,op,mn,arg in mutated.rows]
        with self.assertRaisesRegex(ValueError,'opcode/register'):
            audit.function_identity(mutated,name,config['BUILD'])

    def test_AD_v2_terminal_strings_and_bound_BUILD_contents_cannot_change(self):
        config,images,dis=self.profiles[2];code=audit.Linked(images['worker'],audit.HIGH,dis['worker'])
        terminal='_kui_toy_loader_trace_terminal'
        original=audit.function_identity(code,terminal,config['BUILD'])
        begin,_,_=audit.schedule.bounded_function(code,terminal)
        for offset in (0x64,0x6A,0x70,0x74,0x86):
            _,value=code.literal(begin+offset,4)
            changed=copy.deepcopy(images['worker']);payload=bytearray(changed['payload'])
            payload[value-audit.HIGH]^=1;changed['payload']=bytes(payload)
            mutated=audit.Linked(changed,audit.HIGH,dis['worker'])
            with self.subTest(label_offset=offset):
                try:identity=audit.function_identity(mutated,terminal,config['BUILD'])
                except ValueError:continue
                self.assertNotEqual(identity,original)
        needle=b'BUILD '+config['BUILD'].encode()+b'\0'
        at=images['worker']['payload'].find(needle)
        self.assertGreaterEqual(at,0)
        name='_retail_display_restore';expected=audit.function_identity(code,name,config['BUILD'])
        changed=copy.deepcopy(images['worker']);payload=bytearray(changed['payload'])
        payload[at+len(b'BUILD ')]^=1;changed['payload']=bytes(payload)
        try:actual=audit.function_identity(audit.Linked(changed,audit.HIGH,dis['worker']),name,config['BUILD'])
        except ValueError:return
        self.assertNotEqual(actual,expected)


if __name__ == '__main__':
    unittest.main()
