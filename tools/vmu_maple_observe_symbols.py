#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the observation stage to typed symbols in its actual low reader."""
import argparse
from pathlib import Path
import struct
from check_loader_layout import EH, SH, SYM, inspect_elf, region
from toy_pilot_symbols import _linked_symbols


def exit_target_cell(data, image):
    """Silver's private vector indirection is exactly one immutable word."""
    name = "_kui_vmu_observe_exit_target"
    target = "_kui_vmu_observe_return_hook"
    typed = _linked_symbols(data, {name, target})
    address, size, kind, section = typed.get(name, (0, 0, 0, None))
    if (size != 4 or kind != 1 or address & 3 or section is None or
            section[1] != 1 or section[2] & 7 != 2 or image["symbols"].get(name) != address or
            not section[3] <= address < address + 4 <= section[3] + section[5] or
            not 0x8C004000 <= address < address + 4 <= 0x8C004000 + len(image["payload"]) or
            region(data, section[4] + address - section[3], 4, "exit target cell") !=
            region(image["payload"], address - 0x8C004000, 4, "linked exit target cell")):
        raise ValueError("Observer exit target must be its exact typed read-only four-byte object")
    destination, count, target_kind, target_section = typed.get(target, (0, 0, 0, None))
    if (target_kind != 2 or not count or count & 1 or destination & 1 or
            target_section is None or target_section[1] != 1 or target_section[2] & 7 != 6 or
            image["symbols"].get(target) != destination or
            not target_section[3] <= destination < destination + count <= target_section[3] + target_section[5] or
            not 0x8C004000 <= destination < destination + count <= 0x8C004000 + len(image["payload"]) or
            struct.unpack_from("<I", image["payload"], address - 0x8C004000)[0] != destination):
        raise ValueError("Observer exit target cell must point to its exact typed low return hook")
    return address, destination


def sd_gateway_span(data, image):
    """Use the isolated linked section, since SH relaxation can stale ST_SIZE."""
    name = "_kui_vmu_observe_sd_lease"
    names = {name, "__vmu_sd_lease_begin", "__vmu_sd_lease_end"}
    typed = _linked_symbols(data, names)
    address, declared, kind, section = typed.get(name, (0, 0, 0, None))
    if (section is None or kind != 2 or not declared or address & 1 or declared & 1 or
            section[1] != 1 or section[2] & 7 != 6 or image["symbols"].get(name) != address or
            not 0x8C004000 <= address < address + declared <= 0x8C004000 + len(image["payload"])):
        raise ValueError("SD lease gateway requires one bounded typed low-code function")
    begin = typed.get("__vmu_sd_lease_begin", (0, 0, 0, None))
    end = typed.get("__vmu_sd_lease_end", (0, 0, 0, None))
    size = section[5]
    if (not size or size & 1 or declared < size or address != section[3] or
            begin != (address, 0, 0, section) or end != (address + size, 0, 0, section) or
            region(data, section[4], size, "SD gateway section") !=
            region(image["payload"], address - 0x8C004000, size, "SD gateway load")):
        raise ValueError("SD lease gateway must own its exact allocated linker section")
    header = EH.unpack_from(data)
    sections = [SH.unpack_from(data, header[6] + index * header[11])
                for index in range(header[12])]
    index = sections.index(section)
    functions = []
    for table in sections:
        if table[1] != 2:
            continue
        for at in range(table[4], table[4] + table[5], table[9]):
            _, value, _, info, _, owner = SYM.unpack_from(data, at)
            if info & 15 == 2 and owner == index:
                functions.append(value)
    if functions != [address]:
        raise ValueError("SD gateway section must contain exactly one typed function")
    return address, size, declared


def generate(path):
    data = Path(path).read_bytes()
    image = inspect_elf(data, 0x8C004000, 0x8C007800)
    names = {
        "KUI_VMU_OBSERVE_LOW_HEAP_HOOK": "_kui_vmu_observe_heap_hook",
        "KUI_VMU_OBSERVE_LOW_RETURN_HOOK": "_kui_vmu_observe_return_hook",
        "KUI_VMU_OBSERVE_LOW_CONTROL": "_kui_vmu_observe_boot_control",
    }
    optional_lease = "_kui_vmu_observe_sd_lease"
    if optional_lease in image["symbols"]:
        names["KUI_VMU_OBSERVE_LOW_SD_LEASE"] = optional_lease
    if "_kui_vmu_observe_exit_target" in image["symbols"]:
        names["KUI_VMU_OBSERVE_LOW_EXIT_TARGET"] = "_kui_vmu_observe_exit_target"
    if "_kui_vmu_native_heap_original" in image["symbols"]:
        names["KUI_VMU_NATIVE_HEAP_ORIGINAL"] = "_kui_vmu_native_heap_original"
    typed = _linked_symbols(data, set(names.values()))
    values = {}
    for macro, name in names.items():
        if macro == "KUI_VMU_OBSERVE_LOW_SD_LEASE":
            values[macro] = sd_gateway_span(data, image)[0]
            continue
        if macro == "KUI_VMU_OBSERVE_LOW_EXIT_TARGET":
            values[macro] = exit_target_cell(data, image)[0]
            continue
        address, size, kind, section = typed.get(name, (0, 0, 0, None))
        if not size or section is None or not section[3] <= address < address + size <= section[3] + section[5]:
            raise ValueError("Missing or unbounded typed observer symbol: " + name)
        if macro.endswith("CONTROL") or macro=="KUI_VMU_NATIVE_HEAP_ORIGINAL":
            expected,reserved=(32,32) if macro=="KUI_VMU_NATIVE_HEAP_ORIGINAL" else (56,64)
            if (size != expected or kind != 1 or address & 31 or section[1] != 8 or
                    section[2] & 7 != 3 or
                    not 0x8C004000 <= address < address + reserved <= 0x8C007800 or
                    address + reserved > section[3] + section[5]):
                raise ValueError("Observer control must own two complete low BSS cache lines")
            # No other runtime symbol may occupy the control's padding.
            # The typed object's ABI is56bytes; its allocation reserves64.
            if any(address + size <= value < address + reserved
                   for value in image["symbols"].values()):
                raise ValueError("Observer control cache-line padding is shared")
        elif (kind != 2 or address & 1 or section[2] & 7 != 6 or
              not 0x8C004000 <= address < address + size <= 0x8C004000 + len(image["payload"])):
            raise ValueError("Observer hook must be a bounded low cached-code function")
        values[macro] = address
    return ("/* Generated from the admitted observation reader ELF. */\n"
            "#ifndef KUI_VMU_MAPLE_OBSERVE_SYMBOLS_H\n#define KUI_VMU_MAPLE_OBSERVE_SYMBOLS_H\n"
            + "".join(f"#define {macro} 0x{value:08x}u\n" for macro, value in values.items())
            + "#endif\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        result = generate(args.elf)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(2, f"Observer symbol admission refused: {error}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)


if __name__ == "__main__":
    main()
