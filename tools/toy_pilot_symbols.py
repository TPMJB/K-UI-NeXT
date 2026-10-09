#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate stage addresses from the actual separate low resident ELF."""
import argparse
from pathlib import Path
import struct
from check_loader_layout import inspect_elf

def generate(path, *, private_p2=False):
    image=inspect_elf(Path(path).read_bytes(),0x8c004000,0x8c007800,
                      private_p2=private_p2)
    syms=image["symbols"]
    names={"LOW_CONTROL":"_kui_toy_pilot_boot_control",
           "LOW_HEAP_HOOK":"_kui_toy_pilot_heap_hook",
           "LOW_RETURN_HOOK":"_kui_toy_pilot_return_hook",
           "LOW_READ_RAW":"_kui_toy_pilot_read_raw"}
    if "_kui_toy_pilot_card_pointer" in syms:
        names.update({"LOW_SCI_ACQUIRE":"_kui_sci_sd_acquire",
                      "LOW_SCI_RELEASE":"_kui_sci_sd_release",
                      "LOW_SCI_HEALTHY":"_kui_sci_sd_healthy"})
    values={key:syms[name] for key,name in names.items()}
    for key,name in (("LOW_MANIFEST","manifest"),("LOW_ACTIVE","active"),
                     ("LOW_DATA_PENDING","pending")):
        at=syms[f"_kui_toy_pilot_{name}_pointer"]-0x8c004000
        values[key]=struct.unpack_from("<I",image["payload"],at)[0]
    if "_kui_toy_pilot_card_pointer" in syms:
        at=syms["_kui_toy_pilot_card_pointer"]-0x8c004000
        values["LOW_SCI_CARD"]=struct.unpack_from("<I",image["payload"],at)[0]
    state={"LOW_CONTROL","LOW_MANIFEST","LOW_ACTIVE","LOW_DATA_PENDING","LOW_SCI_CARD"}
    for key,value in values.items():
        offset=0x20000000 if private_p2 and key in state else 0
        alignment=4 if key in state else 2
        if value % alignment or not 0x8c004000+offset<=value<0x8c007800+offset:
            raise ValueError("Toy low exported address outside its typed resident alias: "+key)
    return "/* Generated from resident-sci.elf; SPDX-License-Identifier: GPL-3.0-only */\n"+"\n".join(
        f"#define KUI_TOY_PILOT_{key} UINT32_C(0x{value:08x})" for key,value in values.items())+"\n"

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("resident");parser.add_argument("output")
    parser.add_argument("--private-p2",action="store_true")
    args=parser.parse_args();Path(args.output).write_text(
        generate(args.resident,private_p2=args.private_p2))
if __name__=="__main__":main()
