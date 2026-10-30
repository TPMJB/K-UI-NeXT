#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind optional native comparison/profile modes to exact immutable ELF bytes.

The linked instruction witnesses independently cover malformed-mode refusal
and the first storage branch. Relay models additionally prove control leaves
the owner initializer, return frame and low control untouched.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_loader_layout import inspect_elf, region
from check_vmu_native_stage_stack import Machine
from check_vmu_native_cache_gateway import CacheRoute
from check_vmu_native_worker_gateway import MASK, read_elf, symbol_bytes
from personalize_vmu_sd_proof import readonly_blob
from runtime_package import ADDRESS, verify
from toy_pilot_symbols import _linked_symbols

STAGE, LIMIT, MODE_BYTES = 0x8CE00000, 0x8CFE0000, 32
START, END = "__native_runtime_mode_start", "__native_runtime_mode_end"
HELPER, DECODER = "_native_runtime_mode", "_kui_vmu_native_mode_decode"


def require(value, message):
    if not value:
        raise ValueError(message)


def template_mode():
    return b"KUINVM01" + struct.pack("<3I", 1, MODE_BYTES, 0) + bytes(12)


def generic_mode(wire):
    require(len(wire) == MODE_BYTES and wire == template_mode(),
            "generic native mode must be its complete canonical normal template")


class ModeHelper(Machine):
    def __init__(self, stage, seed, mask, result, *, body=None,off_mode=None):
        super().__init__(stage, HELPER, seed, mask, 0x8CFF0000, body=body)
        self.result, self.refused = result, False
        self.symbols = stage[4]
        self.background="_native_background_boot_region" in self.symbols
        from check_native_vmu_toggle_profile import stage_profile
        self.toggle=stage_profile(stage)
        self.capability_reads=[]
        self.off_mode=off_mode
        if self.toggle:
            at,wire,section=symbol_bytes(stage,'_kui_vmu_native_toggle_capability',1)
            require(section[2]&7==2,'Mode helper capability must be immutable')
            self.capability_word=at+16
            if self.off_mode is None:self.off_mode=struct.unpack_from('<I',wire,16)[0]
        self.must_refuse=(bool(result&0x80000000) or self.background and result in (1,3) or
                          result==7 and (not self.toggle or self.off_mode!=7))
        self.targets = {self.symbols[DECODER][0]: "decode", self.symbols["_stopped"][0]: "stop"}

    def read(self,address):
        if self.toggle and address==self.capability_word:
            assert self.result==7,'mode helper samples toggle capability outside reader-only admission'
            self.capability_reads.append(address)
            return self.off_mode
        return super().read(address)

    def call(self, target):
        if target == self.stop_pc:
            return False
        assert target in self.targets, "mode helper called an unreviewed decoder/terminal"
        label = self.targets[target]
        self.calls.append(label)
        if label == "decode":
            assert self.calls == ["decode"]
            assert self.r[4:6] == [self.symbols[START][0], MODE_BYTES]
            self.abi_return(self.result)
        else:
            assert self.must_refuse and self.calls == ["decode", "stop"]
            assert self.r[5] == self.result
            self.refused, self.pc = True, self.stop_pc
        return True

    def check(self):
        assert self.capability_reads==([self.capability_word] if self.toggle and self.result==7 else [])
        if self.must_refuse:
            assert self.refused and self.calls == ["decode", "stop"]
            return
        registers, pr, _, _, _, sr, fpscr, fr = self.before
        assert not self.refused and self.calls == ["decode"] and self.r[0] == self.result
        assert self.r[8:] == registers[8:] and self.pr == pr and self.sr & ~1 == sr & ~1
        assert self.fpscr == fpscr and self.fr == fr


