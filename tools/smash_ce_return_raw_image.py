#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate/verify canonical return data against the current linked raw reader."""
import argparse
import hashlib
import json
from pathlib import Path
import zlib
from check_vmu_native_worker_gateway import read_elf,symbol_bytes

PREFIX_BYTES,RAW_BYTES=260,512
ACCEPTED_SHA='7289dc76bd6ea1d3ddd4ae1f6f65aa1dbd1b31bc4e80f2345727bceb01d325c4'
SYMBOL='_kui_smash_ce_return_raw_prefix'

def require(ok,message):
    if not ok:raise ValueError(message)

def accepted(raw,resident):
    image=read_elf(resident);address,prefix,section=symbol_bytes(image,'_kui_vc2_ce_vmu_read_card',2)
    require(address==0xac008100 and len(prefix)==PREFIX_BYTES and section[5]==PREFIX_BYTES,
        'Current raw reader changed address, initialized prefix or section geometry')
    require(len(raw)==RAW_BYTES and raw==prefix+bytes(RAW_BYTES-PREFIX_BYTES),
        'Current raw file differs from the fresh linked reader plus canonical zero padding')
    require(hashlib.sha256(raw).hexdigest()==ACCEPTED_SHA,
        'Current raw reader differs from the explicitly reviewed image; regenerate/review its ABI')
    return prefix

def header(prefix):
    rows=['    '+','.join(f'0x{x:02x}'for x in prefix[i:i+16])+','for i in range(0,len(prefix),16)]
    return ('/* SPDX-License-Identifier: GPL-3.0-only */\n'
        '/* Generated from the CURRENT accepted raw reader; verify against every fresh resident.\n'
        f' * Full512-byte SHA256: {ACCEPTED_SHA} */\n'
        '#ifndef KUI_SMASH_CE_RETURN_RAW_IMAGE_H\n#define KUI_SMASH_CE_RETURN_RAW_IMAGE_H\n'
        '#define KUI_SMASH_CE_RETURN_RAW_PREFIX_BYTES 260u\n'
        '#define KUI_SMASH_CE_RETURN_RAW_ZERO_BYTES 252u\n'
        'const uint8_t kui_smash_ce_return_raw_prefix[KUI_SMASH_CE_RETURN_RAW_PREFIX_BYTES]={\n'+
        '\n'.join(rows)+'\n};\n#endif\n')

def verify(raw,resident,asset,controller):
    prefix=accepted(raw,resident)
    require(Path(asset).read_text()==header(prefix),'Canonical source asset differs from the fresh accepted raw image')
    image=read_elf(controller);address,embedded,section=symbol_bytes(image,SYMBOL,1)
    begin=image[4]['__smash_ce_return_begin'][0];end=image[4]['__smash_ce_return_binary_end'][0]
    require(image[4].get('__kui_smash_ce_return_raw_image')==(1,0,0,0xfff1),
        'Controller lacks the exact canonical raw-image capability')
    require('__smash_ce_return_raw_shadow_begin'not in image[4]and
        '__kui_smash_ce_return_raw_shadow'not in image[4], 'Controller retains an unproved raw-shadow lease')
    require(begin<=address<address+PREFIX_BYTES<=end and section[1]==1 and embedded==prefix,
        'Initialized controller prefix differs from current raw code or escapes its code CRC domain')
    return dict(schema='kui.smash-ce.canonical-raw-image.v1',status='PASS',prefix_bytes=PREFIX_BYTES,
        zero_tail_bytes=RAW_BYTES-PREFIX_BYTES,raw_sha256=hashlib.sha256(raw).hexdigest(),
        raw_crc32=f'{zlib.crc32(raw)&0xffffffff:08x}',fresh_resident_and_controller_byte_exact=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=('generate','verify'))
    p.add_argument('--raw',type=Path,required=True);p.add_argument('--resident',type=Path,required=True)
    p.add_argument('--header',type=Path,required=True);p.add_argument('--controller',type=Path)
    a=p.parse_args()
    try:
        raw=a.raw.read_bytes();prefix=accepted(raw,a.resident)
        if a.command=='generate':a.header.write_text(header(prefix))
        else:
            require(a.controller is not None,'Verification requires the fresh linked controller')
            print(json.dumps(verify(raw,a.resident,a.header,a.controller),indent=2))
    except (ValueError,OSError)as error:p.error(str(error))
