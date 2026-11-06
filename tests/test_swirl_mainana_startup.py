#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay original Swirl MAINANA to its final pre-NK FN8 admission point.

The original loader's scalar instructions execute with the production native
GD core. Its CPU mapper, GD firmware gateway and synchronous data arrival are
authored boundaries. No supplied game bytes are embedded or emitted. This is
not a cache, DMA, interrupt, SD timing or console compatibility model.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from test_native_standalone_foreground_gin import Gin
from test_vmu_native_sdk_poll_execution import PollSH
from test_vmu_native_heap_execution import MASK

SAMPLE_SHA = '82bb1160814548f4656b273353fd6168a691fb5f215b77377860ff9fd2156c8a'
KERNEL_SHA = 'a19080f2e243c04e01a3a473c7a3caa36fb85a5a8fbb2163c8f7a1b2c8ed5ea8'
EMBEDDED_OFFSET, EMBEDDED_BYTES, EMBEDDED_ADDRESS = 0x2509c, 0x3aa4, 0x8ce08300
ENTRY, ABORT_PR, FIRMWARE = 0x8ce09348, 0x8ce09362, 0x8c004000
FAD, SECTORS, BODY_BYTES = 493127, 893, 0x1be000

ADAPTER = r'''
#include "kui/retail_gd.h"
#include "kui/vc2_ce_vmu_handoff.h"
#include <stdint.h>
#include <string.h>
static struct kui_retail_gd service;
static uint8_t *ram;
static uint8_t *map(void *unused,uint32_t a,uint32_t n,int access) {
    (void)unused;(void)access;
    const uint32_t physical=a&UINT32_C(0x1fffffff);
    if(physical<UINT32_C(0x0c000000) || physical>=UINT32_C(0x0d000000) ||
       n>UINT32_C(0x0d000000)-physical)return 0;
    return ram+physical-UINT32_C(0x0c000000);
}
static int check(void *unused,uint32_t lba,uint32_t n,uint32_t bytes) {
    (void)unused;
    return lba>=45000u && lba<600000u && n<=600000u-lba && bytes==2048u?0:-1;
}
static int read_payload(void *unused,uint32_t lba,uint32_t n,uint32_t bytes,void *out) {
    (void)unused;(void)lba;(void)n;(void)bytes;(void)out;return -1;
}
int model_init(uint8_t *memory) {
    static const union kui_retail_slot tracks[]={{.track={.start_lba=45000u,.end_lba=600000u,.control=4u}}};
    const struct kui_gd_ops ops={0,map,check,read_payload};
    ram=memory;
    return kui_retail_gd_init(&service,tracks,1u,&ops,UINT32_C(0x8c008000),UINT32_C(0x8d000000));
}
int32_t model_call(uint32_t a,uint32_t b,uint32_t c,uint32_t fn) {
    return kui_retail_gd_dispatch(&service,a,b,c,fn);
}
void model_progress(uint32_t bytes) {kui_retail_gd_progress(&service,bytes/2048u,0u);}
void model_snapshot(uint32_t out[16]) {
    const struct kui_retail_gd *s=&service;
    const uint32_t values[]={s->token,s->command,s->area,s->request_bytes,
        s->completed_bytes,s->outputs[0],s->error,(uint32_t)s->status,
        s->pending,s->count,s->executing,s->initialized,s->destination,
        s->lba,s->sector_bytes,(uint32_t)(uintptr_t)s->ops.context};
    memcpy(out,values,sizeof(values));
}
void model_expected_profile(uint32_t out[7]) {
    const uint32_t values[]={KUI_VC2_VMU_KERNEL_FAD,KUI_VC2_VMU_KERNEL_SECTORS,
        KUI_VC2_VMU_KERNEL_BYTES,KUI_VC2_VMU_BODY_BYTES,KUI_VC2_VMU_BODY_CRC32,
        KUI_VC2_VMU_PREFIX_CRC32,KUI_VC2_VMU_ABORT_PR};
    memcpy(out,values,sizeof(values));
}
'''

def sha(data):
    return hashlib.sha256(data).hexdigest()