class StorageRoute(Machine):
    """Stop normal modes at profile decode; execute the complete control exit."""
    def __init__(self, stage, seed, mask, mode, result, *, body=None,storage=0x8ce70000,selected=1):
        super().__init__(stage, "_native_persist_storage_init", seed, mask, 0x8CFF0000, body=body)
        self.symbols, self.mode, self.result = stage[4], mode, result
        from check_native_vmu_toggle_profile import stage_profile,stage_pins
        self.toggle=stage_profile(stage)
        if self.toggle:
            _,original,section=symbol_bytes(stage,'_native_persist_storage_init',2)
            require(section[2]&7==6 and (len(original),hashlib.sha256(original).hexdigest())==
                    stage_pins(stage,'toggle')['_native_persist_storage_init'],
                    'Toggle storage route lacks its exact whole-function instruction pin')
        self.mode_prefix=['mode']*(2 if self.toggle and mode!=1 else 1)
        self.ready_reads=0
        self.storage, self.selected, self.route = storage, selected, None
        self.r[4:6] = [self.storage, self.selected]
        self.before = self.r.copy(), self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr.copy()
        self.mem[self.symbols["_native_persist_ready"][0]] = 0
        self.targets = {self.symbols[name][0]: label for name, label in
            ((HELPER, "mode"), ("_kui_retail_storage_init", "storage"),
             ("_kui_vmu_persist_profile_decode", "profile"))}

    def plain(self, word, pc):
        if word & 0xF00F == 0x6000:  # mov.b @Rm,Rn, signed extension
            n, m = word >> 8 & 15, word >> 4 & 15
            address = self.r[m]
            assert address == self.symbols["_native_persist_ready"][0], "unexpected pre-profile byte access"
            self.ready_reads+=1
            value = self.mem[address] & 255
            self.r[n] = (value - 256 if value & 128 else value) & MASK
            return
        super().plain(word, pc)

    def call(self, target):
        if target == self.stop_pc:
            return False
        assert target in self.targets, "storage mode reached an unexpected external callback"
        label = self.targets[target]
        self.calls.append(label)
        if label == "mode":
            assert self.calls==self.mode_prefix[:len(self.calls)] and len(self.calls)<=len(self.mode_prefix), \
                "storage activity precedes immutable mode admission"
            self.abi_return(self.mode)
        elif label == "storage":
            assert (self.mode==1 or self.toggle and self.mode==7) and self.calls==self.mode_prefix+['storage']
            assert self.r[4:6] == [self.storage, self.selected]
            assert self.ready_reads==0,'ordinary storage bypass sampled virtual profile state'
            self.route = "reader-only" if self.mode==7 else "control"
            self.abi_return(self.result)
        else:
            assert self.mode in (0, 2, 3, 4, 5, 6) and self.calls == self.mode_prefix+['profile']
            profile_bytes=self.symbols['__vmu_sd_profile_blob_end'][0]-self.symbols['__vmu_sd_profile_blob_start'][0]
            assert profile_bytes==0x1000 and self.r[4:7]==[self.symbols['_native_persist_profile'][0],
                self.symbols['__vmu_sd_profile_blob_start'][0],profile_bytes], \
                'profile route lost its exact typed prepared-profile arguments'
            self.route, self.pc = "profile", self.stop_pc
        return True

    def check(self):
        if self.mode!=1 and not(self.toggle and self.mode==7):
            assert self.route == "profile" and self.calls == self.mode_prefix+['profile']
            return
        registers, pr, _, _, _, sr, fpscr, fr = self.before
        assert self.route==('reader-only' if self.mode==7 else 'control') and self.calls==self.mode_prefix+['storage']
        # A sibling call to ordinary init may leave ABI-volatile PR clobbered.
        # Its target/arguments, complete returned value and callee-saved state
        # remain exact; the enclosing startup caller owns its return frame.
        assert self.r[0] == self.result and self.r[8:] == registers[8:]
        assert self.sr & ~1 == sr & ~1 and self.fpscr == fpscr and self.fr == fr


