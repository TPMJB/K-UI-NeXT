#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the tiny linked SH PIO adapter up to its single SD writer entry.

Unknown instructions, conditional paths, unexpected stores and other callees
fail closed. This witnesses the native NULL callback and one-sector arguments;
it does not model the SD protocol or the writer itself.
"""
import struct


def check_pio_entry(image, address, size, writer, *, base, cases=16):
    payload = image["payload"]
    if (address & 1 or size & 1 or not size or
            not base <= address < address + size <= base + len(payload)):
        raise ValueError("PIO adapter must be one bounded high-code function")
    for case in range(cases):
        registers = [(0x19283746 * (n + 1) + case * 0x10101) & 0xffffffff for n in range(16)]
        card, lba, data = 0xac800000 + case * 256, 0x123456 + case, 0xac900000 + case * 512
        registers[4:7] = [card, lba, data]
        registers[15] = 0xacfdff00
        stack_top = registers[15]
        memory = {card + 24: 0x8c004123}
        null_stores = 0
        pc = address

        def read(at):
            if at & 3:
                raise ValueError("PIO adapter makes an unaligned word read")
            if base <= at <= base + len(payload) - 4:
                return struct.unpack_from("<I", payload, at - base)[0]
            if at not in memory:
                raise ValueError("PIO adapter reads outside callback/own stack")
            return memory[at]

        def write(at, value):
            nonlocal null_stores
            value &= 0xffffffff
            if at == card + 24:
                if value or null_stores:
                    raise ValueError("PIO adapter must clear its payload callback exactly once")
                null_stores += 1
            elif at & 3 or not stack_top - 128 <= at < stack_top:
                raise ValueError("PIO adapter writes outside callback/own stack")
            memory[at] = value

        def step(at, *, delay=False):
            if at & 1 or not address <= at <= address + size - 2:
                raise ValueError("PIO adapter escapes its typed instruction span")
            op = struct.unpack_from("<H", payload, at - base)[0]
            n, m = op >> 8 & 15, op >> 4 & 15
            target = None
            if op == 0x0009:
                pass
            elif op & 0xf000 == 0xe000:  # MOV #imm,Rn
                value = op & 255
                registers[n] = value if value < 128 else (value - 256) & 0xffffffff
            elif op & 0xf00f == 0x6003:  # MOV Rm,Rn
                registers[n] = registers[m]
            elif op & 0xf000 == 0xd000:  # MOV.L @(disp,PC),Rn
                registers[n] = read(((at + 4) & ~3) + (op & 255) * 4)
            elif op & 0xf000 == 0x5000:  # MOV.L @(disp,Rm),Rn
                registers[n] = read(registers[m] + (op & 15) * 4)
            elif op & 0xf000 == 0x1000:  # MOV.L Rm,@(disp,Rn)
                write(registers[n] + (op & 15) * 4, registers[m])
            elif op & 0xf00f == 0x6002:  # MOV.L @Rm,Rn
                registers[n] = read(registers[m])
            elif op & 0xf00f == 0x2002:  # MOV.L Rm,@Rn
                write(registers[n], registers[m])
            elif op & 0xf00f == 0x2006:  # MOV.L Rm,@-Rn
                registers[n] -= 4
                write(registers[n], registers[m])
            elif op == 0x4f22:  # STS.L PR,@-R15
                registers[15] -= 4
                write(registers[15], 0x81234560)
            elif op & 0xf000 == 0x7000:  # ADD #imm,Rn
                value = op & 255
                registers[n] = (registers[n] + (value if value < 128 else value - 256)) & 0xffffffff
            elif op & 0xf00f == 0x300c:  # ADD Rm,Rn
                registers[n] = (registers[n] + registers[m]) & 0xffffffff
            elif op & 0xf00f == 0x200a:  # XOR Rm,Rn
                registers[n] ^= registers[m]
            elif op & 0xf0ff in (0x400b, 0x402b):  # JSR/JMP @Rn
                target = registers[n]
            elif op & 0xf000 in (0xa000, 0xb000):  # BRA/BSR disp
                displacement = op & 4095
                target = at + 4 + (displacement if displacement < 2048 else displacement - 4096) * 2
            else:
                raise ValueError(f"Unsupported native PIO adapter instruction: {op:04x}")
            if delay and target is not None:
                raise ValueError("Control transfer in PIO adapter delay slot")
            return target

        for _ in range(128):
            target = step(pc)
            if target is None:
                pc += 2
                continue
            step(pc + 2, delay=True)
            if target == writer:
                if (null_stores != 1 or memory[card + 24] != 0 or
                        registers[4:8] != [card, lba, 1, data]):
                    raise ValueError("Native writer entry lacks NULL callback/exact one-sector arguments")
                break
            if not address <= target < address + size:
                raise ValueError("PIO adapter transfers to an unexpected callee")
            pc = target
        else:
            raise ValueError("PIO adapter never reaches its admitted SD writer")
    return {"native_cases": cases, "transfer_block_offset": 24,
            "callback_null_at_writer_entry": True, "write_blocks": 1,
            "adapter": f"0x{address:08x}", "writer": f"0x{writer:08x}"}