class Mainana(Gin):
    def __init__(self, sample, kernel, core):
        PollSH.__init__(self, b'', 2, 1)
        self.gbr = self.mach = self.macl = self.spc = self.ssr = 0
        self.vbr = 0x8c00f400
        self.pc = ENTRY
        self.pr = 0x8ce08636
        self.r[15] = 0x8cfef000
        self.sr = 0x700000f0
        self.kernel, self.core = kernel, core
        self.calls, self.pieces, self.cache_operations = [], [], []
        self.store_block(EMBEDDED_ADDRESS, sample[EMBEDDED_OFFSET:EMBEDDED_OFFSET+EMBEDDED_BYTES])
        self.store_block(0x8ce00000, struct.pack('<4I', 0x414d4159, FAD, SECTORS, 1))
        self.write(0x8c0000bc, FIRMWARE)
        self.memory_reference = (ctypes.c_uint8 * len(self.memory)).from_buffer(self.memory)
        assert core.model_init(self.memory_reference) == 0
        self.writes = []

    def snapshot(self):
        words = (ctypes.c_uint32 * 16)()
        self.core.model_snapshot(words)
        return list(words)

    def plain(self, op, pc):
        if op & 0xf0ff == 0x0093:  # OCBI: recorded, no cache simulated.
            self.cache_operations.append((pc, self.r[op >> 8 & 15]))
            self.instructions += 1
        else:
            super().plain(op, pc)

    def gateway(self):
        a, b, c, function = self.r[4:8]
        before = self.snapshot()
        if function == 8:
            assert self.pr == ABORT_PR
            assert before == [1, 28, FAD, len(self.kernel), BODY_BYTES, 0x800,
                0, 3, 0, 0, 0, 1, 0x0c010000, FAD-150+SECTORS, 2048, 0], before
            assert self.block(0x8ced4000, 2048) == self.kernel[:2048]
            assert self.block(0x0c010000, BODY_BYTES) == self.kernel[2048:]
            self.calls.append(dict(function=8, pr=hex(self.pr), admission_snapshot=before))
            return True
        result = self.core.model_call(a, b, c, function)
        assert result >= 0, (function, hex(self.pr), result)
        row = dict(function=function, pr=hex(self.pr), result=result)
        if function == 0:
            assert a == 28
            params = [self.read(b), self.read(b+4)]
            assert params == [FAD, SECTORS]
            row['request'] = params
        if function == 6:
            state = self.snapshot()
            destination, size = self.read(b), self.read(b+4)
            assert state[8] == 1 and state[9] * 2048 == size
            offset = (state[13] - (FAD-150)) * 2048
            payload = self.kernel[offset:offset+size]
            assert len(payload) == size
            self.store_block(destination, payload)
            self.core.model_progress(size)
            self.pieces.append(dict(destination=hex(destination), bytes=size,
                source_offset=offset, sha256=sha(payload)))
        self.calls.append(row)
        self.r[0], self.pc = result & MASK, self.pr
        return False

