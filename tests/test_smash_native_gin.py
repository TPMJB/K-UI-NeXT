#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the native GIN ABI and exact original-menu context admission.

Default fixtures are authored GINS records and command metadata. --originals
also executes the SHA-bound original menu's context-copy span for all ten
selections. Original executable bytes remain external and are never bundled.
No SDK, hardware, filesystem or gameplay execution is claimed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
CATALOG=[('ALTBEAST','2','0','0'),('COLUMNS','0','0','0'),('GOLDAXE','0','0','0'),
         ('PHSTAR2','0','0','0'),('REVSHIN','0','1','0'),('SHFORCE','0','0','1'),
         ('SONIC1','0','1','0'),('STRAGE2','0','1','0'),('VECTORM','4','2','0'),('WRESTWAR','0','0','0')]
OWNERS={'GINXFER.BIN':'fa12196993a7c757a78565963e257cd33443f80b3f226729fdc1cf7da51ed5b3',
        'MDE_US.BIN':'04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61',
        '1ST_READ.BIN':'3a311b189a4870ccbefc4fcd306e2d38e5ae810ae5edd225af6b9aa6d5df5f10'}

def canonical():
    b=bytearray(8192)
    struct.pack_into('<8I',b,0,0x47494e53,1,0x3ac59e76,8192,0,0,4,0)
    for offset,text in [(0x40,b'\\manifest.bin'),(0x80,b'\\ginxfer.bin'),
                        (0x100,b'\\manifest.bin'),(0x300,b'\\manifest')]:
        b[offset:offset+len(text)]=text
    assert zlib.crc32(b)==0x11634ac3
    return bytes(b)

def authored():
    table=bytearray();offsets=[];args=[]
    for folder,mode,skip,sound in CATALOG:
        offsets.append(len(table));rom=(folder+'.SGA').encode()
        tokens=[b'\\MDE_US.BIN',rom,('MODE'+mode).encode(),('SKIP'+skip).encode(),('SOUND'+sound).encode()]
        args.append((b'\0'.join(tokens)+b'\0\0').ljust(256,b'\0'))
        table+=folder.encode().ljust(8,b' ')+b'\0'+tokens[0]+b' '+rom.ljust(13,b' ')+b' '.join(tokens[2:])+b'\0'
    table+=b'VIRTCOP2\0\\V'
    rows=[]
    for (folder,*_),offset,argv in zip(CATALOG,offsets,args):
        b=bytearray(canonical());struct.pack_into('<4I',b,16,0x8ce00000,903,4,1)
        b[0xc0:0x100]=table[offset:offset+64];b[0x100:0x200]=argv
        rows.append((bytes(b),folder.encode().ljust(12,b' ')+b'\1\4\x5a\xa5'))
    return rows

def original_contexts(directory):
    sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
    from test_native_standalone_foreground_gin import Gin,LOAD,GIN_STAGE,GIN_ENTRY
    class MenuCPU(Gin):
        def plain(self,op,pc):
            if op&0xf00f in (0x2004,0x2005):
                n,m=op>>8&15,op>>4&15;size=1 if op&15==4 else 2
                self.r[n]=(self.r[n]-size)&0xffffffff
                self.write(self.r[n],self.r[m],size);self.instructions+=1
            else:super().plain(op,pc)
    data={name:(directory/name).read_bytes()for name in OWNERS}
    assert all(hashlib.sha256(data[name]).hexdigest()==digest for name,digest in OWNERS.items())
    gin,mde,menu=(data[name]for name in ('GINXFER.BIN','MDE_US.BIN','1ST_READ.BIN'))
    assert mde[0x18ee60:0x190e60]==canonical()
    results=[]
    for (folder,*_),expected in zip(CATALOG,authored()):
        c=MenuCPU(gin,mde,b'');c.store_block(LOAD,menu)
        ctx=0x8c036a44;row=menu.index(folder.encode().ljust(8,b' ')+b'\0',0x17f28)
        command=menu[row+9:menu.index(b'\0',row+9)]
        argv=(b'\0'.join(command.split())+b'\0\0').ljust(256,b'\0')
        c.store_block(ctx+256,argv);c.store_block(0x8c0080fe,b'\x5a\xa5')
        c.r[9]=GIN_STAGE;c.r[12]=ctx;c.r[10]=LOAD+row;c.r[15]=0x8c00f000
        c.write(c.r[15],903);c.write(c.r[15]+8,0x8ce00000);c.write(c.r[15]+12,4)
        c.pc=0x8c015e12;c.writes=[]
        for _ in range(100000):
            if c.pc==GIN_ENTRY:break
            c.step()
        else:raise AssertionError('original context-copy span exceeded bound')
        touched=set()
        for _,address,_,width in c.writes:
            if GIN_STAGE<=address and address+width<=GIN_STAGE+8192:
                touched.update(range(address-GIN_STAGE,address+width-GIN_STAGE))
        assert touched==set(range(16,32))|set(range(36,896))
        result=(c.block(GIN_STAGE,8192),c.block(0x8c0080f0,16))
        assert result==expected,(folder,'original menu output differs from authored ABI')
        results.append(result)
    return results

