#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate stage addresses from the actual separate low resident ELF."""
import argparse
from pathlib import Path
import struct
from check_loader_layout import inspect_elf

def generate(path):
    image=inspect_elf(Path(path).read_bytes(),0x8c004000,0x8c007800)
    syms=image["symbols"]
    names={"LOW_CONTROL":"_kui_toy_pilot_boot_control",
           "LOW_HEAP_HOOK":"_kui_toy_pilot_heap_hook",
           "LOW_RETURN_HOOK":"_kui_toy_pilot_return_hook",
           "LOW_READ_RAW":"_kui_toy_pilot_read_raw"}
    values={key:syms[name] for key,name in names.items()}
    for key,name in (("LOW_MANIFEST","manifest"),("LOW_ACTIVE","active"),
                     ("LOW_DATA_PENDING","pending")):
        at=syms[f"_kui_toy_pilot_{name}_pointer"]-0x8c004000
        values[key]=struct.unpack_from("<I",image["payload"],at)[0]
    if any(not 0x8c004000<=value<0x8c007800 for value in values.values()):
        raise ValueError("Toy low exported address outside the resident")
    return "/* Generated from resident-sci.elf; SPDX-License-Identifier: GPL-3.0-only */\n"+"\n".join(
        f"#define KUI_TOY_PILOT_{key} UINT32_C(0x{value:08x})" for key,value in values.items())+"\n"

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("resident");parser.add_argument("output")
    args=parser.parse_args();Path(args.output).write_text(generate(args.resident))
if __name__=="__main__":main()
