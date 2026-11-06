#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the late VC2 gate to every initialized immutable helper byte.

This packages independently authored helper code only. It neither reads game
files nor prepares a runtime memory lease. An optional collection return
controller travels only in the former padding. Its launch-local runtime is
sealed separately by the current stage; the late gate checks the full live
carrier before copying the controller and restoring zero helper padding.
"""
import argparse
import hashlib
import struct
from pathlib import Path
import zlib

RESERVE_BYTES = 48 * 1024


def prepared(blob, return_code=None, runtime_offset=None):
    if not blob or len(blob) > RESERVE_BYTES or len(blob) & 3:
        raise ValueError("VC2 helper must be a nonempty aligned image within 48 KiB")
    image = bytearray(blob + bytes(RESERVE_BYTES-len(blob)))
    if return_code is not None:
        if len(blob)!=41152 or runtime_offset is None or not return_code or len(return_code)&3 or len(return_code)>7968:
            raise ValueError("CE return transport requires the exact41152-byte immutable helper and bounded code")
        if runtime_offset&3 or runtime_offset+632>len(return_code) or return_code[runtime_offset:runtime_offset+632]!=bytes(632):
            raise ValueError("CE return runtime must be the exact aligned632-byte zero slot")
        image[41152:41184]=struct.pack('<8I',0x5349554b,0x31525443,1,32,41152,len(return_code),runtime_offset,632)
        image[41184:41184+len(return_code)]=return_code
    return bytes(image)


def header(blob, return_code=None, runtime_offset=None, copy_offset=None):
    image = prepared(blob,return_code,runtime_offset)
    return ("/* SPDX-License-Identifier: GPL-3.0-only */\n"
            "/* Generated from the exact initialized helper; do not edit. */\n"
            "#ifndef KUI_VC2_CE_VMU_GENERATED_SYMBOLS_H\n"
            "#define KUI_VC2_CE_VMU_GENERATED_SYMBOLS_H\n"
            f"#define KUI_VC2_VMU_BLOB_BYTES 0x{len(blob):08x}\n"
            f"#define KUI_VC2_VMU_BLOB_CRC32 0x{zlib.crc32(blob):08x}\n"
            f"#define KUI_VC2_VMU_PREPARED_CRC32 0x{zlib.crc32(image):08x}\n"
            f"#define KUI_VC2_VMU_BLOB_SHA256 \"{hashlib.sha256(blob).hexdigest()}\"\n"
            +(f"#define KUI_SMASH_CE_RETURN_CODE_BYTES {len(return_code)}u\n"
              f"#define KUI_SMASH_CE_RETURN_CODE_CRC32 0x{zlib.crc32(return_code):08x}u\n"
              f"#define KUI_SMASH_CE_RETURN_RUNTIME_OFFSET {runtime_offset}u\n"
              f"#define KUI_SMASH_CE_RETURN_RUNTIME_P2 0x{0xac008300+runtime_offset:08x}u\n"
              f"#define KUI_SMASH_CE_RETURN_COPY_HIGH_P2 0x{0xacfce0e0+copy_offset:08x}u\n" if return_code is not None else '')+
            "#endif\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("blob", type=Path)
    parser.add_argument("output_header", type=Path)
    parser.add_argument("--prepared-output", type=Path)
    parser.add_argument("--return-bin", type=Path)
    parser.add_argument("--return-elf", type=Path)
    args = parser.parse_args()
    try:
        blob = args.blob.read_bytes()
        code=None;runtime_offset=None;copy_offset=None
        if bool(args.return_bin)!=bool(args.return_elf):raise ValueError("CE return ELF and binary must be supplied together")
        if args.return_bin:
            from check_vmu_native_worker_gateway import read_elf
            elf=read_elf(args.return_elf);symbols=elf[4]
            begin=symbols['__smash_ce_return_begin'][0];end=symbols['__smash_ce_return_binary_end'][0]
            runtime,n,kind,_=symbols['_kui_smash_ce_return_runtime']
            code=args.return_bin.read_bytes();runtime_offset=runtime-begin
            copy_offset=symbols['__smash_ce_return_copy'][0]-begin
            if begin!=0xac008300 or end-begin!=len(code) or n!=632 or kind!=1:
                raise ValueError("CE controller ELF transport geometry changed")
        text = header(blob,code,runtime_offset,copy_offset)
        image = prepared(blob,code,runtime_offset)
        args.output_header.write_text(text)
        if args.prepared_output:
            args.prepared_output.write_bytes(image)
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
