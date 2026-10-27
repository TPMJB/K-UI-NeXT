#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute linked Maple gateways with only their passive C calls bypassed.

Actual supplied original SDK instructions provide the submit-state reference.
The existing gateway frame, mask, private stack and restoration still execute.
This is an instruction/ABI proof, not a cycle, cache, IRQ or console result.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from check_native_bottom_heap import ABSOLUTE_ONE, require
from check_vmu_native_persistent import CachedGateway
from check_vmu_native_readonly import ReadonlyGateway, relocated
from check_vmu_native_worker_gateway import P2, read_elf
from test_armada_ce_maple_hook import CPU, MASK
from test_vmu_native_heap_execution import verified_program_image, signed

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = 'kui.native.maple-bypass.linked-gateways.v1'
SOURCES = ('tools/check_native_maple_bypass.py', 'tools/check_vmu_native_persistent.py',
    'tools/check_vmu_native_readonly.py', 'tools/check_vmu_native_worker_gateway.py',
    'tests/test_armada_ce_maple_hook.py', 'tests/test_vmu_native_heap_execution.py',
    'src/loader/vmu_native_passive_hooks.S', 'include/kui/native_isolation.h')


def sha(data):
    return hashlib.sha256(data).hexdigest()


class OriginalSubmit(ReadonlyGateway):
    """Decode the six actual owner instructions, not a hand-written replay."""
    def __init__(self, image, initial, manager):
        CPU.__init__(self, 0, 0x8c010000, 1)
        self.image, self.initial, self.manager = image, initial, manager
        self.r, self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, fr = initial
        self.r, self.fr = self.r.copy(), fr.copy()
        self.mem = {self.r[14]: manager, manager + 0x10f4: 0xa05f6c00}
        self.reads, self.writes = [], []

    def code(self, address, size):
        offset = address - 0x8c010000
        require(0 <= offset < offset + size <= len(self.image), 'Original code/literal escaped input')
        return self.image[offset:offset+size]

    def write(self, address, value):
        require(address == 0xa05f6c18, 'Original submit wrote outside its MDST register')
        self.writes.append(address)
        self.mem[address] = value & MASK

    def read(self, address):
        return CPU.read(self, address)

    def plain(self, word, pc):
        if word >> 12 == 9:
            address = pc + 4 + (word & 255) * 2
            self.r[word >> 8 & 15] = signed(int.from_bytes(self.code(address, 2), 'little'), 16) & MASK
        else:
            ReadonlyGateway.plain(self, word, pc)

    def execute(self, begin, end):
        require(end - begin == 12, 'Original admitted submit span is not six instructions')
        for pc in range(begin, end, 2):
            self.plain(int.from_bytes(self.code(pc, 2), 'little'), pc)
        return self


