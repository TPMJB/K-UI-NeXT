#!/usr/bin/env python3
"""Compile the actual high collector in both isolated profiles."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from check_ce_demand_profile import ObserverCPU


class DemandDiagnosticTests(unittest.TestCase):
    def test_observer_integer_forms(self):
        def cpu(a=0,b=0,t=0):
            out=object.__new__(ObserverCPU)
            out.r=[0]*16;out.r[1]=a;out.r[2]=b
            out.sr=0x400000f0|t;out.instructions=0;out.executing=0;out.macl=0
            return out
        def signed(value,bits):
            return int.from_bytes((value&((1<<bits)-1)).to_bytes(bits//8,'little'),'little',signed=True)
        values=(0,1,0xffffffff,0x80000000,0x7fffffff,0x12345678)
        c=cpu(t=1);c.r[4]=0x3000;seen=[]
        c.read=lambda address,count:seen.append((address,count)) or 0x8001
        c.plain(0x854f,0)
        self.assertEqual((c.r[0],seen,c.sr),(0xffff8001,[(0x301e,2)],0x400000f1))
        for size,load,store in ((1,0x032c,0x0234),(2,0x032d,0x0235),(4,0x032e,0x0236)):
            c=cpu(t=1);c.r[0]=0x1000;c.r[2]=0x2040;seen=[]
            c.read=lambda address,count:seen.append((address,count)) or ((1<<(8*size))-1)
            c.plain(load,0)
            self.assertEqual((c.r[3],seen,c.sr),(0xffffffff,[(0x3040,size)],0x400000f1))
            c.write=lambda address,value,count:seen.append((address,value,count))
            c.plain(store,2)
            self.assertEqual(seen[-1],(0x3040,0xffffffff,size))
        for immediate in range(256):
            for value in values:
                c=cpu();c.r[0]=value;c.plain(0x8800|immediate,0)
                self.assertEqual(c.sr&1,int(value==(signed(immediate,8)&0xffffffff)))
        for t in (0,1):
            c=cpu(t=t);c.plain(0x0329,0)
            self.assertEqual((c.r[3],c.sr),(t,0x400000f0|t))
            for a in values:
                c=cpu(a,t=t);c.plain(0x621a,0)
                self.assertEqual((c.r[2],c.sr&1),((-a-t)&0xffffffff,int(bool(a or t))))
                for b in values:
                    c=cpu(a,b,t);c.plain(0x312a,0)
                    self.assertEqual((c.r[1],c.sr&1),((a-b-t)&0xffffffff,int(a<b+t)))
        for a in values:
            c=cpu(a,t=1);c.plain(0x6217,0)
            self.assertEqual((c.r[2],c.sr),((~a)&0xffffffff,0x400000f1))
            c=cpu(a);c.plain(0x4110,0)
            self.assertEqual((c.r[1],c.sr&1),((a-1)&0xffffffff,int(a==1)))
            for opcode,bits in ((0x4119,8),(0x4129,16)):
                c=cpu(a,t=1);c.plain(opcode,0)
                self.assertEqual((c.r[1],c.sr),(a>>bits,0x400000f1))
            for opcode,positive in ((0x4111,False),(0x4115,True)):
                c=cpu(a);c.plain(opcode,0)
                self.assertEqual(c.sr&1,int(signed(a,32)>0 if positive else signed(a,32)>=0))
            for b in (*values,0xffffffe0,0xffffffe1,0xffffffdf,31,32,33):
                for opcode,arithmetic in ((0x412d,False),(0x412c,True)):
                    c=cpu(a,b,t=1);c.plain(opcode,0)
                    if signed(b,32)>=0:expected=(a<<(b%32))&0xffffffff
                    else:
                        shift=(-signed(b,32))%32 or 32
                        expected=((signed(a,32) if arithmetic else a)>>shift)&0xffffffff
                    self.assertEqual((c.r[1],c.sr),(expected,0x400000f1))
                c=cpu(a,b,t=1);c.plain(0x212a,0)
                self.assertEqual((c.r[1],c.sr),(a^b,0x400000f1))
                for opcode,expected in ((0x0127,(a*b)&0xffffffff),
                    (0x212e,((a&0xffff)*(b&0xffff))&0xffffffff),
                    (0x212f,(signed(a,16)*signed(b,16))&0xffffffff)):
                    c=cpu(a,b,t=1);c.plain(opcode,0);c.plain(0x031a,2)
                    self.assertEqual((c.r[3],c.sr),(expected,0x400000f1))

    def test_actual_collector_profiles(self):
        with tempfile.TemporaryDirectory(prefix="kui-demand-") as temp:
            for profile in (1, 2, 3):
                output = Path(temp) / f"profile{profile}"
                command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                           "-fsanitize=undefined", "-fno-sanitize-recover=all", "-Iinclude", "-Isrc/loader",
                           "-DKUI_RETAIL_CE=1", "-DKUI_ARMADA_CE_STREAM=1", "-DKUI_RETAIL_ASYNC_TEST=1",
                           "-DKUI_RETAIL_DEMAND_DIAGNOSTIC_TEST=1",
                           f"-DKUI_RETAIL_DEMAND_DIAGNOSTIC={profile}",
                           "-DKUI_RETAIL_DEMAND_PROFILE_ID=0xce000002u",
                           "tests/test_retail_demand_diagnostic.c", "-o", str(output)]
                subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
                result = subprocess.run([str(output)], cwd=ROOT, check=True, capture_output=True, text=True)
                self.assertIn("contracts OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