HARNESS=r'''
#include "kui/smash_native_gin.h"
#include "kui/smash_native_menu.h"
#include "kui/retail_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"line %u: %s\n",__LINE__,#c); return 1; } } while(0)
static unsigned char original[8192],live[8192],high[8192],before[8192];
static void seed_blank(struct kui_smash_native_gin_seed *s) {
    memset(s,0,sizeof(*s));s->magic0=KUI_SMASH_NATIVE_GIN_MAGIC0;
    s->magic1=KUI_SMASH_NATIVE_GIN_MAGIC1;s->version=1;s->bytes=160;s->profile=3;
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *f=fopen(argv[1],"rb");CHECK(f);
    CHECK(fread(original,1,sizeof(original),f)==sizeof(original));
    CHECK(sizeof(struct kui_smash_native_gin_transfer)==8628);
    CHECK(offsetof(struct kui_smash_native_gin_transfer,context)==176);
    CHECK(offsetof(struct kui_smash_native_gin_transfer,ip)==8368);
    CHECK(offsetof(struct kui_smash_native_gin_transfer,crc32)==8624);
    struct kui_smash_native_gin_seed seed,fresh;
    seed_blank(&fresh);CHECK(kui_smash_native_gin_blank(&fresh));
    CHECK(!kui_smash_native_gin_valid(&fresh));
    unsigned char prefix[0x200+160+1];memset(prefix,0,sizeof(prefix));memcpy(prefix+0x200,&fresh,160);
    CHECK(kui_smash_native_gin_capable(prefix,sizeof(prefix)));
    CHECK(!kui_smash_native_gin_capable(prefix,sizeof(prefix)-2));
    CHECK(!kui_smash_native_gin_capable(NULL,sizeof(prefix)));
    CHECK(!kui_smash_native_gin_blank(NULL));CHECK(!kui_smash_native_gin_valid(NULL));
    for(unsigned bit=0;bit<160*8;bit++) {
        prefix[0x200+bit/8]^=(unsigned char)(1u<<(bit%8));
        CHECK(!kui_smash_native_gin_capable(prefix,sizeof(prefix)));
        prefix[0x200+bit/8]^=(unsigned char)(1u<<(bit%8));
    }
    uint32_t frame[21]={0x28,0,0x8c000000,0x8c00f400,0x700000f0};
    for(unsigned i=13;i<21;i++)frame[i]=0x87654321u^i;
    const uint32_t ccrs[]={0,0x100,0x101,0x109,0x909,0x129};
    for(unsigned c=0;c<sizeof(ccrs)/sizeof(ccrs[0]);c++) {
        seed=fresh;CHECK(kui_smash_native_gin_capture(&seed,frame,0x8c00f3ac,ccrs[c]));
        CHECK(kui_smash_native_gin_valid(&seed));CHECK(!memcmp(seed.frame,frame,sizeof(frame)));
        CHECK(seed.ccr==ccrs[c]);struct kui_smash_native_gin_seed saved=seed;
        CHECK(!kui_smash_native_gin_capture(&seed,frame,0x8c00f3ac,ccrs[c]));
        CHECK(!memcmp(&seed,&saved,sizeof(seed)));
        for(unsigned bit=0;bit<160*8;bit++) {
            seed=saved;((unsigned char *)&seed)[bit/8]^=(unsigned char)(1u<<(bit%8));
            CHECK(!kui_smash_native_gin_valid(&seed));
        }
    }
    seed=fresh;CHECK(kui_smash_native_gin_capture(&seed,frame,0xac00f3ac,0x109));
    for(unsigned i=0;i<13;i++) {
        frame[i]^=1;seed=fresh;
        CHECK(!kui_smash_native_gin_capture(&seed,frame,0x8c00f3ac,0x109));
        CHECK(!memcmp(&seed,&fresh,sizeof(seed)));frame[i]^=1;
    }
    seed=fresh;CHECK(!kui_smash_native_gin_capture(&seed,frame,0x8c00f3b0,0x109));
    CHECK(!kui_smash_native_gin_capture(&seed,frame,0x8c00f3ac,0x189));
    CHECK(!kui_smash_native_gin_capture(&seed,NULL,0x8c00f3ac,0x109));
    CHECK(!memcmp(&seed,&fresh,sizeof(seed)));
    unsigned checks=0;
    for(unsigned selection=1;selection<=10;selection++) {
        unsigned char ip[16];CHECK(fread(live,1,8192,f)==8192);CHECK(fread(ip,1,16,f)==16);
        memcpy(high,live,8192);memcpy(before,live,8192);
        struct kui_smash_native_menu_seed menu={.magic0=KUI_SMASH_NATIVE_MENU_SEED_MAGIC0,
            .magic1=KUI_SMASH_NATIVE_MENU_SEED_MAGIC1,.version=1,.bytes=304};
        CHECK(kui_smash_native_menu_seed_capture(&menu,live+0x100,ip));CHECK(menu.selection==selection);
        CHECK(kui_smash_native_gin_context_matches(original,live,high,&menu));
        /* Even synchronized corruption of loaded and high contexts must not
         * turn the menu's write footprint into an arbitrary mutation grant. */
        for(unsigned at=0;at<8192;at++) {
            live[at]^=1;high[at]^=1;
            CHECK(!kui_smash_native_gin_context_matches(original,live,high,&menu));
            live[at]^=1;high[at]^=1;++checks;
            high[at]^=1;CHECK(!kui_smash_native_gin_context_matches(original,live,high,&menu));
            high[at]^=1;++checks;
        }
        CHECK(!memcmp(live,before,8192));CHECK(!memcmp(high,before,8192));
        menu.crc32^=1;CHECK(!kui_smash_native_gin_context_matches(original,live,high,&menu));menu.crc32^=1;
        CHECK(kui_smash_native_gin_normalize(original,live));CHECK(!memcmp(original,live,8192));
        memcpy(live,before,8192);original[0]^=1;
        CHECK(!kui_smash_native_gin_normalize(original,live));CHECK(!memcmp(live,before,8192));original[0]^=1;
        CHECK(!kui_smash_native_gin_normalize(live,live));CHECK(!memcmp(live,before,8192));
    }
    CHECK(fgetc(f)==EOF);CHECK(!fclose(f));
    CHECK(!kui_smash_native_gin_context_matches(NULL,live,high,NULL));
    CHECK(!kui_smash_native_gin_normalize(NULL,live));
    printf("PASS native GIN: 10 selections, %u single-byte context corruptions refused; continuation ABI and exact normalization passed\n",checks);
    return 0;
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--originals',type=Path);p.add_argument('--cc',default=os.environ.get('CC','cc'))
    p.add_argument('--report',type=Path);p.add_argument('--no-sanitize',action='store_true')
    args=p.parse_args();rows=original_contexts(args.originals)if args.originals else authored()
    with tempfile.TemporaryDirectory(prefix='smash-native-gin-')as temporary:
        d=Path(temporary);fixtures=d/'fixtures.bin';fixtures.write_bytes(canonical()+b''.join(a+b for a,b in rows))
        harness=d/'test.c';harness.write_text(HARNESS);program=d/'test'
        command=[args.cc,'-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-Wpedantic','-I'+str(ROOT/'include')]
        if not args.no_sanitize:command+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie']
        command += [str(harness),str(ROOT/'src/core/smash_native_gin.c'),str(ROOT/'src/core/smash_native_menu.c'),
                    str(ROOT/'src/core/retail_image.c'),'-o',str(program)]
        subprocess.run(command,check=True)
        result=subprocess.run([str(program),str(fixtures)],check=True,text=True,capture_output=True,
                              env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
    report={'schema':'kui.smash.native-gin-pure.v1','passed':True,'selections':10,
            'single_byte_context_rejections':163840,'original_menu_copy_span_executed':bool(args.originals),
            'context_sha256':[hashlib.sha256(a).hexdigest()for a,_ in rows],
            'limits':['Original GIN RTE/register-bank switching, native SDK, media, gameplay and timing are outside this host test.']}
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(result.stdout,end='');print('Original menu source span executed:',bool(args.originals))
if __name__=='__main__':main()
