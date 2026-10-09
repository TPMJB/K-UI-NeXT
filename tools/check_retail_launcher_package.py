#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check shipped packages against the launcher's production C layout gate.

This executes the exact source gate on a host, not Dreamcast game code. It
checks one accepted native package and malformed packages with valid outer CRCs.
An optional rejected package covers accidentally installing a launcher as a
game-launch package. Console launch and game behavior still need hardware tests.
"""
import argparse
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

from runtime_package import envelope, verify

ROOT = Path(__file__).resolve().parents[1]


def source_function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


PRELUDE = '''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "kui/retail_loader_layout.h"
#include "kui/retail_image.h"
struct kui_runtime_info {uint32_t payload_bytes,memory_bytes;};
struct kui_runtime_image {void *data;struct kui_runtime_info info;};
'''
MAIN = '''
int main(int argc,char **argv) {
    for(int i=1;i<argc;i++) {
        FILE *f=fopen(argv[i],"rb"); if(!f)return 2;
        if(fseek(f,0,SEEK_END))return 2;
        long n=ftell(f); if(n<64 || fseek(f,0,SEEK_SET))return 2;
        uint8_t *bytes=malloc((size_t)n); if(!bytes)return 2;
        if(fread(bytes,1,(size_t)n,f)!=(size_t)n || fclose(f))return 2;
        struct kui_runtime_image image={.data=bytes+64};
        image.info.payload_bytes=le32(bytes+16);
        image.info.memory_bytes=le32(bytes+28);
        if(image.info.payload_bytes!=(uint32_t)n-64u)return 2;
        bool expected=strstr(argv[i],"reject-")==NULL;
        bool accepted=layout(&image,false);
        if(accepted!=expected) {
            fprintf(stderr,"FAIL production launcher gate: %s\\n",argv[i]);
            free(bytes);return 1;
        }
        printf("PASS production launcher gate: %s %s\\n",
            strrchr(argv[i],'/')+1,accepted?"accepted":"rejected");
        free(bytes);
    }
    return 0;
}
'''


def check(package, rejected=None, cc='cc'):
    info = verify(package)
    source = (ROOT / 'src/apps/games_retail.c').read_text()
    c = PRELUDE + source_function(source, 'static uint32_t le32(') + \
        source_function(source, 'static bool layout(') + MAIN
    with tempfile.TemporaryDirectory(prefix='kui-launch-gate-') as temp:
        folder = Path(temp)
        (folder / 'gate.c').write_text(c)
        subprocess.run(shlex.split(cc) + [
            '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
            '-fno-pie', '-no-pie', '-fsanitize=address,undefined',
            '-I', str(ROOT / 'include'), str(folder / 'gate.c'),
            '-o', str(folder / 'gate')], check=True)
        paths = []

        def add(name, data):
            verify(data)
            path = folder / (name + '.kui')
            path.write_bytes(data)
            paths.append(str(path))

        add('native-video', package)
        payload = bytearray(package[64:])
        header = 0x100
        stage_bytes = struct.unpack_from('<I', payload, header + 28)[0]
        fields = (
            ('stage-length', header + 28, stage_bytes - 4),
            ('stage-alignment', header + 28, stage_bytes + 1),
            ('resident', header + 52, 0x8c004004),
            ('limit', header + 56, 0x8c007804),
            ('flags', header + 60, 1),
            ('stage-address', header + 24, 0x8ce10000),
            ('manifest-size', header + 20, 8192),
        )
        for name, offset, value in fields:
            bad = payload.copy()
            struct.pack_into('<I', bad, offset, value)
            add('reject-' + name, envelope(bad, len(bad), info['build']))
        for name, offset in (('magic', header), ('manifest', 0x1000)):
            bad = payload.copy()
            bad[offset] ^= 1
            add('reject-' + name, envelope(bad, len(bad), info['build']))
        add('reject-memory', envelope(payload, len(payload) + 4, info['build']))
        add('reject-wrong-file', rejected if rejected is not None else
            envelope(bytes(len(payload)), len(payload), info['build']))
        subprocess.run([str(folder / 'gate'), *paths], check=True,
                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--reject', type=Path)
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    info = check(args.package.read_bytes(),
                 args.reject.read_bytes() if args.reject else None, args.cc)
    print(f"PASS shipped native package {info['build']}: "
          f"{info['payload_bytes']} payload bytes; 11 rejection cases")


if __name__ == '__main__':
    main()
