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
        self.assertEqual(result["mutation_rejections"], 42)

    def test_context_stack_interior_has_exact_private_bounds(self):
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

    def test_context_cannot_shift_valid_stack_interior_literal(self):
        name="_transfer_context_valid"
        refs=audit.prepared_signature(self.worker,self.binary,self.wire,name)[2]
        offsets=[offset for offset,target,interior,kind in refs
                 if target==".stack" and interior==256 and kind==3]
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

    def test_identity_hook_and_free_walk_bounds_are_pinned(self):
        for name, offset, original in (
                ("_poll_lease_valid", 0x1a, 0xe208),
                ("_installed_hooks_valid", 0x0e, 0xe21f),
                ("_installed_hooks_valid", 0x2e, 0x720c),
                ("_kui_vmu_native_readonly_release_check", 0x7e, 0xe140),
                ("_kui_vmu_native_readonly_release_check", 0x1e4, 0x0400)):
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

    def test_private_plan_and_witness_geometry_is_required(self):
        for name in ("_lifetime_plan", "_hook_witness", "_hook_witness_count", "_hook_witness_ready"):
            for index, delta in ((0, -0x20000000), (1, 4), (2, 1)):
                with self.subTest(name=name, index=index):
                    symbols = copy.deepcopy(self.worker[4]); record = list(symbols[name])
                    record[index] += delta; symbols[name] = tuple(record)
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
