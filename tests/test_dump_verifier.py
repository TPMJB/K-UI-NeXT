# SPDX-License-Identifier: GPL-3.0-only
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import verify_dump as v


class DumpVerifierTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)
        tracks=[]
        for n,session,start in ((1,0,150),(2,1,45150)):
            data=bytes(range(256))*9+b"x"*48
            name=f"track{n:02}.bin";(self.path/name).write_bytes(data)
            tracks.append(dict(number=n,session=session,control=4,start_fad=start,end_fad=start+1,
                toc_end_fad=start+1,excluded_tail_sectors=0,file=name,bytes=len(data),
                crc32=f"{zlib.crc32(data):08x}",sha256=hashlib.sha256(data).hexdigest()))
        self.m=dict(schema=1,complete=True,saved_data_verified=True,profile=v.PROFILE,identity="0"*64,sector_bytes=2352,tracks=tracks)
        self.write();(self.path/"disc.gdi").write_text("2\n1 0 4 2352 track01.bin 0\n2 45000 4 2352 track02.bin 0\n")

    def write(self):
        (self.path/"manifest.json").write_text(json.dumps(self.m))

    def test_valid_and_references(self):
        results=v.verify(self.path)
        self.assertEqual(v.compare_reference(results,self.m),(True,2))
        partial=copy.deepcopy(self.m);partial["tracks"].pop()
        self.assertEqual(v.compare_reference(results,partial),(False,1))
        partial["tracks"][0]["sha256"]="1"*64
        with self.assertRaises(ValueError):v.compare_reference(results,partial)

    def crc_only(self):
        """The same dump as a schema 2 (CRC-only) manifest."""
        m=copy.deepcopy(self.m);m["schema"]=2;m["hashes"]=["crc32"]
        del m["saved_data_verified"]
        for t in m["tracks"]: del t["sha256"]
        return m

    def test_schema2_crc_only(self):
        self.m=self.crc_only();self.write()
        results=v.verify(self.path)
        self.assertEqual([r["sha256_recorded"] for r in results],[False,False])
        self.assertEqual(len(results[0]["sha256"]),64)     # computed here for reference
        self.assertEqual(v.compare_reference(results,self.m),(True,2))
        # A reference that carries SHA-256 is still compared against the computed one.
        ref=copy.deepcopy(self.m)
        for r,t in zip(ref["tracks"],results): r["sha256"]=t["sha256"]
        self.assertEqual(v.compare_reference(results,ref),(True,2))
        ref["tracks"][0]["sha256"]="1"*64
        with self.assertRaises(ValueError):v.compare_reference(results,ref)
        # A tampered track fails on CRC32 alone.
        p=self.path/"track01.bin";data=p.read_bytes();p.write_bytes(b"!"+data[1:])
        with self.assertRaises(ValueError):v.verify(self.path)

    def test_schema2_refuses_claims_and_wrong_shape(self):
        base=self.crc_only()
        for label,change in (("claim",lambda m:m.update(saved_data_verified=True)),
                             ("reference",lambda m:m.update(reference="not compared")),
                             ("hashes",lambda m:m.update(hashes=["crc32","sha256"])),
                             ("nohashes",lambda m:m.pop("hashes")),
                             ("incomplete",lambda m:m.update(complete=False)),
                             ("sha",lambda m:m["tracks"][0].update(sha256="0"*64)),
                             ("schema3",lambda m:m.update(schema=3))):
            self.m=copy.deepcopy(base);change(self.m);self.write()
            with self.assertRaises(ValueError,msg=label):v.verify(self.path)
        # Schema 1 must still demand what it always did.
        self.m=copy.deepcopy(base);self.m["schema"]=1;self.write()
        with self.assertRaises(ValueError):v.verify(self.path)

    def test_corruption_and_incomplete(self):
        p=self.path/"track01.bin";data=p.read_bytes();p.write_bytes(b"!"+data[1:])
        with self.assertRaises(ValueError):v.verify(self.path)
        p.write_bytes(data[:-1])
        with self.assertRaises(ValueError):v.verify(self.path)
        p.write_bytes(data);self.m["complete"]=False;self.write()
        with self.assertRaises(ValueError):v.verify(self.path)

    def test_metadata_and_path_validation(self):
        original=copy.deepcopy(self.m)
        for key,value in (("file","../outside"),("bytes",2353),("start_fad",149),("control",2),("excluded_tail_sectors",150)):
            self.m=copy.deepcopy(original);self.m["tracks"][0][key]=value;self.write()
            with self.assertRaises(ValueError):v.verify(self.path)
        self.m=original;self.write();(self.path/"disc.gdi").write_text("wrong\n")
        with self.assertRaises(ValueError):v.verify(self.path)

    def test_named_gdi_and_legacy_compatibility(self):
        for schema in (1, 2):
            with self.subTest(schema=schema):
                if schema == 2:
                    self.m = self.crc_only()
                self.m["gdi_file"] = "MDK2.gdi"
                self.write()
                legacy = self.path / "disc.gdi"
                named = self.path / "MDK2.gdi"
                if legacy.exists():
                    legacy.rename(named)
                self.assertEqual(len(v.verify(self.path)), 2)
                # The explicit name wins; a stale second descriptor cannot mask
                # corruption of the actual game descriptor.
                legacy.write_bytes(named.read_bytes())
                named.write_text("wrong\n")
                with self.assertRaisesRegex(ValueError, "GDI descriptor"):
                    v.verify(self.path)
                named.write_bytes(legacy.read_bytes())
                legacy.unlink()

    def test_named_gdi_rejects_unsafe_names_and_links(self):
        for name in ("../disc.gdi", "/disc.gdi", "C:disc.gdi", "a\\disc.gdi",
                     "a\n.gdi", ".gdi", ".hidden.gdi", "x.gdi.", "x.gdi ",
                     "x" * 100 + ".gdi", "x.bin", None, 42):
            with self.subTest(name=name):
                self.m["gdi_file"] = name
                self.write()
                with self.assertRaisesRegex(ValueError, "Unsafe GDI filename"):
                    v.verify(self.path)
        self.m["gdi_file"] = "alias.gdi"
        self.write()
        (self.path / "alias.gdi").symlink_to(self.path / "disc.gdi")
        with self.assertRaisesRegex(ValueError, "Missing/unsafe GDI"):
            v.verify(self.path)

    def test_a_missing_metadata_file_names_the_path_it_looked_for(self):
        # Run from tools/ with docs/evidence/x.json, the reference was looked for in tools/docs/evidence and
        # the error named only x.json. It now says where it looked, and calls a symbolic link a link.
        with tempfile.TemporaryDirectory() as d:
            missing = Path(d) / "nowhere" / "reference.json"
            with self.assertRaises(ValueError) as caught:
                v.load_json(missing)
            self.assertIn(str(missing.resolve()), str(caught.exception))
            self.assertIn("relative path is taken from the directory you ran this in", str(caught.exception))
            target, link = Path(d) / "real.json", Path(d) / "link.json"
            target.write_text("{}")
            link.symlink_to(target)
            with self.assertRaises(ValueError) as caught:
                v.load_json(link)
            self.assertIn("symbolic link", str(caught.exception))