class DiagnosticRoute(Machine):
    """Execute the immutable mode -> worker policy conversion, not its source."""
    def __init__(self, stage, seed, mask, mode, *, body=None):
        super().__init__(stage, "_native_runtime_diagnostics", seed, mask, 0x8CFF0000, body=body)
        self.symbols, self.mode = stage[4], mode
        from check_native_vmu_toggle_profile import stage_profile
        self.toggle=stage_profile(stage)

    def call(self, target):
        if target == self.stop_pc:
            return False
        assert target == self.symbols[HELPER][0], "worker policy called an unreviewed mode source"
        self.calls.append("mode")
        assert self.calls == ["mode"]
        self.abi_return(self.mode)
        return True

    def plain(self, word, pc):
        n, m = word >> 8 & 15, word >> 4 & 15
        if word & 0xF00F == 0x000C:  # signed byte from the actual linked switch table
            address = (self.r[0] + self.r[m]) & MASK
            tables = [(name, record) for name, record in self.symbols.items()
                      if name.startswith('_CSWTCH.') and record[2] == 1 and
                      record[0] <= address < record[0] + record[1]]
            assert len(tables) == 1, 'policy byte read leaves its typed constant table'
            name, record = tables[0]
            _, data, section = symbol_bytes(self.image, name, 1)
            assert section[2] & 7 == 2, 'policy table is writable or executable'
            value = data[address - record[0]]
            self.r[n] = (value - 256 if value & 128 else value) & MASK
            return
        if word & 0xF00F == 0x600C:
            self.r[n] = self.r[m] & 255
            return
        super().plain(word, pc)

    def check(self):
        registers, pr, _, _, _, sr, fpscr, fr = self.before
        expected={2:1,4:2,5:3,6:4,**({7:5} if self.toggle else {})}.get(self.mode,0)
        assert self.calls == ["mode"] and self.r[0] == expected
        assert self.r[8:] == registers[8:] and self.pr == pr
        assert self.sr & ~1 == sr & ~1 and self.fpscr == fpscr and self.fr == fr


