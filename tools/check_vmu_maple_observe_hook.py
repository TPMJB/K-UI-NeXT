#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual linked SH4 observer trampoline with a hostile C callee.

This deliberately implements only the trampoline's integer instruction subset.
Unexpected code fails closed. It checks architectural register/stack/SR replay;
it does not model cache, exceptions, pipeline timing or hardware concurrency.
"""
import random
import struct


def check_hook(image, exports, *, cases=128):
    base = 0x8CFD0000
    payload = image["payload"]
    hook = exports["hook"]
    visit = image["symbols"].get("_kui_vmu_maple_observer_visit")
    if not visit:
        raise ValueError("Hook proof lacks the linked observer visit symbol")
    rng = random.Random(0x564D5531)
    for case in range(cases):
        registers = [rng.getrandbits(32) for _ in range(16)]
        registers[0] = hook  # The installed JMP patch consumes dead R0.
        registers[14] = 0x8C1223A4
        registers[15] = 0x8C00EF00
        original = list(registers)
        initial_sr = 0x40000000 | (case % 16) << 4 | (case & 1) | (case & 2) << 8
        state = {"sr": initial_sr, "pr": rng.getrandbits(32),
                 "mach": rng.getrandbits(32), "macl": rng.getrandbits(32),
                 "gbr": rng.getrandbits(32)}
        initial_state = dict(state)
        memory = {0x8C1223A4: 0x8C100000, 0x8C1010F4: 0xA05F6C00}
        stores = []
        visits = 0
        pc = hook

        def read(address):
            if address & 3:
                raise ValueError("Unaligned hook word read")
            if base <= address <= base + len(payload) - 4:
                return struct.unpack_from("<I", payload, address - base)[0]
            if address not in memory:
                raise ValueError(f"Unexpected hook word read: {address:08x}")
            return memory[address]

        def write(address, value):
            if address & 3:
                raise ValueError("Unaligned hook word write")
            if address == 0xA05F6C18:
                stores.append((value, state["sr"]))
            elif not (original[15] - 128 <= address < original[15] or
                      exports["stack_bottom"] <= address < exports["stack_top"]):
                raise ValueError(f"Hook writes outside its two frames: {address:08x}")
            memory[address] = value & 0xFFFFFFFF

        def push(n, value):
            registers[n] = (registers[n] - 4) & 0xFFFFFFFF
            write(registers[n], value)

        def pop(n):
            value = read(registers[n])
            registers[n] = (registers[n] + 4) & 0xFFFFFFFF
            return value

        def instruction(address, delay=False):
            nonlocal visits
            if not base <= address <= base + len(payload) - 2 or address & 1:
                raise ValueError("Hook executes outside admitted code")
            op = struct.unpack_from("<H", payload, address - base)[0]
            n, m = op >> 8 & 15, op >> 4 & 15
            transfer = None
            if op == 0x0009:  # NOP
                pass
            elif op & 0xF00F == 0x2006:  # MOV.L Rm,@-Rn
                push(n, registers[m])
            elif op & 0xF00F == 0x6006:  # MOV.L @Rm+,Rn
                value = pop(m)
                registers[n] = value
            elif op & 0xF00F == 0x6002:  # MOV.L @Rm,Rn
                registers[n] = read(registers[m])
            elif op & 0xF00F == 0x6003:  # MOV Rm,Rn
                registers[n] = registers[m]
            elif op & 0xF000 == 0xD000:  # MOV.L @(disp,PC),Rn
                registers[n] = read(((address + 4) & ~3) + (op & 255) * 4)
            elif op & 0xF000 == 0x5000:  # MOV.L @(disp,Rm),Rn
                registers[n] = read((registers[m] + (op & 15) * 4) & 0xFFFFFFFF)
            elif op & 0xF000 == 0x1000:  # MOV.L Rm,@(disp,Rn)
                write((registers[n] + (op & 15) * 4) & 0xFFFFFFFF, registers[m])
            elif op & 0xF00F == 0x000E:  # MOV.L @(R0,Rm),Rn
                registers[n] = read((registers[0] + registers[m]) & 0xFFFFFFFF)
            elif op & 0xF0FF == 0x0002:  # STC SR,Rn
                registers[n] = state["sr"]
            elif op & 0xF0FF == 0x400E:  # LDC Rn,SR
                state["sr"] = registers[n]
            elif op & 0xF0FF in (0x4002, 0x4012, 0x4022, 0x4013):
                key = {0x4002: "mach", 0x4012: "macl", 0x4022: "pr", 0x4013: "gbr"}[op & 0xF0FF]
                push(n, state[key])
            elif op & 0xF0FF in (0x4006, 0x4016, 0x4026, 0x4017):
                key = {0x4006: "mach", 0x4016: "macl", 0x4026: "pr", 0x4017: "gbr"}[op & 0xF0FF]
                state[key] = pop(n)
            elif op & 0xFF00 == 0xCB00:  # OR #imm,R0
                registers[0] |= op & 255
            elif op & 0xF000 == 0x7000:  # ADD #imm,Rn
                immediate = op & 255
                if immediate & 128:
                    immediate -= 256
                registers[n] = (registers[n] + immediate) & 0xFFFFFFFF
            elif op & 0xF0FF in (0x400B, 0x402B):  # JSR/JMP @Rn
                if delay:
                    raise ValueError("Control transfer in hook delay slot")
                transfer = registers[n]
                if op & 0xF0FF == 0x400B:
                    state["pr"] = address + 4
            else:
                raise ValueError(f"Unsupported hook instruction: {op:04x} at {address:08x}")
            return transfer

        for steps in range(256):
            if pc == 0x8C0BF270:
                break
            transfer = instruction(pc)
            if transfer is None:
                pc += 2
                continue
            instruction(pc + 2, delay=True)
            if transfer == visit:
                visits += 1
                if (registers[4] != initial_sr or state["sr"] & 0xF0 != 0xF0 or
                        registers[15] != exports["stack_top"] - 4):
                    raise ValueError("Observer C entry has incorrect SR argument, mask or stack")
                # SH ABI permits R0..R7, SR.T, PR and MAC to change across C.
                # Also clobber GBR to exercise the hook's explicit extra save.
                for index in range(8):
                    registers[index] = rng.getrandbits(32)
                state["sr"] ^= 1
                for key in ("pr", "mach", "macl", "gbr"):
                    state[key] = rng.getrandbits(32)
                pc += 4
            else:
                pc = transfer
        else:
            raise ValueError("Hook did not resume within its instruction bound")
        expected = list(original)
        expected[0], expected[2], expected[3] = 0xFD8, 0xA05F6C00, 0x8C100000
        if registers != expected or state != initial_state:
            raise ValueError("Hook changed inherited state beyond the six displaced instructions")
        if visits != 1 or stores != [(original[12], initial_sr)]:
            raise ValueError("Hook must visit once and replay the original DMA start exactly once")
    return {"cases": cases, "integer_state_preserved": True,
            "original_dma_start_writes": 1, "maximum_steps": steps + 1,
            "model_limits": "integer trampoline only; no caches, exceptions or hardware timing"}
