#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute actual CE failure decisions through stopped() to all displayed digits.

The linked validators, CRC bodies, call arguments and complete renderer execute.
Only physical PVR register values and VRAM are authored. Optional historical
references describe their own bytes; they do not identify installed console code.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
import test_smash_ce_return_loader as controller
from test_smash_card_receipt_transport import ActualPureSH
from test_smash_native_gin_linked import State
from smash_ce_return_view import audit as view_audit

ABS_ONE=(1,0,0,0xfff1)
MASKS={'bad_runtime':0x400,'bad_raw':0x800,'bad_helper':0x1000,'bad_receipt':0x500}

def require(ok,message):
    if not ok:raise AssertionError(message)
def sha(data):return hashlib.sha256(data).hexdigest()


class FaultChainCPU(ActualPureSH,controller.CECPU):
    def __init__(self,data,kind):
        self.vram=bytearray(b'\x5b'*0x800000);self.pixel_writes=0
        super().__init__(data,kind)

    def write(self,address,value,count=4):
        if 0xa5000000<=address<0xa5800000:
            at=address-0xa5000000
            require(count==2 and not at&1 and at+count<=len(self.vram),'Unbounded/non-RGB565 VRAM write')
            fields=[self.hardware[0xa05f8050]]
            if self.hardware[0xa05f80d0]&16:fields.append(self.hardware[0xa05f8054])
            size=self.hardware[0xa05f805c];words=(size&1023)+1
            stride=(words-1+(size>>20&1023))*4;inside=False
            for base in fields:
                if at<base:continue
                row,column=divmod(at-base,stride)
                inside|=8<=row<48 and not column&1 and 8<=column//2<160
            require(inside,'Renderer escaped its validated banner')
            self.vram[at:at+2]=(value&0xffff).to_bytes(2,'little');self.pixel_writes+=1;return
        return super().write(address,value,count)


def glyph(cpu,base,stride,line,cell):
    rows=[]
    for row in range(7):
        bits=0
        for column in range(5):
            x=12+cell*12+column*2;y=12+line*18+row*2
            pixels=[]
            for dy in (0,1):
                for dx in (0,1):
                    at=base+(y+dy)*stride+(x+dx)*2
                    pixels.append(int.from_bytes(cpu.vram[at:at+2],'little'))
            require(len(set(pixels))==1 and pixels[0] in (0xa000,0xffff),'Malformed replicated glyph pixel')
            bits=(bits<<1)|int(pixels[0]==0xffff)
        rows.append(bits)
    return bytes(rows)


def decode(cpu,image,base,stride):
    _,table,_=symbol_bytes(image,'_fault_glyphs',1)
    require(len(table)==19*7,'Fault glyph table shape changed')
    for cell in range(3):require(glyph(cpu,base,stride,0,cell)==table[(16+cell)*7:(17+cell)*7],
        'Renderer did not draw the complete RET prefix')
    output=[]
    for line in (0,1):
        text=''
        for digit in range(8):
            rows=glyph(cpu,base,stride,line,digit+(4 if not line else 0))
            matches=[i for i in range(16)if rows==table[i*7:i*7+7]]
            require(len(matches)==1,'Displayed digit has no unique hexadecimal glyph')
            text+='0123456789ABCDEF'[matches[0]]
        output.append(text)
    return output


def trace_contract(image):
    marker=image[4].get('__kui_smash_ce_return_trace_revision')
    require(marker in (None,ABS_ONE),'Unsupported CE controller trace revision')
    masked='_runtime_failures' in image[4]
    require(masked or '_runtime_valid' in image[4],'No linked CE integrity validator')
    require(not marker or masked,'Revision1 lost its diagnostic failure masks')
    return int(marker is not None),masked,0xce010000 if marker else 0xce000000


