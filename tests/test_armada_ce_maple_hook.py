#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the complete authored SH bridge/leaf in a closed integer model."""
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from check_armada_ce_maple_hook import BRIDGE,HOOK,LOG,BRIDGE_BYTES,HOOK_BYTES,check,read_section_elf
MASK=0xffffffff

class CPU:
    def __init__(self, seed, base, oldmode, samples=0, visits=0):
        rng=random.Random(seed);self.r=[rng.getrandbits(32) for _ in range(16)]
        self.r[4]=0x00030a60;self.r[15]=0x08a03000
        self.pr=base+0x5db6;self.mach=rng.getrandbits(32);self.macl=rng.getrandbits(32);self.gbr=rng.getrandbits(32)
        self.sr=(rng.getrandbits(10)&0x301)|0x8000|(oldmode<<30)
        self.fpscr=rng.getrandbits(32);self.fr=[rng.getrandbits(32) for _ in range(32)]
        self.base=base;self.pc=base+0x5b0c;self.mem={LOG+4*i:0 for i in range(30)}
        self.mem[LOG+4]=visits;self.mem[LOG+8]=samples
        self.mem.update({0xff000008:0x8c1458a0,0xff000000:0xfffffc03,0xff00001c:0x105,0x8c145890:0x8c14e8b4})
        self.reads=[];self.writes=[];self.apis=[];self.steps=0;self.stack_min=self.r[15]
        self.start_sp=self.r[15];self.mode=oldmode
        self.initial=(self.r.copy(),self.pr,self.mach,self.macl,self.gbr,self.sr,self.fpscr,self.fr.copy())
    def code(self,a,n):
        if self.base+0x5b0c<=a and a+n<=self.base+0x5b10:
            return struct.pack('<I',0x0009a64a)[a-self.base-0x5b0c:a-self.base-0x5b0c+n]
        if self.base+0x67a4<=a and a+n<=self.base+0x6800:
            return BRIDGE_BYTES[a-self.base-0x67a4:a-self.base-0x67a4+n]
        if HOOK<=a and a+n<=HOOK+len(HOOK_BYTES):
            assert self.sr&0x40000000,'P2 entered before SetKMode(true)'
            return HOOK_BYTES[a-HOOK:a-HOOK+n]
        raise AssertionError('Unadmitted code read '+hex(a))
    def read(self,a):
        assert a%4==0;self.reads.append(a)
        if a in self.mem:return self.mem[a]
        return struct.unpack('<I',self.code(a,4))[0]
    def write(self,a,v):
        assert a%4==0
        assert (LOG<=a<LOG+100) or (self.start_sp-52<=a<self.start_sp), 'Unadmitted data write '+hex(a)
        self.writes.append(a);self.mem[a]=v&MASK;self.stack_min=min(self.stack_min,a)
    def t(self,x):self.sr=(self.sr&~1)|int(bool(x))
    def api(self):
        desired=bool(self.r[4]);old=self.mode;self.apis.append(int(desired));self.mode=int(desired)
        # Model every volatile GPR and MAC/GBR as clobbered, beyond the actual
        # small SetKMode implementation. The bridge must restore all of them.
        for i in range(8):self.r[i]=(0x11002000+i*0x1234567+len(self.apis))&MASK
        self.r[0]=old;self.mach^=0xa5a5a5a5;self.macl^=0x5a5a5a5a;self.gbr^=0xc0ded00d
        self.sr=(self.sr&~0x40000001)|(int(desired)<<30)|((self.sr^1)&1)
        self.pc=self.pr
    def plain(self,w,pc):
        n=(w>>8)&15;m=(w>>4)&15;lo=w&15
        if w==9:return
        if w>>12==0xe:self.r[n]=((w&255)-256 if w&128 else w&255)&MASK;return
        if w>>12==7:self.r[n]=(self.r[n]+((w&255)-256 if w&128 else w&255))&MASK;return
        if w>>12==0xd:self.r[n]=self.read((pc&~3)+4+4*(w&255));return
        if w>>12==5:self.r[n]=self.read((self.r[m]+lo*4)&MASK);return
        if w>>12==1:self.write((self.r[n]+lo*4)&MASK,self.r[m]);return
        if w&0xf00f==0x6003:self.r[n]=self.r[m];return
        if w&0xf00f==0x6002:self.r[n]=self.read(self.r[m]);return
        if w&0xf00f==0x6006:
            self.r[n]=self.read(self.r[m]);self.r[m]=(self.r[m]+4)&MASK;return
        if w&0xf00f==0x2006:
            self.r[n]=(self.r[n]-4)&MASK;self.write(self.r[n],self.r[m]);return
        if w&0xf00f==0x2002:self.write(self.r[n],self.r[m]);return
        if w&0xf00f==0x3000:self.t(self.r[n]==self.r[m]);return
        if w&0xf00f==0x3002:self.t(self.r[n]>=self.r[m]);return
        if w&0xf0ff==0x4010:self.r[n]=(self.r[n]-1)&MASK;self.t(self.r[n]==0);return
        if w==0xcbf0:self.r[0]|=0xf0;return
        if w==0x0302:
            assert self.sr&0x40000000;self.r[3]=self.sr;return
        if w in (0x400e,0x430e):
            assert self.sr&0x40000000;self.sr=self.r[n];return
        saves={0x4f22:'pr',0x4f02:'mach',0x4f12:'macl',0x4f13:'gbr'}
        pops={0x4f26:'pr',0x4f06:'mach',0x4f16:'macl',0x4f17:'gbr'}
        if w in saves:
            self.r[15]-=4;self.write(self.r[15],getattr(self,saves[w]));return
        if w in pops:
            setattr(self,pops[w],self.read(self.r[15]));self.r[15]+=4;return
        raise AssertionError('Unknown/noninteger instruction '+hex(w)+' at '+hex(pc))
    def run(self):
        while self.pc!=self.base+0x5b10:
            self.steps+=1;assert self.steps<1000
            pc=self.pc;w=struct.unpack('<H',self.code(pc,2))[0]
            if w>>12 in (0xa,0xb) or w in (0x400b,0x000b):
                if w>>12 in (0xa,0xb):
                    d=w&4095;d=d-4096 if d&2048 else d;target=(pc+4+d*2)&MASK
                    if w>>12==0xb:self.pr=pc+4
                elif w==0x400b:target=self.r[0];self.pr=pc+4
                else:target=self.pr
                slot=struct.unpack('<H',self.code(pc+2,2))[0];self.plain(slot,pc+2)
                if target==self.base+0x6374:self.api()
                else:self.pc=target
            elif w>>8 in (0x89,0x8b):
                d=w&255;d=d-256 if d&128 else d
                take=bool(self.sr&1)==(w>>8==0x89)
                self.pc=(pc+4+d*2 if take else pc+2)&MASK
            else:self.plain(w,pc);self.pc=(pc+2)&MASK
        return self

class HookTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.directory=Path(cls.temp.name)
        gcc=ROOT/'.deps/sh-elf/bin/sh-elf-gcc';copy=ROOT/'.deps/sh-elf/bin/sh-elf-objcopy'
        if not gcc.exists():raise unittest.SkipTest('Native SH toolchain unavailable')
        subprocess.run([str(gcc),'-m4-single-only','-ml','-ffreestanding','-c',str(ROOT/'src/loader/armada_ce_maple_hook.S'),'-o',str(cls.directory/'hook.o')],check=True)
        subprocess.run([str(gcc),'-m4-single-only','-ml','-nostdlib','-Wl,--build-id=none','-T'+str(ROOT/'src/loader/armada_ce_maple_hook.ld'),'-o',str(cls.directory/'hook.elf'),str(cls.directory/'hook.o')],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        for sec in ['bridge','hook']:subprocess.run([str(copy),'--only-section=.'+sec,'-O','binary',str(cls.directory/'hook.elf'),str(cls.directory/(sec+'.bin'))],check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def test_native_complete_bytes_and_layout(self):
        result=check(self.directory);self.assertEqual(result['bridge_bytes'],92);self.assertEqual(result['helper_bytes'],136)
    def test_every_authored_byte_change_refuses(self):
        good=(self.directory/'hook.elf').read_bytes()
        # Locate each authored byte in its dedicated linked section; ELF reader
        # independently rejects modified instructions, literals and padding.
        for code in [BRIDGE_BYTES,HOOK_BYTES]:
            at=good.find(code);self.assertGreaterEqual(at,0)
            for i in range(len(code)):
                changed=bytearray(good);changed[at+i]^=1
                with self.subTest(at=at+i),self.assertRaises(ValueError):read_section_elf(changed)
    def test_cpu_preservation_and_mode_bridge(self):
        for base in [0x01dd0000,0x03dd0000,0x09dd0000]:
            for mode in [0,1]:
                for seed in range(80):
                    c=CPU(seed,base,mode).run();r,pr,mach,macl,gbr,sr,fpscr,fr=c.initial
                    self.assertEqual(c.r[:15],r[:15]);self.assertEqual(c.r[15],r[15]-8)
                    self.assertEqual((c.pr,c.mach,c.macl,c.gbr,c.fpscr,c.fr),(pr,mach,macl,gbr,fpscr,fr))
                    self.assertEqual(c.sr&~1,sr&~1);self.assertEqual(c.mode,mode);self.assertEqual(c.apis,[1,mode])
                    self.assertEqual(c.stack_min,r[15]-52)
                    self.assertEqual((c.mem[r[15]-4],c.mem[r[15]-8]),(r[8],r[9]))
                    expected_sr=(sr&~0x40000001)|0x40000000|((sr^1)&1)
                    expected=[r[4],r[15],pr,expected_sr,mode,0x8c1458a0,0xfffffc03,0x105,0x8c14e8b4,0]
                    self.assertEqual([c.mem[LOG+20+4*i] for i in range(10)],expected)
                    self.assertEqual([c.mem[LOG+60+4*i] for i in range(10)],expected)
                    self.assertTrue(c.mem[LOG+32]&0x40000000)
                    self.assertEqual([c.mem[LOG+i*4] for i in range(5)],[0x314b484d,1,1,32,mode])
                    self.assertTrue(all(c.mem[LOG+100+4*i]==0 for i in range(5)))
    def test_record_cap_and_counter_saturation(self):
        for samples in [1,31,32,33,0xffffffff]:
            for visits in [1,0xfffffffe,0xffffffff]:
                c=CPU(11,0x03dd0000,0,samples,visits).run()
                self.assertEqual(c.mem[LOG+4],min(visits+1,0xffffffff))
                self.assertEqual(c.mem[LOG+8],samples+1 if samples<32 else samples)
                if samples>=32:
                    self.assertFalse(any(a in c.reads for a in [0xff000008,0xff000000,0xff00001c,0x8c145890]))
                    self.assertFalse(any(LOG+20<=a<LOG+100 for a in c.writes))
                else:self.assertFalse(any(LOG+20<=a<LOG+60 for a in c.writes))

if __name__=='__main__':unittest.main()
