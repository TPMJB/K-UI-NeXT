#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual opt-in continuation shim and unchanged mapped bridge.

The closed integer model replaces the authored C phase/worker functions and
the original consumer with ABI adversaries. Its interrupt markers save and
restore a preempted worker context; they do not claim to model the CE kernel's
interrupt entry implementation or the provider's storage transactions.
"""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from test_armada_ce_maple_hook import CPU as CounterCPU, MASK, ROOT


HOOK = 0xACFC4000
PROOF_BOTTOM = 0xACFCF800
PROOF_TOP = 0xACFCFE00
PROOF_FRAME = 24
WORKER_BOTTOM = 0xACFCE800
WORKER_TOP = 0xACFCF800
CE_FRAME = 28
WORKER_PATCH = 0xB4F70009
FORBIDDEN_SR = 0x100000F0
BRIDGE_RETURN = 0xACFC5000
WAIT_FRAME = 96
NATIVE_WAIT_ADAPTER_FRAME = 24
NATIVE_WAIT_CALLEE_FRAME = 28
NATIVE_WAIT_ENTRY = 0x01E5877C
NATIVE_WAIT_HANDLE = 0x00224401
WAIT_TOTAL_FRAME = WAIT_FRAME + NATIVE_WAIT_ADAPTER_FRAME + NATIVE_WAIT_CALLEE_FRAME


class ContinuationCPU(CounterCPU):
    def __init__(self, seed, base, oldmode, bridge, helper, symbols,
                 actions=(0,), results=(), wait_results=None, incoming_mask=0,
                 blocked=False):
        super().__init__(seed, base, oldmode)
        self.r[8] = 0x00030A60
        self.sr = ((self.sr & ~FORBIDDEN_SR) | (incoming_mask << 4) |
                   (0x10000000 if blocked else 0))
        self.initial = (self.r.copy(), self.pr, self.mach, self.macl, self.gbr,
                        self.sr, self.fpscr, self.fr.copy())
        self.pc = base + 0x5DB0
        self.bridge, self.helper, self.symbols = bridge, helper, symbols
        self.actions, self.results = list(actions), list(results)
        self.wait_results = ([0 for result in results if not result]
                             if wait_results is None else list(wait_results))
        self.original_mode = oldmode
        self.c_calls, self.service_calls, self.original_calls = [], [], []
        self.wait_calls, self.bridge_returns = [], []
        self.events, self.sr_changes, self.preemptions, self.stack_changes = [], [], [], []
        self.guest_stack_min = self.start_sp
        self.proof_stack_min, self.worker_stack_min = PROOF_TOP, WORKER_TOP
        self.wait_stack_min = self.start_sp - CE_FRAME
        self.service_result = None
        self.terminal = False
        self.in_wait = False
        self.native_homes = []

    def code(self, address, size):
        if self.base + 0x5DB0 <= address and address + size <= self.base + 0x5DB6:
            patch = struct.pack('<IH', WORKER_PATCH, 0x6483)
            offset = address - self.base - 0x5DB0
            return patch[offset:offset + size]
        if self.base + 0x67A4 <= address and address + size <= self.base + 0x6800:
            offset = address - self.base - 0x67A4
            return self.bridge[offset:offset + size]
        if HOOK <= address and address + size <= HOOK + len(self.helper):
            assert self.sr & 0x40000000, 'P2 entered outside kernel mode'
            offset = address - HOOK
            return self.helper[offset:offset + size]
        raise AssertionError('Unadmitted code read ' + hex(address))

    def write(self, address, value):
        assert address % 4 == 0
        guest = self.start_sp - CE_FRAME <= address < self.start_sp
        proof = PROOF_TOP - PROOF_FRAME <= address < PROOF_TOP
        worker = WORKER_BOTTOM <= address < WORKER_TOP
        wait = (self.in_wait and self.start_sp - CE_FRAME - WAIT_FRAME -
                NATIVE_WAIT_ADAPTER_FRAME - NATIVE_WAIT_CALLEE_FRAME <= address
                < self.start_sp - CE_FRAME)
        private = address == BRIDGE_RETURN
        assert guest or proof or worker or wait or private, 'Unadmitted data write ' + hex(address)
        self.writes.append(address)
        self.mem[address] = value & MASK
        if guest:
            self.guest_stack_min = min(self.guest_stack_min, address)
        if proof:
            assert self.sr & 0x40000000
            assert self.sr & 0xF0 == 0xF0, 'Proof frame written while unmasked'
            self.proof_stack_min = min(self.proof_stack_min, address)
        if worker:
            assert self.sr & 0x40000000
            assert not self.sr & FORBIDDEN_SR, 'Worker stack used while blocked'
            self.worker_stack_min = min(self.worker_stack_min, address)
        if wait:
            assert self.sr & 0x40000000
            assert not self.sr & FORBIDDEN_SR, 'Wait stack used while blocked'
            self.wait_stack_min = min(self.wait_stack_min, address)
        if private:
            assert self.sr & 0xF0 == 0xF0
            assert self.r[15] == PROOF_TOP - PROOF_FRAME
            assert self.r[5] in (0, 1), 'Bridge return rewritten during RESUME'
            expected = self.base + (0x67C6 if self.r[5] == 0 else 0x67DE)
            assert value == expected, 'Stored bridge return does not match this slot'
            self.bridge_returns.append((self.r[5], value))

    def plain(self, word, pc):
        # Every authored change between the ordinary, proof and service
        # stacks must occur while masked. C adversary prologues are separate.
        switch = ((word & 0xF00F == 0x6003 or word >> 12 in (5, 13)) and
                  (word >> 8) & 15 == 15)
        old_sp = self.r[15]
        if switch:
            assert self.sr & 0xF0 == 0xF0, 'Stack changed while IRQs unmasked'
        if word & 0xF0FF == 0x4022:  # STS.L PR,@-Rn
            register = (word >> 8) & 15
            self.r[register] = (self.r[register] - 4) & MASK
            self.write(self.r[register], self.pr)
            return
        if word & 0xF0FF == 0x402A:  # LDS Rn,PR
            self.pr = self.r[(word >> 8) & 15]
            return
        if word & 0xF00F == 0x2008:  # TST Rm,Rn
            self.t(not (self.r[(word >> 8) & 15] & self.r[(word >> 4) & 15]))
            return
        if word >> 8 == 0x88:  # CMP/EQ #imm,R0
            value = word & 255
            if value & 128:
                value -= 256
            self.t(self.r[0] == (value & MASK))
            return
        if word == 0x400E:  # LDC R0,SR
            self.sr_changes.append((self.r[15], self.r[0], pc))
        super().plain(word, pc)
        if switch:
            self.stack_changes.append((old_sp, self.r[15], self.sr))

    def api(self):
        assert self.r[15] == self.start_sp - CE_FRAME
        self.events.append(('mode', int(bool(self.r[4]))))
        super().api()

    def c_run(self):
        assert self.sr & 0x40000000
        assert self.sr & 0xF0 == 0xF0, 'Proof phase called unmasked'
        assert self.r[15] == PROOF_TOP - PROOF_FRAME
        assert self.r[4] == self.initial[0][8], 'Extension argument clobbered'
        assert self.r[6] == self.original_mode, 'Previous mode clobbered'
        phase = self.r[5]
        frame = [self.mem[PROOF_TOP - PROOF_FRAME + 4*i] for i in range(6)]
        assert frame[0] == self.original_mode
        assert frame[1] == self.initial[0][8]
        assert frame[4] == self.initial[0][8]
        assert frame[5] == self.start_sp - CE_FRAME
        assert self.mem[BRIDGE_RETURN] == self.base + (0x67DE if phase == 1 else 0x67C6)
        if phase == 0:
            assert not self.c_calls
            assert self.r[7] == frame[2], 'Incoming SR not passed intact'
        elif phase == 2:
            assert self.service_calls and not self.original_calls
            assert self.r[7] == self.service_result, 'Worker result lost while remasking'
        else:
            assert phase == 1 and len(self.original_calls) == 1
        self.c_calls.append((phase, self.r[4], self.r[15], self.sr, self.r[7]))
        self.events.append(('phase', phase))
        if phase == 1:
            action = 0
        else:
            assert self.actions, 'Unexpected extra proof phase'
            action = self.actions.pop(0) & MASK
        return_pc = self.pr
        # Phase C has an explicit r8 save/restore in the shim. Challenge it,
        # along with all ordinary caller registers and the bridge MAC saves.
        for index in range(9):
            self.r[index] = (0xA1020304 + index * 0x13579BD + phase) & MASK
        self.mach ^= 0x45AB67CD
        self.macl ^= 0x89EF0123
        self.gbr ^= 0xA55AA55A
        self.pr ^= 0x76543210
        self.sr ^= 1
        self.r[0] = action
        self.pc = return_pc

    def preempt(self, kind, expected_sp=None):
        # A CE IRQ/context-switch ABI must preserve the interrupted private
        # worker's complete context. Temporarily substitute a hostile context
        # to catch accidental dependence on globally retained register state.
        saved = (self.r.copy(), self.pr, self.mach, self.macl, self.gbr,
                 self.sr, self.mode, self.fpscr, self.fr.copy())
        if expected_sp is None:
            expected_sp = (self.start_sp - CE_FRAME - WAIT_FRAME
                           if self.in_wait else WORKER_TOP - 64)
        assert self.r[15] == expected_sp
        assert not self.sr & FORBIDDEN_SR
        self.preemptions.append((kind, self.r[15], self.sr))
        self.events.append((kind, len(self.service_calls)))
        self.r = [(0xDEAD0000 + 97*i) & MASK for i in range(16)]
        self.pr, self.mach, self.macl, self.gbr = (0xCAFE0001, 2, 3, 4)
        self.sr, self.mode, self.fpscr = 0x500080F1, 1, 0x40001
        self.fr = [0xABCD0000 + i for i in range(32)]
        (self.r, self.pr, self.mach, self.macl, self.gbr, self.sr,
         self.mode, self.fpscr, self.fr) = saved

    def service(self):
        assert self.sr & 0x40000000, 'Service outside kernel mode'
        assert not self.sr & FORBIDDEN_SR, 'Service called with IRQs blocked'
        assert self.r[15] == WORKER_TOP, 'Service not on separate worker stack'
        assert self.r[8] == PROOF_TOP - PROOF_FRAME, 'Proof frame pointer lost'
        assert self.r[4] == self.initial[0][8]
        assert not self.original_calls, 'Consumer ran before service completed'
        assert self.results, 'Unexpected service invocation'
        result = self.results.pop(0) & MASK
        self.service_calls.append((self.r[4], self.r[15], self.sr, result))
        self.events.append(('service', result))
        return_pc = self.pr
        callee_saved = self.r[8:16]
        self.r[15] -= 64
        for index in range(16):
            self.write(self.r[15] + 4*index, 0xC0010000 + index)
        self.preempt('irq')
        self.preempt('preempt')
        self.r[15] += 64
        for index in range(8):
            self.r[index] = (0xB2030405 + index * 0x2468ACE) & MASK
        self.mach ^= 0x98AB76CD
        self.macl ^= 0x543210FE
        self.gbr ^= 0xC3C3C3C3
        self.pr ^= 0xFEDCBA98
        self.sr ^= 1
        assert self.r[8:16] == callee_saved, 'Worker violated the C ABI'
        self.r[0] = self.service_result = result
        self.pc = return_pc

    def native_wait_callee(self, result, interrupts=True):
        """Exact reviewed CE caller-home behavior, without owner code bytes.

        COREDLL RVA877c saves r8/PR, then spills r4..r7 to incoming SP+0..12.
        The source-owned metadata audit independently checks those forms.
        Everything else is a hostile native ABI boundary.
        """
        entry_sp, return_pc, old_r8 = self.r[15], self.pr, self.r[8]
        arguments = self.r[4:8].copy()
        self.r[15] -= 4
        self.write(self.r[15], old_r8)
        self.r[15] -= 4
        self.write(self.r[15], return_pc)
        homes = []
        # The source spills r5 before r4; preserve that order as well as all
        # four offsets, including unused r6/r7 homes for a two-argument API.
        for index in (1, 0, 2, 3):
            address = self.r[15] + 8 + 4*index
            self.write(address, arguments[index])
            homes.append((address, arguments[index]))
        self.native_homes.append(homes)
        self.r[15] -= 20
        for index in range(5):
            self.write(self.r[15]+4*index, 0xCE000000+index)
        if interrupts:
            self.preempt('wait_irq', self.r[15])
            self.preempt('wait_preempt', self.r[15])
        for index in range(8):
            self.r[index] = (0xE5060708 + index*0x579BDF) & MASK
        self.gbr ^= 0xDEADBEEF
        self.mach ^= 0x1256ABCD
        self.macl ^= 0xEFA12345
        self.sr ^= 1
        self.r[15] += 20
        self.pr = self.read(self.r[15])
        self.r[15] += 4
        self.r[8] = self.read(self.r[15])
        self.r[15] += 4
        assert self.r[15] == entry_sp and self.r[8] == old_r8
        self.r[0] = result & MASK
        self.pc = self.pr

    def native_wait_adapter(self, result):
        """Execute the actual authored GCC-to-CE assembly adapter."""
        stack, gbr, saved = self.r[15], self.gbr, self.r[8:15].copy()
        return_pc = 0xCEAD0000
        self.r[4:7] = [NATIVE_WAIT_ENTRY, NATIVE_WAIT_HANDLE, 1]
        self.pr = return_pc
        self.pc = self.symbols['_kui_armada_ce_maple_native_wait']
        while self.pc != return_pc:
            self.steps += 1
            assert self.steps < 20000
            pc = self.pc
            word = struct.unpack('<H', self.code(pc, 2))[0]
            if word & 0xF0FF == 0x400B or word == 0x000B:
                if word == 0x000B:
                    target = self.pr
                else:
                    target = self.r[(word >> 8) & 15]
                    self.pr = pc+4
                self.plain(struct.unpack('<H', self.code(pc+2, 2))[0], pc+2)
                if target == NATIVE_WAIT_ENTRY:
                    assert self.r[15] == stack-NATIVE_WAIT_ADAPTER_FRAME
                    assert self.r[4:6] == [NATIVE_WAIT_HANDLE, 1]
                    self.native_wait_callee(258 if not result else result)
                else:
                    self.pc = target
            else:
                self.plain(word, pc)
                self.pc = pc+2
        assert self.r[15] == stack and self.r[8:15] == saved
        assert self.gbr == gbr, 'Native wait changed caller GBR'
        assert self.r[0] == (258 if not result else result)

    def wait(self):
        assert self.sr & 0x40000000, 'Wait outside kernel mode'
        assert not self.sr & FORBIDDEN_SR, 'Wait called with IRQs blocked'
        assert self.r[15] == self.start_sp - CE_FRAME, 'CE wait on private stack'
        assert self.sr == self.mem[PROOF_TOP - PROOF_FRAME + 8], 'Wait did not restore exact incoming SR'
        assert self.r[8] == PROOF_TOP - PROOF_FRAME
        assert self.r[4] == self.initial[0][8]
        assert not self.original_calls
        assert self.service_calls[-1][3] == 0, 'Wait after non-BUSY service result'
        assert self.wait_results, 'Unexpected wait invocation'
        result = self.wait_results.pop(0) & MASK
        self.wait_calls.append((self.r[4], self.r[15], self.sr, result))
        self.events.append(('wait', result))
        return_pc = self.pr
        callee_saved = self.r[8:16]
        self.in_wait = True
        self.r[15] -= WAIT_FRAME
        for index in range(WAIT_FRAME // 4):
            self.write(self.r[15] + 4*index, 0xC0020000 + index)
        self.native_wait_adapter(result)
        self.r[15] += WAIT_FRAME
        self.in_wait = False
        for index in range(8):
            self.r[index] = (0xD4050607 + index * 0x467ACE) & MASK
        self.mach ^= 0x79AB67CD
        self.macl ^= 0xCDAB0123
        self.gbr ^= 0x87654321
        self.pr ^= 0xF0E1D2C3
        self.sr ^= 1
        assert self.r[8:16] == callee_saved, 'Wait violated the C ABI'
        self.r[0] = result
        self.service_result = 0 if not result else MASK-1
        self.pc = return_pc

    def original_completion(self):
        assert not self.original_calls, 'Original consumer called twice'
        assert not self.actions and not self.results and not self.wait_results, 'Consumer before completion decision'
        assert self.r[4] == self.initial[0][8]
        assert self.mode == self.original_mode
        assert bool(self.sr & 0x40000000) == bool(self.original_mode)
        assert self.r[15] == self.start_sp - CE_FRAME
        assert self.sr & ~1 == self.initial[5] & ~1
        self.original_calls.append((self.r[4], self.mode, self.r[15], self.pr))
        self.events.append(('original', self.original_mode))
        return_pc = self.pr
        for index in range(8):
            self.r[index] = (0xD3040506 + index * 0x3579BDF) & MASK
        self.mach ^= 0x98765432
        self.macl ^= 0x13579BDF
        self.gbr ^= 0x55AA55AA
        self.pr ^= 0x87654321
        self.sr ^= 1
        self.pc = return_pc

    def run(self):
        while self.pc != self.base + 0x5DB6:
            self.steps += 1
            assert self.steps < 20000, 'Unbounded nonterminal execution'
            pc = self.pc
            word = struct.unpack('<H', self.code(pc, 2))[0]
            jsr = word & 0xF0FF == 0x400B
            if word >> 12 in (0xA, 0xB) or jsr or word == 0x000B:
                if word >> 12 in (0xA, 0xB):
                    displacement = word & 4095
                    displacement -= 4096 if displacement & 2048 else 0
                    target = (pc + 4 + displacement*2) & MASK
                    if word >> 12 == 0xB:
                        self.pr = pc + 4
                elif jsr:
                    target = self.r[(word >> 8) & 15]
                    self.pr = pc + 4
                else:
                    target = self.pr
                self.plain(struct.unpack('<H', self.code(pc + 2, 2))[0], pc + 2)
                if word >> 12 == 0xA and target == pc:
                    assert self.sr & 0xF0 == 0xF0
                    assert self.r[15] == PROOF_TOP - PROOF_FRAME
                    assert not self.original_calls
                    self.terminal = True
                    self.events.append(('terminal', pc))
                    return self
                if target == self.base + 0x6374:
                    self.api()
                elif target == self.symbols['_kui_armada_ce_maple_traffic_run']:
                    self.c_run()
                elif target == self.symbols['_kui_armada_ce_vmu_runtime_work']:
                    self.service()
                elif target == self.symbols['_kui_armada_ce_maple_runtime_wait']:
                    self.wait()
                elif target == self.base + 0x5B0C:
                    self.original_completion()
                else:
                    self.pc = target
            elif word >> 8 in (0x89, 0x8B):
                displacement = word & 255
                displacement -= 256 if displacement & 128 else 0
                take = bool(self.sr & 1) == (word >> 8 == 0x89)
                self.pc = (pc + 4 + displacement*2 if take else pc + 2) & MASK
            else:
                self.plain(word, pc)
                self.pc = (pc + 2) & MASK
        return self


class ContinuationNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        prefix = ROOT / '.deps/sh-elf/bin'
        gcc = prefix / 'sh-elf-gcc'
        if not gcc.exists():
            raise unittest.SkipTest('Native SH toolchain unavailable')
        flags = ['-m4-single-only', '-ml', '-ffreestanding', '-I'+str(ROOT/'include')]
        defines = ['-DKUI_ARMADA_CE_MAPLE_FENCE=1', '-DKUI_ARMADA_CE_MAPLE_A2=1',
                   '-DKUI_ARMADA_CE_MAPLE_WRITE_LAYOUT=1',
                   '-DKUI_ARMADA_CE_MAPLE_WRITE_STAGING=1',
                   '-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME=1']
        stub = directory / 'stub.S'
        stub.write_text('.section .text,"ax"\n.align 1\n'
                        '.global _kui_armada_ce_maple_traffic_run\n'
                        '_kui_armada_ce_maple_traffic_run:\nrts\nnop\n'
                        '.global _kui_armada_ce_vmu_runtime_work\n'
                        '_kui_armada_ce_vmu_runtime_work:\nrts\nnop\n'
                        '.global _kui_armada_ce_maple_runtime_wait\n'
                        '_kui_armada_ce_maple_runtime_wait:\nrts\nnop\n')
        linker = directory / 'test.ld'
        linker.write_text('''OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_armada_ce_maple_hook)
SECTIONS {
 . = 0x01dd67a4; .bridge : { KEEP(*(.bridge)) }
 . = 0xacfc4000; .hook : { KEEP(*(.hook.entry)) *(.text .text.* .rodata .rodata.*) }
 . = 0xacfc5000; .private (NOLOAD) : { _kui_armada_ce_maple_bridge_return = .; . += 4; }
 __kui_vmu_worker_stack_bottom = 0xacfce800;
 __kui_vmu_worker_stack_top = 0xacfcf800;
 __kui_maple_stack_bottom = 0xacfcf800;
 __kui_maple_stack_top = 0xacfcfe00;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
''')
        sources = [ROOT/'src/loader/armada_ce_maple_fence_bridge.S',
                   ROOT/'src/loader/armada_ce_maple_traffic_entry.S', stub]
        objects = []
        for index, source in enumerate(sources):
            obj = directory / ('input'+str(index)+'.o')
            subprocess.run([str(gcc), *flags, *defines, '-c', str(source),
                            '-o', str(obj)], check=True)
            objects.append(str(obj))
        elf = directory / 'continuation.elf'
        subprocess.run([str(gcc), *flags, '-nostdlib', '-Wl,--build-id=none',
                        '-T'+str(linker), '-o', str(elf), *objects], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for section in ('bridge', 'hook'):
            output = directory / (section+'.bin')
            subprocess.run([str(prefix/'sh-elf-objcopy'), '--only-section=.'+section,
                            '-O', 'binary', str(elf), str(output)], check=True)
            setattr(cls, section, output.read_bytes())
        # The feature must not change the mapped wrapper at all. Compile its
        # disabled profile separately, retaining the same reservation target.
        disabled_bridge = directory / 'bridge-disabled.o'
        disabled_flags = [value for value in defines
                          if not value.startswith('-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME=')]
        subprocess.run([str(gcc), *flags, *disabled_flags,
                        '-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME=0', '-c', str(sources[0]),
                        '-o', str(disabled_bridge)], check=True)
        disabled_elf = directory / 'bridge-disabled.elf'
        subprocess.run([str(gcc), *flags, '-nostdlib', '-Wl,--build-id=none',
                        '-T'+str(linker), '-o', str(disabled_elf), str(disabled_bridge),
                        *objects[1:]], check=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
        disabled_binary = directory / 'bridge-disabled.bin'
        subprocess.run([str(prefix/'sh-elf-objcopy'), '--only-section=.bridge',
                        '-O', 'binary', str(disabled_elf), str(disabled_binary)],
                       check=True)
        cls.disabled_bridge = disabled_binary.read_bytes()
        cls.symbols = {}
        for line in subprocess.check_output([str(prefix/'sh-elf-nm'), '-n', str(elf)],
                                            text=True).splitlines():
            fields = line.split()
            if len(fields) == 3:
                cls.symbols[fields[2]] = int(fields[0], 16)

    def cpu(self, seed=0, base=0x01DD0000, mode=0, **kwargs):
        return ContinuationCPU(seed, base, mode, self.bridge, self.hook,
                               self.symbols, **kwargs).run()

    def preserved(self, cpu):
        r, pr, mach, macl, gbr, sr, fpscr, fr = cpu.initial
        self.assertFalse(cpu.terminal)
        self.assertEqual(cpu.r[8:16], r[8:16])
        self.assertEqual((cpu.pr, cpu.mach, cpu.macl, cpu.gbr, cpu.fpscr, cpu.fr),
                         (pr, mach, macl, gbr, fpscr, fr))
        self.assertEqual(cpu.sr & ~1, sr & ~1)
        self.assertEqual(cpu.mode, cpu.original_mode)
        self.assertEqual(cpu.apis, [1, cpu.original_mode, 1, cpu.original_mode])
        self.assertEqual(cpu.guest_stack_min, r[15] - CE_FRAME)
        self.assertEqual(cpu.proof_stack_min, PROOF_TOP - PROOF_FRAME)
        self.assertEqual(len(cpu.original_calls), 1)
        self.assertFalse(cpu.actions or cpu.results or cpu.wait_results)
        self.assertEqual(cpu.bridge_returns,
                         [(0, cpu.base+0x67C6), (1, cpu.base+0x67DE)])

    def test_linked_fixed_bridge_and_disjoint_stack_contract(self):
        self.assertEqual(len(self.bridge), 92)
        self.assertEqual(self.bridge, self.disabled_bridge)
        self.assertEqual(self.symbols['_kui_armada_ce_maple_bridge'], 0x01DD67A4)
        self.assertEqual(self.symbols['__kui_maple_bridge_end'], 0x01DD6800)
        self.assertEqual(self.symbols['_kui_armada_ce_maple_hook'], HOOK)
        self.assertEqual(self.symbols['_kui_armada_ce_maple_bridge_return'], BRIDGE_RETURN)
        self.assertEqual(self.symbols['__kui_vmu_worker_stack_bottom'], WORKER_BOTTOM)
        self.assertEqual(self.symbols['__kui_vmu_worker_stack_top'], WORKER_TOP)
        self.assertEqual(self.symbols['__kui_maple_stack_bottom'], PROOF_BOTTOM)
        self.assertEqual(self.symbols['__kui_maple_stack_top'], PROOF_TOP)
        self.assertEqual(WORKER_TOP - WORKER_BOTTOM, 4096)
        self.assertEqual(PROOF_TOP - PROOF_BOTTOM, 1536)
        self.assertEqual(self.symbols['__kui_maple_traffic_shim_end'] - HOOK, 184)
        # Only the literal helper address differs from the accepted mapped
        # bridge. Compile its source with the same A2/layout profile above.
        self.assertIn(struct.pack('<I', HOOK), self.bridge)
        self.assertLessEqual(self.symbols['__kui_maple_traffic_shim_end'],
                             self.symbols['_kui_armada_ce_maple_traffic_run'])

    def test_native_callee_homes_reproduce_old_saved_pr_and_r8_corruption(self):
        cpu = ContinuationCPU(3, 0x01DD0000, 0, self.bridge, self.hook, self.symbols)
        cpu.sr = 0x40008000
        cpu.in_wait = True
        ce_sp = cpu.start_sp-CE_FRAME
        # The old linked GCC wrapper saved r8/PR in8 bytes, then called the
        # native API directly, without its mandatory16 caller-home bytes.
        unsafe_sp, expected_pr, expected_r8 = ce_sp-8, HOOK+0x3456, PROOF_TOP-PROOF_FRAME
        cpu.r[15] = unsafe_sp
        cpu.r[8] = expected_r8
        cpu.mem[unsafe_sp], cpu.mem[unsafe_sp+4] = expected_pr, expected_r8
        cpu.r[4:8] = [NATIVE_WAIT_HANDLE, 1, 0xCC006666, 0xDD007777]
        cpu.pr = HOOK+0x1234
        cpu.native_wait_callee(258, interrupts=False)
        self.assertEqual(sorted(at for at, _ in cpu.native_homes[0]),
                         [unsafe_sp+4*i for i in range(4)])
        self.assertEqual(cpu.mem[unsafe_sp], NATIVE_WAIT_HANDLE)
        self.assertEqual(cpu.mem[unsafe_sp+4], 1)
        self.assertEqual((cpu.mem[ce_sp], cpu.mem[ce_sp+4]), (0xCC006666, 0xDD007777))
        self.assertNotEqual(cpu.mem[unsafe_sp], expected_pr)
        self.assertNotEqual(cpu.mem[unsafe_sp+4], expected_r8)

    def test_actual_adapter_reserves_native_homes_below_its_saved_pr_and_gbr(self):
        cpu = self.cpu(29, actions=(1, 0), results=(0,))
        self.preserved(cpu)
        self.assertEqual(len(cpu.native_homes), 1)
        caller_sp = cpu.start_sp-CE_FRAME-WAIT_FRAME
        native_sp = caller_sp-NATIVE_WAIT_ADAPTER_FRAME
        self.assertEqual(sorted(at for at, _ in cpu.native_homes[0]),
                         [native_sp+4*i for i in range(4)])
        # Homes stop before the adapter's saved GBR/PR. The native adversary
        # overwrites all four homes and GBR, yet actual adapter RTS and all
        # callee-saved registers still survive through the original consumer.
        self.assertLess(max(at for at, _ in cpu.native_homes[0]), caller_sp-8)

    def test_no_pending_and_service_done_preserve_original_context(self):
        cases = 0
        for base in (0x01DD0000, 0x03DD0000, 0x09DD0000):
            for mode in (0, 1):
                for seed in range(32):
                    for actions, results in (((0,), ()), ((1, 0), (1,))):
                        with self.subTest(base=hex(base), mode=mode, seed=seed,
                                          service=bool(results)):
                            cpu = self.cpu(seed, base, mode, actions=actions, results=results)
                            self.preserved(cpu)
                            expected = [0, 2, 1] if results else [0, 1]
                            self.assertEqual([c[0] for c in cpu.c_calls], expected)
                            self.assertEqual(len(cpu.preemptions), 2*len(results))
                            self.assertEqual(cpu.worker_stack_min,
                                             WORKER_TOP - (64 if results else 0))
                            cases += 1
        self.assertEqual(cases, 384)

    def test_busy_then_done_repeats_service_before_exactly_one_consumer(self):
        for count in (1, 2, 8, 64):
            for result in (0, 1, MASK, MASK-1, 0x80000000, 0x13579BDF):
                with self.subTest(count=count, result=hex(result)):
                    results = (0,)*(count-1) + (result,)
                    cpu = self.cpu(23, actions=(1,)*count+(0,), results=results)
                    self.preserved(cpu)
                    self.assertEqual([s[3] for s in cpu.service_calls], list(results))
                    self.assertEqual([c[4] for c in cpu.c_calls if c[0] == 2], list(results))
                    self.assertEqual(len(cpu.wait_calls), results.count(0))
                    original = next(i for i, event in enumerate(cpu.events)
                                    if event[0] == 'original')
                    self.assertTrue(all(i < original for i, event in enumerate(cpu.events)
                                        if event[0] == 'service'))
                    self.assertEqual(len(cpu.preemptions), (count+results.count(0))*2)

    def test_errors_and_unknown_actions_cannot_fall_through_to_consumer(self):
        for action in (2, 3, MASK, 0x80000000, 0x13579BDF):
            for result in (0, 1, MASK, MASK-1):
                for after_service in (False, True):
                    with self.subTest(action=hex(action), result=hex(result),
                                      after_service=after_service):
                        cpu = self.cpu(17, actions=(1, action) if after_service else (action,),
                                       results=(result,) if after_service else ())
                        self.assertTrue(cpu.terminal)
                        self.assertFalse(cpu.original_calls)
                        self.assertEqual(len(cpu.service_calls), int(after_service))
                        self.assertEqual(cpu.apis, [1])
                        self.assertLess(cpu.steps, 150)

    def test_masked_or_blocked_entry_never_calls_service(self):
        for mode in (0, 1):
            for irq_mask in range(16):
                for blocked in (False, True):
                    if not irq_mask and not blocked:
                        continue
                    with self.subTest(mode=mode, mask=irq_mask, blocked=blocked):
                        cpu = self.cpu(9, mode=mode, actions=(1,),
                                       incoming_mask=irq_mask, blocked=blocked)
                        self.assertTrue(cpu.terminal)
                        self.assertFalse(cpu.service_calls or cpu.original_calls)
                        self.assertEqual([c[0] for c in cpu.c_calls], [0])
        # An ordinary no-pending response still restores the exact prior SR.
        for irq_mask in (1, 7, 15):
            cpu = self.cpu(8, actions=(0,), incoming_mask=irq_mask, blocked=True)
            self.preserved(cpu)

    def test_each_unmasked_transition_uses_service_stack_and_remasks_before_proof(self):
        cpu = self.cpu(31, actions=(1, 1, 0), results=(0, MASK))
        self.preserved(cpu)
        unmasked_private = [(sp, sr) for sp, sr, _ in cpu.sr_changes
                            if sp >= WORKER_BOTTOM and not sr & 0xF0]
        self.assertEqual(unmasked_private,
                         [(WORKER_TOP, cpu.c_calls[0][4])]*2)
        self.assertTrue(all(not sr & FORBIDDEN_SR for _, _, sr in cpu.preemptions))
        self.assertEqual([(kind, sp) for kind, sp, _ in cpu.preemptions],
                         [('irq', WORKER_TOP-64), ('preempt', WORKER_TOP-64),
                          ('wait_irq', cpu.start_sp-CE_FRAME-WAIT_TOTAL_FRAME),
                          ('wait_preempt', cpu.start_sp-CE_FRAME-WAIT_TOTAL_FRAME),
                          ('irq', WORKER_TOP-64), ('preempt', WORKER_TOP-64)])
        phases = [event[0] for event in cpu.events]
        self.assertEqual(phases,
                         ['mode', 'phase', 'service', 'irq', 'preempt', 'wait',
                          'wait_irq', 'wait_preempt', 'phase',
                          'service', 'irq', 'preempt', 'phase', 'mode', 'original',
                          'mode', 'phase', 'mode'])
        ce_sp, proof_sp = cpu.start_sp-CE_FRAME, PROOF_TOP-PROOF_FRAME
        self.assertEqual([(before, after) for before, after, _ in cpu.stack_changes],
                         [(ce_sp, proof_sp), (proof_sp, WORKER_TOP),
                          (WORKER_TOP, proof_sp), (proof_sp, ce_sp),
                          (ce_sp, proof_sp), (proof_sp, WORKER_TOP),
                          (WORKER_TOP, proof_sp), (proof_sp, ce_sp),
                          (ce_sp, proof_sp), (proof_sp, ce_sp)])

    def test_busy_wait_failure_maps_to_minus_two_and_never_uses_private_stack(self):
        for base in (0x01DD0000, 0x03DD0000, 0x09DD0000):
            for mode in (0, 1):
                for wait_result in (0, 1, MASK, MASK-1, 0x80000000, 0x13579BDF):
                    with self.subTest(base=hex(base), mode=mode, wait_result=hex(wait_result)):
                        cpu = self.cpu(7, base, mode, actions=(1, 0), results=(0,),
                                       wait_results=(wait_result,))
                        self.preserved(cpu)
                        self.assertEqual(len(cpu.wait_calls), 1)
                        self.assertEqual(cpu.wait_calls[0][1], cpu.start_sp-CE_FRAME)
                        self.assertEqual(cpu.wait_stack_min, cpu.start_sp-CE_FRAME-WAIT_TOTAL_FRAME)
                        self.assertEqual(cpu.c_calls[1][4], 0 if not wait_result else MASK-1)
        cpu = self.cpu(8, actions=(1, 2), results=(0,), wait_results=(MASK,))
        self.assertTrue(cpu.terminal)
        self.assertEqual(cpu.c_calls[1][4], MASK-1)
        self.assertFalse(cpu.original_calls)


if __name__ == '__main__':
    unittest.main()