def case(build,image,helper,view,profile,run,kind,interlace):
    revision,masked,family=trace_contract(image);detail=MASKS[kind] if masked else 0
    if kind=='bad_raw' and '_kui_smash_ce_return_raw_prefix' in image[4]:detail|=0x400
    original=controller.CECPU;controller.CECPU=FaultChainCPU
    try:cpu=controller.fixture(image,helper,view,profile,kind)
    finally:controller.CECPU=original
    cpu.bind(image,run,controller.TARGET,0)
    base=0x120000;stride=2560 if interlace else 1280
    cpu.hardware.update({0xa05f8044:0x800005,0xa05f8050:base,0xa05f8054:base+1280,
        0xa05f80d0:0x150 if interlace else 0x100,
        0xa05f805c:319|((239 if interlace else 479)<<10)|((321 if interlace else 1)<<20)})
    stopped=image[4]['_stopped'][0];display=image[4]['_kui_smash_return_fault_display'][0]
    seen=[];after_display=None
    for _ in range(5000000):
        if cpu.pc==stopped:
            require(cpu.r[4:6]==[4,detail],'Integrity failure changed its stopped() reason/detail')
            seen.append(dict(boundary='stopped',reason=hex(cpu.r[4]),detail=hex(cpu.r[5])))
        if cpu.pc==display:
            require(cpu.r[4:6]==[family|4,detail],'stopped() changed the mask passed to the actual renderer')
            seen.append(dict(boundary='renderer',reason=hex(cpu.r[4]),detail=hex(cpu.r[5])))
            after_display=cpu.pr
        if after_display is not None and cpu.pc==after_display:break
        cpu.step()
    else:raise AssertionError('Actual failure-to-renderer chain exceeded its instruction bound')
    require(len(seen)==2 and cpu.pixel_writes and not cpu.raw_calls and cpu.freeze_at is None,
        'Failure path omitted rendering or began retirement/media reads')
    require(not cpu.hardware_writes and not cpu.diagnostic_writes,'Failure chain changed PVR controls or old hardware ownership')
    fields=[base,base+1280]if interlace else[base]
    decoded=[decode(cpu,image,first,stride)for first in fields]
    require(all(text==[f'{family|4:08X}',f'{detail:08X}']for text in decoded),
        'Complete rendered reason/detail differs from the originating failure')
    return dict(case=kind,profile=profile,trace_revision=revision,integrity_mask=masked,
        interlaced=interlace,call_arguments=seen,decoded_fields=decoded,all16_hex_digits_checked=True,
        all_glyph_pixels_and_replication_checked=True,actual_instructions=cpu.instructions,
        fast_actual_CRC_instructions=cpu.fast_instructions,pixel_writes=cpu.pixel_writes,
        no_retirement_or_raw_reads=True)


def build_cases(build,profile,run,expected=None,full=True):
    build=Path(build);paths=[build/'ce-return.elf',build/'helper/helper.elf',build/'helper/helper.bin',build/'resident-sci.elf']
    before={str(path):sha(path.read_bytes())for path in paths}
    image,helper=read_elf(paths[0]),read_elf(paths[1]);revision,masked,family=trace_contract(image)
    if expected=='masked':require(masked and not revision,'Masked historical reference changed its contract')
    if expected=='zero':require(not masked and not revision,'Legacy-zero reference changed its contract')
    view=view_audit(paths[1],paths[3],paths[2]);view['old_e0']=read_elf(paths[3])[4]['_kui_retail_menu_hook'][0]
    kinds=[(kind,False)for kind in MASKS]if full and masked else[('bad_raw',False)]
    if full and masked:kinds.append(('bad_raw',True))
    cases=[case(build,image,helper,view,profile,run,kind,interlace)for kind,interlace in kinds]
    require(before=={str(path):sha(path.read_bytes())for path in paths},'Linked failure-chain inputs changed during replay')
    return dict(build=str(build),inputs=before,trace_revision=revision,reason_family=hex(family),
        integrity_mask=masked,zero_detail_from_reason4_possible=not masked,cases=cases)


