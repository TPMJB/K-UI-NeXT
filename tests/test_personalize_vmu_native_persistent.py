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
import vmu_native_cache_seed as cache
import vmu_native_mode as mode
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

def record_fixture(slug='ps2'):
    """Synthetic canonical metadata; never a production game certificate."""
    wire=bytearray(1024);wire[:8]=b'KUINVR01'
    values={8:1,12:1024,16:1,20:1,24:1024,28:0x8c010000,
        64:1,68:104,72:1,76:1024,80:0x8c010000,84:7,132:12,
        168:1,172:440,192:1,404:12,608:1,612:72,616:1,620:1,
        624:1024,628:0x8c010000,700:1,704:48,708:1,712:0x8c010000,716:1024,720:3}
    for offset,value in values.items():struct.pack_into('<I',wire,offset,value)
    wire[32:64]=hashlib.sha256(('synthetic '+slug).encode()).digest();wire[136:168]=wire[32:64]
    wire[992:]=hashlib.sha256(wire[:992]).digest()
    cache.decode_record(wire)
    return bytes(wire)

class NativeCachePersonalizationTests(NativePersonalizationTests):
    def setUp(self):
        super().setUp()
        self.seed_offset=len(self.module)
        payload=self.module[64:]+cache.template_seed()+bytes(32)
        self.module=envelope(payload,len(payload),COMMIT[:12])
        self.manifest.update(module_bytes=len(self.module),module_sha256=tool.sha(self.module),
            cache_seed_module_offset=self.seed_offset,cache_seed_bytes=1120,
            cache_seed_sha256=tool.sha(cache.template_seed()),cache_record_bytes=1024,
            cache_detector_epoch=1,cache_catalog_path=tool.CATALOG_PATH)
        self.catalog={'schema':tool.CATALOG_SCHEMA,'source_commit':COMMIT,'detector_epoch':1,'games':[]}
        directory=self.package/'native-cache';directory.mkdir()
        for slug,title in (('ps2','Power Stone 2'),('skies','Skies of Arcadia'),('sa2','Sonic Adventure 2')):
            wire=record_fixture(slug);metadata=cache.decode_record(wire)
            (directory/(slug+'.nvr')).write_bytes(wire)
            self.catalog['games'].append(dict(slug=slug,title=title,record_path='native-cache/'+slug+'.nvr',
                **{key:metadata[key] for key in ('record_sha256','record_digest','image_sha256','image_bytes','load_address')}))
        self.save_package()

    def save_package(self):
        raw=(json.dumps(self.catalog,indent=2,sort_keys=True)+'\n').encode()
        (self.package/tool.CATALOG_PATH).write_bytes(raw)
        self.manifest['cache_catalog_sha256']=tool.sha(raw)
        (self.package/tool.BASE_MODULE).write_bytes(self.module)
        (self.package/tool.MANIFEST).write_text(json.dumps(self.manifest))

    def install(self,profile=None,game=None):
        return tool.install(self.card,package=self.package,profile=profile,game=game)

    # Inherited legacy tests run against a legacy package only. This class's
    # extra manifest/seed spans require their own explicit expectations.
    def test_only_exact_profile_and_two_runtime_checksums_change(self):
        before=self.snapshots();self.install();after=self.destination.read_bytes()
        self.assertEqual(before,self.snapshots())
        self.assertEqual(after[self.seed_offset:self.seed_offset+1120],cache.template_seed())

    def test_extracted_zip_layout_runs_with_only_two_shipped_codecs(self):
        tools=self.package/'tools';tools.mkdir();source=Path(tool.__file__).parent
        for name in ('runtime_package.py','vmu_persist_profile.py','vmu_native_cache_seed.py'):
            (tools/name).write_bytes((source/name).read_bytes())
        (self.package/'personalize.py').write_bytes(Path(tool.__file__).read_bytes())
        listed=subprocess.run([sys.executable,str(self.package/'personalize.py'),'--list-games'],
            cwd=self.root,capture_output=True,text=True,check=True)
        self.assertIn('ps2\tPower Stone 2',listed.stdout)
        self.assertIn('sa2\tSonic Adventure 2',listed.stdout)
        before=self.snapshots()
        installed=subprocess.run([sys.executable,str(self.package/'personalize.py'),str(self.card),'--game','ps2',
            '--profile','KUI/vmu-test/p0001/retail-persistent-profile-0001.bin'],
            cwd=self.root,capture_output=True,text=True,check=True)
        self.assertIn('Power Stone 2 (ps2)',installed.stdout);self.assertEqual(before,self.snapshots())

    def test_game_selection_changes_only_seed_profile_checksums(self):
        before=self.snapshots();report=self.install(game='ps2');after=self.destination.read_bytes()
        self.assertEqual(report['game'],'ps2');self.assertEqual(before,self.snapshots())
        selected=(self.package/'native-cache/ps2.nvr').read_bytes()
        seed=after[self.seed_offset:self.seed_offset+1120]
        self.assertEqual(cache.decode_seed(seed),cache.decode_record(selected))
        self.assertEqual(seed[64:1088],selected)
        allowed=((32,36),(60,64),(self.manifest['profile_module_offset'],self.manifest['profile_module_offset']+4096),
            (self.seed_offset,self.seed_offset+1120))
        self.assertTrue(all(a==b or any(first<=i<end for first,end in allowed)
            for i,(a,b) in enumerate(zip(self.module,after))))
        with patch.object(tool.os,'replace',side_effect=AssertionError('must not rewrite')):
            self.assertTrue(self.install(game='ps2')['already_installed'])
        self.install(game='skies');self.assertNotEqual(self.destination.read_bytes(),after)

    def test_listing_does_not_open_card_or_change_loader(self):
        old=self.destination.read_bytes();before=self.snapshots()
        self.assertEqual(set(tool.list_games(self.package)),{'ps2','skies','sa2'})
        self.assertEqual(old,self.destination.read_bytes());self.assertEqual(before,self.snapshots())

    def test_catalog_pins_and_schema_refuse_corruption(self):
        old=self.destination.read_bytes()
        raw=(self.package/tool.CATALOG_PATH).read_bytes()
        (self.package/tool.CATALOG_PATH).write_bytes(raw+b' ')
        with self.assertRaisesRegex(ValueError,'catalog differs'):self.install(game='ps2')
        self.save_package()
        original=json.loads(json.dumps(self.catalog))
        for mutate in (
            lambda p:p.update(source_commit='f'*40),lambda p:p.update(detector_epoch=True),
            lambda p:p.update(detector_epoch=2),lambda p:p['games'][0].update(slug='recv'),
            lambda p:p['games'].append(p['games'][0]),lambda p:p['games'][0].update(record_path='../ps2.nvr'),
            lambda p:p['games'][0].update(record_digest='0'*64),lambda p:p['games'][0].update(record_sha256='0'*64),
            lambda p:p['games'][0].update(image_sha256='0'*64),lambda p:p['games'][0].update(image_bytes=True),
            lambda p:p['games'][0].update(load_address='ac010000'),lambda p:p['games'][0].update(title='bad\ntitle')):
            self.catalog=json.loads(json.dumps(original));mutate(self.catalog);self.save_package()
            with self.subTest(catalog=self.catalog),self.assertRaises(ValueError):self.install(game='ps2')
            self.assertEqual(old,self.destination.read_bytes())
        self.catalog=original;self.save_package()
        with self.assertRaisesRegex(ValueError,'Unknown packaged'):self.install(game='recv')

    def test_record_corruption_and_resealed_forgery_never_replaces_loader(self):
        path=self.package/'native-cache/ps2.nvr';original=path.read_bytes();old=self.destination.read_bytes()
        for length in (0,1023,1025):
            path.write_bytes((original+b'X')[:length])
            with self.assertRaises(ValueError):self.install(game='ps2')
        path.write_bytes(original)
        forged=bytearray(original);forged[88]^=4;forged[992:]=hashlib.sha256(forged[:992]).digest()
        path.write_bytes(forged)
        self.catalog['games'][0]['record_sha256']=tool.sha(forged);self.save_package()
        with self.assertRaisesRegex(ValueError,'independently pinned'):self.install(game='ps2')
        self.assertEqual(old,self.destination.read_bytes())

    def test_complete_optional_manifest_and_disjoint_seed_required(self):
        for field in tool.CACHE_FIELDS:
            changed=dict(self.manifest);del changed[field]
            (self.package/tool.MANIFEST).write_text(json.dumps(changed))
            with self.subTest(missing=field),self.assertRaises(ValueError):self.install(game='ps2')
        self.save_package()
        for changes in ({'cache_seed_module_offset':True},{'cache_seed_bytes':True},
            {'cache_record_bytes':1023},{'cache_detector_epoch':2},
            {'cache_seed_module_offset':self.manifest['profile_module_offset']},
            {'cache_seed_module_offset':self.seed_offset+4},{'cache_seed_sha256':'0'*64}):
            self.manifest_write(**changes)
            with self.subTest(changes=changes),self.assertRaises(ValueError):self.install(game='ps2')
        self.save_package()
        payload=self.module[64:]+cache.template_seed();self.module=envelope(payload,len(payload),COMMIT[:12])
        self.manifest.update(module_bytes=len(self.module),module_sha256=tool.sha(self.module));self.save_package()
        with self.assertRaisesRegex(ValueError,'duplicated|ambiguous'):self.install(game='ps2')

    def test_game_and_explicit_p0005_preserve_pair(self):
        pair(self.card,'p0005',number='0005');before=self.snapshots()
        report=self.install(Path('KUI/vmu-test/p0005/retail-persistent-profile-0005.bin'),game='sa2')
        self.assertEqual(report['pair_path'],'/KUI/vmu-test/p0005');self.assertEqual(report['game'],'sa2')
        self.assertEqual(before,self.snapshots())

    def test_cache_record_symlinks_and_directory_entries_are_refused(self):
        path=self.package/'native-cache/ps2.nvr';wire=path.read_bytes();path.unlink()
        external=self.root/'record';external.write_bytes(wire);path.symlink_to(external)
        with self.assertRaisesRegex(ValueError,'ordinary'):self.install(game='ps2')
        path.unlink();path.mkdir()
        with self.assertRaisesRegex(ValueError,'ordinary'):self.install(game='ps2')

