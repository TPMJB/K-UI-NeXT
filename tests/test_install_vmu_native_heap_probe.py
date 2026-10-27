#!/usr/bin/env python3
import importlib.util,json,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('installer',Path(__file__).resolve().parents[1]/'tools/install_vmu_native_heap_probe.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)/'card';self.parent=self.root/'KUI/apps/games';self.parent.mkdir(parents=True)
        self.bundle=Path(self.temp.name)/'bundle';(self.bundle/'KUI/apps/games').mkdir(parents=True)
        self.original=b'KUIRUN1\0'+b'O'*128;self.probe=b'KUIRUN1\0'+b'P'*192
        self.active=self.parent/m.ACTIVE;self.active.write_bytes(self.original)
        self.set_probe(self.probe);(self.root/'save.vmd').write_bytes(b'user save')
    def set_probe(self,data):
        (self.bundle/'KUI/apps/games'/m.ACTIVE).write_bytes(data)
        (self.bundle/'probe-report.json').write_text(json.dumps({'module_sha256':m.sha(data)}))
    def test_install_repeat_restore(self):
        m.install(self.root,self.bundle);m.install(self.root,self.bundle)
        self.assertEqual(self.active.read_bytes(),self.probe)
        m.install(self.root,self.bundle,True);m.install(self.root,self.bundle,True)
        self.assertEqual(self.active.read_bytes(),self.original)
        self.assertEqual((self.root/'save.vmd').read_bytes(),b'user save')
    def test_bad_package(self):
        (self.bundle/'probe-report.json').write_text('{"module_sha256":"wrong"}')
        with self.assertRaises(ValueError):m.install(self.root,self.bundle)
        self.assertEqual(self.active.read_bytes(),self.original)
        self.assertFalse((self.parent/m.BACKUP).exists())
    def test_incomplete_backup(self):
        (self.parent/m.BACKUP).write_bytes(b'keep')
        with self.assertRaises(ValueError):m.install(self.root,self.bundle)
        self.assertEqual((self.parent/m.BACKUP).read_bytes(),b'keep')
    def test_corrupt_backup(self):
        m.install(self.root,self.bundle);(self.parent/m.BACKUP).write_bytes(b'bad')
        with self.assertRaises(ValueError):m.install(self.root,self.bundle,True)
        self.assertEqual(self.active.read_bytes(),self.probe)
    def test_preserve_later_loader(self):
        m.install(self.root,self.bundle);later=b'KUIRUN1\0'+b'L'*200;self.active.write_bytes(later)
        with self.assertRaises(ValueError):m.install(self.root,self.bundle,True)
        self.assertEqual(self.active.read_bytes(),later)
    def test_new_probe_requires_restore(self):
        m.install(self.root,self.bundle);self.set_probe(b'KUIRUN1\0'+b'N'*200)
        with self.assertRaises(ValueError):m.install(self.root,self.bundle)
        self.assertEqual(self.active.read_bytes(),self.probe)
    def test_restored_original_accepts_next_probe(self):
        m.install(self.root,self.bundle);m.install(self.root,self.bundle,True)
        next_probe=b'KUIRUN1\0'+b'N'*200;self.set_probe(next_probe);m.install(self.root,self.bundle)
        self.assertEqual(self.active.read_bytes(),next_probe)
        m.install(self.root,self.bundle,True);self.assertEqual(self.active.read_bytes(),self.original)
        self.assertEqual((self.parent/m.BACKUP).read_bytes(),self.original)
    def test_symlink(self):
        self.active.unlink();self.active.symlink_to(self.root/'save.vmd')
        with self.assertRaises(ValueError):m.install(self.root,self.bundle)
        self.assertEqual((self.root/'save.vmd').read_bytes(),b'user save')
if __name__=='__main__':unittest.main()
