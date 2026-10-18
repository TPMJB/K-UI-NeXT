#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the observation stage to typed symbols in its actual low reader."""
import argparse
from pathlib import Path
from check_loader_layout import inspect_elf
from toy_pilot_symbols import _linked_symbols


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
    typed = _linked_symbols(data, set(names.values()))
    values = {}
    for macro, name in names.items():
        address, size, kind, section = typed.get(name, (0, 0, 0, None))
        if not size or section is None or not section[3] <= address < address + size <= section[3] + section[5]:
            raise ValueError("Missing or unbounded typed observer symbol: " + name)
        if macro.endswith("CONTROL"):
            if (size != 56 or kind != 1 or address & 31 or section[1] != 8 or
                    section[2] & 7 != 3 or
                    not 0x8C004000 <= address < address + 64 <= 0x8C007800 or
                    address + 64 > section[3] + section[5]):
                raise ValueError("Observer control must own two complete low BSS cache lines")
            # No other runtime symbol may occupy the control's padding.
            # The typed object's ABI is56bytes; its allocation reserves64.
            if any(address + size <= value < address + 64
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