class BypassGateway(CachedGateway):
    def __init__(self, seed, mask, base, blob, symbols, mode, alias, t, driver):
        super().__init__(seed, mask, base, blob, symbols, mode)
        self.start_sp = alias | 0x0c00ec00
        self.r[15], self.r[14] = self.start_sp, driver[9]
        self.manager = driver[10]
        self.mem[self.r[14]] = self.manager
        self.mem[self.manager + 0x10f4] = 0xa05f6c00
        self.resume, self.consumer = driver[7], driver[14]
        self.mem[symbols['_kui_vmu_native_passive_resume']] = self.resume
        self.mem[symbols['_kui_vmu_native_passive_consumer']] = self.consumer
        self.sr = (self.sr & ~0xf1) | (mask << 4) | t
        self.initial = (self.r.copy(), self.pr, self.mach, self.macl, self.gbr,
                        self.sr, self.fpscr, self.fr.copy())
        self.sr_loads, self.executed = 0, []

    def read(self, address):
        if address == self.symbols['_kui_vmu_native_passive_consumer']:
            require(self.mode == 'complete' and not self.called and self.r[15] == self.start_sp,
                    'Consumer cell read before original owner frame restoration')
            require(self.sr == self.initial[5], 'Consumer cell read before inherited SR restoration')
        return CPU.read(self, address)

    def callback(self, target):
        raise AssertionError('Bypass reached a passive C entry: ' + hex(target))

    def plain(self, word, pc):
        self.executed.append(pc)
        if word & 0xf0ff == 0x400e:
            if self.sr_loads:
                require(self.r[15] == self.start_sp and
                        self.code(pc+2, 8) == struct.pack('<4H', 9, 9, 9, 9),
                        'Inherited SR restored before full owner-frame consumption/latency')
            self.sr_loads += 1
        return super().plain(word, pc)

    def verify(self, image, driver):
        self.run()
        require(not self.called and self.sr_loads == 2, 'C call executed or gateway SR sequence changed')
        initial = self.initial
        if self.mode == 'submit':
            original = OriginalSubmit(image, initial, self.manager).execute(driver[6], driver[7])
            expected = original.r
            require(self.pc == driver[7] and self.writes.count(0xa05f6c18) == 1 and
                    self.mem[0xa05f6c18] == original.mem[0xa05f6c18],
                    'Submit differs from original MDST store/control transfer')
        else:
            expected = initial[0].copy()
            expected[3] = self.consumer
            require(self.pc == self.consumer and self.writes.count(0xa05f6c18) == 0,
                    'Completion differs from original consumer tail')
        require(self.r == expected, 'Gateway changed a live integer register/SP')
        require((self.pr,self.mach,self.macl,self.gbr,self.sr,self.fpscr,self.fr) == initial[1:],
                'Gateway changed inherited PR/SR/special/FPU state')
        owner_writes = [a for a in self.writes if self.start_sp-96 <= a < self.start_sp]
        private_writes = [a for a in self.writes if self.private_top-256 <= a < self.private_top]
        owner_reads = [a for a in self.reads if self.start_sp-96 <= a < self.start_sp]
        private_reads = [a for a in self.reads if self.private_top-256 <= a < self.private_top]
        require(len(owner_writes) == len(owner_reads) == 20 and
                len(private_writes) == len(private_reads) == 1,
                'Gateway owner/private frame traffic changed')
        return self


def assembler_comparison():
    cc = ROOT / '.deps/sh-elf/bin/sh-elf-gcc'
    flags = ['-x','assembler-with-cpp','-c','-DKUI_VMU_NATIVE_CACHED_CODE=1',
             '-DKUI_VMU_NATIVE_READONLY_PROBE=1','-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1']
    with tempfile.TemporaryDirectory(prefix='kui-maple-bypass-') as directory:
        path = Path(directory)
        blobs = []
        for flag in (0, 1):
            obj, binary = path/(str(flag)+'.o'), path/(str(flag)+'.text')
            subprocess.run([str(cc),*flags,'-DKUI_NATIVE_MAPLE_BYPASS_ISOLATION='+str(flag),
                str(ROOT/'src/loader/vmu_native_passive_hooks.S'),'-o',str(obj)], check=True, capture_output=True)
            subprocess.run([str(cc.with_name('sh-elf-objcopy')),'--dump-section','.text='+str(binary),str(obj)],
                check=True, capture_output=True)
            blobs.append(binary.read_bytes())
    normal, bypass = blobs
    changed = [i for i in range(0,len(normal),2) if normal[i:i+2] != bypass[i:i+2]]
    require(len(normal) == len(bypass) and len(changed) == 2 and
            all(normal[i:i+2] == struct.pack('<H', 0x400b) and
                bypass[i:i+2] == struct.pack('<H', 9) for i in changed),
            'Assembly control differs beyond the two JSR-to-NOP sites')
    return dict(bytes=len(normal), changed_halfword_offsets=changed,
                only_two_C_call_instructions_replaced=True,
                normal_sha256=sha(normal), bypass_sha256=sha(bypass))


def call_site(binary, symbols, role):
    start = symbols['_kui_vmu_native_passive_'+role+'_hook']
    target = symbols['_kui_vmu_native_passive_'+role]
    matches = []
    for pc in range(start, start+128, 2):
        at = (pc & 0x1fffffff) - (P2 & 0x1fffffff)
        word = struct.unpack_from('<H', binary, at)[0]
        if word >> 8 != 0xd0:
            continue
        literal = (((pc+4)&~3)+(word&255)*4 & 0x1fffffff) - (P2 & 0x1fffffff)
        if struct.unpack_from('<I', binary, literal)[0] == target:
            require(binary[at+2:at+6] == struct.pack('<2H', 9, 9), 'C call is not exactly NOP/NOP')
            matches.append(at+2)
    require(len(matches) == 1, 'Missing unique actual C-call bypass site: '+role)
    return matches[0]