def audit(sample, kernel, catalog, compiler='cc'):
    assert len(sample) == 204576 and sha(sample) == SAMPLE_SHA
    assert len(kernel) == 0x1be800 and sha(kernel) == KERNEL_SHA
    assert struct.unpack_from('<5I', kernel, 16) == (1, 0x0c010000, 0x800, BODY_BYTES, 0x0c010000)
    assert zlib.crc32(kernel[:2048]) == 0x7e29bc00
    assert zlib.crc32(kernel[2048:]) == 0x14a27ac2
    rows = [r for r in catalog['entries'] if r['normalized_path'] == 'SWIRL/0WINCEOS.BIN']
    assert len(rows) == 1 and rows[0]['flags'] == 0
    assert rows[0]['lba']+150 == FAD and rows[0]['bytes'] == len(kernel)
    # Original SAMPLE's loader-copy call binds the exact embedded image.
    word = lambda pc: struct.unpack_from('<H', sample, pc-0x8c010000)[0]
    literal = lambda pc: struct.unpack_from('<I', sample, ((pc+4)&~3)+(word(pc)&255)*4-0x8c010000)[0]
    assert literal(0x8c018d60) == EMBEDDED_ADDRESS
    assert literal(0x8c018d66) == 0x8c010000+EMBEDDED_OFFSET
    assert word(0x8c018d64) == 0x9613
    assert struct.unpack_from('<H', sample, 0x8d8e)[0] == EMBEDDED_BYTES
    flags = ['-DKUI_ON_CONSOLE=1','-DKUI_RETAIL_GD_ASYNC=1',
        '-DKUI_NATIVE_OCRAM_BOOTSTRAP=1','-DKUI_RETAIL_NATIVE_DRIVE_STATE=1',
        '-DKUI_NATIVE_GD_LEGACY_STREAM=1','-DKUI_NATIVE_GD_MMU_BUFFERS=1',
        '-DKUI_NATIVE_GD_DMA_STREAM=1','-DKUI_NATIVE_GD_PIO_STREAM=1','-DKUI_NATIVE_GD_VRAM_DMA=1',
        '-DKUI_SMASH_SWIRL_VMU=1']
    with tempfile.TemporaryDirectory(prefix='kui-swirl-mainana-') as tmp:
        library = Path(tmp)/'core.so'
        command = [compiler,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fPIC','-shared','-I',str(ROOT/'include'),*flags,'-x','c','-',
            str(ROOT/'src/core/retail_gd.c'),'-o',str(library)]
        built = subprocess.run(command,input=ADAPTER,text=True,capture_output=True)
        assert built.returncode == 0, built.stderr
        core = ctypes.CDLL(str(library))
        core.model_call.argtypes = [ctypes.c_uint32]*4
        core.model_call.restype = ctypes.c_int32
        core.model_init.argtypes = [ctypes.POINTER(ctypes.c_uint8)]
        core.model_progress.argtypes = [ctypes.c_uint32]
        core.model_snapshot.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
        core.model_expected_profile.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
        expected = (ctypes.c_uint32 * 7)()
        core.model_expected_profile(expected)
        assert list(expected) == [FAD, SECTORS, len(kernel), BODY_BYTES,
            zlib.crc32(kernel[2048:]), zlib.crc32(kernel[:2048]), ABORT_PR]
        cpu = Mainana(sample,kernel,core)
        for _ in range(1000000):
            if cpu.pc == FIRMWARE:
                if cpu.gateway(): break
            else: cpu.step()
        else: raise AssertionError('Original MAINANA did not reach final FN8 within instruction bound')
        assert cpu.pieces[0]['destination'] == '0x8ced4000' and cpu.pieces[0]['bytes'] == 2048
        assert cpu.pieces[1]['destination'] == '0xc010000' and cpu.pieces[1]['bytes'] == BODY_BYTES
        assert len(cpu.pieces) == 2
        return dict(schema='kui.swirl.original-mainana-pre-nk.v1',status='PASS',
            input_sha256={'SAMPLE.BIN':SAMPLE_SHA,'0WINCEOS.BIN':KERNEL_SHA},
            embedded_loader=dict(offset=hex(EMBEDDED_OFFSET),bytes=EMBEDDED_BYTES,
                address=hex(EMBEDDED_ADDRESS),sha256=sha(sample[EMBEDDED_OFFSET:EMBEDDED_OFFSET+EMBEDDED_BYTES])),
            kernel=dict(fad=FAD,sectors=SECTORS,bytes=len(kernel),prefix_crc32='7e29bc00',
                body_bytes=BODY_BYTES,body_crc32='14a27ac2',destination='0x0c010000'),
            gd_calls=cpu.calls,transfer_pieces=cpu.pieces,original_loader_instructions=cpu.instructions,
            final=dict(pc=hex(cpu.pc),pr=hex(cpu.pr),sp=hex(cpu.r[15]),sr=hex(cpu.sr)),
            source_sha256={str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in
                [Path(__file__).resolve(),ROOT/'src/core/retail_gd.c',ROOT/'tests/test_native_standalone_foreground_gin.py',
                 ROOT/'tests/test_vmu_native_sdk_poll_execution.py',ROOT/'tests/test_vmu_native_heap_execution.py',
                 ROOT/'include/kui/vc2_ce_vmu_handoff.h']},
            compiled_swirl_profile_matches_original_tuple=True,
            catalog_matches_original_kernel_extent=True,
            limits=['Original supplied scalar MAINANA instructions and the production GD core execute.',
                'Entry state, CPU RAM mapper, firmware gateway and synchronous exact-file data arrival are authored boundaries.',
                'Stops on the original final FN8 before the helper installer and before any NK instruction.',
                'Cache operations are recorded without a cache model. No SCI/DMA/interrupt/scheduler timing or console compatibility is certified.'])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--originals',type=Path,required=True)
    parser.add_argument('--report',type=Path)
    parser.add_argument('--catalog',type=Path)
    parser.add_argument('--cc',default='cc')
    args=parser.parse_args()
    catalog = args.catalog or args.originals.parent/'directory-catalog.json'
    report=audit((args.originals/'SAMPLE.BIN').read_bytes(),(args.originals/'0WINCEOS.BIN').read_bytes(),
        json.loads(catalog.read_text()),args.cc)
    text=json.dumps(report,indent=2)+'\n'
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(text)
    print(text,end='')

if __name__=='__main__': main()
