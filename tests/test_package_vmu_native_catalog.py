# SPDX-License-Identifier: GPL-3.0-only
"""The release catalog contains only five pinned, data-only records."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from package_vmu_native_persistent import cache_catalog
import personalize_vmu_native_persistent as personalizer
import vmu_native_cache_seed as seed_codec


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='kui-catalog-')
        self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name)
        (self.root/'docs/evidence').mkdir(parents=True)
        (self.root/'tests/fixtures/native-cache').mkdir(parents=True)
        self.evidence=json.loads((ROOT/'docs/evidence/native-cache-catalog-inputs.json').read_bytes())
        for slug in ('ps2','skies','sa2','grandia2','evolution1'):
            relative=f'tests/fixtures/native-cache/{slug}.nvr'
            shutil.copyfile(ROOT/relative,self.root/relative)
        self.write()

    def write(self):
        (self.root/'docs/evidence/native-cache-catalog-inputs.json').write_text(json.dumps(self.evidence))

    def test_exact_catalog_and_commit(self):
        wire,files=cache_catalog(self.root,'a'*40)
        catalog=json.loads(wire)
        self.assertEqual(catalog['source_commit'],'a'*40)
        self.assertEqual({g['slug'] for g in catalog['games']},{'ps2','skies','sa2','grandia2','evolution1'})
        self.assertEqual(set(files),{'native-cache/catalog.json',*(f'native-cache/{s}.nvr' for s in ('ps2','skies','sa2','grandia2','evolution1'))})
        self.assertEqual(files['native-cache/catalog.json'],wire)
        for game in catalog['games']:
            record=files[game['record_path']]
            self.assertEqual(len(record),1024)
            self.assertEqual(hashlib.sha256(record).hexdigest(),game['record_sha256'])
            self.assertEqual(hashlib.sha256(record[:992]).hexdigest(),game['record_digest'])
        self.assertNotIn(b'input_relative',wire)

    def test_personalizer_consumes_complete_release_catalog(self):
        # Exercise the producer/consumer boundary using all real reviewed
        # metadata records. Synthetic three-game personalizer fixtures cannot
        # catch a stale release-side title allowlist.
        commit='b'*40
        wire,files=cache_catalog(self.root,commit)
        for relative,data in files.items():
            path=self.root/relative;path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(data)
        seed=seed_codec.template_seed()
        manifest=dict(source_commit=commit,
            cache_seed_module_offset=64,cache_seed_bytes=len(seed),
            cache_seed_sha256=hashlib.sha256(seed).hexdigest(),
            cache_record_bytes=seed_codec.RECORD_BYTES,
            cache_detector_epoch=seed_codec.DETECTOR_EPOCH,
            cache_catalog_path=personalizer.CATALOG_PATH,
            cache_catalog_sha256=hashlib.sha256(wire).hexdigest())
        accepted=personalizer.cache_catalog(self.root,manifest)
        self.assertEqual(set(accepted),{'ps2','skies','sa2','grandia2','evolution1'})
        for game in json.loads(wire)['games']:
            with self.subTest(slug=game['slug']):
                self.assertEqual(accepted[game['slug']],
                    {**game,'wire':files[game['record_path']]})

    def test_bad_evidence_refuses(self):
        original=copy.deepcopy(self.evidence)
        mutations=[lambda e:e.update(detector_epoch=2),lambda e:e.update(schema='other'),
            lambda e:e['inputs'].append(copy.deepcopy(e['inputs'][0])),
            lambda e:e['inputs'][0].update(slug='recv'),
            lambda e:e['inputs'][0].update(record_fixture='../external.bin')]
        for field in ('record_bytes','record_sha256','record_digest','image_bytes','image_sha256','load_address'):
            mutations.append(lambda e,f=field:e['inputs'][0].update({f:0 if isinstance(e['inputs'][0][f],int) else '0'*64}))
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                self.evidence=copy.deepcopy(original);mutation(self.evidence);self.write()
                with self.assertRaises(ValueError):cache_catalog(self.root,'a'*40)

    def test_each_record_corruption_refuses(self):
        for slug in ('ps2','skies','sa2','grandia2','evolution1'):
            path=self.root/f'tests/fixtures/native-cache/{slug}.nvr';original=path.read_bytes()
            for offset in (0,16,32,88,608,748,991,992,1023):
                with self.subTest(slug=slug,offset=offset):
                    changed=bytearray(original);changed[offset]^=1;path.write_bytes(changed)
                    with self.assertRaises(ValueError):cache_catalog(self.root,'a'*40)
                    path.write_bytes(original)


if __name__=='__main__':unittest.main()
