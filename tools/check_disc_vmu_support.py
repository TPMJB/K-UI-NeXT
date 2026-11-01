#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the actual independently linked support header and terminal gateway."""
import argparse
from pathlib import Path
import struct

from vmu_native_relocations import EH, SH, SYM, region, generate


def audit(data, binary):
    # Validate complete ELF/flat bytes/relocations before using section data.
    reloc = generate(data, binary, 0x8c112000, 0x2000)
    h = EH.unpack(region(data, 0, EH.size))
    sections = [SH.unpack(region(data, h[6]+i*SH.size, SH.size)) for i in range(h[12])]
    values = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = region(data, strings[4], strings[5])
        for at in range(section[4], section[4]+section[5], SYM.size):
            name, address, _, _, _, owner = SYM.unpack(region(data, at, SYM.size))
            if owner and name:
                name = names[name:].split(b"\0", 1)[0].decode("ascii")
                if name.startswith(("_kui_disc_vmu", "__disc_vmu")):
                    if name in values and values[name] != address:
                        raise ValueError("Conflicting support symbol values")
                    values[name] = address

    def symbol(name):
        if name not in values:
            raise ValueError(f"Missing support symbol {name}")
        return values[name]

    def read(address, size):
        found = [s for s in sections if s[1] == 1 and s[2] & 2 and
                 s[3] <= address and address + size <= s[3] + s[5]]
        if len(found) != 1:
            raise ValueError(f"Support instruction/literal outside initialized ELF at {address:08x}")
        s = found[0]
        return region(data, s[4]+address-s[3], size)

    header = struct.unpack("<16I", read(0xac112000, 64))
    expected = [0x44565331, 1, 64,
                symbol("_kui_disc_vmu_support_bind"),
                symbol("_kui_disc_vmu_support_sd_lease"),
                symbol("_kui_disc_vmu_support_terminal"),
                symbol("__disc_vmu_support_bss_begin"),
                symbol("__disc_vmu_support_bss_end"), 0xac114000] + [0] * 7
    if list(header) != expected:
        raise ValueError("Support exports disagree with exact linked ABI")
    if not (0xac112040 <= header[6] <= header[7] <= 0xac113fe0 and
            not (header[6] | header[7]) & 31 and
            all(0x8c112040 <= address < header[6]-0x20000000 and not address & 1
                for address in header[3:6])):
        raise ValueError("Support exports violate P1-code/P2-state reservation")
    pc = header[5]
    branches = []

    def instruction(expected, what):
        nonlocal pc
        actual = struct.unpack("<H", read(pc, 2))[0]
        if actual != expected:
            raise ValueError(f"{what} at {pc:08x}: {actual:04x}, expected {expected:04x}")
        pc += 2

    def literal(register, expected, what):
        nonlocal pc
        opcode = struct.unpack("<H", read(pc, 2))[0]
        if opcode & 0xff00 != 0xd000 | register << 8:
            raise ValueError(f"{what} requires PC-relative MOV.L into r{register}")
        address = ((pc + 4) & ~3) + (opcode & 255) * 4
        actual = struct.unpack("<I", read(address, 4))[0]
        if actual != expected:
            raise ValueError(f"{what}: {actual:08x}, expected {expected:08x}")
        pc += 2

    def refusal(kind=0x8b00):
        nonlocal pc
        opcode = struct.unpack("<H", read(pc, 2))[0]
        if opcode & 0xff00 != kind:
            raise ValueError("Terminal validation lost its conditional refusal")
        displacement = opcode & 255
        if displacement & 128:
            displacement -= 256
        branches.append(pc + 4 + 2 * displacement)
        pc += 2

    instruction(0x0002, "capture SR without owner stack")
    literal(1, 0x100000f0, "BL/IMASK15")
    instruction(0x201b, "mask SR bits")
    instruction(0x400e, "hold masked SR")
    literal(1, symbol("_kui_disc_vmu_support_state"), "P2 support state")
    instruction(0x6012, "read readiness")
    literal(2, 0x44565342, "bound-state marker")
    instruction(0x3020, "require bound state"); refusal()
    instruction(0x5311, "read pinned worker terminal")
    instruction(0x5412, "read pinned worker stack")
    literal(1, 0xac112000, "relocated support self")
    instruction(0x3410, "require stack at worker/support boundary"); refusal()
    literal(2, 0x12000, "worker span")
    instruction(0x3128, "derive worker header at runtime")
    instruction(0x6012, "read worker magic")
    literal(2, 0x4b4e5031, "native worker magic")
    instruction(0x3020, "check worker magic"); refusal()
    instruction(0x5011, "read worker version")
    instruction(0x8804, "check worker version4"); refusal()
    instruction(0x5012, "read worker export bytes")
    instruction(0x8850, "check worker80B exports"); refusal()
    instruction(0x5016, "read worker terminal export")
    instruction(0x3030, "require exact pinned terminal"); refusal()
    instruction(0x501a, "read worker stack export")
    instruction(0x3040, "require exact pinned stack"); refusal()
    instruction(0x501b, "read worker endpoint")
    instruction(0x3040, "require complete worker span"); refusal()
    literal(2, 0x20000000, "P2-to-P1 alias difference")
    instruction(0x3128, "derive P1 worker base")
    instruction(0x7150, "exclude worker exports from code")
    instruction(0x3312, "bound terminal below"); refusal()
    instruction(0x6143, "copy stack endpoint")
    instruction(0x3128, "derive P1 stack endpoint")
    literal(0, 4096, "worker stack bytes")
    instruction(0x3108, "derive worker stack bottom")
    instruction(0x3312, "exclude terminal from stack"); refusal(0x8900)
    instruction(0x6033, "copy terminal pointer")
    instruction(0xc801, "require aligned terminal"); refusal()
    instruction(0x6f43, "select proved private stack")
    instruction(0x3f28, "use existing P1 stack alias")
    instruction(0x432b, "jump to proved worker terminal")
    instruction(0x0009, "terminal jump delay slot")
    if any(address != pc for address in branches):
        raise ValueError("Terminal validation branch escapes its local stop loop")
    instruction(0xaffe, "failure stays in support stop loop")
    instruction(0x0009, "stop-loop delay slot")
    return {"binary_bytes": len(binary), "memory_bytes": 0x2000,
            "relocations": (len(reloc)-72)//8, "terminal": header[5]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    try:
        result = audit(args.elf.read_bytes(), args.binary.read_bytes())
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, f"Physical-disc support audit failed: {error}\n")
    print(f"PASS physical-disc support: {result['binary_bytes']} initialized bytes, "
          f"8KiB owned tail, {result['relocations']} internal pointers; "
          "P1 code/P2 state, exact exports and terminal uses no owner stack")


if __name__ == "__main__":
    main()
