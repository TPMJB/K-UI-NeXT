#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Legacy recovery gates; native builds are mocked, approved bytes are external.

Run with --approved-dir <directory containing apps/games>, or set
KUI_APPROVED_LEGACY_DIR. No historical binaries are copied into the tests.
"""
import argparse
from contextlib import redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import warnings
import zipfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import update_build_legacy as legacy
from runtime_package import envelope
from update_package import legacy_catalog

APPROVED=os.environ.get('KUI_APPROVED_LEGACY_DIR')
COMMIT='012345abcdef'+'9'*28
BUILD=COMMIT[:12]
EXPECTED={
    'apps/games/cdda-toy-raw.kui':('9739eb693b02',82580,'b935e90a744e5ea4e7ec114182682e4e418cae320b55db25b01a3f2540cd46ab'),
    'apps/games/cdda-toy-cooked.kui':('a8eba3e74b03',82696,'733cea7241557d215256365aa7003e252860b4d02e70e4611787d45916f13211'),
    'apps/games/retail-cdda-mdk2.kui':('89c4650e444f',64588,'42b9a8c958548521ec0bac351ff55b4edc5a51bd83b26f8a5c7cd97ebcd1a1b7'),
}


class BuildFixture:
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='kui-legacy-gates-');self.addCleanup(self.temporary.cleanup)
        self.base=Path(self.temporary.name);self.stage=self.base/'stage'
        self.builds=self.base/'builds';self.evidence=self.base/'evidence';self.commands=[]

    def run_fake(self,command,**kwargs):
        self.commands.append(tuple(command))
        if command[0]=='make':
            directory=Path(next(arg[6:] for arg in command if arg.startswith('BUILD=')))
            profile=command[2].removeprefix('Makefile.vmu-');directory.mkdir(parents=True,exist_ok=True)
            data=envelope(bytes(32),32,BUILD);(directory/'retail-maple-observe.kui').write_bytes(data)
            template=Path(next(arg[11:] for arg in command if arg.startswith('SD_PROFILE='))).read_bytes()
            (directory/'vmu-sd-profile.bin').write_bytes(template)
            (directory/'observer-artifact-audit.json').write_text(json.dumps({
                'maple_profile':{'id':2 if profile=='silver' else 1},'exports':{'readonly_mode':4},
                'source_commit':COMMIT,'source_dirty':False,'module_sha256':hashlib.sha256(data).hexdigest(),
                'sd_profile_sha256':hashlib.sha256(template).hexdigest()}))
        elif 'tools/package_vmu_maple_observe.py' not in command and not any('legacy-production-admission' in str(arg) for arg in command):
            raise AssertionError('Unexpected external command: '+repr(command))
        return mock.Mock(returncode=0)

    def build(self,**kwargs):
        with redirect_stdout(io.StringIO()):
            return legacy.build_legacy(self.stage,BUILD,evidence_dir=self.evidence,build_root=self.builds,**kwargs)


class BuildGates(BuildFixture,unittest.TestCase):
    def test_dirty_final_and_allow_dirty_final_fail_before_any_output_or_command(self):
        for dirty,allow in ((True,False),(True,True),(False,True)):
            with self.subTest(dirty=dirty,allow=allow),mock.patch.object(legacy,'source_state',return_value=(COMMIT,dirty)),\
                    mock.patch.object(legacy.subprocess,'run') as run:
                with self.assertRaises(ValueError):self.build(allow_dirty=allow)
                run.assert_not_called();self.assertFalse(self.stage.exists());self.assertFalse(self.builds.exists())

    def test_dirty_build_only_never_reads_legacy_or_stages_a_release(self):
        self.stage.mkdir();sentinel=self.stage/'keep';sentinel.write_bytes(b'keep this staging file')
        with mock.patch.object(legacy,'source_state',return_value=(COMMIT,True)),\
                mock.patch.object(legacy.subprocess,'run',side_effect=self.run_fake),\
                mock.patch.object(legacy,'legacy_sources',side_effect=AssertionError('build-only read historical files')):
            report=self.build(build_only=True,allow_dirty=True)
        self.assertEqual(list(self.stage.iterdir()),[sentinel]);self.assertEqual(sentinel.read_bytes(),b'keep this staging file')
        self.assertTrue(report['build_only']);self.assertEqual(report['members'],[])
        self.assertFalse(report['legacy_members_complete']);self.assertFalse(report['update_cohort_prepared'])
        self.assertEqual(len(self.commands),2);self.assertTrue(all(command[0]=='make' for command in self.commands))
        for command in self.commands:
            self.assertIn('READONLY_PROOF=4',command);self.assertIn('BUILD_ID='+BUILD,command)

    def test_build_id_and_jobs_refuse_without_native_commands(self):
        for build,jobs in (('f'*12,2),(BUILD,0),(BUILD,33)):
            with self.subTest(build=build,jobs=jobs),mock.patch.object(legacy,'source_state',return_value=(COMMIT,False)),\
                    mock.patch.object(legacy.subprocess,'run') as run:
                with self.assertRaises(ValueError):legacy.build_legacy(self.stage,build,jobs,
                    evidence_dir=self.evidence,build_root=self.builds)
                run.assert_not_called()

    def test_changed_commit_or_dirty_source_before_admission_never_stages(self):
        for states in (((COMMIT,False),('f'*40,False)),
                       ((COMMIT,False),(COMMIT,False),(COMMIT,True))):
            with self.subTest(states=states),mock.patch.object(legacy,'source_state',side_effect=states),\
                    mock.patch.object(legacy.subprocess,'run',side_effect=self.run_fake),\
                    mock.patch.object(legacy,'legacy_sources') as sources:
                with self.assertRaises(ValueError):self.build()
                sources.assert_not_called();self.assertFalse(self.stage.exists())

    def test_symlink_build_and_evidence_directories_are_refused(self):
        outside=self.base/'outside';outside.mkdir()
        for index,role in enumerate(('builds','evidence')):
            self.builds=self.base/('builds-'+str(index));self.evidence=self.base/('evidence-'+str(index))
            target=self.builds if role=='builds' else self.evidence
            target.symlink_to(outside,target_is_directory=True)
            with self.subTest(target=target.name),mock.patch.object(legacy,'source_state',return_value=(COMMIT,True)),\
                    mock.patch.object(legacy.subprocess,'run') as run:
                with self.assertRaises(ValueError):self.build(build_only=True,allow_dirty=True)
                run.assert_not_called()
            target.unlink()

    def test_template_output_symlink_does_not_overwrite_external_file(self):
        self.builds.mkdir();external=self.base/'private-file';external.write_bytes(b'keep')
        (self.builds/'blank-template.bin').symlink_to(external)
        with mock.patch.object(legacy,'source_state',return_value=(COMMIT,True)),\
                mock.patch.object(legacy.subprocess,'run',side_effect=self.run_fake):
            with self.assertRaises(ValueError):self.build(build_only=True,allow_dirty=True)
        self.assertEqual(external.read_bytes(),b'keep')

    def test_evidence_log_and_component_directory_symlinks_stop_before_make(self):
        external=self.base/'private-file';external.write_bytes(b'keep')
        outside=self.base/'outside';outside.mkdir()
        for index,role in enumerate(('log','component')):
            self.builds=self.base/('builds-'+str(index));self.evidence=self.base/('evidence-'+str(index))
            if role=='log':
                self.evidence.mkdir();(self.evidence/'silver-persistent.log').symlink_to(external)
            else:
                self.builds.mkdir();(self.builds/'silver').symlink_to(outside,target_is_directory=True)
            with self.subTest(role=role),mock.patch.object(legacy,'source_state',return_value=(COMMIT,True)),\
                    mock.patch.object(legacy.subprocess,'run') as run:
                with self.assertRaises(ValueError):self.build(build_only=True,allow_dirty=True)
                run.assert_not_called()
            self.assertEqual(external.read_bytes(),b'keep');self.assertEqual(list(outside.iterdir()),[])

    def test_make_recipe_metacharacters_in_build_directory_are_refused(self):
        for name in ('space path','dollar$(touch marker)','back`touch marker`','semi;name'):
            self.builds=self.base/name
            with self.subTest(name=name),mock.patch.object(legacy,'source_state',return_value=(COMMIT,True)),\
                    mock.patch.object(legacy.subprocess,'run') as run:
                with self.assertRaisesRegex(ValueError,'unsupported'):self.build(build_only=True,allow_dirty=True)
                run.assert_not_called();self.assertFalse(self.builds.exists())


class ApprovedBytes(BuildFixture,unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not APPROVED:raise unittest.SkipTest('Set KUI_APPROVED_LEGACY_DIR or --approved-dir for frozen-byte checks')
        cls.approved=Path(APPROVED)
        cls.data={path:(cls.approved/path).read_bytes() for path in legacy.LEGACY_MEMBERS}

    def write_inputs(self,base):
        for relative,data in self.data.items():
            path=base/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)

    def archive(self,names=None,extra=()):
        archive=self.base/'legacy.zip'
        with warnings.catch_warnings():
            warnings.simplefilter('ignore',UserWarning)
            with zipfile.ZipFile(archive,'w') as output:
                for relative,data in self.data.items():output.writestr((names or {}).get(relative,'KUI/'+relative),data)
                for name,data in extra:output.writestr(name,data)
        return archive

    def test_actual_frozen_build_size_sha_and_runtime_crc_are_admitted(self):
        pins=legacy_catalog();self.assertEqual(set(pins),set(EXPECTED))
        for path,data in self.data.items():
            with self.subTest(path=path):
                build,size,sha=EXPECTED[path]
                self.assertEqual((pins[path]['build'],pins[path]['bytes'],pins[path]['sha256']),(build,size,sha))
                self.assertEqual((len(data),hashlib.sha256(data).hexdigest()),(size,sha))
                self.assertEqual(zlib.crc32(data[64:]),struct.unpack_from('<I',data,32)[0])
                proof=legacy.validate_legacy(path,data);self.assertTrue(proof['validated']);self.assertEqual(proof['build'],build)
        found=legacy.legacy_sources(self.approved);self.assertEqual(set(found),set(EXPECTED))

    def test_wrong_paths_swapped_corrupt_truncated_and_restamped_files_refuse(self):
        for path,data in self.data.items():
            for name in ('../'+path,path.upper(),'runtime.kui'):
                with self.subTest(path=name),self.assertRaises(ValueError):legacy.validate_legacy(name,data)
            changed=bytearray(data);changed[-1]^=1
            variants=(bytes(changed),data[:-1],data+b'\0',next(value for key,value in self.data.items() if key!=path))
            for value in variants:
                with self.subTest(path=path,size=len(value)),self.assertRaises(ValueError):legacy.validate_legacy(path,value)
            changed=bytearray(data);changed[40:52]=BUILD.encode();struct.pack_into('<I',changed,60,zlib.crc32(changed[:60]))
            with self.assertRaises(ValueError):legacy.validate_legacy(path,bytes(changed))

    def test_runtime_crc_and_old_build_remain_independent_of_outer_pin(self):
        path=legacy.LEGACY_MEMBERS[0];data=self.data[path]
        changed=bytearray(data);changed[-1]^=1
        pins=legacy_catalog();pins[path]={**pins[path],'sha256':hashlib.sha256(changed).hexdigest()}
        with self.assertRaisesRegex(ValueError,'CRC|checksum'):legacy.validate_legacy(path,bytes(changed),pins)
        changed=bytearray(data);changed[40:52]=BUILD.encode();struct.pack_into('<I',changed,60,zlib.crc32(changed[:60]))
        pins[path]={**pins[path],'sha256':hashlib.sha256(changed).hexdigest()}
        with self.assertRaisesRegex(ValueError,'build differs'):legacy.validate_legacy(path,bytes(changed),pins)

    def test_fixed_archive_members_only_no_extraction_and_duplicates_refuse(self):
        names={legacy.LEGACY_MEMBERS[0]:'preserved-toy-tests/AP-raw-reference.kui',
               legacy.LEGACY_MEMBERS[1]:'preserved-toy-tests/AT-cooked-data.kui'}
        archive=self.archive(names,extra=(('../escaped.txt',b'never extract'),('unknown/'+legacy.LEGACY_MEMBERS[0],b'wrong')))
        read_names=[];read=zipfile.ZipFile.read
        def bounded_read(source,name,*args,**kwargs):
            read_names.append(name);return read(source,name,*args,**kwargs)
        with mock.patch.object(zipfile.ZipFile,'extract',side_effect=AssertionError('extract called')),\
                mock.patch.object(zipfile.ZipFile,'extractall',side_effect=AssertionError('extractall called')),\
                mock.patch.object(zipfile.ZipFile,'read',new=bounded_read):
            found=legacy.legacy_sources(legacy_archives=[archive])
        self.assertEqual(set(found),set(EXPECTED));self.assertFalse((self.base.parent/'escaped.txt').exists())
        self.assertEqual(set(read_names),{names.get(path,'KUI/'+path) for path in self.data})
        for path in names:self.assertEqual(found[path][1]['source_member'],names[path])
        path=legacy.LEGACY_MEMBERS[0];archive=self.archive(extra=(('KUI/'+path,self.data[path]),))
        with self.assertRaisesRegex(ValueError,'Ambiguous'):legacy.legacy_sources(legacy_archives=[archive])

    def test_unknown_archive_locations_and_wrong_sized_fixed_members_refuse(self):
        names={path:'unknown/'+path for path in self.data};archive=self.archive(names)
        with self.assertRaisesRegex(ValueError,'Missing'):legacy.legacy_sources(legacy_archives=[archive])
        path=legacy.LEGACY_MEMBERS[0];archive=self.archive({path:'unknown/'+path},extra=(('KUI/'+path,self.data[path][:-1]),))
        with self.assertRaisesRegex(ValueError,'wrongly sized'):legacy.legacy_sources(legacy_archives=[archive])

    def test_symlink_input_file_parent_archive_and_archive_parent_refuse(self):
        inputs=self.base/'inputs';self.write_inputs(inputs);path=inputs/legacy.LEGACY_MEMBERS[0]
        path.unlink();path.symlink_to(self.approved/legacy.LEGACY_MEMBERS[0])
        with self.assertRaisesRegex(ValueError,'Symlink'):legacy.legacy_sources(inputs)
        alias=self.base/'input-alias';alias.symlink_to(self.approved,target_is_directory=True)
        with self.assertRaisesRegex(ValueError,'Symlink'):legacy.legacy_sources(alias)
        archive=self.archive();link=self.base/'archive-link.zip';link.symlink_to(archive)
        with self.assertRaises(ValueError):legacy.legacy_sources(legacy_archives=[link])
        parent=self.base/'archive-parent';parent.symlink_to(self.base,target_is_directory=True)
        with self.assertRaises(ValueError):legacy.legacy_sources(legacy_archives=[parent/archive.name])

    def test_clean_final_stages_exact_five_members_and_retains_legacy_bytes(self):
        with mock.patch.object(legacy,'source_state',return_value=(COMMIT,False)),\
                mock.patch.object(legacy.subprocess,'run',side_effect=self.run_fake):
            report=self.build(legacy_dir=self.approved)
        self.assertTrue(report['legacy_members_complete']);self.assertTrue(report['production_legacy_admission'])
        self.assertEqual({item['path'] for item in report['members']},set(legacy.EXPECTED_MEMBERS))
        for path,data in self.data.items():self.assertEqual((self.stage/path).read_bytes(),data)
        for profile,path in legacy.VMU_MEMBERS.items():self.assertEqual((self.stage/path).read_bytes(),(self.builds/profile/'retail-maple-observe.kui').read_bytes())
        self.assertTrue(any('tools/package_vmu_maple_observe.py' in command for command in self.commands))
        self.assertTrue(any(command[0].endswith('/legacy-production-admission') for command in self.commands))

    def test_symlink_stage_root_parent_and_members_leave_external_bytes_intact(self):
        outside=self.base/'outside';outside.mkdir();sentinel=outside/'private';sentinel.write_bytes(b'keep')
        variants=('root','parent',*legacy.EXPECTED_MEMBERS)
        for index,variant in enumerate(variants):
            self.stage=self.base/('stage-'+str(index))
            if variant=='root':self.stage.symlink_to(outside,target_is_directory=True)
            elif variant=='parent':
                self.stage.mkdir();(self.stage/'apps').symlink_to(outside,target_is_directory=True)
            else:
                target=self.stage/variant;target.parent.mkdir(parents=True,exist_ok=True);target.symlink_to(sentinel)
            with self.subTest(variant=variant),mock.patch.object(legacy,'source_state',return_value=(COMMIT,False)),\
                    mock.patch.object(legacy.subprocess,'run',side_effect=self.run_fake):
                with self.assertRaisesRegex(ValueError,'Symlink|ordinary'):self.build(legacy_dir=self.approved)
            self.assertEqual(sentinel.read_bytes(),b'keep')

    def test_wrong_persistent_profile_provenance_and_public_template_never_stage(self):
        for index,fault in enumerate(('profile','mode','commit','dirty','module','template','private')):
            self.stage=self.base/('stage-'+str(index))
            def changed(command,**kwargs):
                result=self.run_fake(command,**kwargs)
                if 'tools/package_vmu_maple_observe.py' in command:
                    directory=Path(command[2]);path=directory/'observer-artifact-audit.json'
                    record=json.loads(path.read_text())
                    if fault=='profile':record['maple_profile']['id']=1
                    elif fault=='mode':record['exports']['readonly_mode']=3
                    elif fault=='commit':record['source_commit']='f'*40
                    elif fault=='dirty':record['source_dirty']=True
                    elif fault=='module':record['module_sha256']='0'*64
                    elif fault=='template':record['sd_profile_sha256']='0'*64
                    else:(directory/'vmu-sd-profile.bin').write_bytes(b'private profile')
                    path.write_text(json.dumps(record))
                return result
            with self.subTest(fault=fault),mock.patch.object(legacy,'source_state',return_value=(COMMIT,False)),\
                    mock.patch.object(legacy.subprocess,'run',side_effect=changed):
                with self.assertRaisesRegex(ValueError,'public-template admission'):self.build(legacy_dir=self.approved)
            self.assertFalse((self.stage/'apps').exists())


if __name__=='__main__':
    parser=argparse.ArgumentParser(add_help=False);parser.add_argument('--approved-dir')
    args,rest=parser.parse_known_args()
    if args.approved_dir:APPROVED=args.approved_dir
    unittest.main(argv=[sys.argv[0],*rest])