class NativeCacheCodecTests(unittest.TestCase):
    def test_seed_and_record_corruptions(self):
        wire=record_fixture();metadata=cache.decode_record(wire)
        seed=cache.fill_seed(cache.template_seed(),wire,metadata['record_digest'])
        self.assertEqual(cache.decode_seed(seed),metadata)
        self.assertIsNone(cache.decode_seed(cache.template_seed()))
        self.assertEqual(cache.find_seed(b'prefix'+seed+b'tail'),6)
        with self.assertRaisesRegex(ValueError,'duplicated'):
            cache.find_seed(seed+cache.template_seed())
        for index in range(1120):
            changed=bytearray(seed);changed[index]^=1
            with self.subTest(index=index),self.assertRaises(ValueError):cache.decode_seed(changed)
        for index in (0,8,12,16,20,24,28,64,68,72,76,80,84,132,136,168,172,192,404,608,612,616,620,624,628,700,704,708,712,716,720,748):
            changed=bytearray(wire);changed[index]^=0x80;changed[992:]=hashlib.sha256(changed[:992]).digest()
            with self.subTest(index=index),self.assertRaises(ValueError):cache.decode_record(changed)
        with self.assertRaises(ValueError):cache.fill_seed(cache.template_seed(),wire,None)
        with self.assertRaises(ValueError):cache.decode_record(wire,10**10)