def check(build, startup, record):
    build, startup, record = Path(build).resolve(), Path(startup).resolve(), Path(record).resolve()
    source, image, extent = verified_program_image(startup, record)
    driver = struct.unpack_from('<18I', record.read_bytes(), 64)
    require(driver[3:5] == (len(image), 0x8c010000), 'NVR does not bind original execution extent')
    artifacts = {p.name:sha(p.read_bytes()) for p in sorted(build.iterdir())
                 if p.suffix in ('.elf','.bin','.reloc') and p.is_file()}
    worker = read_elf(build/'native-worker.elf')
    for elf,role in ((read_elf(build/'stage.elf'),'stage'), (worker,'worker')):
        for marker in ('maple_bypass','reader_only','ra_tick_off'):
            require(elf[4].get('__kui_native_'+marker+'_isolation_'+role) == ABSOLUTE_ONE,
                    'Missing paired isolation capability: '+marker+'/'+role)
        require('__kui_native_e1_wait_capture_'+role not in elf[4], 'Bypass includes incompatible wait capture')
    symbols = {name:value[0] for name,value in worker[4].items()}
    binary, wire = (build/'native-worker.bin').read_bytes(), (build/'native-worker.reloc').read_bytes()
    sites = {role:call_site(binary,symbols,role) for role in ('submit','complete')}
    cases, writes = 0, dict(submit_mdst=0, complete_mdst=0)
    for destination in (P2, 0xac35e220, 0xac700000):
        blob = relocated(binary,wire,destination)
        mapped = {name:address+destination-P2 for name,address in symbols.items()}
        for alias in (0,0x80000000,0xa0000000):
            for seed in (0,1):
                for mask in range(16):
                    for t in (0,1):
                        for role in ('submit','complete'):
                            cpu = BypassGateway(seed,mask,destination,blob,mapped,role,alias,t,driver).verify(image,driver)
                            cases += 1
                            writes[role+'_mdst'] += cpu.writes.count(0xa05f6c18)
    refusals = []
    for role,site in sites.items():
        changed = bytearray(binary)
        struct.pack_into('<H',changed,site,0x400b)
        try:
            BypassGateway(0,0,P2,bytes(changed),symbols,role,0x80000000,0,driver).verify(image,driver)
        except (AssertionError,ValueError) as error:
            refusals.append(dict(mutation=role+'-C-call-restored', reason=str(error)))
        else:
            raise ValueError('C-call trap mutation was accepted')
    provenance = json.loads((build/'entry.compile.json').read_text())
    require(provenance['source_dirty'] is False and provenance['elf_sha256'] == artifacts['entry.elf'],
            'Clean runtime provenance does not bind entry ELF')
    require(all(sha((build/name).read_bytes()) == digest for name,digest in artifacts.items()),
            'Artifact changed during linked gateway proof')
    return dict(schema=SCHEMA,status='PASS',source_commit=provenance['commit'],build=str(build),
        artifact_sha256=artifacts, source_sha256={name:sha((ROOT/name).read_bytes()) for name in SOURCES},
        inputs=dict(startup_sha256=sha(source),record_sha256=sha(record.read_bytes()),extent=extent),
        assembler_comparison=assembler_comparison(), instruction_cases=cases,
        C_calls=0, exact_original_submit_replay=True, inherited_registers_PR_SR_SP_preserved=True,
        gateway_frames_and_stack_traffic_retained=True, all_imasks_and_T=True,
        relocation_destinations=3,owner_stack_aliases=3,seeds=2,
        mdst_store_counts=writes,actual_call_site_offsets=sites,mutation_refusals=refusals,
        scope=__doc__.strip())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','startup','record','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    report=check(args.build,args.startup,args.record)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(schema=SCHEMA,status='PASS',output=str(args.output),cases=report['instruction_cases'])))


if __name__=='__main__':
    main()
