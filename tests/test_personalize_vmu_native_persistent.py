#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fixed-span module personalization never mutates existing VMU pair files."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import personalize_vmu_native_persistent as tool
from runtime_package import envelope,verify
from vmu_persist_profile import decode_profile,encode_profile,template_profile

COMMIT='a1234567890b'*3+'cdef'

def owner_profile(pair='p0001',cid=1):
    p=decode_profile(template_profile())
    p['cid']=bytes([cid])*16
    p['cid']=p['cid'].hex()
    p['image_id']=bytes([int(pair[1:])])*16
    p['identity_sha256']=hashlib.sha256(p['image_id']).hexdigest()
    p['image_id']=p['image_id'].hex();p['pair_path']='/KUI/vmu-test/'+pair
    return encode_profile(p)

def generic(package):
    template=template_profile();payload=b'\x09\x00'*64+template+bytes(32)
    module=envelope(payload,len(payload),COMMIT[:12])
    manifest={'schema':tool.SCHEMA,'source_commit':COMMIT,'build':COMMIT[:12],
        'module_sha256':tool.sha(module),'module_bytes':len(module),
        'profile_module_offset':64+128,'profile_bytes':4096,'profile_sha256':tool.sha(template),
        'storage_writes_enabled':True}
    (package/tool.BASE_MODULE).parent.mkdir(parents=True)
    (package/tool.BASE_MODULE).write_bytes(module)
    (package/tool.MANIFEST).write_text(json.dumps(manifest))
    return module,manifest

def pair(root,name='p0001',cid=1,number='0001'):
    directory=root/'KUI/vmu-test'/name;directory.mkdir(parents=True,exist_ok=True)
    profile=owner_profile(name,cid)
    path=directory/('retail-persistent-profile-'+number+'.bin');path.write_bytes(profile)
    # Current contents intentionally differ from the exported snapshot. The
    # actual console must recover and validate them after live ownership checks.
    (directory/'card.vmd').write_bytes(b'B'*131072)
    (directory/'card.journal').write_bytes(b'J'*1024)
    (directory/'identity.bin').write_bytes(bytes.fromhex(decode_profile(profile)['image_id']))
    return path

class NativePersonalizationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.package=self.root/'package';self.card=self.root/'card'
        self.card.mkdir();self.module,self.manifest=generic(self.package)
        self.profile=pair(self.card)
        self.destination=self.card/tool.INSTALL;self.destination.parent.mkdir(parents=True)
        self.destination.write_bytes(b'previous ordinary loader')

    def install(self,profile=None):
        return tool.install(self.card,package=self.package,profile=profile)

    def snapshots(self):
        return {p.relative_to(self.card).as_posix():p.read_bytes()
            for p in (self.card/'KUI/vmu-test').rglob('*') if p.is_file()}

    def manifest_write(self,**changes):
        p=dict(self.manifest);p.update(changes)
        (self.package/tool.MANIFEST).write_text(json.dumps(p))

    def test_only_exact_profile_and_two_runtime_checksums_change(self):
        before=self.snapshots();report=self.install();after=self.destination.read_bytes()
        self.assertEqual(self.snapshots(),before)
        self.assertEqual((self.package/tool.BASE_MODULE).read_bytes(),self.module)
        self.assertEqual(report['pair_path'],'/KUI/vmu-test/p0001')
        self.assertEqual(verify(after)['build'],COMMIT[:12])
        offset=self.manifest['profile_module_offset'];wire=self.profile.read_bytes()
        self.assertEqual(after[offset:offset+4096],wire)
        allowed=((32,36),(60,64),(offset,offset+4096))
        self.assertTrue(all(a==b or any(first<=i<end for first,end in allowed)
            for i,(a,b) in enumerate(zip(self.module,after))))
        self.assertEqual(set(p.relative_to(self.destination.parent).as_posix()
            for p in self.destination.parent.iterdir()),{'retail-boot.kui'})

    def test_same_personalization_is_a_no_write_success(self):
        self.install()
        with patch.object(tool.os,'replace',side_effect=AssertionError('must not rewrite')):
            self.assertTrue(self.install()['already_installed'])

    def test_extracted_zip_layout_runs_with_only_two_shipped_codecs(self):
        tools=self.package/'tools';tools.mkdir()
        source=Path(__file__).resolve().parents[1]/'tools'
        for name in ('runtime_package.py','vmu_persist_profile.py'):
            (tools/name).write_bytes((source/name).read_bytes())
        (self.package/'personalize.py').write_bytes(Path(tool.__file__).read_bytes())
        result=subprocess.run([sys.executable,str(self.package/'personalize.py'),str(self.card)],
            cwd=self.root,capture_output=True,text=True,check=True)
        self.assertIn('Installed native persistent VMU test',result.stdout)
        self.assertEqual(verify(self.destination.read_bytes())['build'],COMMIT[:12])

    def test_multiple_pairs_require_explicit_selection(self):
        second=pair(self.card,'p0002')
        before=self.snapshots();old=self.destination.read_bytes()
        with self.assertRaisesRegex(ValueError,'Several prepared'):
            self.install()
        self.assertEqual(self.destination.read_bytes(),old);self.assertEqual(self.snapshots(),before)
        result=self.install(second.relative_to(self.card))
        self.assertEqual(result['pair_path'],'/KUI/vmu-test/p0002');self.assertEqual(self.snapshots(),before)

    def test_repeated_exports_with_same_ownership_select_latest_snapshot(self):
        p=decode_profile(self.profile.read_bytes());p['base_sha256']=hashlib.sha256(b'new snapshot').hexdigest()
        newer=self.profile.with_name('retail-persistent-profile-0002.bin');newer.write_bytes(encode_profile(p))
        report=self.install();self.assertEqual(report['profile'],str(newer.relative_to(self.card)))

    def test_different_ownership_in_same_pair_is_ambiguous(self):
        self.profile.with_name('retail-persistent-profile-0002.bin').write_bytes(owner_profile(cid=2))
        with self.assertRaisesRegex(ValueError,'Several prepared'):
            self.install()

    def test_wrong_identity_missing_source_and_moved_profile_refused(self):
        identity=self.profile.parent/'identity.bin';old=identity.read_bytes();identity.write_bytes(b'X'*16)
        with self.assertRaisesRegex(ValueError,'identity'):
            self.install(self.profile)
        identity.write_bytes(old)
        journal=self.profile.parent/'card.journal';journal.unlink()
        with self.assertRaises(OSError):self.install(self.profile)
        journal.write_bytes(bytes(1024))
        moved=pair(self.card,'p0002');moved.write_bytes(self.profile.read_bytes())
        with self.assertRaisesRegex(ValueError,'differs from its prepared pair'):
            self.install(moved)

    def test_template_corrupt_profile_and_other_card_refused(self):
        for wire in (template_profile(),b'X'*4096,self.profile.read_bytes()[:-1]):
            self.profile.write_bytes(wire)
            with self.assertRaises(ValueError):self.install(self.profile)
        elsewhere=pair(self.root/'elsewhere')
        with self.assertRaisesRegex(ValueError,'leaves'):
            self.install(elsewhere)
        with self.assertRaisesRegex(ValueError,'leaves'):
            self.install(Path('../elsewhere/KUI/vmu-test/p0001')/elsewhere.name)

    def test_pair_source_and_install_symlinks_are_refused(self):
        image=self.profile.parent/'card.vmd';image.unlink();image.symlink_to(self.root/'unrelated')
        with self.assertRaises((OSError,ValueError)):self.install(self.profile)
        image.unlink();image.write_bytes(bytes(131072))
        unrelated=self.root/'unrelated';unrelated.write_bytes(b'keep')
        self.destination.unlink();self.destination.symlink_to(unrelated)
        with self.assertRaisesRegex(ValueError,'ordinary'):self.install()
        self.assertEqual(unrelated.read_bytes(),b'keep')

    def test_corrupted_generic_module_and_wrong_manifest_bounds_refused(self):
        changed=bytearray(self.module);changed[64]^=1
        (self.package/tool.BASE_MODULE).write_bytes(changed)
        with self.assertRaisesRegex(ValueError,'checksum'):self.install()
        (self.package/tool.BASE_MODULE).write_bytes(self.module)
        for changes in ({'profile_module_offset':self.manifest['profile_module_offset']+4},
                        {'profile_module_offset':True},{'profile_bytes':True},
                        {'module_sha256':'0'*64},{'storage_writes_enabled':1},
                        {'source_commit':'f'*40}):
            self.manifest_write(**changes)
            with self.subTest(changes=changes),self.assertRaises(ValueError):self.install()

    def test_even_coherent_duplicate_template_is_refused(self):
        payload=self.module[64:]+template_profile();module=envelope(payload,len(payload),COMMIT[:12])
        (self.package/tool.BASE_MODULE).write_bytes(module)
        self.manifest_write(module_bytes=len(module),module_sha256=tool.sha(module))
        with self.assertRaisesRegex(ValueError,'duplicated'):self.install()

    def test_failed_atomic_replace_keeps_old_loader_and_pair(self):
        old=self.destination.read_bytes();before=self.snapshots()
        with patch.object(tool.os,'replace',side_effect=OSError('injected failure')):
            with self.assertRaisesRegex(OSError,'injected'):self.install()
        self.assertEqual(self.destination.read_bytes(),old);self.assertEqual(self.snapshots(),before)
        self.assertFalse(list(self.destination.parent.glob('.native-vmu-*')))

if __name__=='__main__':unittest.main()
