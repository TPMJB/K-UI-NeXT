#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the old linked DRIVE collision and the candidate linked wait gate.

Original bytes are read locally and never emitted. Sleep is a modeled loan
release, not a CE scheduler or hardware boot. Admission executes production C.
"""
import argparse, ctypes, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
import test_vc2_ce_vmu_gd_wait_native as gate
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from armada_ce_rom_audit import ROMInventory

def sha(data):return hashlib.sha256(data).hexdigest()
def read(image,at,n):
    rows=[s for s in image[3] if s[1]==1 and s[2]&2 and s[3]<=at and at+n<=s[3]+s[5]]
    assert len(rows)==1
    s=rows[0];off=s[4]+at-s[3];return image[0][off:off+n]
def bind(helper,resident):
    gate.ADMIT=helper[4]['_kui_vc2_ce_vmu_gd_wait_admit'][0]
    gate.FAULT=helper[4]['_kui_vc2_ce_vmu_gd_wait_fault'][0]
    gate.RETRY=resident[4]['__kui_vc2_gd_retry_before_trace'][0]
    gate.LEGACY=resident[4]['__kui_vc2_gd_legacy_busy'][0]
    gate.ACTIVE_P1=resident[4]['_kui_retail_hook_active'][0]
    gate.ACTIVE_P2=gate.ACTIVE_P1|0x20000000
class Linked(gate.CPU):
    def __init__(self,code,admit,**kw):super().__init__(code,**kw);self.actual_admit=admit
    def admit(self):
        args=self.r[4:8].copy();self.admit_calls.append(args)
        assert args==[self.caller_pr,self.incoming_sr,self.initial_args[0],3|(self.initial_args[3]<<8)]
        result=self.actual_admit(*args)
        self.homes();target=self.pr;self.clobber();self.r[0]=result;self.pc=target

def old_error(helper,resident,rom,module):
    code=symbol_bytes(helper,'_kui_vc2_ce_vmu_gd_wait',2)[1]
    assert code[:10]==struct.pack('<5H',0x6173,0x2118,0x8901,0x432b,9)
    bind(helper,resident);cpu=Linked(code,lambda *_:0,function=4).run()
    assert cpu.stop=='legacy' and not cpu.admit_calls and not cpu.sleep_calls
    pc=gate.LEGACY;stop=pc+24;trace=[]
    for _ in range(20):
        if pc==stop:break
        w=struct.unpack('<H',read(resident,pc,2))[0];trace.append(hex(pc))
        if w>>8 in (0x89,0x8b):
            d=w&255;d-=256 if d&128 else 0
            take=bool(cpu.sr&1)==(w>>8==0x89);pc=pc+4+2*d if take else pc+2
        else:cpu.plain(w,pc);pc+=2
    assert pc==stop and cpu.r[0]==0xffffffff
    # Execute the exact original TST/BF/error-load/move sequence. SetKMode
    # between the firmware return and this branch preserves saved R9.
    r=[0]*16;r[9]=cpu.r[0];pc=0x23d6;t=False;original=[]
    for _ in range(8):
        if pc==0x23f4:break
        w=struct.unpack('<H',rom.rva_read(module,pc,2))[0];original.append(hex(pc))
        n=(w>>8)&15;m=(w>>4)&15
        if w&0xf00f==0x2008:t=not(r[n]&r[m]);pc+=2
        elif w>>8==0x8b:
            d=w&255;d-=256 if d&128 else 0;pc=pc+2 if t else pc+4+2*d
        elif w>>12==0xd:
            at=(pc&~3)+4+4*(w&255);r[n]=struct.unpack('<I',rom.rva_read(module,at,4))[0];pc+=2
        elif w&0xf00f==0x6003:r[n]=r[m];pc+=2
        else:raise AssertionError(hex(w))
    assert pc==0x23f4 and r[0]==0xc0000185
    return dict(gd_result='0xffffffff',original_error='0xc0000185',linked_trace=trace,
                original_rvas=original,gate_sha256=sha(code),driver_window_sha256=sha(rom.rva_read(module,0x23ac,0x60)))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('kernel','old-helper-elf','old-resident-elf','helper-elf','resident-elf'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--report',type=Path);a=p.parse_args()
    assert sha(a.kernel.read_bytes())=='b728e8eaef03bd7cc5acbe3dc3363ec9fafcaca90916cf21781d2e72ff99b3bc'
    rom=ROMInventory(a.kernel);module=rom.modules['wsegacd.dll']
    negative=old_error(read_elf(a.old_helper_elf),read_elf(a.old_resident_elf),rom,module)
    helper=read_elf(a.helper_elf);resident=read_elf(a.resident_elf);bind(helper,resident)
    code=symbol_bytes(helper,'_kui_vc2_ce_vmu_gd_wait',2)[1];assert len(code)==192
    evidence=json.loads((ROOT/'docs/evidence/vc2-gd-request-busy-source.json').read_text())
    controls=[r for r in evidence['firmware_calls'] if r['function'] not in (0,1,2)];assert len(controls)==11
    with tempfile.TemporaryDirectory() as temporary:
        lib=Path(temporary)/'admit.so'
        subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-shared','-fPIC',
          '-DKUI_VC2_GD_WAIT_HOST_TEST=1','-I'+str(ROOT/'include'),str(ROOT/'src/loader/vc2_ce_vmu_gd_wait.c'),'-o',str(lib)],check=True)
        admit=ctypes.CDLL(str(lib)).kui_vc2_ce_vmu_gd_wait_admit
        admit.argtypes=[ctypes.c_uint32]*4;admit.restype=ctypes.c_uint32
        rows=[]
        for row in controls:
            for sleeps in (1,2,8):
                c=Linked(code,admit,function=row['function'],caller_pr=0x01cf0000|int(row['return_rva'],16),sleeps=sleeps).run()
                assert c.stop=='retry' and len(c.sleep_calls)==sleeps
                assert c.r[4:8]==c.initial_args and c.r[15]==gate.FRAME
                assert (c.gbr,c.mach,c.macl)==c.initial_special
                rows.append(dict(function=row['function'],return_rva=row['return_rva'],sleeps=sleeps))
    report=dict(schema='kui.vc2.gd-loan-control-collision.v1',status='PASS',old=negative,
      candidate=dict(gate_bytes=len(code),gate_sha256=sha(code),cases=rows),
      inputs={str(v.resolve()):sha(v.read_bytes()) for k,v in vars(a).items() if isinstance(v,Path) and k!='report'},
      production_C_admission=True,original_bytes_emitted=False,console_boot_verified=False,
      model_boundaries=['SH integer instructions','Sleep releases loan2','CE argument-home adversary'])
    text=json.dumps(report,indent=2)+'\n'
    if a.report:a.report.write_text(text)
    print(text)
if __name__=='__main__':main()
