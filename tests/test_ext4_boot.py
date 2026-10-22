#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real clean ext4 images, runtime corruption and strictly read-only failures."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]

def run(*args):
    subprocess.run([str(x) for x in args], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

def wrap_gpt(clean, path):
    count=clean.stat().st_size//512;first=2048;total=first+count+2048
    entries=bytearray(128*128)
    entries[:16]=bytes.fromhex('af3dc60f838472478e793d69d8477de4')
    entries[16:32]=bytes(range(1,17))
    struct.pack_into('<QQ',entries,32,first,first+count-1)
    def header(current,backup,table):
        h=bytearray(512);h[:8]=b'EFI PART'
        struct.pack_into('<II',h,8,0x10000,92)
        struct.pack_into('<QQQQ',h,24,current,backup,34,total-34)
        h[56:72]=bytes(range(17,33))
        struct.pack_into('<QIII',h,72,table,128,128,zlib.crc32(entries))
        struct.pack_into('<I',h,16,zlib.crc32(h[:92]))
        return h
    mbr=bytearray(512);mbr[446+4]=0xee
    struct.pack_into('<II',mbr,446+8,1,total-1);mbr[510:512]=b'\x55\xaa'
    with path.open('wb') as f:
        f.truncate(total*512);f.write(mbr);f.write(header(1,total-1,2));f.write(entries)
        f.seek(first*512)
        with clean.open('rb') as source:shutil.copyfileobj(source,f)
        f.seek((total-33)*512);f.write(entries);f.write(header(total-1,1,total-33))

def main():
    if not shutil.which('mkfs.ext4'):
        raise SystemExit('mkfs.ext4 is required for ext4 bootstrap fixture tests')
    with tempfile.TemporaryDirectory(prefix='kui-ext4-') as temporary:
        root=Path(temporary);tree=root/'tree';(tree/'KUI').mkdir(parents=True)
        payload=bytes((i*29+7)&255 for i in range(65536))
        header=bytearray(64);header[:8]=b'KUIRUN1\0'
        struct.pack_into('<8I',header,8,1,64,len(payload),0x8c010000,0x8c010000,len(payload),zlib.crc32(payload),0)
        header[40:52]=b'0123456789ab';struct.pack_into('<I',header,60,zlib.crc32(header[:60]))
        runtime=tree/'KUI/runtime.kui';runtime.write_bytes(header+payload)
        def make(name):
            image=root/(name+'.img')
            with image.open('wb') as f:f.truncate(32*1024*1024)
            run('mkfs.ext4','-q','-F','-b','4096','-I','256','-O','none,has_journal,ext_attr,resize_inode,dir_index,filetype,extent,64bit,flex_bg,sparse_super,large_file,huge_file,dir_nlink,extra_isize,metadata_csum','-d',tree,image)
            return image
        clean=make('clean')
        def check(image, expected):
            env=dict(os.environ);env.setdefault('ASAN_OPTIONS','detect_leaks=0')
            subprocess.run([str(ROOT/'build/ext4-boot'),str(image),expected],check=True,env=env)
        check(clean,'ok');check(clean,'cancel');check(clean,'io')
        for name,offset,value in [('dirty',58,0),('recovery',96,4),('feature',96,0x8000)]:
            image=root/(name+'.img');shutil.copyfile(clean,image)
            with image.open('r+b') as f:
                f.seek(1024+offset)
                if offset==58:f.write(struct.pack('<H',value))
                else:
                    old=struct.unpack('<I',f.read(4))[0];f.seek(1024+offset);f.write(struct.pack('<I',old|value))
            check(image,'invalid')
        image=root/'super-checksum.img';shutil.copyfile(clean,image)
        with image.open('r+b') as f:
            f.seek(1024+104);value=f.read(1);f.seek(1024+104);f.write(bytes([value[0]^1]))
        check(image,'invalid')
        # Referenced group/inode/directory metadata checksum failures must
        # reject the load even though the runtime bytes themselves are valid.
        with clean.open('rb') as f:
            f.seek(4096+8);inode_table=struct.unpack('<I',f.read(4))[0]
        root_block=int(subprocess.check_output(['debugfs','-R','blocks /',str(clean)],stderr=subprocess.DEVNULL).split()[0])
        for name,location in [('group',4096+30),('inode',inode_table*4096+256+8),('directory',root_block*4096+4092)]:
            image=root/(name+'.img');shutil.copyfile(clean,image)
            with image.open('r+b') as f:
                f.seek(location);value=f.read(1);f.seek(location);f.write(bytes([value[0]^1]))
            check(image,'metadata')
        runtime.write_bytes(header+payload[:-1]+bytes([payload[-1]^1]));check(make('checksum'),'checksum')
        runtime.unlink();check(make('missing'),'missing')
        plain=root/'plain.img';plain.write_bytes(bytes(4096));check(plain,'notfound')
        # Partition adapter offset: same ext4 volume at LBA2048 in a single Linux MBR partition.
        partitioned=root/'mbr.img'
        with partitioned.open('wb') as f:
            mbr=bytearray(512);mbr[446+4]=0x83
            struct.pack_into('<II',mbr,446+8,2048,clean.stat().st_size//512);mbr[510:512]=b'\x55\xaa'
            f.write(mbr);f.seek(2048*512)
            with clean.open('rb') as source:shutil.copyfileobj(source,f)
        check(partitioned,'ok')
        gpt=root/'gpt.img';wrap_gpt(clean,gpt);check(gpt,'ok')
    print('ext4 bootstrap fixture checks passed')

if __name__=='__main__':main()
