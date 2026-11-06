#!/usr/bin/env python3
"""Locate the authenticated native menu record for the prefix verifier."""
import argparse
from pathlib import Path
from check_vmu_native_worker_gateway import read_elf


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("stage",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    image=read_elf(args.stage)
    address=image[4]["__smash_native_menu_seed"][0]
    offset=0x2000+address-0x8ce00000
    if offset<0x2000 or offset+304>0x100000:
        raise ValueError("native menu seed outside temporary capsule")
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text("/* Generated from the linked native stage. */\n"
        f"#define KUI_SMASH_NATIVE_GIN_MENU_SEED_OFFSET 0x{offset:x}u\n")


if __name__=="__main__":
    main()
