#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked CE RAM-clear leaf and bind the original clear intervals.

The small native C interpreter executes every linked integer store, including
both boundary canaries. It models only this closed SH instruction subset; it
does not model caches, bus timing, or a Dreamcast session. Original retail
bytes remain local inputs and are reported only as bounded metadata/hashes.
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
sys.path.insert(0, str(ROOT / 'tools'))
from check_vmu_native_worker_gateway import read_elf

SYMBOL = '_kui_smash_ce_return_ram_clear'
BEGIN, END = 0xac010000, 0xad000000
SAMPLE_OFFSET = 0x2509c
MAIGO_SHA = '55d89bfb608806138f5c675ba2187a99a0b4e2f11f7f65048c3f406b8993826a'
SAMPLE_SHA = '82bb1160814548f4656b273353fd6168a691fb5f215b77377860ff9fd2156c8a'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def image_bytes(image, address, count):
    matches = [p for p in image[2] if p[0] == 1 and
               p[2] <= address < address + count <= p[2] + p[4]]
    require(len(matches) == 1, 'RAM-clear bytes are not in one initialized segment')
    p = matches[0]
    return image[0][p[1] + address - p[2]:p[1] + address - p[2] + count]


def decode_leaf(blob, address):
    require(len(blob) >= 32, 'Truncated linked RAM-clear leaf')
    words = struct.unpack_from('<16H', blob)
    require(words[0] & 0xff00 == 0xd000 and words[1] & 0xff00 == 0xd100,
            'RAM-clear entry no longer loads its two bounds into R0/R1')
    require(words[2:] == (0xe200, 0x2022, 0x1021, 0x1022, 0x1023, 0x1024,
                         0x1025, 0x1026, 0x1027, 0x7020, 0x3012, 0x8bf4, 0x000b, 0x0009),
            'RAM-clear closed store/add/compare/branch/return body changed')
    literal_addresses = [((address + pc + 4) & ~3) + (words[pc // 2] & 255) * 4
                         for pc in (0, 2)]
    require(all(address + 32 <= at <= address + len(blob) - 4 for at in literal_addresses),
            'RAM-clear bound literal leaves the initialized leaf window')
    bounds = tuple(struct.unpack_from('<I', blob, at - address)[0]
                   for at in literal_addresses)
    require(bounds == (BEGIN, END), 'RAM-clear linked begin/end differ from the CE contract')
    return literal_addresses, bounds


# Every instruction in the admitted loop executes here in native code. The
# array includes the entire physical main RAM plus a word beyond its high end.
# Unsupported instructions, any MMIO/store outside the interval, and an
# unbounded instruction stream fail closed. Special/FPU state is seeded too.
NATIVE = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
struct result { uint32_t error, steps, stores, first, last, final_r0, final_r1;
    uint32_t low_ok, high_ok, cleared, preserved, upper_dirty_before;
    uint32_t sr_before, sr_after, mmio_reads, mmio_writes; };
struct cpu { uint32_t r[16], pr, sr, gbr, vbr, mach, macl, fpul, fpscr;
    uint32_t fr[32]; };
static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p+2) << 16); }
int execute(const uint8_t *code, uint32_t size, uint32_t base,
            uint32_t seed, struct result *out) {
    enum { RAM_BYTES=0x1000000, CLEAR_BEGIN=0x10000, SAMPLE_END=0xe00000 };
    uint8_t *ram=malloc(RAM_BYTES+4u);
    if(!ram)return -1;
    memset(out,0,sizeof(*out));memset(ram,0xa5,RAM_BYTES+4u);
    /* Original SAMPLE leaves the upper2MiB intact. Preserve a dirty witness
       there, then execute the actual replacement leaf over that same RAM. */
    if(seed)memset(ram+CLEAR_BEGIN,0,SAMPLE_END-CLEAR_BEGIN);
    out->upper_dirty_before=ram[0xfd0000]==0xa5 && ram[RAM_BYTES-1]==0xa5;
    struct cpu c;
    uint32_t *all=(uint32_t *)&c;
    for(unsigned i=0;i<sizeof(c)/4u;++i)all[i]=0x81234567u+i*0x1020304u+seed;
    c.pr=0xac00a5a0u;c.sr=0x500080f0u|(seed&1u);
    struct cpu before=c;
    uint32_t pc=base;
    for(unsigned limit=0;limit<0x1000100u;++limit) {
        if(pc==c.pr)break;
        if(pc<base || pc+2u>base+size) {out->error=1;break;}
        uint16_t op=u16(code+pc-base);uint32_t next=pc+2u;
        ++out->steps;
        if((op&0xf000u)==0xd000u) {
            uint32_t at=((pc+4u)&~3u)+(op&255u)*4u;
            if(at<base || at+4u>base+size) {out->error=2;break;}
            c.r[(op>>8)&15]=u32(code+at-base);
        } else if(op==0xe200u)c.r[2]=0;
        else if(op==0x2022u || (op>=0x1021u && op<=0x1027u)) {
            uint32_t a=c.r[0]+(op==0x2022u?0u:(op&15u)*4u);
            if(a<0xac010000u || a>=0xad000000u || (a&3u)) {out->error=3;break;}
            uint32_t at=(a&0x1fffffffu)-0x0c000000u;
            if(!out->stores)out->first=a;
            out->last=a;++out->stores;
            memcpy(ram+at,&c.r[2],4u);
        } else if(op==0x7020u)c.r[0]+=32u;
        else if(op==0x3012u)c.sr=(c.sr&~1u)|(c.r[0]>=c.r[1]);
        else if((op&0xff00u)==0x8b00u) {
            if(!(c.sr&1u))next=pc+4u+(int8_t)(op&255u)*2;
        } else if(op==0x000bu) {
            if(pc+4u>base+size || u16(code+pc+2u-base)!=0x0009u) {out->error=4;break;}
            ++out->steps;next=c.pr;
        } else {out->error=5;break;}
        pc=next;
    }
    if(pc!=c.pr && !out->error)out->error=6;
    out->final_r0=c.r[0];out->final_r1=c.r[1];
    out->sr_before=before.sr;out->sr_after=c.sr;
    out->low_ok=out->high_ok=out->cleared=1;
    for(unsigned i=0;i<CLEAR_BEGIN;++i)if(ram[i]!=0xa5)out->low_ok=0;
    for(unsigned i=RAM_BYTES;i<RAM_BYTES+4u;++i)if(ram[i]!=0xa5)out->high_ok=0;
    for(unsigned i=CLEAR_BEGIN;i<RAM_BYTES;++i)if(ram[i])out->cleared=0;
    out->preserved=!memcmp(c.r+3,before.r+3,13u*4u) && c.pr==before.pr &&
        (c.sr&~1u)==(before.sr&~1u) &&
        !memcmp(&c.gbr,&before.gbr,sizeof(c)-((uint8_t *)&c.gbr-(uint8_t *)&c));
    free(ram);return 0;
}
'''


class Result(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in
                ('error', 'steps', 'stores', 'first', 'last', 'final_r0', 'final_r1',
                 'low_ok', 'high_ok', 'cleared', 'preserved', 'upper_dirty_before',
                 'sr_before', 'sr_after', 'mmio_reads', 'mmio_writes')]


def audit_leaf(image, compiler='cc'):
    require(SYMBOL in image[4], 'Missing CE-only RAM-clear leaf symbol')
    require(image[4].get('__kui_smash_ce_return_ram_clear') == (1, 0, 0, 0xfff1),
            'Missing exact CE-only RAM-clear ABI marker')
    address = image[4][SYMBOL][0]
    require(0xac008300 <= address < 0xac00c000, 'RAM-clear leaf is not in the private P2 controller')
    # The actual body is32B; bounds are in its immediately following literal
    # pool. No symbol-size or disassembler interpretation is required.
    blob = image_bytes(image, address, 40)
    literals, bounds = decode_leaf(blob, address)
    rows = []
    with tempfile.TemporaryDirectory(prefix='kui-ce-ram-clear-') as tmp:
        library = Path(tmp) / 'model.so'
        built = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                                '-shared', '-fPIC', '-x', 'c', '-', '-o', str(library)],
                               input=NATIVE, text=True, capture_output=True)
        require(built.returncode == 0, built.stderr)
        model = ctypes.CDLL(str(library)).execute
        model.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32, ctypes.c_uint32,
                          ctypes.c_uint32, ctypes.POINTER(Result)]
        model.restype = ctypes.c_int
        code = (ctypes.c_uint8 * len(blob)).from_buffer_copy(blob)
        for seed in (0, 1):
            result = Result()
            require(model(code, len(blob), address, seed, ctypes.byref(result)) == 0,
                    'Native leaf executor failed')
            row = {name: getattr(result, name) for name, _ in Result._fields_}
            require(not row['error'] and all(row[k] for k in
                    ('low_ok', 'high_ok', 'cleared', 'preserved', 'upper_dirty_before')),
                    'Linked RAM-clear execution violated state/boundary proof: '+repr(row))
            require(row['stores'] == (END-BEGIN)//4 and row['first'] == BEGIN and
                    row['last'] == END-4 and row['final_r0'] == END and row['final_r1'] == END,
                    'RAM-clear loop did not visit every aligned word exactly once')
            require(row['steps'] == 11*((END-BEGIN)//32)+5,
                    'RAM-clear instruction bound changed')
            require(not row['mmio_reads'] and not row['mmio_writes'], 'RAM-clear accessed MMIO')
            rows.append(row)
    # Deliberately altered bounds/body must fail admission before any bulk
    # effect can be substituted by a containing controller proof.
    for label, offset in [('begin_literal', literals[0]-address),
                          ('end_literal', literals[1]-address), ('store_opcode', 6)]:
        changed = bytearray(blob);changed[offset] ^= 4
        try:
            decode_leaf(changed, address)
        except AssertionError:
            pass
        else:
            raise AssertionError('RAM-clear admission accepted mutation: '+label)
    leaf_bytes=max(literals)+4-address
    return dict(status='PASS', symbol=SYMBOL, address=hex(address),
                elf_sha256=sha(image[0]), leaf_bytes=leaf_bytes,
                leaf_sha256=sha(blob[:leaf_bytes]),
                instruction_bytes=32, instruction_sha256=sha(blob[:32]),
                literal_addresses=[hex(v) for v in literals],
                begin=hex(bounds[0]), end_exclusive=hex(bounds[1]), bytes=END-BEGIN,
                stores=(END-BEGIN)//4, native_executions=rows,
                mutation_refusals=['begin_literal','end_literal','store_opcode'],
                preserved=['R3..R15','PR','SR exceptT','GBR','VBR','MACH','MACL','FPUL','FPSCR',
                           'both FPU register banks','low64KiB RAM','word after16MiB RAM'],
                limits=['Closed native-C SH subset executes all stores; no cache or bus timing model.',
                        'Containing controller must separately prove the leaf runs after final old-owner use and before capsule loading.'])


def source_window(data, offset, count):
    block = data[offset:offset+count]
    require(len(block) == count, 'Original clear source window truncated')
    return dict(offset=hex(offset), bytes=count, sha256=sha(block),
                crc32=hex(zlib.crc32(block)&0xffffffff))


def audit_originals(maigo, sample):
    require(len(maigo)==14856 and sha(maigo)==MAIGO_SHA, 'Exact original MAIGO identity differs')
    require(len(sample)==204576 and sha(sample)==SAMPLE_SHA, 'Exact original SAMPLE identity differs')
    embedded = sample[SAMPLE_OFFSET:SAMPLE_OFFSET+0x3aa4]
    require(maigo[0x208:0x284] == embedded[0x208:0x284], 'Original restore/call prefix differs')
    rows = []
    for title, data in [('MAIGO return',maigo),('SAMPLE pre-NK',embedded)]:
        word = lambda at: struct.unpack_from('<H',data,at)[0]
        long = lambda at: struct.unpack_from('<I',data,at)[0]
        require([word(at) for at in range(0x3d6,0x3e0,2)] ==
                [0x4110,0x8ffd,0xf00b,0x000b,0x0009], 'Original descending clear loop changed')
        require(word(0x212)==0xd0a8 and long(0x4b4)==0x140000 and word(0x214)==0x406a,
                'Original source no longer selects64-bit FMOV mode')
        require(not any(data[0x430:0x4b0]), 'Original initialized FPU banks are not zero')
        require(word(0x27e)==0xd161 and word(0x280)==0xb0a9 and word(0x282)==0x6023,
                'Original main-RAM clear no longer uses checked count and restored R2')
        require(word(0x278)==0x91c0 and word(0x27a)==0xb0ac and word(0x27c)==0x60f3,
                'Original first clear no longer uses restored OCRAM stack')
        require(long(0x52c)==0x7e001000, 'Original OCRAM stack changed')
        end, count = long(0x4dc), long(0x404)
        require(end-count*8 == 0x8c010000, 'Original FMOV interval has unexpected lower bound')
        # DT decrements before BF/S; its delayed64-bit predecrement store runs
        # on both branch outcomes. Exactly count stores cover[end-count*8,end).
        rows.append(dict(title=title, begin=hex(end-count*8), end_exclusive=hex(end),
                         iterations=count, store_bytes=8, bytes=count*8,
                         first_descending_store=hex(end-8), last_descending_store='0x8c010000',
                         count_literal=source_window(data,0x404,4),
                         end_literal=source_window(data,0x4dc,4),
                         leaf=source_window(data,0x3d6,10)))
    require(rows[0]['end_exclusive']=='0x8d000000' and
            rows[1]['end_exclusive']=='0x8ce00000', 'Original title/return clear intervals changed')
    return dict(status='PASS', input_sha256={'MAIGO.BIN':MAIGO_SHA,'SAMPLE.BIN':SAMPLE_SHA},
                shared_restore_prefix=source_window(maigo,0x208,0x7c), intervals=rows,
                first_clear=dict(begin='0x7dfff000',end_exclusive='0x7e001000',
                                 bytes=0x400*8,meaning='OCRAM, separate from main RAM'),
                retained_sample_interval=['0x8ce00000','0x8d000000'],
                reproduced_boundary='Native replacement-leaf execution begins after the smaller SAMPLE clear with dirty upper RAM, then proves the entire main-RAM target is zero.',
                limits=['Original interval proof validates exact source identities and the closed loop/restore contract; no retail instruction bytes are emitted.',
                        'A RAM-state mismatch is reproduced. The console white-screen cause remains unmeasured until the focused build is tested.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--maigo',type=Path)
    parser.add_argument('--sample',type=Path)
    parser.add_argument('--report',type=Path)
    parser.add_argument('--cc',default='cc')
    args=parser.parse_args()
    require(bool(args.maigo)==bool(args.sample),'Provide both --maigo and --sample')
    sources=('tests/test_smash_ce_return_ram_clear.py','src/loader/smash_ce_return_ram_clear.S',
             'src/loader/smash_ce_return_loader.c','include/kui/smash_ce_return_loader.h',
             'Makefile.smash-ce-return-probe','tools/check_vmu_native_worker_gateway.py',
             'tools/check_loader_layout.py','tests/test_armada_ce_maple_hook.py')
    source_sha={p:sha((ROOT/p).read_bytes()) for p in sources}
    elf_sha=sha(args.elf.read_bytes())
    original_bytes={name:path.read_bytes() for name,path in
                    [('MAIGO.BIN',args.maigo),('SAMPLE.BIN',args.sample)] if path}
    leaf=audit_leaf(read_elf(args.elf),args.cc)
    require(leaf['elf_sha256']==elf_sha,'ELF changed before leaf execution')
    report=dict(schema='kui.smash.ce.return.ram-clear.v1',status='PASS',
                elf_sha256=elf_sha,leaf_sha256=leaf['leaf_sha256'],
                test_sha256=source_sha['tests/test_smash_ce_return_ram_clear.py'],
                source_sha256=source_sha,
                original_sources={},linked_leaf=leaf)
    if args.maigo:
        report['original_source']=audit_originals(original_bytes['MAIGO.BIN'],original_bytes['SAMPLE.BIN'])
        report['original_sources']={'MAIGO.BIN':MAIGO_SHA,'SAMPLE.BIN':SAMPLE_SHA}
    require(source_sha=={p:sha((ROOT/p).read_bytes()) for p in sources},
            'Execution source changed during RAM-clear proof')
    require(sha(args.elf.read_bytes())==elf_sha,'ELF changed during RAM-clear proof')
    for name,path in [('MAIGO.BIN',args.maigo),('SAMPLE.BIN',args.sample)]:
        if path:
            require(path.read_bytes()==original_bytes[name],'Original input changed during proof: '+name)
    report['inputs_immutable_during_execution']=True
    text=json.dumps(report,indent=2)+'\n'
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(text)
    print(text,end='')


if __name__=='__main__':
    main()
