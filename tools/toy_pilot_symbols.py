#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate stage addresses from the actual separate low resident ELF."""
import argparse
from pathlib import Path
import struct
from check_loader_layout import EH, SH, SYM, inspect_elf, region

def _linked_symbols(data,wanted):
    """Read typed sizes/sections only after strict ELF admission."""
    header=EH.unpack_from(data)
    sections=[SH.unpack_from(data,header[6]+index*header[11])
              for index in range(header[12])]
    result={}
    for section in sections:
        if section[1]!=2:
            continue
        strings=sections[section[6]]
        names=region(data,strings[4],strings[5],"admitted symbol names")
        for at in range(section[4],section[4]+section[5],section[9]):
            name,value,size,info,_,index=SYM.unpack_from(data,at)
            if not name:
                continue
            end=names.find(b"\0",name)
            if end<0:
                raise ValueError("Unterminated admitted ELF symbol name")
            label=names[name:end].decode("ascii")
            if label not in wanted:
                continue
            if label in result:
                raise ValueError("Duplicate admitted ELF symbol: "+label)
            result[label]=(value,size,info&15,
                           sections[index] if index<len(sections) else None)
    return result


def _data_probe_symbols(data,image,private_p2):
    if not private_p2:
        raise ValueError("DATA probe requires the private P2 resident")
    typed=_linked_symbols(data,{"_card","_kui_retail_hook_sr",
        "_kui_retail_native_caller","_diagnostic","_image",
        "_read_sectors","_transfer_block"})
    syms=image["symbols"]
    begin=syms.get("__retail_resident_bss_begin",0)
    end=syms.get("__retail_resident_bss_end",0)
    if not 0xac004000<=begin<end<=0xac007800:
        raise ValueError("DATA probe requires exact low private BSS bounds")
    result={}
    # Reviewed SH ABI: storage is 72 bytes, device.sd starts at +4 and is
    # 44 bytes; the stream starts at +48. The generated CARD is the whole
    # storage object, so C users must retain independent offsetof/size asserts.
    # The SCI diagnostic is sixteen uint32_t words; its final three counters
    # are at +52/+56/+60. The image is exactly 544 bytes and 32-byte aligned;
    # its isolated 512-byte block begins at +32. Neither admission depends on
    # untyped names or a masked alias of an arbitrary caller buffer.
    for key,name,size,alignment in (("LOW_PROBE_CARD","_card",72,4),
                          ("LOW_PROBE_SR","_kui_retail_hook_sr",4,4),
                          ("LOW_PROBE_CALLER","_kui_retail_native_caller",8,4),
                          ("LOW_PROBE_DIAGNOSTIC","_diagnostic",64,4),
                          ("LOW_PROBE_IMAGE_BLOCK","_image",544,32)):
        at,actual,kind,section=typed.get(name,(0,0,0,None))
        if (actual!=size or kind!=1 or at%alignment or
                not begin<=at<at+size<=end or section is None or
                section[1]!=8 or section[2]&7!=3 or
                not section[3]<=at<at+size<=section[3]+section[5]):
            raise ValueError("DATA probe lacks exact typed low P2 state: "+name)
        result[key]=at+32 if key=="LOW_PROBE_IMAGE_BLOCK" else at
    for key,name in (("LOW_PROBE_READ","_read_sectors"),
                     ("LOW_PROBE_BLOCK","_transfer_block")):
        at,size,kind,section=typed.get(name,(0,0,0,None))
        if (kind!=2 or not size or size%2 or at%2 or
                not 0x8c004000<=at<at+size<=0x8c004000+len(image["payload"]) or
                section is None or section[2]&7!=6 or
                not section[3]<=at<at+size<=section[3]+section[5]):
            raise ValueError("DATA probe lacks exact cached low callback: "+name)
        result[key]=at
    return result


def generate(path, *, private_p2=False, data_probe=False):
    data=Path(path).read_bytes()
    image=inspect_elf(data,0x8c004000,0x8c007800,
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
    if data_probe:
        values.update(_data_probe_symbols(data,image,private_p2))
        state.update({"LOW_PROBE_CARD","LOW_PROBE_SR","LOW_PROBE_CALLER",
                      "LOW_PROBE_DIAGNOSTIC","LOW_PROBE_IMAGE_BLOCK"})
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
    parser.add_argument("--data-probe",action="store_true")
    args=parser.parse_args();Path(args.output).write_text(
        generate(args.resident,private_p2=args.private_p2,data_probe=args.data_probe))
if __name__=="__main__":main()