def delivery_binding(build,archive,profile):
    role='vc2'if profile==1 else'swirl';build=Path(build)
    code=(build/'ce-return.bin').read_bytes();image=read_elf(build/'ce-return.elf')
    with zipfile.ZipFile(archive)as z:
        module=z.read('KUI/apps/games/retail-smash-'+role+'.kui')
        transport=z.read('KUI/apps/games/smash-'+role+'-vmu-helper.bin')
    require(module==(build/('retail-smash-'+role+'-child.kui')).read_bytes() and
        transport==(build/'helper/prepared.bin').read_bytes(),'ZIP module/helper differs from the supplied actual build')
    header=struct.unpack_from('<8I',transport,41152)
    require(header[:5]==(0x5349554b,0x31525443,1,32,41152) and header[5]==len(code) and
        header[6]==image[4]['_kui_smash_ce_return_runtime'][0]-0xac008300 and header[7]==632 and
        transport[41184:41184+len(code)]==code,'ZIP does not contain the exact linked controller/runtime slot')
    return dict(zip_sha256=sha(Path(archive).read_bytes()),module_sha256=sha(module),transport_sha256=sha(transport),
        controller_sha256=sha(code),controller_bytes=len(code),runtime_offset=header[6],
        module_and_transport_byte_exact=True)


def audit(build,profile,masked_reference=None,zero_reference=None,archive=None):
    sources=('tests/test_smash_ce_return_fault_chain.py','tests/test_smash_ce_return_loader.py',
        'tests/test_smash_card_receipt_transport.py','tests/test_smash_native_gin_linked.py',
        'tests/sh4_integer_fast.c','src/loader/smash_ce_return_loader.c','src/loader/smash_return_fault_display.c')
    before={source:sha((ROOT/source).read_bytes())for source in sources}
    with tempfile.TemporaryDirectory(prefix='kui-ce-fault-chain-')as folder:
        library=Path(folder)/'integer.so'
        subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O2','-shared','-fPIC',
            str(ROOT/'tests/sh4_integer_fast.c'),'-o',str(library)],check=True,capture_output=True)
        lib=ctypes.CDLL(str(library));run=lib.sh4_fast_run
        run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
        current=build_cases(build,profile,run)
        references=[]
        if masked_reference:references.append(build_cases(masked_reference,profile,run,'masked',False))
        if zero_reference:references.append(build_cases(zero_reference,profile,run,'zero',False))
    require(before=={source:sha((ROOT/source).read_bytes())for source in sources},'Failure-chain proof source changed during replay')
    return dict(schema='kui.smash-ce.return-fault-chain.v1',status='PASS',profile=profile,
        test_sha256=before['tests/test_smash_ce_return_fault_chain.py'],source_sha256=before,
        current=current,references=references,delivery=delivery_binding(build,archive,profile)if archive else None,
        limits=['Actual linked failure decision, CRC bodies, stopped arguments and complete RGB565 renderer execute; no validator/CRC/renderer substitution.',
            'PVR register values and coherent VRAM are fixtures; no physical scanout/cache/timing or installed-SD identity is proved.',
            'Historical reference outcomes describe only their exact linked bytes and do not establish which controller ran on the console.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--profile',type=int,choices=(1,2),required=True)
    parser.add_argument('--masked-reference',type=Path,help='Optional older mask-enabled, revision0 role build')
    parser.add_argument('--zero-reference',type=Path,help='Optional legacy boolean-validator role build')
    parser.add_argument('--zip',dest='archive',type=Path,help='Optionally bind exact packaged module/helper/controller bytes')
    parser.add_argument('--report',type=Path);args=parser.parse_args()
    result=audit(args.build,args.profile,args.masked_reference,args.zero_reference,args.archive)
    rendered=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(rendered)
    print(rendered,end='')
if __name__=='__main__':main()
