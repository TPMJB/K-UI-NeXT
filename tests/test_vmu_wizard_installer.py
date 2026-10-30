#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real CLI install/restore and interrupted-publication recovery on a temp SD."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from runtime_package import envelope

spec = importlib.util.spec_from_file_location('wizard_installer', ROOT/'tools/install_vmu_wizard.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class WizardInstaller(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kui-wizard-installer-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.card = self.work/'card'
        self.package = self.work/'package'
        self.runtime = envelope(b'old-ui!!', 4096, '1562c62b5330')
        self.new = envelope(b'new-ui!!', 8192, '0123456789ab')
        self.foreign = envelope(b'other-ui', 4096, 'abcdefabcdef')
        self.physical = envelope(b'physical'+installer.PHYSICAL_DESCRIPTOR,4096,'0123456789ab')
        self.dual = envelope(b'dual-vmu', 4096, '1734830d08df')
        (self.card/'KUI/apps/games').mkdir(parents=True)
        (self.package/'gui').mkdir(parents=True)
        (self.card/installer.RUNTIME).write_bytes(self.runtime)
        (self.card/installer.DUAL).write_bytes(self.dual)
        (self.package/'gui/runtime.kui').write_bytes(self.new)
        (self.package/'physical').mkdir()
        (self.package/'physical/retail-physical.kui').write_bytes(self.physical)
        (self.package/'wizard.json').write_bytes(installer.wire(dict(
            schema=installer.SCHEMA, source_commit='0123456789ab'+'c'*28, build='0123456789ab',
            runtime_sha256=installer.sha(self.new),physical_sha256=installer.sha(self.physical),
            replaces=[str(path) for path in installer.TARGETS.values()],
            preserved_native_modules=[str(path) for path in installer.NATIVE_MODULES],
            console_compatibility_verified=False)))
        shutil.copyfile(ROOT/'tools/install_vmu_wizard.py', self.package/'install.py')
        for name in ('KUI/native-general/backup/gui.kui', 'KUI/native-general/state.json',
                     'KUI/vmu-test/p0005/card.vmd', 'KUI/vmu-test/p0005/card.journal',
                     'KUI/vmu-test/p0005/identity.bin',
                     'KUI/vmu-test/p0005/retail-persistent-profile-0005.bin',
                     'KUI/apps/games/launch-settings-save.bin', 'KUI/apps/games/retail-boot.kui',
                     'KUI/apps/games/retail-native-standalone.kui',
                     'KUI/native-vmu-only/state.json', 'KUI/native-vmu-only/backup/original.kui',
                     'KUI/native-vmu-only-skies/state.json','KUI/skies-launch-ui/state.json',
                     'KUI/skies-next-loader/state.json','KUI/skies-next-ui/state.json',
                     'KUI/skies-read-fix-loader/state.json','KUI/skies-background72/background/state.json',
                     'KUI/skies-background72/background/backup/original.kui','KUI/skies-background72-ui/state.json',
                     'KUI/vmu-wizard/checkpoints/111111111111/state.json',
                     'KUI/vmu-wizard/checkpoints/111111111111/runtime-before.kui'):
            path = self.card/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(envelope(b'old-game',4096,'d6d517fa133a') if Path(name) in installer.NATIVE_MODULES
                             else ('protected '+name).encode())
        self.protected = self.snapshot()

    def snapshot(self):
        return {str(path.relative_to(self.card)): path.read_bytes()
                for path in self.card.rglob('*') if path.is_file() and
                path.relative_to(self.card) not in installer.TARGETS.values() and
                str(path.relative_to(self.card)) != 'KUI/vmu-wizard/installer.lock' and
                not str(path.relative_to(self.card)).startswith('KUI/vmu-wizard/checkpoints/0123456789ab/')}

    def cli(self, *args, passed=True):
        result = subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card), *args],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, passed, result.stdout+result.stderr)
        self.assertEqual(self.snapshot(), self.protected)
        return result

    def checkpoint(self):
        return self.card/installer.PARENT/'checkpoints/0123456789ab'

    def test_install_reinstall_restore_exact_previous_ui(self):
        installed = self.cli()
        self.assertIn('Home > VMU tools > Import saves', installed.stdout)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.new)
        self.assertEqual((self.card/installer.PHYSICAL).read_bytes(),self.physical)
        self.assertEqual((self.checkpoint()/'runtime-before.kui').read_bytes(), self.runtime)
        receipt = (self.checkpoint()/'state.json').read_bytes()
        self.assertIn('"changed": []', self.cli().stdout)
        self.assertEqual((self.checkpoint()/'state.json').read_bytes(), receipt)
        self.cli('--restore')
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.runtime)
        self.assertIn('"changed": []', self.cli('--restore').stdout)
        self.cli()
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.new)

    def test_foreign_valid_ui_is_backed_up_on_first_install(self):
        (self.card/installer.RUNTIME).write_bytes(self.foreign)
        self.cli()
        self.cli('--restore')
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.foreign)

    def test_foreign_later_update_is_never_overwritten(self):
        self.cli()
        (self.card/installer.RUNTIME).write_bytes(self.foreign)
        for args in ((), ('--restore',)):
            result = self.cli(*args, passed=False)
            self.assertIn('changed outside this wizard', result.stderr)
            self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.foreign)

    def test_corrupt_checkpoint_is_refused(self):
        self.cli()
        (self.checkpoint()/'runtime-before.kui').write_bytes(self.foreign)
        self.cli('--restore', passed=False)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.new)

    def test_corrupt_native_module_is_refused_before_publication(self):
        (self.card/installer.DUAL).write_bytes(b'damaged-module')
        self.protected = self.snapshot()
        self.cli(passed=False)
        self.assertFalse(self.checkpoint().exists())
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.runtime)

    def test_compatible_existing_builds_and_absent_modules_are_supported(self):
        for build in ('34dc7b5e15ee','1734830d08df','abcdefabcdef'):
            (self.card/installer.DUAL).write_bytes(envelope(b'dual-vmu',4096,build))
            self.protected=self.snapshot()
            self.cli();self.cli('--restore')
        for relative in installer.NATIVE_MODULES:(self.card/relative).unlink()
        self.protected=self.snapshot()
        self.cli();self.cli('--restore')
        for relative in installer.NATIVE_MODULES:self.assertFalse((self.card/relative).exists())

    def test_gui_restore_preserves_later_valid_loader_change_or_removal(self):
        self.cli()
        (self.card/installer.DUAL).write_bytes(envelope(b'new-dual',4096,'abcdefabcdef'))
        (self.card/installer.NATIVE_MODULES[1]).unlink()
        self.protected=self.snapshot()
        self.cli('--restore')
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(),self.runtime)

    def test_corrupt_package_is_refused(self):
        (self.package/'gui/runtime.kui').write_bytes(self.foreign)
        self.cli(passed=False)
        self.assertFalse(self.checkpoint().exists())

    def test_wrong_package_source_or_scope_is_refused_before_checkpoint(self):
        path=self.package/'wizard.json';original=path.read_bytes();record=json.loads(original)
        for key,value in (('source_commit','d'*40),('source_commit',123),
                          ('replaces',[str(installer.RUNTIME),str(installer.DUAL)]),
                          ('native_modules_updated',True),('console_compatibility_verified',True),
                          ('required_dual_build','1734830d08df')):
            changed=dict(record);changed[key]=value;path.write_bytes(installer.wire(changed))
            self.cli(passed=False)
            self.assertFalse(self.checkpoint().exists())
            self.assertEqual((self.card/installer.RUNTIME).read_bytes(),self.runtime)
        path.write_bytes(original)

    def test_install_without_games_directory_preserves_absent_existing_loaders(self):
        shutil.rmtree(self.card/'KUI/apps')
        self.protected=self.snapshot()
        self.cli();self.cli('--restore')
        self.assertFalse((self.card/installer.PHYSICAL).exists())
        for relative in installer.NATIVE_MODULES:self.assertFalse((self.card/relative).exists())

    def test_restore_without_checkpoint_is_refused(self):
        self.cli('--restore', passed=False)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.runtime)

    def test_symlink_runtime_is_refused_without_touching_external_file(self):
        outside = self.work/'outside.kui'
        outside.write_bytes(self.runtime)
        (self.card/installer.RUNTIME).unlink()
        (self.card/installer.RUNTIME).symlink_to(outside)
        self.cli(passed=False)
        self.assertEqual(outside.read_bytes(), self.runtime)
        self.assertFalse(self.checkpoint().exists())

    def test_symlink_parent_or_checkpoint_is_refused(self):
        self.cli()
        checkpoint = self.checkpoint()
        moved = self.work/'checkpoint'
        checkpoint.rename(moved)
        checkpoint.symlink_to(moved, target_is_directory=True)
        self.cli('--restore', passed=False)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.new)

    def test_symlink_card_root_is_refused(self):
        alias = self.work/'alias'
        alias.symlink_to(self.card, target_is_directory=True)
        with self.assertRaises(ValueError):
            installer.run(alias, self.package)
        self.assertFalse(self.checkpoint().exists())

    def test_interrupt_after_pending_receipt_recovers_from_old_runtime(self):
        real = installer.publish
        def interrupted(path, data, root):
            real(path, data, root)
            if path.name == 'state.json' and json.loads(data)['pending']:
                raise OSError('injected power loss after pending receipt')
        with mock.patch.object(installer, 'publish', interrupted):
            with self.assertRaises(OSError):
                installer.run(self.card, self.package)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.runtime)
        self.cli()
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.new)

    def test_failure_after_runtime_replace_rolls_back_both_original_states(self):
        real=installer.publish;injected=False
        def interrupted(path,data,root):
            nonlocal injected
            real(path,data,root)
            if path==self.card/installer.RUNTIME and not injected:
                injected=True;raise OSError('injected after GUI rename')
        with mock.patch.object(installer,'publish',interrupted):
            with self.assertRaises(OSError):installer.run(self.card,self.package)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(),self.runtime)
        self.assertFalse((self.card/installer.PHYSICAL).exists())
        self.cli('--restore')
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(),self.runtime)

    def test_interrupt_before_final_receipt_can_finish_install(self):
        real = installer.publish
        def interrupted(path, data, root):
            if path.name == 'state.json' and not json.loads(data)['pending']:
                raise OSError('injected final receipt failure')
            real(path, data, root)
        with mock.patch.object(installer, 'publish', interrupted):
            with self.assertRaises(OSError):
                installer.run(self.card, self.package)
        self.cli()
        self.assertFalse(json.loads((self.checkpoint()/'state.json').read_bytes())['pending'])

    def test_external_change_after_pending_receipt_is_preserved(self):
        real = installer.publish
        def changed(path, data, root):
            real(path, data, root)
            if path.name == 'state.json' and json.loads(data)['pending']:
                (self.card/installer.RUNTIME).write_bytes(self.foreign)
        with mock.patch.object(installer, 'publish', changed):
            with self.assertRaisesRegex(ValueError, 'changed before runtime replacement'):
                installer.run(self.card, self.package)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.foreign)
        self.cli('--restore', passed=False)

    def test_existing_physical_reader_is_backed_up_and_restored_exactly(self):
        old=envelope(b'old-phys',4096,'abcdefabcdef')
        (self.card/installer.PHYSICAL).write_bytes(old)
        self.cli()
        self.assertEqual((self.checkpoint()/'physical-before.kui').read_bytes(),old)
        self.cli('--restore')
        self.assertEqual((self.card/installer.PHYSICAL).read_bytes(),old)

    def test_corrupt_or_symlink_physical_predecessor_is_preserved(self):
        path=self.card/installer.PHYSICAL;path.write_bytes(b'corrupt-physical')
        self.cli(passed=False)
        self.assertEqual(path.read_bytes(),b'corrupt-physical')
        self.assertFalse(self.checkpoint().exists())
        outside=self.work/'outside-physical.kui';outside.write_bytes(self.foreign)
        path.unlink();path.symlink_to(outside)
        self.cli(passed=False)
        self.assertEqual(outside.read_bytes(),self.foreign)
        self.assertFalse(self.checkpoint().exists())

    def test_changed_physical_reader_is_never_overwritten(self):
        self.cli();foreign=envelope(b'new-phys',4096,'abcdefabcdef')
        (self.card/installer.PHYSICAL).write_bytes(foreign)
        self.cli('--restore',passed=False)
        self.assertEqual((self.card/installer.PHYSICAL).read_bytes(),foreign)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(),self.new)

    def test_interrupted_mixed_gui_physical_states_restore_exactly(self):
        self.cli()
        path=self.checkpoint()/'state.json';state=json.loads(path.read_text())
        state['pending']=True
        state['allowed']={'runtime':[installer.sha(self.runtime),installer.sha(self.new)],
                          'physical':[None,installer.sha(self.physical)]}
        path.write_bytes(installer.wire(state))
        (self.card/installer.RUNTIME).write_bytes(self.runtime)
        self.cli('--restore')
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(),self.runtime)
        self.assertFalse((self.card/installer.PHYSICAL).exists())

    def test_physical_package_requires_exact_zero_vmu_descriptor(self):
        data=envelope(b'not-phys',4096,'0123456789ab')
        (self.package/'physical/retail-physical.kui').write_bytes(data)
        path=self.package/'wizard.json';record=json.loads(path.read_text())
        record['physical_sha256']=installer.sha(data);path.write_bytes(installer.wire(record))
        self.cli(passed=False)
        self.assertFalse(self.checkpoint().exists())

    def test_present_native_symlink_is_refused_before_checkpoint(self):
        outside=self.work/'outside-native.kui';outside.write_bytes(self.dual)
        (self.card/installer.DUAL).unlink();(self.card/installer.DUAL).symlink_to(outside)
        before=outside.read_bytes()
        result=subprocess.run([sys.executable,str(self.package/'install.py'),str(self.card)],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(outside.read_bytes(),before)
        self.assertFalse(self.checkpoint().exists())

    def test_module_added_after_pending_receipt_is_not_overwritten(self):
        (self.card/installer.DUAL).unlink();self.protected=self.snapshot()
        real=installer.publish
        def changed(path,data,root):
            real(path,data,root)
            if path.name=='state.json' and json.loads(data)['pending']:
                (self.card/installer.DUAL).write_bytes(self.dual)
        with mock.patch.object(installer,'publish',changed):
            with self.assertRaisesRegex(ValueError,'changed before runtime replacement'):
                installer.run(self.card,self.package)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(),self.runtime)
        self.assertEqual((self.card/installer.DUAL).read_bytes(),self.dual)

    def test_module_corruption_after_pending_receipt_refuses_runtime_replace(self):
        real=installer.publish
        def changed(path,data,root):
            real(path,data,root)
            if path.name=='state.json' and json.loads(data)['pending']:
                (self.card/installer.NATIVE_MODULES[1]).write_bytes(b'corrupt')
        with mock.patch.object(installer,'publish',changed):
            with self.assertRaises(ValueError):installer.run(self.card,self.package)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(),self.runtime)

    def test_no_write_repeated_install_and_restore(self):
        for restore in (False,True):
            installer.run(self.card,self.package,restore)
            with mock.patch.object(installer,'publish',side_effect=AssertionError('unexpected publication')):
                self.assertEqual(installer.run(self.card,self.package,restore)['changed'],[])

    def test_incomplete_checkpoint_does_not_replace_runtime(self):
        real = installer.publish
        def interrupted(path, data, root):
            real(path, data, root)
            if path.name == 'runtime-before.kui':
                raise OSError('injected checkpoint write failure')
        with mock.patch.object(installer, 'publish', interrupted):
            with self.assertRaises(OSError):
                installer.run(self.card, self.package)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.runtime)
        self.assertFalse(self.checkpoint().exists())
        self.cli()