class NativeRuntimeModePersonalizationTests(NativeCachePersonalizationTests):
    def setUp(self):
        super().setUp();self.mode_offset=len(self.module)
        self.module=envelope(self.module[64:]+mode.template_mode()+bytes(32),len(self.module)-64+64,COMMIT[:12])
        self.manifest.update(module_bytes=len(self.module),module_sha256=tool.sha(self.module),
            runtime_mode_module_offset=self.mode_offset,runtime_mode_bytes=mode.MODE_BYTES,
            runtime_mode_sha256=tool.sha(mode.template_mode()),
            runtime_mode_choices={name:value for value,name in mode.MODE_NAMES.items()})
        self.save_package()

    def install(self,profile=None,game=None,**options):
        return tool.install(self.card,package=self.package,profile=profile,game=game,**options)

    def installed_mode(self):
        return mode.decode_mode(self.destination.read_bytes()[self.mode_offset:self.mode_offset+32])

    def test_all_modes_preserve_pairs_and_only_permitted_spans(self):
        before=self.snapshots()
        for options,expected in (({},0),({'control':True},1),({'profile_runtime':True},2),
                                 ({'lease_only':True},3),({'hooks_only':True},4),({'raw_chain':True},5),
                                 ({'evolution_trace':True},6),({},0)):
            report=self.install(game='sa2',**options);after=self.destination.read_bytes()
            self.assertEqual(report['runtime_mode'],mode.MODE_NAMES[expected]);self.assertEqual(self.installed_mode(),expected)
            self.assertEqual(before,self.snapshots())
            allowed=((32,36),(60,64),(self.manifest['profile_module_offset'],self.manifest['profile_module_offset']+4096),
                (self.seed_offset,self.seed_offset+1120),(self.mode_offset,self.mode_offset+32))
            self.assertTrue(all(a==b or any(first<=i<end for first,end in allowed)
                for i,(a,b) in enumerate(zip(self.module,after))))
            with patch.object(tool.os,'replace',side_effect=AssertionError('must not rewrite')):
                self.assertTrue(self.install(game='sa2',**options)['already_installed'])

    def test_mode_manifest_is_complete_typed_and_disjoint(self):
        old=self.destination.read_bytes()
        for field in tool.MODE_FIELDS:
            changed=dict(self.manifest);del changed[field]
            (self.package/tool.MANIFEST).write_text(json.dumps(changed))
            with self.subTest(missing=field),self.assertRaises(ValueError):self.install()
        self.save_package()
        for changes in ({'runtime_mode_module_offset':True},{'runtime_mode_bytes':True},
            {'runtime_mode_bytes':31},{'runtime_mode_sha256':'0'*64},
            {'runtime_mode_module_offset':self.manifest['profile_module_offset']},
            {'runtime_mode_module_offset':self.seed_offset},
            {'runtime_mode_module_offset':self.mode_offset+4}):
            self.manifest_write(**changes)
            with self.subTest(changes=changes),self.assertRaises(ValueError):self.install(control=True)
        self.assertEqual(old,self.destination.read_bytes())

    def test_noncanonical_or_duplicate_generic_modes_refused(self):
        original=self.module
        for choice in (1,2,3,4,5,6):
            payload=bytearray(original[64:]);struct.pack_into('<I',payload,self.mode_offset-64+16,choice)
            self.module=envelope(payload,len(payload),COMMIT[:12])
            self.manifest.update(module_sha256=tool.sha(self.module));self.save_package()
            with self.subTest(mode=choice),self.assertRaisesRegex(ValueError,'absent|misplaced|duplicated'):
                self.install(control=True)
        self.module=envelope(original[64:]+mode.fill_mode(mode.template_mode(),2),len(original)-64+32,COMMIT[:12])
        self.manifest.update(module_bytes=len(self.module),module_sha256=tool.sha(self.module));self.save_package()
        with self.assertRaisesRegex(ValueError,'duplicated'):self.install()

    def test_legacy_package_refuses_explicit_runtime_modes(self):
        legacy=self.root/'legacy';generic(legacy);old=self.destination.read_bytes();before=self.snapshots()
        for option in ({'control':True},{'profile_runtime':True},{'lease_only':True},{'hooks_only':True},{'raw_chain':True},{'evolution_trace':True}):
            with self.subTest(option=option),self.assertRaisesRegex(ValueError,'no audited native runtime mode'):
                tool.install(self.card,package=legacy,**option)
        self.assertEqual(old,self.destination.read_bytes());self.assertEqual(before,self.snapshots())

    def test_conflicting_or_nonboolean_options_refused(self):
        for options in ({'control':True,'profile_runtime':True}, {'control':True,'lease_only':True},
                        {'profile_runtime':True,'lease_only':True},
                        {'control':True,'hooks_only':True}, {'profile_runtime':True,'hooks_only':True},
                        {'lease_only':True,'hooks_only':True},
                        {'control':True,'profile_runtime':True,'lease_only':True},
                        {'control':1},{'profile_runtime':1},{'lease_only':1},{'hooks_only':1},{'raw_chain':1},{'evolution_trace':1},
                        *({name:True,'raw_chain':True} for name in ('control','profile_runtime','lease_only','hooks_only')),
                        *({name:True,'evolution_trace':True} for name in ('control','profile_runtime','lease_only','hooks_only','raw_chain'))):
            with self.subTest(options=options),self.assertRaisesRegex(ValueError,'at most one'):
                self.install(**options)

    def test_old_mode_package_cannot_select_lease_only(self):
        before=self.snapshots();old=self.destination.read_bytes()
        del self.manifest['runtime_mode_choices'];self.save_package()
        with self.assertRaisesRegex(ValueError,'no audited lease-only'):
            self.install(lease_only=True)
        self.assertEqual(old,self.destination.read_bytes());self.assertEqual(before,self.snapshots())
        # Earlier 0/1/2 packages remain installable with their original modes.
        for options,expected in (({},'normal'),({'control':True},'control'),({'profile_runtime':True},'profile')):
            with self.subTest(options=options):self.assertEqual(self.install(**options)['runtime_mode'],expected)

    def test_prior_mode_packages_refuse_hooks_only_before_profile_selection_or_replace(self):
        before=self.snapshots();old=self.destination.read_bytes()
        for choices in (None,tool.MODE_CHOICES_LEASE_ONLY):
            if choices is None:self.manifest.pop('runtime_mode_choices',None)
            else:self.manifest['runtime_mode_choices']=dict(choices)
            self.save_package()
            with self.subTest(choices=choices),\
                 patch.object(tool,'select_profile',side_effect=AssertionError('must not select a profile')),\
                 patch.object(tool.os,'replace',side_effect=AssertionError('must not replace loader')):
                with self.assertRaisesRegex(ValueError,'no audited hooks-only'):
                    self.install(hooks_only=True)
            self.assertEqual(old,self.destination.read_bytes());self.assertEqual(before,self.snapshots())

    def test_prior_packages_refuse_raw_chain_before_profile_selection_or_replace(self):
        before=self.snapshots();old=self.destination.read_bytes()
        for choices in (None,tool.MODE_CHOICES_LEASE_ONLY,tool.MODE_CHOICES_HOOKS_ONLY):
            if choices is None:self.manifest.pop('runtime_mode_choices',None)
            else:self.manifest['runtime_mode_choices']=dict(choices)
            self.save_package()
            with self.subTest(choices=choices),\
                 patch.object(tool,'select_profile',side_effect=AssertionError('must not select a profile')),\
                 patch.object(tool.os,'replace',side_effect=AssertionError('must not replace loader')):
                with self.assertRaisesRegex(ValueError,'no audited raw-chain'):
                    self.install(raw_chain=True)
            self.assertEqual(old,self.destination.read_bytes());self.assertEqual(before,self.snapshots())

    def test_prior_lease_only_capability_retains_all_original_modes(self):
        self.manifest['runtime_mode_choices']=dict(tool.MODE_CHOICES_LEASE_ONLY);self.save_package()
        before=self.snapshots()
        for options,expected in (({},0),({'control':True},1),({'profile_runtime':True},2),({'lease_only':True},3)):
            with self.subTest(options=options):
                self.install(**options);self.assertEqual(self.installed_mode(),expected)
                self.assertEqual(before,self.snapshots())

    def test_prior_packages_refuse_evolution_trace_before_profile_selection_or_replace(self):
        before=self.snapshots();old=self.destination.read_bytes()
        for choices in (None,tool.MODE_CHOICES_LEASE_ONLY,tool.MODE_CHOICES_HOOKS_ONLY,tool.MODE_CHOICES_RAW_CHAIN):
            if choices is None:self.manifest.pop('runtime_mode_choices',None)
            else:self.manifest['runtime_mode_choices']=dict(choices)
            self.save_package()
            with self.subTest(choices=choices),\
                 patch.object(tool,'select_profile',side_effect=AssertionError('must not select a profile')),\
                 patch.object(tool.os,'replace',side_effect=AssertionError('must not replace loader')):
                with self.assertRaisesRegex(ValueError,'no audited evolution-trace'):
                    self.install(evolution_trace=True)
            self.assertEqual(old,self.destination.read_bytes());self.assertEqual(before,self.snapshots())

    def test_mode_capability_declaration_requires_exact_integer_choices(self):
        old=self.destination.read_bytes();before=self.snapshots()
        for choices in ({'lease-only':3}, {'normal':False,'control':1,'profile':2,'lease-only':3},
                        {'normal':0,'control':1,'profile':2,'lease-only':2},
                        {**tool.MODE_CHOICES_LEASE_ONLY,'hooks-only':True},
                        {**tool.MODE_CHOICES_LEASE_ONLY,'hooks-only':5},
                        {**tool.MODE_CHOICES_HOOKS_ONLY,'extra':5},
                        {**tool.MODE_CHOICES_RAW_CHAIN,'raw-chain':True},
                        {**tool.MODE_CHOICES_RAW_CHAIN,'raw-chain':6},
                        {**tool.MODE_CHOICES_EVOLUTION_TRACE,'evolution-trace':True},
                        {**tool.MODE_CHOICES_EVOLUTION_TRACE,'evolution-trace':5}, [], 3):
            self.manifest_write(runtime_mode_choices=choices)
            with self.subTest(choices=choices),self.assertRaisesRegex(ValueError,'capabilities'):
                self.install(lease_only=True)
        self.assertEqual(old,self.destination.read_bytes());self.assertEqual(before,self.snapshots())

    def test_extracted_zip_layout_runs_with_only_two_shipped_codecs(self):
        tools=self.package/'tools';tools.mkdir();source=Path(tool.__file__).parent
        for name in ('runtime_package.py','vmu_persist_profile.py','vmu_native_cache_seed.py','vmu_native_mode.py'):
            (tools/name).write_bytes((source/name).read_bytes())
        (self.package/'personalize.py').write_bytes(Path(tool.__file__).read_bytes())
        args=[sys.executable,str(self.package/'personalize.py'),str(self.card),'--game','sa2',
            '--profile','KUI/vmu-test/p0001/retail-persistent-profile-0001.bin']
        before=self.snapshots()
        for flag,expected in (('--control',1),('--profile-runtime',2),('--lease-only',3),('--hooks-only',4),('--raw-chain',5),('--evolution-trace',6),(None,0)):
            result=subprocess.run(args+([flag] if flag else []),cwd=self.root,capture_output=True,text=True,check=True)
            self.assertEqual(self.installed_mode(),expected);self.assertEqual(before,self.snapshots())
            expected_text={1:'virtual VMU is disabled',3:'reserved RAM remains allocated',
                           4:'physical Maple traffic passes without virtual replies',
                           5:'active hooks, original DMA list',
                           6:'Evolution executable tracing enabled'}
            self.assertIn(expected_text.get(expected,'native persistent VMU test'),result.stdout)
        for flags in (['--control','--profile-runtime'],['--control','--lease-only'],
                      ['--profile-runtime','--lease-only'],['--list-games','--lease-only'],
                      ['--control','--hooks-only'],['--profile-runtime','--hooks-only'],
                      ['--lease-only','--hooks-only'],['--list-games','--hooks-only'],
                      *([flag,'--raw-chain'] for flag in ('--control','--profile-runtime','--lease-only','--hooks-only','--list-games')),
                      *([flag,'--evolution-trace'] for flag in ('--control','--profile-runtime','--lease-only','--hooks-only','--raw-chain','--list-games'))):
            result=subprocess.run(args+flags,cwd=self.root,capture_output=True,text=True)
            self.assertEqual(result.returncode,2);self.assertEqual(before,self.snapshots())

if __name__=='__main__':unittest.main()