def instruction_routes(stage, *, self_test=False):
    cases = storage = diagnostics = reader_only_storage = 0
    background="_native_background_boot_region" in stage[4]
    from check_native_vmu_toggle_profile import stage_profile
    toggle=stage_profile(stage)
    modes=(0,2,4,5,6) if background else (0,1,2,3,4,5,6)
    if toggle:modes+= (7,)
    for seed in range(12):
        for mask in range(16):
            for result in (0, 1, 2, 3, 4, 5, 6,*((7,) if toggle else ()),MASK):
                ModeHelper(stage, seed, mask, result).run().check(); cases += 1
            for mode in modes:
                DiagnosticRoute(stage, seed, mask, mode).run().check(); diagnostics += 1
                for result in (0, 1, MASK):
                    StorageRoute(stage, seed, mask, mode, result).run().check(); storage += 1
                    if mode==7:reader_only_storage+=1
    capability_cases=ordinary_argument_cases=0
    if toggle:
        for off_mode in (0,1,6,7,8,MASK):
            ModeHelper(stage,7,15,7,off_mode=off_mode).run().check();capability_cases+=1
        for address in (0x8ce70000,0x8ce80000):
            for selected in (0,1,2,3,MASK):
                for result in (0,1,MASK):
                    StorageRoute(stage,7,15,7,result,storage=address,selected=selected).run().check()
                    ordinary_argument_cases+=1
    mutations = 0
    if self_test:
        first_mutations=[
            (HELPER, DECODER, lambda body: ModeHelper(stage, 7, 15, MASK, body=body)),
            (HELPER, "_stopped", lambda body: ModeHelper(stage, 7, 15, MASK, body=body))]
        if not background:first_mutations.extend((
            ("_native_persist_storage_init", HELPER, lambda body: StorageRoute(stage, 7, 15, 1, 0, body=body)),
            ("_native_persist_storage_init", "_kui_retail_storage_init", lambda body: StorageRoute(stage, 7, 15, 1, 0, body=body))))
        for symbol,target_name,factory in first_mutations:
            _, body, _ = symbol_bytes(stage, symbol, 2)
            at = body.find(struct.pack("<I", stage[4][target_name][0]))
            require(at >= 0, "mode gate lacks its exact callback literal: " + target_name)
            changed = bytearray(body); struct.pack_into("<I", changed, at, STAGE + 2)
            try:
                factory(bytes(changed)).run().check()
            except (AssertionError, ValueError, struct.error, KeyError):
                mutations += 1
            else:
                raise ValueError("mode gate accepted a changed decoder/storage/terminal")
        # Swapping CONTROL and PROFILE in either exact first selector must
        # fail even though both values are canonical and every target is a
        # real admitted function. This independently tests the route's
        # meaning, rather than merely its callback addresses.
        control_mutations=() if background else (
            ("_native_persist_storage_init", lambda body: StorageRoute(stage, 7, 15, 1, 0, body=body)),
            ("_kui_retail_stage_relay", lambda body: CacheRoute(stage, 7, 15, True, mode=1, blob=body)))
        for symbol, factory in control_mutations:
            _, body, _ = symbol_bytes(stage, symbol, 2)
            words = struct.unpack("<" + "H" * (len(body) // 2), body)
            selector = next((i for i, op in enumerate(words) if op == 0x8801), None)
            require(selector is not None, "runtime mode route lacks its exact CONTROL comparison")
            changed = bytearray(body); struct.pack_into("<H", changed, selector * 2, 0x8802)
            try:
                factory(bytes(changed)).run().check()
            except (AssertionError, ValueError, struct.error, KeyError):
                mutations += 1
            else:
                raise ValueError("runtime route accepted CONTROL/PROFILE confusion")
        if background:
            # Native Background must retain the 192KiB live reservation.
            # Its real mode helper refuses CONTROL1 and LEASE_ONLY3 before
            # returning to either storage preparation or the relay. Mutate
            # each selector instruction and execute every canonical result.
            _,body,_=symbol_bytes(stage,HELPER,2)
            for opcode in (0xe0fd,0x2059,0x8801):
                at=body.find(struct.pack('<H',opcode))
                require(at>=0,'Background mode helper lacks exact live-reservation selector')
                changed=bytearray(body);struct.pack_into('<H',changed,at,opcode^1)
                try:
                    for mode in (0,1,2,3,4,5,6,MASK):
                        ModeHelper(stage,7,15,mode,body=bytes(changed)).run().check()
                except (AssertionError,ValueError,struct.error,KeyError):mutations+=1
                else:raise ValueError('Background mode helper accepted changed reservation selector')
        _, policy, _ = symbol_bytes(stage, "_native_runtime_diagnostics", 2)
        edits = []
        at = policy.find(struct.pack("<I", stage[4][HELPER][0]))
        require(at >= 0, "worker policy lacks its exact immutable mode callback")
        changed = bytearray(policy); struct.pack_into("<I", changed, at, STAGE + 2)
        edits.append(bytes(changed))
        # GCC lowers the five-element policy to a bounded byte table.
        # Mutate the exact bounds/index arithmetic, then execute all modes.
        policy_bound=0xe205 if toggle else 0xe204
        for immediate in (policy_bound, 0x71FE, 0x3126):
            at = policy.find(struct.pack("<H", immediate))
            require(at >= 0, "worker policy lacks exact canonical mode comparison")
            # Narrow4 to3 so the mutation excludes reachable trace mode6.
            # Widening4 to5 would change only unreachable decoder results.
            changed = bytearray(policy)
            struct.pack_into("<H", changed, at,
                             0xe204 if toggle and immediate==policy_bound else
                             immediate ^ (7 if immediate == 0xE204 else 1))
            edits.append(bytes(changed))
        for changed in edits:
            try:
                for mode in (0, 1, 2, 3, 4, 5, 6,*((7,) if toggle else ())):
                    DiagnosticRoute(stage, 7, 15, mode, body=changed).run().check()
            except (AssertionError, ValueError, struct.error, KeyError):
                mutations += 1
            else:
                raise ValueError("worker policy accepted changed selector/callback")
        if toggle:
            _,storage_body,_=symbol_bytes(stage,'_native_persist_storage_init',2)
            # These are reviewed offsets in the exact pinned toggle body.
            # Each counterfactual still executes all real argument and return
            # instructions, with hostile ABI callbacks and bounded writes.
            for offset,expected,replacement in (
                    (0x18,0x490b,0x4e0b),(0x1c,0x8801,0x8802),
                    (0x3a,0x490b,0x0009),(0x3a,0x490b,0x4e0b),
                    (0x3e,0x8807,0x8806),(0x42,0x65b3,0xe502),
                    (0x22,0x6483,0x6493),(0x36,0x412b,0x0009)):
                assert struct.unpack_from('<H',storage_body,offset)[0]==expected, \
                    'toggle storage mutation no longer names its reviewed instruction'
                changed=bytearray(storage_body);struct.pack_into('<H',changed,offset,replacement)
                try:
                    for mode in (0,2,4,5,6,7):
                        for result in (0,1,MASK):
                            StorageRoute(stage,7,15,mode,result,body=bytes(changed)).run().check()
                except (AssertionError,ValueError,struct.error,KeyError):mutations+=1
                else:raise ValueError('Toggle storage route accepted altered call order/mode/ABI')
            _,helper,_=symbol_bytes(stage,HELPER,2)
            selectors=[offset for offset in range(0,len(helper),2)
                       if struct.unpack_from('<H',helper,offset)[0]==0x8807]
            require(len(selectors)==2,'Toggle mode helper lacks both exact reader-only/capability comparisons')
            for offset in selectors:
                changed=bytearray(helper);struct.pack_into('<H',changed,offset,0x8806)
                try:
                    for mode in (0,2,4,5,6,7):
                        for off_mode in (6,7):
                            ModeHelper(stage,7,15,mode,body=bytes(changed),off_mode=off_mode).run().check()
                except (AssertionError,ValueError,struct.error,KeyError):mutations+=1
                else:raise ValueError('Toggle helper accepted altered reader-only/capability comparison')
    return {"mode_helper_instruction_cases": cases, "storage_instruction_cases": storage,
            "worker_policy_instruction_cases": diagnostics,
            "mutation_rejections": mutations, "malformed_mode_stops_before_storage": True,
            "control_storage_bypasses_profile_acquire_recovery_and_writes": not background,
            "background_refuses_control_and_lease_only_before_storage":background,
            "reader_only_toggle_profile":toggle,
            "reader_only_storage_cases":reader_only_storage,
            "reader_only_capability_cases":capability_cases,
            "reader_only_ordinary_argument_cases":ordinary_argument_cases,
            "reader_only_bypasses_profile_acquire_recovery_and_writes":toggle,
            "reader_only_no_virtual_profile_state_reads":toggle,
            "reader_only_worker_policy":5 if toggle else None,
            "typed_profile_arguments_checked":True}


def audit(stage_data, entry_data, module, *, cached_code, stage_image=None, resident_image=None, self_test=False):
    require(type(cached_code) is bool, "mode audit requires an explicit worker alias policy")
    stage = inspect_elf(stage_data, STAGE, LIMIT)
    symbols = stage["symbols"]
    present = {name for name in (START, END) if name in symbols}
    if not present:
        require(HELPER not in symbols and DECODER not in symbols and stage["payload"].find(template_mode()) < 0,
                "runtime mode implementation/template lacks exact typed exports")
        return {"enabled": False}
    require(present == {START, END} and cached_code, "runtime mode requires both exports and cached native code")
    require(HELPER in symbols and DECODER in symbols, "runtime mode lacks its bounded helper/decoder")
    typed = _linked_symbols(stage_data, {START, END, HELPER, DECODER})
    address, size, kind, _ = typed[START]
    require(kind == 1 and size == MODE_BYTES, "runtime mode must be one sized typed32B object")
    for name in (HELPER, DECODER):
        function, count, function_kind, section = typed[name]
        require(function_kind == 2 and count and section is not None and section[1] == 1 and
                section[2] & 7 == 6 and STAGE <= function < function + count <= LIMIT and
                section[3] <= function < function + count <= section[3] + section[5],
                "runtime mode requires its bounded executable helper/decoder: " + name)
    bound, file_offset = readonly_blob(stage_data, stage, START, END, MODE_BYTES)
    require(bound == address, "typed runtime mode binding differs")
    wire = region(stage_data, file_offset, MODE_BYTES, "typed native mode")
    generic_mode(wire)
    offset = address - STAGE
    require(stage["payload"].find(wire) == offset and stage["payload"].find(wire, offset + 1) < 0,
            "stage must contain exactly its unique typed normal mode")
    spans = (("__vmu_sd_profile_blob_start", "__vmu_sd_profile_blob_end"),
             ("__native_resolution_cache_seed_start", "__native_resolution_cache_seed_end"),
             ("__native_worker_blob_begin", "__native_worker_blob_end"),
             ("__native_worker_reloc_begin", "__native_worker_reloc_end"),
             ("__retail_trampoline_start", "__retail_trampoline_end"))
    for first, last in spans:
        require(first in symbols and last in symbols and symbols[first] < symbols[last],
                "runtime mode lacks bounded protected span: " + first)
        require(address + MODE_BYTES <= symbols[first] or symbols[last] <= address,
                "runtime mode overlaps profile/cache/worker/relocation/trampoline")
    entry = inspect_elf(entry_data, ADDRESS, 0x8C112000)
    begin, entry_offset = readonly_blob(entry_data, entry, "__retail_stage_blob_start", "__retail_stage_blob_end", len(stage["payload"]))
    require(region(entry_data, entry_offset, len(stage["payload"]), "entry stage") == stage["payload"],
            "entry embeds a different runtime-mode stage")
    verify(module)
    require(module[64:] == entry["payload"], "module differs from runtime-mode entry")
    module_offset = 64 + begin - ADDRESS + offset
    require(region(module, module_offset, MODE_BYTES, "module runtime mode") == wire and
            module.find(wire) == module_offset and module.find(wire, module_offset + 1) < 0,
            "module must contain exactly its typed unique normal mode")
    report = {"enabled": True, "module_offset": module_offset, "bytes": MODE_BYTES,
              "template_sha256": hashlib.sha256(wire).hexdigest(), "address": hex(address),
              "typed_immutable_unique_object": True, "profile_cache_worker_relocation_trampoline_disjoint": True,
              "modes": {"normal": 0, "control": 1, "profile": 2, "lease-only": 3, "hooks-only": 4, "raw-chain": 5, "evolution-trace": 6}}
    if stage_image is not None:
        background="_native_background_boot_region" in stage_image[4]
        require(background==bool(resident_image is not None and
                '_kui_vmu_native_background_manifest_bind' in resident_image[4]),
                'Runtime mode stage/reader profile mismatch')
        report.update(instruction_routes(stage_image, self_test=self_test))
        if background:
            report['modes']={name:value for name,value in report['modes'].items() if value not in (1,3)}
            if report.get('reader_only_toggle_profile'):report['modes']['reader-only']=7
            report['lease_only']={'enabled':False,'refused_by_exact_mode_helper':True}
        else:
            from check_vmu_native_lease_only import audit as audit_lease_only
            report["lease_only"] = audit_lease_only(stage_image, resident_image, self_test=self_test)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path); parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(); directory = args.directory
    try:
        report = audit((directory / "stage.elf").read_bytes(), (directory / "entry.elf").read_bytes(),
            (directory / "retail-maple-observe.kui").read_bytes(), cached_code=True,
            stage_image=read_elf(directory / "stage.elf"),
            resident_image=read_elf(directory / "resident-sci.elf"), self_test=args.self_test)
    except (OSError, ValueError, AssertionError, KeyError, struct.error) as error:
        parser.exit(2, "Native runtime mode audit refused: " + str(error) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