class WizardPackageBindings(unittest.TestCase):
    def setUp(self):
        import package_vmu_wizard
        self.tool=package_vmu_wizard
        self.commit='0123456789ab'+'c'*28
        self.temp=tempfile.TemporaryDirectory(prefix='wizard-gui-proof-');self.addCleanup(self.temp.cleanup)
        self.directory=Path(self.temp.name)
        self.runtime=envelope(b'new-ui!!',8192,self.commit[:12])
        from retail_package import RAM_PATCH_SEED
        self.physical=envelope(b'physical'+installer.PHYSICAL_DESCRIPTOR+RAM_PATCH_SEED,4096,self.commit[:12])
        self.elf=b'synthetic linked artifact fixture'
        (self.directory/'runtime.kui').write_bytes(self.runtime)
        (self.directory/'kui-runtime.elf').write_bytes(self.elf)
        (self.directory/'kui-runtime.map').write_bytes(b'nonempty synthetic map fixture')
        self.proof=dict(commit=self.commit,source_dirty=False,elf_sha256=self.tool.sha(self.elf))
        self.write_proof()

    def write_proof(self):
        (self.directory/'kui-runtime.compile.json').write_text(json.dumps(self.proof))

    def test_exact_two_target_manifest_preserves_all_existing_game_modules(self):
        record=self.tool.package_metadata(self.runtime,self.physical,self.commit)
        self.assertEqual(record['schema'],installer.SCHEMA)
        self.assertEqual(record['replaces'],[str(path) for path in installer.TARGETS.values()])
        self.assertEqual(record['preserved_native_modules'],[str(path) for path in installer.NATIVE_MODULES])
        self.assertEqual(record['physical_sha256'],installer.sha(self.physical))
        self.assertNotIn('required_dual_build',record)
        self.assertEqual(record['source_commit'],self.commit)

    def test_exact_gui_linked_source_build_and_payload_are_required(self):
        with mock.patch.object(self.tool,'flatten_elf',return_value=(self.runtime[64:],8192)):
            files=self.tool.gui_artifacts(self.directory/'runtime.kui',self.commit)
            self.assertEqual(set(files),{'runtime.kui','kui-runtime.elf','kui-runtime.map','kui-runtime.compile.json'})
        for payload,memory in ((b'wrongpayload',8192),(self.runtime[64:],4096)):
            with mock.patch.object(self.tool,'flatten_elf',return_value=(payload,memory)):
                with self.assertRaises(ValueError):self.tool.gui_artifacts(self.directory/'runtime.kui',self.commit)

    def test_dirty_stale_or_wrong_elf_provenance_refuses(self):
        for key,value in (('source_dirty',True),('commit','f'*40),('elf_sha256','d'*64)):
            saved=dict(self.proof);self.proof[key]=value;self.write_proof()
            with self.assertRaises(ValueError):self.tool.gui_artifacts(self.directory/'runtime.kui',self.commit)
            self.proof=saved;self.write_proof()

    @staticmethod
    def actual_shaped_stacks():
        return {'scif':dict(conservative_bytes=1144,available_bytes=1232,assembly_allowance=256,retained_c_frames=49),
                'sci':dict(conservative_bytes=1184,available_bytes=1232,assembly_allowance=256,retained_c_frames=42),
                'ide':dict(conservative_bytes=1148,available_bytes=1232,assembly_allowance=256,retained_c_frames=50),
                'scia':dict(worst_bytes=220,available_bytes=304,margin=64,call_graph=True)}

    def test_actual_stack_report_shapes_preserve_each_existing_stack_gate(self):
        self.tool.validate_resident_stacks(self.actual_shaped_stacks())
        for name in ('scif','sci','ide'):
            for field,value in (('conservative_bytes',1233),('conservative_bytes',-1),
                                ('assembly_allowance',0),('retained_c_frames',0),('available_bytes',True)):
                stacks=self.actual_shaped_stacks();stacks[name][field]=value
                with self.subTest(name=name,field=field,value=value):
                    with self.assertRaises(ValueError):self.tool.validate_resident_stacks(stacks)
        for field,value in (('worst_bytes',305),('worst_bytes',241),('worst_bytes',-1),
                            ('margin',-1),('margin',0),('call_graph',False),('available_bytes',True)):
            stacks=self.actual_shaped_stacks();stacks['scia'][field]=value
            with self.subTest(name='scia',field=field,value=value):
                with self.assertRaises(ValueError):self.tool.validate_resident_stacks(stacks)
        stacks=self.actual_shaped_stacks();stacks['sci']={'margin':48}
        with self.assertRaises(ValueError):self.tool.validate_resident_stacks(stacks)

    def test_physical_audit_requires_zero_worker_and_exact_linked_descriptor(self):
        report=dict(schema='kui.native.physical-standard.audit.v1',
            source_provenance={'commit':self.commit,'source_dirty':False},runtime={'build':self.commit[:12]},
            module_sha256=installer.sha(self.physical),reader=0,transport_mask=7,lease_bytes=0,worker_bytes=0,
            virtual_cards=0,background_reader_enabled=False,prefetch_enabled=False,console_execution_verified=False,
            existing_plain_resident_bytes_identical=True,
            descriptor={'module_offset':72,'bytes':32,'allocated_read_only':True,'unique_complete_descriptor':True},
            standard_only_runtime_guard=True,compiler_callgraphs={'complete_files':1},mutation_rejections=36,
            layout={**{name:{'payload_bytes':8,'unresolved_symbols':0} for name in
                ('resident-scif','resident-sci','resident-ide','resident-scia','stage','entry')},
                'resident_stacks':self.actual_shaped_stacks()},
            instruction_audit={name:1 for name in ('resident-scif','resident-sci','resident-ide','resident-scia','stage','entry')})
        from check_retail_physical import GUARD
        report['standard_only_guard']=dict(exact_immutable_body=True,body_bytes=len(GUARD),body_sha256=installer.sha(GUARD),
            actual_stage_call_bound=True,actual_stage_result_checked=True,instruction_cases=81,reader=0,transport_mask=7)
        report['ram_patch_seed_blank']=True
        report['ram_patch_seed']=dict(module_offset=104,bytes=64,initialized_data=True,blank=True)
        self.tool.validate_physical(report,self.commit,self.physical)
        for key,value in (('reader',1),('transport_mask',2),('worker_bytes',73728),('lease_bytes',73728),
                          ('virtual_cards',1),('background_reader_enabled',True),('prefetch_enabled',True),
                          ('existing_plain_resident_bytes_identical',False),('module_sha256','d'*64),
                          ('standard_only_runtime_guard',False),('mutation_rejections',0),
                          ('compiler_callgraphs',{'complete_files':0}),('instruction_audit',{}),
                          ('standard_only_guard',{}),('ram_patch_seed_blank',False)):
            changed=dict(report);changed[key]=value
            with self.assertRaises(ValueError):self.tool.validate_physical(changed,self.commit,self.physical)
        changed=dict(report,descriptor=dict(report['descriptor'],allocated_read_only=False))
        with self.assertRaises(ValueError):self.tool.validate_physical(changed,self.commit,self.physical)

    def test_wrong_runtime_source_identifier_or_missing_evidence_refuses(self):
        with self.assertRaises(ValueError):self.tool.package_metadata(self.runtime,self.physical,'d'*40)
        with self.assertRaises(ValueError):self.tool.package_metadata(self.runtime,self.physical,self.commit[:12])
        with self.assertRaisesRegex(ValueError,'targeted host evidence'):
            self.tool.package(self.directory/'runtime.kui',self.directory/'out')


if __name__ == '__main__':
    unittest.main()
