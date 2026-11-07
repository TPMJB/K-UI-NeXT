#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Guard exact manual Games inventory, source/input freeze and no-overwrite."""
import importlib.util
from pathlib import Path
import tempfile
import subprocess
import types
import unittest
from unittest.mock import Mock, patch
import zipfile
import warnings

HERE=Path(__file__).resolve().parent
PRODUCER=HERE/'package_games_manual_test.py'
if not PRODUCER.exists(): PRODUCER=HERE.parent/'tools/package_games_manual_test.py'
spec=importlib.util.spec_from_file_location('games_manual_delivery',PRODUCER)
delivery=importlib.util.module_from_spec(spec);spec.loader.exec_module(delivery)
from runtime_package import envelope


class ManualGames(unittest.TestCase):
    def files(self):
        module=envelope(bytes(32),32,'123456789abc')
        return {name:module if name.endswith('.kui') else bytes(49152) for name in delivery.TARGETS}

    def test_exact_28_software_inventory_and_identical_physical_GUI_alias(self):
        files=self.files();report=delivery.inventory(files)
        self.assertEqual(len(report),28)
        self.assertNotIn(delivery.CE_PROBE,report)
        self.assertEqual(set(delivery.SERVICES),set(delivery.WORKERS))
        self.assertEqual(len(delivery.INSTALL_TARGETS-{delivery.CE_PROBE}),15)

    def test_missing_each_owner_and_control_user_optional_or_KUP_members_refuse(self):
        for name in delivery.TARGETS:
            files=self.files();files.pop(name)
            with self.subTest(missing=name),self.assertRaisesRegex(ValueError,'exactly28'):delivery.inventory(files)
        for name in ('KUI/system/activation-a.bin','KUI/VMU/private.bin','KUI/apps/games/selection-a.bin',
                     delivery.CE_PROBE,'KUI/test.kup','README.md'):
            files=self.files();files[name]=b'forbidden'
            with self.subTest(extra=name),self.assertRaisesRegex(ValueError,'exactly28'):delivery.inventory(files)

    def test_changed_GUI_alias_and_corrupt_checked_envelope_refuse(self):
        files=self.files();files['KUI/boot.kui']=envelope(bytes([1])*32,32,'123456789abc')
        with self.assertRaisesRegex(ValueError,'aliases differ'):delivery.inventory(files)
        files=self.files();name=delivery.SERVICES['standard'];files[name]=files[name][:-1]+b'x'
        with self.assertRaisesRegex(ValueError,'checksum'):delivery.inventory(files)

    def test_input_bytes_additions_and_symlinks_cannot_change_under_reports(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path=root/'module';path.write_bytes(b'checked')
            inputs=delivery.Inputs();inputs.tree(root);inputs.recheck();path.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'Input changed'):inputs.recheck()
            inputs=delivery.Inputs();inputs.tree(root);(root/'extra').write_bytes(b'new')
            with self.assertRaisesRegex(ValueError,'tree changed'):inputs.recheck()
            alias=root/'alias';alias.symlink_to(path)
            with self.assertRaisesRegex(ValueError,'ordinary file'):delivery.Inputs().read(alias)

    def test_metadata_commit_keeps_actual_binary_source_and_only_exact_binding_promotions(self):
        binary='c'*40;metadata='a'*40
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);folder=root/'resources/updater-bindings';folder.mkdir(parents=True)
            resources={name:name.encode() for name in delivery.RESOURCE_NAMES}
            for name,body in resources.items():(folder/name).write_bytes(body)
            changed=['resources/updater-bindings/'+name for name in resources]
            def git(*args):
                if args==('rev-parse','HEAD'):return metadata
                if args==('rev-parse','HEAD^{tree}'):return 'b'*40
                if args==('status','--porcelain'):return ''
                if args[0]=='diff':return '\n'.join(changed)
                if args==('rev-parse',binary+'^{tree}'):return 'd'*40
                raise AssertionError(args)
            with patch.object(delivery,'ROOT',root),patch.object(delivery,'git',side_effect=git),\
                 patch.object(delivery.subprocess,'run',return_value=subprocess.CompletedProcess([],0)):
                result=delivery.source_state(binary,resources)
                self.assertEqual(result['binary_source_commit'],binary)
                self.assertEqual(result['tooling_commit'],metadata)
                for path in ('src/apps/games_retail.c','third_party/library.c','firmware/kui-wifi/components/kwlink/kwlink.c','Makefile','Makefile.dc'):
                    changed.append(path)
                    with self.subTest(path=path),self.assertRaisesRegex(ValueError,'Production source'):delivery.source_state(binary,resources)
                    changed.pop()
                (folder/delivery.RESOURCE_NAMES[0]).write_bytes(b'wrong')
                with self.assertRaisesRegex(ValueError,'qualified GUI input'):delivery.source_state(binary,resources)

    def test_archive_recheck_rejects_duplicate_extra_and_changed_software(self):
        files=delivery.release_files(self.files(),dict(tooling_commit='a'*40,tooling_tree='b'*40,binary_source_commit='c'*40,binary_source_tree='d'*40,source_url='https://example.test/source'),b'GNU GENERAL PUBLIC LICENSE Version 3')
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for kind in ('duplicate','extra','changed'):
                path=root/(kind+'.zip')
                with zipfile.ZipFile(path,'w') as z:
                    for name,body in files.items():z.writestr(name,body)
                    if kind=='duplicate':
                        with warnings.catch_warnings():
                            warnings.simplefilter('ignore',UserWarning);z.writestr('KUI/runtime.kui',files['KUI/runtime.kui'])
                    elif kind=='extra':z.writestr('KUI/system/activation-a.bin',bytes(128))
                if kind=='changed':
                    changed=dict(files);changed[delivery.SERVICES['standard']]=b'changed'
                    with self.assertRaisesRegex(ValueError,'software changed'):delivery.archive_matches(path,changed)
                else:
                    with self.assertRaisesRegex(ValueError,'unauthorized'):delivery.archive_matches(path,files)

    def test_existing_output_or_evidence_refuse_before_any_qualification(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);args=types.SimpleNamespace(output=root/'test.zip',evidence_dir=root/'evidence')
            for existing in (args.output,args.evidence_dir):
                if existing==args.output:existing.write_bytes(b'preserve')
                else:existing.mkdir()
                with patch.object(delivery,'qualify') as qualify,self.assertRaisesRegex(ValueError,'new output'):
                    delivery.publish(args)
                qualify.assert_not_called()
                if existing.is_file():self.assertEqual(existing.read_bytes(),b'preserve');existing.unlink()
                else:existing.rmdir()

    def test_nested_output_and_evidence_refuse_without_creating_directories(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for output,evidence in ((root/'x.zip',root/'x.zip/evidence'),(root/'evidence/x.zip',root/'evidence')):
                args=types.SimpleNamespace(output=output,evidence_dir=evidence)
                with patch.object(delivery,'qualify') as qualify,self.assertRaisesRegex(ValueError,'separate new paths'):
                    delivery.publish(args)
                qualify.assert_not_called();self.assertEqual(list(root.iterdir()),[])

    def test_publication_is_exact_software_only_with_external_build_and_guide(self):
        files=self.files();identity=dict(tooling_commit='a'*40,tooling_tree='b'*40,
            binary_source_commit='c'*40,binary_source_tree='d'*40)
        report=dict(identity,source_url='https://example.test/source')
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);args=types.SimpleNamespace(output=root/'test.zip',evidence_dir=root/'evidence',binary_source_commit='c'*40)
            with patch.object(delivery,'qualify',return_value=(files,report,Mock(hashes={},read=Mock(return_value=b'GNU GENERAL PUBLIC LICENSE Version 3')),{})),\
                 patch.object(delivery,'smash_admission',return_value=dict(status='PASS',source_dirty=False)),\
                 patch.object(delivery,'source_state',return_value=identity):
                result=delivery.publish(args)
            with zipfile.ZipFile(args.output) as z:released={name:z.read(name) for name in z.namelist()}
            delivery.archive_matches(args.output,released)
            self.assertEqual(result['actual_packaged_Smash_admission']['status'],'PASS')
            self.assertEqual({p.name for p in args.evidence_dir.iterdir()},{'QUALIFICATION.json'})
            self.assertIn(b'ce-probe.kui',released['README-FIRST.txt'])
            self.assertEqual(set(released),delivery.TARGETS|{'README-FIRST.txt','BUILD.json','LICENSE.txt'})
            self.assertEqual({name for name in released if name.startswith('KUI/')},delivery.TARGETS)

    def test_source_change_before_publish_leaves_no_install_or_evidence_artifact(self):
        files=self.files();report=dict(tooling_commit='a'*40,tooling_tree='b'*40,binary_source_commit='c'*40,binary_source_tree='d'*40,source_url='https://example.test/source')
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);args=types.SimpleNamespace(output=root/'test.zip',evidence_dir=root/'evidence',binary_source_commit='c'*40)
            with patch.object(delivery,'qualify',return_value=(files,report,Mock(hashes={},read=Mock(return_value=b'GNU GENERAL PUBLIC LICENSE Version 3')),{})),\
                 patch.object(delivery,'smash_admission',return_value=dict(status='PASS',source_dirty=False)),\
                 patch.object(delivery,'source_state',return_value={}):
                with self.assertRaisesRegex(ValueError,'Source changed'):delivery.publish(args)
            self.assertFalse(args.output.exists());self.assertFalse(args.evidence_dir.exists())


if __name__=='__main__':unittest.main()
