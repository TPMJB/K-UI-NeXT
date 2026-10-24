#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked reservation-only retirement path with hostile ABI callees.

This checks control flow and the complete store set. Allocator ownership and
worker arm retain their independent C/SH proofs; successful arm is a precondition.
"""
import struct

from check_vmu_native_stage_stack import Machine, canonical
from check_vmu_native_worker_gateway import MASK, symbol_bytes

HELPER = "_native_passive_lease_only"
SDK_INSTALL = "_native_sdk_coherence_install"


def require(value, message):
    if not value:
        raise ValueError(message)


class LeaseOnly(Machine):
    def __init__(self, stage, resident, seed, mask, slot_alias, target_alias, outcome="ok", *, body=None):
        super().__init__(stage, HELPER, seed, mask, 0x8CEA0000, body=body)
        self.symbols, self.outcome = stage[4], outcome
        self.control = resident[4]["_kui_vmu_observe_boot_control"][0]
        self.lease, self.exports = 0x8CE90000, 0xACFD0000
        self.slot = slot_alias | 0x0C04EDF0
        self.target = target_alias | 0x0C1633A8
        self.consumer, self.completion = 0x8C1DAEF6, 0x8C1DAF38
        self.end = 0xACFF0000
        self.r[4:6] = [self.lease, self.exports]
        self.before = self.r.copy(), self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr.copy()
        self.stores, self.store_sites, self.refused, self.lines, self.readback = [], [], False, [], False
        heap, driver, exits = (self.symbols[s][0] for s in ("_native_heap", "_native_driver", "_native_exits"))
        self.exit_entries = (0x8C165730, 0x8C165760, 0x8C165790)[:2 + (seed & 1)]
        self.mem.update({heap + 28: self.slot, heap + 32: self.target,
                         heap + 24: 0x8C1633A8, driver + 48: self.completion,
                         driver + 56: self.consumer, self.completion: self.consumer,
                         self.lease + 104: self.exports, canonical(self.exports) + 44: self.end,
                         exits + 20: len(self.exit_entries), canonical(self.slot): 0x8C006AA0})
        self.mem.update({exits + 24 + 4 * i: entry for i, entry in enumerate(self.exit_entries)})
        self.initial_control = {self.control + 4 * i: (0xA5900000 + i) for i in range(14)}
        self.mem.update(self.initial_control)
        self.targets = {self.symbols[s][0]: label for s, label in (
            ("_memcmp", "window"), ("_kui_vmu_native_heap_lease_valid", "lease"),
            ("_native_report", "stop"), ("_retail_display_line", "line"))}

    def read(self, address):
        if canonical(address) == canonical(self.slot):
            require(address == (canonical(self.slot) | 0x20000000), "initializer readback is not P2")
            require(self.published == [canonical(self.slot) & ~31], "initializer read precedes exact publication")
            self.readback = True
            if self.outcome == "readback":
                return self.target ^ 4
        if address == self.completion and self.outcome == "completion":
            return self.consumer ^ 4
        return super().read(address)

    def write(self, address, value):
        if self.start_sp - 512 <= address < self.start_sp:
            return super().write(address, value)
        key, value = canonical(address), value & MASK
        if key == canonical(self.slot):
            require(address == canonical(self.slot) and not self.stores and value == self.target,
                    "initializer restoration changed the raw original target or P1 write alias")
        else:
            require(address == self.control + 0x20000000 + (key - self.control), "low control store is not P2")
            require(self.control <= key < self.control + 56 and self.readback and "lease" in self.calls,
                    "reservation publication precedes its final proofs or escapes low control")
        self.stores.append((key, value)); self.store_sites.append(self.pc); self.mem[key] = value

    def string(self, address):
        for p in self.image[2]:
            if p[0] == 1 and p[2] <= address < p[2] + p[4]:
                data = self.image[0][p[1] + address - p[2]:p[1] + p[4]]
                return data.split(b"\0", 1)[0].decode("ascii")
        raise AssertionError("banner escapes linked image")

    def call(self, target):
        if target == self.stop_pc or self.base <= target < self.base + len(self.blob):
            return False
        require(target in self.targets, "lease-only path called an unreviewed worker/storage function: " + hex(target))
        label = self.targets[target]; self.calls.append(label)
        if label == "window":
            i = self.calls.count("window") - 1
            require(i < len(self.exit_entries) and self.r[4:7] ==
                    [self.exit_entries[i], self.symbols["_persistent_exit_windows"][0] + 12 * i, 12],
                    "lease-only exit window admission changed")
            self.abi_return(int(self.outcome == "window"))
        elif label == "lease":
            require(self.readback and self.r[4:8] ==
                    [self.lease, self.symbols["_native_read"][0], 0, 1], "lease proof ABI changed")
            self.abi_return(int(self.outcome != "lease"))
        elif label == "line":
            self.lines.append(self.string(self.r[4])); self.abi_return()
        else:
            require(self.r[4:6] == [9, 7 if self.outcome == "window" else 9], "wrong refusal diagnostic")
            self.refused, self.pc = True, self.stop_pc
        return True

    def check(self):
        if self.outcome != "ok":
            expected = [] if self.outcome == "window" else [(canonical(self.slot), self.target)]
            require(self.refused and self.stores == expected and not self.lines,
                    "failed reservation retirement published live state")
            return
        expected = [(canonical(self.slot), self.target), (self.control + 12, 0x0CFD0000),
                    (self.control + 32, self.end), (self.control + 40, 0), (self.control + 52, 0),
                    (self.control, 0), (self.control + 4, 0), (self.control + 8, 4)]
        require(not self.refused and self.stores == expected,
                "lease-only store set changed: callbacks must be null, installed must be last")
        require(self.lines == ["RAM RESERVATION ONLY / NO VIRTUAL VMU",
                              "NO IN-GAME SAVE WRITES - STARTING GAME"], "lease-only banner overstates storage policy")
        registers, _, _, _, _, sr, fpscr, fr = self.before
        # The second banner is a sibling call: it returns directly to the
        # original caller, while ABI-volatile PR may remain callee-clobbered.
        require(self.r[8:] == registers[8:] and self.pc == self.stop_pc and self.sr & ~1 == sr & ~1 and
                self.fpscr == fpscr and self.fr == fr, "lease-only retirement changed owner ABI state")
        changed = {key for key, _ in self.stores}
        require(all(self.mem[key] == value for key, value in self.initial_control.items() if key not in changed),
                "lease-only retirement changed an unrelated owner frame field")


class SdkInstall(Machine):
    """Execute the linked installer after independent window/slot admission.

    Only those two game-image predicates and the separately audited cache
    publisher are ABI callbacks. The actual linked helper and passive-entry
    leaf execute every descriptor/state check, slot store and return value.
    """
    def __init__(self, stage, seed, mask, kind, outcome="ok", *, body=None):
        super().__init__(stage, SDK_INSTALL, seed, mask, 0x8CEA0000, body=body)
        self.symbols, self.kind, self.outcome = stage[4], kind, outcome
        self.exports = 0xACFD0000
        self.bytes = self.symbols["__native_worker_blob_end"][0] - self.symbols["__native_worker_blob_begin"][0]
        self.table, self.state = self.exports + 0x80, self.exports + self.bytes
        self.slot, self.initializer, self.lock = 0x8C040100, 0x8C1DA8CC, 0x8C1DC538
        self.target = self.exports + (0x600 if kind else 0x500)
        self.stores, self.publications = [], []
        self.r[4:7] = [self.exports, self.exports, self.bytes]
        self.before = self.r.copy(), self.pr, self.sr, self.fpscr, self.fr.copy()
        table = [0x4B545231, 2, 40, *(0x8CFD0100 + 0x100*i for i in range(4)),
                 self.exports + 0x500, self.exports + 0x600, self.state]
        state = [self.lock, 0x8C1DC534, 0x8C304500, self.initializer+12,
                 0, 0, 0, 0x4E434F31, (self.lock+4)|0xA0000000, kind, self.initializer, 0]
        if outcome.startswith("table:"):table[int(outcome.split(":")[1])] ^= 1
        if outcome.startswith("state:"):state[int(outcome.split(":")[1])] ^= 1
        self.mem.update({canonical(self.exports)+28:self.state, canonical(self.exports)+32:self.state+0x100,
                         canonical(self.exports)+36:self.exports+0x1F000,
                         canonical(self.exports)+76:self.table, canonical(self.slot):self.initializer})
        self.mem.update({canonical(self.table)+4*i:value for i,value in enumerate(table)})
        self.mem.update({canonical(self.state)+4*i:value for i,value in enumerate(state)})
        driver, layout = (self.symbols[name][0] for name in ("_native_driver", "_passive_layout"))
        self.mem.update({driver+36:0x8C1DC534,driver+40:0x8C304500, driver+44:self.lock,
                         layout+8:self.initializer, layout+24:kind})
        self.entry, self.entry_body, _ = symbol_bytes(stage, "_passive_entry", 2)
        self.targets = {self.symbols[name][0]:label for name,label in (
            ("_native_sdk_coherence_window_valid","window"),
            ("_native_sdk_coherence_slot","slot"),
            ("_kui_native_passive_publish","publish"))}

    def code(self, address, size):
        if self.entry <= address < address+size <= self.entry+len(self.entry_body):
            return self.entry_body[address-self.entry:address-self.entry+size]
        return super().code(address,size)

    def read(self, address):
        if canonical(address)==canonical(self.slot):
            require(address==self.slot+0x20000000 and self.publications==[(self.slot,self.slot+4)],
                    "SDK slot readback precedes exact publication or is not P2")
            if self.outcome=="readback":return self.initializer
        return super().read(address)

    def write(self, address, value):
        if self.start_sp-512<=address<self.start_sp:return super().write(address,value)
        require(address==self.slot and value==self.target and not self.stores and
                self.calls==["window","slot"], "SDK installer writes outside its one admitted call-target word")
        self.stores.append((address,value));self.mem[canonical(address)]=value&MASK

    def plain(self, word, pc):
        n,m=word>>8&15,word>>4&15
        if word&0xF00F==0x600D:self.r[n]=self.r[m]&0xffff;return
        if word&0xF00F==0x600A:
            value=-self.r[m]-(self.sr&1);self.r[n]=value&MASK;self.t(value<0);return
        if word&0xF00F==0x300A:
            value=self.r[n]-self.r[m]-(self.sr&1);self.r[n]=value&MASK;self.t(value<0);return
        if word&0xF00F==0x200A:self.r[n]^=self.r[m];return
        super().plain(word,pc)

    def call(self, target):
        if target==self.stop_pc or self.base<=target<self.base+len(self.blob) or \
                self.entry<=target<self.entry+len(self.entry_body):return False
        require(target in self.targets,"SDK installer reached an unreviewed callback: "+hex(target))
        label=self.targets[target];self.calls.append(label)
        if label=="window":
            require(self.calls==["window"],"SDK installer changed window-admission order")
            self.abi_return(int(self.outcome!="window"))
        elif label=="slot":
            require(self.calls==["window","slot"] and
                    self.start_sp-512<=self.r[4]<self.start_sp and not self.r[4]&3,
                    "SDK installer changed admitted-slot output ABI")
            self.mem[canonical(self.r[4])]=self.slot
            self.abi_return(int(self.outcome!="slot"))
        else:
            require(self.calls==["window","slot","publish"] and self.r[4:6]==[self.slot,self.slot+4] and
                    self.stores==[(self.slot,self.target)],"SDK installer changed its single-slot publisher ABI")
            self.publications.append(tuple(self.r[4:6]));self.abi_return()
        return True

    def check(self):
        expected=1 if self.outcome=="ok" else MASK-1 if self.outcome=="readback" else MASK
        require(self.r[0]==expected,"linked SDK installer changed its measured success/refusal return")
        written=self.outcome in ("ok","readback")
        require(self.stores==([(self.slot,self.target)] if written else []) and
                self.publications==([(self.slot,self.slot+4)] if written else []),
                "SDK installer published a refused descriptor/state or changed its store set")
        registers,pr,sr,fpscr,fr=self.before
        require(self.r[8:]==registers[8:] and self.pr==pr and self.sr&~1==sr&~1 and
                self.fpscr==fpscr and self.fr==fr,"SDK installer changed owner callee-saved state")
        return self.r[0]


def sdk_install_returns(stage,self_test=False):
    cases=0;results={}
    outcomes=("ok","window","slot","readback",*("table:"+str(i) for i in range(10)),
              *("state:"+str(i) for i in range(12)))
    for seed in range(4):
        for mask in (0,6,15):
            for kind in (0,1):
                for outcome in outcomes:
                    value=SdkInstall(stage,seed,mask,kind,outcome).run().check();cases+=1
                    if outcome in ("ok","window","readback"):results[outcome]=value
    rejected=0
    if self_test:
        _,body,_=symbol_bytes(stage,SDK_INSTALL,2)
        mutations=[]
        # Remove the sole game-data store, change its target, or bypass the
        # two independently admitted image predicates/publication callback.
        for name in ("_native_sdk_coherence_window_valid","_native_sdk_coherence_slot","_kui_native_passive_publish"):
            raw=struct.pack("<I",stage[4][name][0]);require(body.count(raw)==1,"ambiguous SDK installer callback")
            changed=bytearray(body);struct.pack_into("<I",changed,body.index(raw),stage[4][SDK_INSTALL][0]+2)
            mutations.append(bytes(changed))
        witness=SdkInstall(stage,7,15,0).run();witness.check()
        # The compiled store is the unique MOV.L R2,@R1; both the readback
        # and observed return must still depend on this actual instruction.
        raw=struct.pack("<H",0x2122);require(body.count(raw)==1,"ambiguous SDK call-target store")
        changed=bytearray(body);struct.pack_into("<H",changed,body.index(raw),0x0009);mutations.append(bytes(changed))
        for changed in mutations:
            try:SdkInstall(stage,7,15,0,body=changed).run().check()
            except (AssertionError,ValueError,KeyError,struct.error):rejected+=1
            else:raise ValueError("SDK installer model accepted a changed slot store/callback")
    return results,{"instruction_cases":cases,"mutation_rejections":rejected,
        "actual_linked_helper_and_entry_leaf":True,"game_window_and_slot_admission_are_preconditions":True,
        "publication_has_separate_actual_assembly_proof":True,"success_return":results["ok"],
        "admission_refusal_return":results["window"],"readback_refusal_return":results["readback"],
        "one_call_target_word_only":True,"descriptor_and_all_twelve_state_words_checked":True}


class LeaseDispatch(Machine):
    """Execute the linked mode selector through return or the first veneer."""
    def __init__(self, stage, resident, seed, mask, mode, install_result=1, *, body=None):
        super().__init__(stage, "_native_passive_run", seed, mask, 0x8CEA0000, body=body)
        self.symbols, self.mode, self.route = stage[4], mode, None
        self.install_result=install_result
        words = struct.unpack("<" + "H" * (len(self.blob) // 2), self.blob)
        candidates = []
        for i in range(len(words) - 21):
            if words[i] & 0xFF00 != 0xD000 or words[i + 1:i + 4] != (0x400B, 0x0009, 0x8803):
                continue
            at = ((self.base + 2 * i + 4) & ~3) + 4 * (words[i] & 255)
            if self.read(at) == self.symbols["_native_runtime_mode"][0]:
                candidates.append(i)
        require(len(candidates) == 1, "missing unique linked lease-only selector")
        i = candidates[0]
        self.selector_offset = 2 * i
        lease_offset = 36 if '_native_evolution_trace_install' in self.symbols else 32
        require(words[i + 4] >> 8 == 0x8F and words[i + 5] == 0x6583 and
                words[i + 6] & 0xFF00 == 0xD100 and words[i + 7:i + 9] == (0x410B, 0x54F0 + lease_offset // 4) and
                words[i + 9] & 0xFF00 == 0x9700 and words[i + 10:i + 12] == (0x3F7C, 0x4F26) and
                words[i + 12:i + 20] == (0x6EF6, 0x6DF6, 0x6CF6, 0x6BF6, 0x6AF6, 0x69F6,
                                          0x000B, 0x68F6),
                "lease-only selector no longer returns through the original owner epilogue")
        at = ((self.base + 2 * (i + 6) + 4) & ~3) + 4 * (words[i + 6] & 255)
        require(self.read(at) == self.symbols[HELPER][0], "lease-only selector targets another helper")
        local_at = self.base + 2 * (i + 9) + 4 + 2 * (words[i + 9] & 255)
        local_bytes = struct.unpack("<h", self.code(local_at, 2))[0]
        require(0 < local_bytes <= 4096 and not local_bytes & 3, "unbounded installer frame")
        self.owner = self.r.copy()
        self.lease, self.exports = 0x8CE90000, 0xACFD0000
        self.pc = self.base + 2 * i
        self.mem[self.start_sp + lease_offset] = self.lease
        self.mem[self.start_sp + local_bytes] = self.stop_pc
        for n, reg in enumerate(range(14, 7, -1), 1):
            self.mem[self.start_sp + local_bytes + 4 * n] = self.owner[reg]
        self.expected_sp = self.start_sp + local_bytes + 32
        self.r[8], self.r[11] = self.exports, self.symbols["_native_heap"][0]
        self.bytes=self.symbols["__native_worker_blob_end"][0]-self.symbols["__native_worker_blob_begin"][0]
        self.r[9]=self.bytes
        self.mem[self.r[11] + 24] = 0x8C1633A8
        self.return_hook = resident[4]["_kui_vmu_observe_return_hook"][0]
        self.targets = {self.symbols[s][0]: label for s, label in (
            ("_native_runtime_mode", "mode"), (HELPER, "lease-only"), (SDK_INSTALL,"sdk-install"),
            ("_native_report","refusal"),("_readonly_patch_entry", "veneer"))}

    def call(self, target):
        if target == self.stop_pc or self.base <= target < self.base + len(self.blob):
            return False
        require(target in self.targets, "reservation selector reached an unexpected callback")
        label = self.targets[target]; self.calls.append(label)
        if label == "mode":
            require(self.calls == ["mode"], "retail callback precedes selector")
            self.abi_return(self.mode)
        elif label == "lease-only":
            require(self.mode == 3 and self.calls == ["mode", "lease-only"] and
                    self.r[4:6] == [self.lease, self.exports], "wrong reservation branch or ABI")
            self.route = "lease-only"; self.abi_return()
        elif label=="sdk-install":
            require(self.mode in (0,1,2,4,5,6) and self.calls==["mode","sdk-install"] and
                    self.r[4:7]==[self.exports,self.exports,self.bytes],"wrong SDK cold installer branch or ABI")
            self.abi_return(self.install_result)
        elif label=="refusal":
            require(self.install_result!=1 and self.calls==["mode","sdk-install","refusal"] and
                    self.r[4:6]==[9,0x47],"SDK installer refusal bypassed its exact terminal")
            self.route,self.pc="refusal",self.stop_pc
        else:
            require(self.mode in (0, 1, 2, 4, 5, 6) and self.install_result==1 and
                    self.calls == ["mode", "sdk-install", "veneer"] and
                    self.r[4:6] == [0x8C1633A8, self.return_hook], "ordinary mode lost its original veneer route")
            self.route, self.pc = "veneer", self.stop_pc
        return True

    def check(self):
        if self.mode == 3:
            require(self.route == "lease-only" and self.calls == ["mode", "lease-only"] and
                    self.r[8:15] == self.owner[8:15] and self.r[15] == self.expected_sp and self.pr == self.stop_pc,
                    "lease-only continued into veneers or failed to restore the installer owner frame")
        else:
            require(self.route == ("veneer" if self.install_result==1 else "refusal"),
                    "ordinary mode bypassed its proved SDK installer result")


def audit(stage, resident, *, self_test=False):
    require(HELPER in stage[4], "runtime mode 3 lacks its exact reservation-only helper")
    require(resident is not None, "lease-only model requires typed resident control")
    control, count, kind, _ = resident[4]["_kui_vmu_observe_boot_control"]
    require(count == 56 and kind == 1 and 0x8C006A00 <= control < control + count <= 0x8C007800,
            "lease-only control is not the exact bounded low object")
    install_results,install_proof=sdk_install_returns(stage,self_test)
    cases = dispatch_cases = 0
    for seed in range(4):
        for mask in (0, 6, 15):
            for slot_alias in (0, 0x80000000, 0xA0000000):
                for target_alias in (0, 0x80000000, 0xA0000000):
                    for outcome in ("ok", "window", "readback", "lease", "completion"):
                        LeaseOnly(stage, resident, seed, mask, slot_alias, target_alias, outcome).run().check()
                        cases += 1
            for mode in (0, 1, 2, 3, 4, 5, 6):
                for result in ([install_results["ok"]] if mode==3 else sorted(set(install_results.values()))):
                    LeaseDispatch(stage, resident, seed, mask, mode,result).run().check(); dispatch_cases += 1
    rejected = 0
    if self_test:
        _, body, _ = symbol_bytes(stage, HELPER, 2)
        mutations = []
        # Each actual external store is independently necessary: removing
        # initializer restoration or callback retirement must fail the model.
        witness = LeaseOnly(stage, resident, 7, 15, 0xA0000000, 0xA0000000).run()
        witness.check()
        for pc in sorted(set(witness.store_sites)):
            changed = bytearray(body); struct.pack_into("<H", changed, pc - witness.base, 0x0009)
            mutations.append((bytes(changed), "ok"))
        for name in ("_kui_vmu_native_heap_lease_valid", "_native_report", "_retail_display_line"):
            at = body.find(struct.pack("<I", stage[4][name][0])); require(at >= 0, "missing helper callback literal")
            changed = bytearray(body); struct.pack_into("<I", changed, at, stage[4][HELPER][0] + 2)
            mutations.append((bytes(changed), "lease" if name == "_native_report" else "ok"))
        # Change each initializer target load to the normalized code pointer;
        # the raw P2 original pointer must remain exact, including its alias.
        words = struct.unpack("<" + "H" * (len(body) // 2), body)
        for i, word in enumerate(words):
            if word >> 12 == 5 and word & 15 == 8:
                changed = bytearray(body); struct.pack_into("<H", changed, 2 * i, (word & ~15) | 6)
                mutations.append((bytes(changed), "ok"))
            if word in (0xE200, 0xE204):
                changed = bytearray(body); struct.pack_into("<H", changed, 2 * i, word ^ 1)
                mutations.append((bytes(changed), "ok"))
        for body, outcome in mutations:
            try:
                LeaseOnly(stage, resident, 7, 15, 0xA0000000, 0xA0000000, outcome, body=body).run().check()
            except (AssertionError, ValueError, KeyError, struct.error):
                rejected += 1
            else:
                raise ValueError("lease-only model accepted changed callback/raw target")
        _, body, _ = symbol_bytes(stage, "_native_passive_run", 2)
        # Canonical modes must retain their meanings, not merely target valid
        # functions. A PROFILE/LEASE_ONLY selector swap must be rejected.
        at = LeaseDispatch(stage, resident, 7, 15, 3).selector_offset + 6
        changed = bytearray(body); struct.pack_into("<H", changed, at, 0x8802)
        try:
            LeaseDispatch(stage, resident, 7, 15, 3, body=bytes(changed)).run().check()
        except (AssertionError, ValueError, KeyError, struct.error):
            rejected += 1
        else:
            raise ValueError("reservation selector accepted PROFILE/LEASE_ONLY confusion")
    return {"instruction_cases": cases, "selector_instruction_cases": dispatch_cases, "mutation_rejections": rejected,
            "exact_raw_initializer_target_restored_through_p1_and_verified_p2": True,
            "reservation_reproved_before_installed": True, "terminal_and_sd_callbacks_null": True,
            "initializer_slot_and_seven_low_control_words_only": True,
            "no_retail_code_or_save_writes": True, "installed_status_written_last": True,
            "mode_three_returns_before_first_retail_veneer": True,
            "mode_three_returns_before_sdk_call_target_install":True,
            "sdk_call_target_installation":install_proof,
            "owner_callee_saved_registers_sr_fpscr_unchanged": True}
