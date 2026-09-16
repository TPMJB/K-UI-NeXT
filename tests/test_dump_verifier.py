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
