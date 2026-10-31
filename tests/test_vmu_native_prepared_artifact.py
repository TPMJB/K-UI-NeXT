# SPDX-License-Identifier: GPL-3.0-only
"""Mutation checks for the actual linked prepared native worker artifact."""
import copy
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import check_vmu_native_persistent as audit


class PreparedArtifact(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = Path(os.environ.get("KUI_NATIVE_ARTIFACT_DIR", ROOT / "build/native-free-cost-dev"))
        if not (cls.directory / "native-worker.elf").exists():
            raise unittest.SkipTest("build the prepared native worker or set KUI_NATIVE_ARTIFACT_DIR")
        cls.worker = audit.read_elf(cls.directory / "native-worker.elf")
        cls.binary = (cls.directory / "native-worker.bin").read_bytes()
        cls.wire = (cls.directory / "native-worker.reloc").read_bytes()
        cls.build = audit.verify((cls.directory / "retail-maple-observe.kui").read_bytes())["build"]

    def check(self, worker=None, wire=None, directory=None):
        return audit.check_prepared_lifetime(directory or self.directory,
            worker or self.worker, self.binary, wire or self.wire, build=self.build)

    def change_body(self, name, offset, value):
        address, body, section = audit.symbol_bytes(self.worker, name, 2)
        changed = bytearray(self.worker[0])
        struct.pack_into("<H", changed, section[4] + address - section[3] + offset, value)
        return (bytes(changed),) + self.worker[1:]

    def test_actual_kernel_and_all_existing_mutation_families_pass(self):
        result = audit.check_prepared_lifetime(self.directory, self.worker, self.binary, self.wire, True)
        self.assertTrue(result["full_storage_guards_before_and_after_loan"])
        # Each non-constant kernel receives both end-instruction mutations,
        # plus one coherent pointer mutation when it contains a relocation.
        # Count from the ELF extents and relocation table independently of
        # the audit's reported kernel metadata and mutation counter.
        kernels = [name for name in audit.PREPARED_PINS if name != ".rodata"]
        relocation_sites = {struct.unpack_from("<I", self.wire, 72 + i * 8)[0]
                            for i in range(struct.unpack_from("<I", self.wire, 28)[0])}
        pointer_kernels = 0
        for name in kernels:
            address, size, _, _ = self.worker[4][name]
            first = audit.physical(address) - audit.physical(audit.P1)
            pointer_kernels += any(first <= at < first + size for at in relocation_sites)
        self.assertEqual(result["mutation_rejections"], 2 * len(kernels) + pointer_kernels)
        self.assertEqual(result["controller_fastlane"]["private_pair_bytes"], 188)

    def test_context_stack_interior_has_exact_private_bounds(self):
        records=audit.prepared_symbol_records(self.worker)
        bottom=self.worker[4]["__vmu_native_worker_stack_bottom"]
        top=self.worker[4]["__vmu_native_worker_stack_top"]
        from native_runtime_layout import layout_for
        stack_bytes=layout_for(self.worker[4]).WORKER_STACK_BYTES
        self.assertIn((".stack.cached",bottom[0]-0x20000000,stack_bytes,3,bottom[3]),records)
        self.assertIn(("__vmu_native_worker_stack_bottom.cached",bottom[0]-0x20000000,0,0,bottom[3]),records)
        self.assertIn(("__vmu_native_worker_stack_top.cached",top[0]-0x20000000,0,0,top[3]),records)
        for name in ("__vmu_native_worker_stack_bottom", "__vmu_native_worker_stack_top"):
            for index, delta in ((0, 32), (1, 4), (2, 1), (3, -1)):
                with self.subTest(name=name, index=index):
                    symbols=copy.deepcopy(self.worker[4]);record=list(symbols[name]);record[index]+=delta
                    symbols[name]=tuple(record)
                    with self.assertRaisesRegex(ValueError, "prepared private stack"):
                        audit.prepared_symbol_records(self.worker[:4]+(symbols,))
        owner=self.worker[4]["__vmu_native_worker_stack_bottom"][3]
        for index, value in ((1, 1), (2, 7), (3, audit.P2+0x1e000), (5, 4092)):
            with self.subTest(section_field=index):
                sections=copy.deepcopy(self.worker[3]);section=list(sections[owner]);section[index]=value
                sections[owner]=tuple(section)
                with self.assertRaisesRegex(ValueError, "prepared private stack"):
                    audit.prepared_symbol_records(self.worker[:3]+(sections,self.worker[4]))
        sections=list(self.worker[3]);sections.append(sections[owner])
        with self.assertRaisesRegex(ValueError, "stack section is ambiguous"):
            audit.prepared_symbol_records(self.worker[:3]+(sections,self.worker[4]))

    def test_context_cannot_shift_valid_cached_stack_boundary(self):
        name="_transfer_context_valid"
        refs=audit.prepared_signature(self.worker,self.binary,self.wire,name)[2]
        offsets=[offset for offset,target,interior,kind in refs
                 if target=="__vmu_native_worker_stack_bottom.cached" and interior==0 and kind==0]
        self.assertEqual(len(offsets),1)
        address,_,section=audit.symbol_bytes(self.worker,name,2)
        changed=bytearray(self.worker[0]);wire=bytearray(self.wire)
        first=audit.physical(address)-audit.physical(audit.P1)
        offset=offsets[0]
        for i in range(struct.unpack_from("<I",wire,28)[0]):
            at,value=struct.unpack_from("<II",wire,72+i*8)
            if at==first+offset:
                struct.pack_into("<I",wire,76+i*8,value+4)
                struct.pack_into("<I",changed,section[4]+address-section[3]+offset,value+4)
                break
        else:self.fail("missing context stack-bound relocation")
        with self.assertRaisesRegex(ValueError,"linked instructions/targets changed"):
            self.check(worker=(bytes(changed),)+self.worker[1:],wire=bytes(wire))

    def test_private_bss_and_header_comparison_views_have_exact_ownership(self):
        records=audit.prepared_symbol_records(self.worker)
        self.assertFalse(any(name==".bss.cached" for name,*_ in records))
        for name in ("__vmu_native_worker_bss_begin","__vmu_native_worker_bss_end"):
            value,_,_,owner=self.worker[4][name]
            self.assertIn((name+".cached",value-0x20000000,0,0,owner),records)
            for index,delta in ((0,32),(1,4),(2,1),(3,-1)):
                with self.subTest(name=name,field=index):
                    symbols=copy.deepcopy(self.worker[4]);record=list(symbols[name]);record[index]+=delta
                    symbols[name]=tuple(record)
                    with self.assertRaisesRegex(ValueError,"prepared private BSS"):
                        audit.prepared_symbol_records(self.worker[:4]+(symbols,))
        owner=self.worker[4]["__vmu_native_worker_bss_begin"][3]
        for index,value in ((1,1),(2,7),(3,audit.P2+80),(5,4092)):
            with self.subTest(bss_section_field=index):
                sections=copy.deepcopy(self.worker[3]);section=list(sections[owner]);section[index]=value
                sections[owner]=tuple(section)
                with self.assertRaisesRegex(ValueError,"prepared private BSS"):
                    audit.prepared_symbol_records(self.worker[:3]+(sections,self.worker[4]))
        sections=list(self.worker[3]);sections.append(sections[owner])
        with self.assertRaisesRegex(ValueError,"BSS section is ambiguous"):
            audit.prepared_symbol_records(self.worker[:3]+(sections,self.worker[4]))
        for index,delta in ((0,4),(1,4),(2,1)):
            with self.subTest(exports_field=index):
                symbols=copy.deepcopy(self.worker[4]);record=list(symbols["_kui_vmu_native_worker_exports"])
                record[index]+=delta;symbols["_kui_vmu_native_worker_exports"]=tuple(record)
                with self.assertRaisesRegex(ValueError,"prepared header boundary"):
                    audit.prepared_symbol_records(self.worker[:4]+(symbols,))

    def test_fault_publication_alias_exception_is_exact(self):
        if audit.FAULT_GATEWAY not in self.worker[4]:self.skipTest("optional fault capture is absent")
        proof=audit.fault_capture_admission(self.worker)
        coherence=audit.sdk_coherence_admission(self.worker)
        policy=audit.worker_load_policy(self.worker,self.binary)
        self.assertEqual(audit.check_worker_pointer_aliases(self.wire,policy,proof,coherence),
                         struct.unpack_from("<I",self.wire,28)[0])
        total=struct.unpack_from("<I",self.wire,28)[0]
        records=[struct.unpack_from("<II",self.wire,72+i*8) for i in range(total)]
        island=next(i for i,(at,value) in enumerate(records) if at==proof["publisher_p2_reference_offset"])
        def changed(index,at=None,value=None):
            wire=bytearray(self.wire);old_at,old_value=records[index]
            struct.pack_into("<II",wire,72+index*8,old_at if at is None else at,old_value if value is None else value)
            return bytes(wire)
        for wire in (changed(island,value=records[island][1]+2),
                     changed(island,at=records[island][0]+4)):
            with self.assertRaisesRegex(ValueError,"uncached pointer aliases"):
                audit.check_worker_pointer_aliases(wire,policy,proof,coherence)
        # Removing or duplicating the island cannot broaden its authorization.
        for extra in (-1,1):
            wire=bytearray(self.wire)
            if extra<0:del wire[72+island*8:80+island*8]
            else:wire.extend(struct.pack("<II",records[island][0]+4,records[island][1]))
            struct.pack_into("<I",wire,28,total+extra)
            with self.assertRaises(ValueError):audit.check_worker_pointer_aliases(bytes(wire),policy,proof,coherence)
        for boundary in proof["cached_bss_boundaries"]+[proof["cached_header_end"]]:
            selected=[i for i,(_,value) in enumerate(records) if value==boundary]
            for i in selected:
                with self.subTest(boundary=hex(boundary)):
                    with self.assertRaisesRegex(ValueError,"cached pointer aliases persistent"):
                        audit.check_worker_pointer_aliases(changed(i,at=0),policy,proof,coherence)
                    with self.assertRaisesRegex(ValueError,"cached pointer aliases persistent"):
                        audit.check_worker_pointer_aliases(changed(i,value=boundary+4),policy,proof,coherence)
        for index,delta in ((0,1),(1,4),(2,1)):
            symbols=copy.deepcopy(self.worker[4]);record=list(symbols[audit.FAULT_STATE]);record[index]+=delta
            symbols[audit.FAULT_STATE]=tuple(record)
            with self.assertRaisesRegex(ValueError,"scratch lacks exact32B"):
                audit.fault_capture_admission(self.worker[:4]+(symbols,))

    def test_sdk_coherence_descriptor_and_scratch_have_exact_ownership(self):
        if audit.SDK_COHERENCE_STATE not in self.worker[4]:self.skipTest("SDK coherence gates are absent")
        metadata=audit.sdk_coherence_metadata(self.worker)
        self.assertTrue(metadata["available"])
        self.assertEqual(metadata["descriptor_bytes"],40)
        self.assertEqual(metadata["state_bytes"],48)
        table="_kui_vmu_native_trace_exports"
        address,body,section=audit.symbol_bytes(self.worker,table,1)
        first=section[4]+address-section[3]
        values=list(struct.unpack("<10I",body))
        for index,value in ((0,0),(1,1),(2,32),(7,values[7]-0x20000000),
                            (7,values[8]),(8,values[8]+2),
                            (9,values[9]-0x20000000),(9,values[9]+4)):
            with self.subTest(field=index,value=hex(value)):
                changed=bytearray(self.worker[0]);struct.pack_into("<I",changed,first+index*4,value)
                with self.assertRaisesRegex(ValueError,"SDK coherence descriptor"):
                    audit.sdk_coherence_metadata((bytes(changed),)+self.worker[1:])
        for index,delta in ((0,1),(1,-4),(2,1),(3,-1)):
            with self.subTest(descriptor_field=index):
                symbols=copy.deepcopy(self.worker[4]);record=list(symbols[table])
                record[index]+=delta;symbols[table]=tuple(record)
                with self.assertRaises(ValueError):
                    audit.sdk_coherence_metadata(self.worker[:4]+(symbols,))
        owner=self.worker[4][table][3]
        for index,value in ((1,8),(2,3),(3,address+4)):
            with self.subTest(descriptor_section_field=index):
                sections=copy.deepcopy(self.worker[3]);record=list(sections[owner])
                record[index]=value;sections[owner]=tuple(record)
                with self.assertRaises(ValueError):
                    audit.sdk_coherence_metadata(self.worker[:3]+(sections,self.worker[4]))
        for index,delta in ((0,1),(1,4),(2,1),(3,-1)):
            with self.subTest(state_field=index):
                symbols=copy.deepcopy(self.worker[4]);record=list(symbols[audit.SDK_COHERENCE_STATE])
                record[index]+=delta;symbols[audit.SDK_COHERENCE_STATE]=tuple(record)
                with self.assertRaisesRegex(ValueError,"SDK coherence scratch"):
                    audit.sdk_coherence_metadata(self.worker[:4]+(symbols,))
        # Updating the descriptor coherently must not let this scratch
        # overlap an existing worker object, even within owned BSS.
        symbols=copy.deepcopy(self.worker[4]);record=list(symbols[audit.SDK_COHERENCE_STATE])
        record[0]=symbols["_config"][0];symbols[audit.SDK_COHERENCE_STATE]=tuple(record)
        changed=bytearray(self.worker[0]);struct.pack_into("<I",changed,first+36,record[0])
        with self.assertRaisesRegex(ValueError,"SDK coherence scratch overlaps"):
            audit.sdk_coherence_metadata((bytes(changed),)+self.worker[1:4]+(symbols,))
        records=audit.prepared_symbol_records(self.worker)
        for name in audit.SDK_COHERENCE_GATES:
            address,_,_,owner=self.worker[4][name]
            self.assertIn((name+".uncached",address+0x20000000,0,2,owner),records)
            self.assertFalse(any(target==name+".uncached" and size for target,_,size,*_ in records))

    def test_sdk_coherence_uncached_entries_are_descriptor_only(self):
        if audit.SDK_COHERENCE_STATE not in self.worker[4]:self.skipTest("SDK coherence gates are absent")
        coherence=audit.sdk_coherence_admission(self.worker)
        fault=audit.fault_capture_admission(self.worker)
        policy=audit.worker_load_policy(self.worker,self.binary)
        self.assertEqual(audit.check_worker_pointer_aliases(self.wire,policy,fault,coherence),
                         struct.unpack_from("<I",self.wire,28)[0])
        count=struct.unpack_from("<I",self.wire,28)[0]
        records=[struct.unpack_from("<II",self.wire,72+8*i) for i in range(count)]
        for site,target in coherence["p2_references"]:
            index=records.index((site,target))
            for at,value in ((site+4,target),(0,target),(site,target+2),
                             (site,target-0x20000000)):
                with self.subTest(site=hex(site),value=hex(value)):
                    changed=bytearray(self.wire);struct.pack_into("<II",changed,72+8*index,at,value)
                    with self.assertRaises(ValueError):
                        audit.check_worker_pointer_aliases(bytes(changed),policy,fault,coherence)
            for extra in (-1,1):
                changed=bytearray(self.wire)
                if extra<0:del changed[72+8*index:80+8*index]
                else:changed.extend(struct.pack("<II",site,target))
                struct.pack_into("<I",changed,28,count+extra)
                with self.assertRaisesRegex(ValueError,"SDK cold descriptor"):
                    audit.check_worker_pointer_aliases(bytes(changed),policy,fault,coherence)
        # No code alias exception is available if the exact gate proof is not
        # supplied; ordinary P1-code/P2-state admission must still reject it.
        with self.assertRaisesRegex(ValueError,"uncached pointer aliases"):
            audit.check_worker_pointer_aliases(self.wire,policy,fault)

    def test_identity_hook_and_free_walk_bounds_are_pinned(self):
        for name, offset, original in (
                ("_poll_lease_valid", 0x1a, 0xe208),
                ("_installed_hooks_valid", 0x0e, 0xe21f),
                ("_installed_hooks_valid", 0x2e, 0x720c),
                # mov #64,r1 feeds cmp/hi against plan.range_count.
                ("_kui_vmu_native_readonly_release_check", 0x88, 0xe140),
                # mov.w at +0x116 loads this 1024-node walk budget.
                ("_kui_vmu_native_readonly_release_check", 0x200, 0x0400)):
            with self.subTest(name=name, offset=offset):
                body = audit.symbol_bytes(self.worker, name, 2)[1]
                self.assertEqual(struct.unpack_from("<H", body, offset)[0], original)
                with self.assertRaisesRegex(ValueError, "linked instructions/targets changed"):
                    self.check(worker=self.change_body(name, offset, original ^ 1))

    def test_coherent_changed_pointer_target_is_refused(self):
        name = "_poll_lease_valid"
        address, _, section = audit.symbol_bytes(self.worker, name, 2)
        pointer_offset = audit.prepared_signature(self.worker, self.binary, self.wire, name)[2][0][0]
        changed = bytearray(self.worker[0]); wire = bytearray(self.wire)
        value = self.worker[4]["_protected_ranges"][0]
        struct.pack_into("<I", changed, section[4] + address - section[3] + pointer_offset, value)
        first = audit.physical(address) - audit.physical(audit.P1)
        for i in range(struct.unpack_from("<I", wire, 28)[0]):
            if struct.unpack_from("<I", wire, 72 + i * 8)[0] == first + pointer_offset:
                struct.pack_into("<I", wire, 76 + i * 8, value)
                break
        else:
            self.fail("missing proved plan pointer relocation")
        with self.assertRaisesRegex(ValueError, "linked instructions/targets changed"):
            self.check(worker=(bytes(changed),) + self.worker[1:], wire=bytes(wire))

    def test_cached_stack_view_cannot_admit_other_cached_mutable_objects(self):
        name="_poll_lease_valid"
        address,_,section=audit.symbol_bytes(self.worker,name,2)
        pointer_offset=audit.prepared_signature(self.worker,self.binary,self.wire,name)[2][0][0]
        changed=bytearray(self.worker[0]);wire=bytearray(self.wire)
        # lifetime_plan has private P2 ownership. Adding a typed P1 stack
        # view must not silently authorize an alias of this persistent state.
        value=self.worker[4]["_lifetime_plan"][0]-0x20000000
        struct.pack_into("<I",changed,section[4]+address-section[3]+pointer_offset,value)
        first=audit.physical(address)-audit.physical(audit.P1)
        for i in range(struct.unpack_from("<I",wire,28)[0]):
            if struct.unpack_from("<I",wire,72+i*8)[0]==first+pointer_offset:
                struct.pack_into("<I",wire,76+i*8,value)
                break
        else:self.fail("missing proved plan pointer relocation")
        with self.assertRaisesRegex(ValueError,"prepared pointer has no typed target"):
            audit.prepared_signature((bytes(changed),)+self.worker[1:],self.binary,bytes(wire),name)

    def test_private_plan_and_witness_geometry_is_required(self):
        names=["_lifetime_plan", "_hook_witness", "_hook_witness_count", "_hook_witness_ready",
               "_controller_pair"]
        if audit.FAULT_STATE in self.worker[4]:names.append(audit.FAULT_STATE)
        for name in names:
            for index, delta in ((0, -0x20000000), (0, 1), (1, 4), (2, 1)):
                with self.subTest(name=name, index=index):
                    symbols = copy.deepcopy(self.worker[4]); record = list(symbols[name])
                    record[index] += delta; symbols[name] = tuple(record)
                    with self.assertRaisesRegex(ValueError, "private P2 BSS ownership"):
                        self.check(worker=self.worker[:4] + (symbols,))
        # The controller snapshot must own its exact 188-byte private BSS
        # object; a private but unrelated NOBITS stack is not a substitute.
        self.assertEqual(self.worker[4]["_controller_pair"][1], 188)
        symbols = copy.deepcopy(self.worker[4]); record = list(symbols["_controller_pair"])
        record[3] = symbols["__vmu_native_worker_stack_bottom"][3]
        symbols["_controller_pair"] = tuple(record)
        with self.assertRaisesRegex(ValueError, "private P2 BSS ownership"):
            self.check(worker=self.worker[:4] + (symbols,))

    def test_full_storage_guard_is_required_at_both_loan_boundaries(self):
        original = (self.directory / "native/src/loader/vmu_native_passive.ci").read_text()
        lines = [line for line in original.splitlines() if
                 'sourcename: "src/loader/vmu_native_passive.c:persistent_lease"' in line and
                 'targetname: "src/loader/vmu_native_passive.c:persistent_storage_guard"' in line]
        self.assertEqual(len(lines), 2)
        for removed in lines:
            with self.subTest(removed=removed), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary); path = directory / "native/src/loader/vmu_native_passive.ci"
                path.parent.mkdir(parents=True); path.write_text(original.replace(removed, "", 1))
                with self.assertRaisesRegex(ValueError, "full guards at both boundaries"):
                    self.check(directory=directory)

    def test_storage_cannot_substitute_identity_for_full_heap_walk(self):
        original = (self.directory / "native/src/loader/vmu_native_passive.ci").read_text()
        edge = next(line for line in original.splitlines() if
                    'sourcename: "src/loader/vmu_native_passive.c:persistent_storage_guard"' in line and
                    'targetname: "kui_vmu_native_heap_lease_valid"' in line)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary); path = directory / "native/src/loader/vmu_native_passive.ci"
            path.parent.mkdir(parents=True)
            path.write_text(original.replace(edge, edge.replace('targetname: "kui_vmu_native_heap_lease_valid"',
                'targetname: "kui_vmu_native_heap_lease_identity_valid"')))
            with self.assertRaisesRegex(ValueError, "required emitted call"):
                self.check(directory=directory)

    def test_constant_build_label_is_bound_to_verified_runtime(self):
        with self.assertRaisesRegex(ValueError, "build identity differs"):
            audit.prepared_signature(self.worker, self.binary, self.wire, ".rodata", "0123456789ab")

    def test_only_correctly_bound_build_bytes_can_change(self):
        strings = self.worker[3][self.worker[1][13]]
        names = audit.region(self.worker[0], strings[4], strings[5], "test section names")
        section = next(s for s in self.worker[3] if names[s[0]:].split(b"\0", 1)[0] == b".rodata")
        body = audit.region(self.worker[0], section[4], section[5], "test constants")
        at = body.index(b"BUILD " + self.build.encode() + b"\0") + 6
        changed = bytearray(self.worker[0])
        changed[section[4] + at:section[4] + at + 12] = b"0123456789ab"
        mutant = (bytes(changed),) + self.worker[1:]
        self.assertEqual(audit.prepared_signature(mutant, self.binary, self.wire, ".rodata", "0123456789ab")[:2],
                         audit.PREPARED_PINS[".rodata"])
        changed[section[4] + at - 1] ^= 1
        with self.assertRaisesRegex(ValueError, "build identity differs"):
            audit.prepared_signature((bytes(changed),) + self.worker[1:], self.binary, self.wire, ".rodata", "0123456789ab")


if __name__ == "__main__":
    unittest.main()
