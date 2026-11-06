#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the supplied VC2 CE Maple source as metadata, without patching it.

The exact full-kernel identity is required before ROMInventory parses tables.
Addresses, sizes, CRCs, hashes and reviewed scalar relationships are emitted;
retail code bytes and disassembly are never emitted. Matching this source
profile admits no runtime installation, memory lifetime, card emulation or I/O.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zlib

from armada_ce_rom_audit import ROMError, ROMInventory


KERNEL_BYTES = 1841152
KERNEL_SHA256 = "b728e8eaef03bd7cc5acbe3dc3363ec9fafcaca90916cf21781d2e72ff99b3bc"
MODULE_FIELDS = ("toc_address", "e32_address", "o32_address", "load_address",
                 "nominal_virtual_base", "entry_rva", "virtual_bytes", "object_flags")
SECTION_FIELDS = ("virtual_bytes", "rva", "stored_bytes", "data_address",
                  "real_address_field", "flags")
# Each section tuple contains the six O32 fields followed by the stored hash.
# Compressed data/resources are hashed in storage; they are never decoded.
MODULES = {
    "maple.dll": {
        "layout": (0x8c1d0830, 0x8c0a6cf4, 0x8c0a6d58, 0x8c114000,
                   0x01ce0000, 0x788c, 0xb000, 0x210e),
        "sections": (
            (0x6c32, 0x1000, 0x6e00, 0x8c0b1000, 0x03ce1000, 0x60000020,
             "d6ea1f462a05584a43eee4fdd2e1000c611e3707ac6a4e2a829098e0e0fc9f4c"),
            (0x2c0, 0x8000, 0xad, 0x8c064da4, 0x03ce8000, 0xc0002040,
             "998947233bcae201fcf69504179f63ee7482bc7cdb1693642e2e36570df291cf"),
            (0x2a0, 0x9000, 0x224, 0x8c0650ac, 0x03ce9000, 0x40002040,
             "9bfe990f9ecf041a9742b2b16b9f353cc1937468d50fa660e835ada22a3e74b3")),
    },
    "mapledev.dll": {
        "layout": (0x8c1d0910, 0x8c0aac94, 0x8c0aacf8, 0x8c1a2000,
                   0x01c10000, 0xd7b8, 0x11000, 0x210e),
        "sections": (
            (0xcabb, 0x1000, 0xcc00, 0x8c10c000, 0x03c11000, 0x60000020,
             "fe332bc1ec282ca17d46eea24ef0b3e972f85017b2e1cb29a3f355e8715a94be"),
            (0x564, 0xe000, 0x181, 0x8c096e6c, 0x03c1e000, 0xc0002040,
             "14483959ae28aa0dc0da3e924b7eb3bfd24fdd20fd13a71376c0bf6a667f1612"),
            (0x7f0, 0xf000, 0x621, 0x8c0a6044, 0x03c1f000, 0x40002040,
             "c818d7c120ff894d5e0661438fe6d1a4fa2649a430e596e738dbd71147a4239d")),
    },
}
CONTRACTS = (
    ("completion_entry", 0x6f64, 32,
     "341964a5ca5e3599f9efd9726f035e78bfcf8203e68aaf56ffd2e40032987677", 0xdf56606e),
    ("completion_walker", 0x6f64, 616,
     "029e36a7dd7e72eda9c9b216e62c684c52f70c2b30028b275161d7965ba8be93", 0xc3d92882),
    ("completion_notification", 0x6f64, 72,
     "1e83627db4fdc3cfcf51d2f53c8f9ae7d33ed93d95b1e0391c3d1e8d7184395f", 0x91b94a28),
    ("worker_loop", 0x71cc, 160,
     "385398ec000deaf1b2cfa456bc4e4ecba2658be330d577cf0424a0aed53a9ed6", 0xc79cb99f),
    ("worker_completion_call", 0x722a, 48,
     "68578ca5edbb1092e8dc1aedeec97c07831db1548a03ad950f834d69a40d11a1", 0xb53b3a8b),
    ("worker_literals", 0x726c, 48,
     "5fc521760cd35f8d90f7fa55ae742ea6369997363c9c6e9b6c420df2111d01b5", 0x309b0d98),
    ("list_builder", 0x6c84, 512,
     "7d036c7f177199949a8d8130d7d1cef5e9d98f9af01a6dcdee94a0d31cfe7b99", 0xe21ee258),
    ("root_load", 0x180a, 58,
     "a8212868bdfea9be1a06978542ea0864a09aee87f40a686e3dde5b72bef8282f", 0xac2b54ea),
    ("SetKMode_thunk", 0x7808, 12,
     "19bb0f7377a6c71f53ee97642f5184cbef8e810363b0ca74d18926c58ed3e62c", 0xa79c772f),
    ("ordinary_worker_create", 0x1ce8, 232,
     "1606d300b9ac2736bf9db05b6927514dfc21b2129786a0a29a693d7d70c08252", 0x12e1c4bc),
    ("ordinary_worker_shutdown", 0x1b5e, 68,
     "9df70aee00a88b7b16dc200fc20155c3211c9fae8f85b312fb3c44cdfbb06561", 0x975df65d),
    ("WaitForSingleObject_thunk", 0x77a8, 12,
     "42d0bf80006c6f2a238dba221a2cd5faf51df50d19920dd86853897e0e6c0349", 0x9cba802c),
    ("main_worker_wrapper", 0x1a08, 24,
     "b383a52f823624424bf7e889853140ee0a2a3b4d6db4a337a4375b37e7b7ad20", 0xc41e50cf),
)
LITERALS = (
    ("root_global", 0x1840, 0x01ce82b8),
    ("builder_target", 0x7280, 0x01ce6c84),
    ("completion_target", 0x7294, 0x01ce6f64),
    ("SetKMode_IAT", 0x7810, 0x01ce8044),
    ("WaitForSingleObject_IAT", 0x77b0, 0x01ce8024),
    ("main_worker_thread_entry", 0x1dbc, 0x01ce1a08),
    ("main_worker_wrapper_target", 0x1a1c, 0x01ce71cc),
)
SHORT_LITERALS = (
    ("ordinary_worker_dma_event_offset", 0x726e, 0x178),
    ("shutdown_main_thread_handle_offset", 0x1c9c, 0x180),
)
WAIT_ABI = dict(module="coredll.dll", ordinal=497, name="WaitForSingleObject",
    export_rva=0x877c, body_bytes=64, body_sha256="f56df5e42b716fe798c85046cd28b6fbee6026792ecfbc1ba3e20326bfefd6e8",
    prologue_bytes=12, prologue_sha256="eb4ff033e28645b20333d091360c6f2b3c616b3be66fa24dc1994bb625ebc6ef",
    prologue_saved_bytes=8, callee_frame_bytes=28, nested_calls_present=True,
    caller_home_bytes=16, caller_home_offsets=[0,4,8,12])
IMPORTS = (
    ("WaitForSingleObject", 497, 0x8024),
    ("CreateThread", 492, 0x8034),
    ("SetKMode", 630, 0x8044),
    ("EnterCriticalSection", 4, 0x8050),
    ("LeaveCriticalSection", 5, 0x8054),
    ("InterruptDone", 628, 0x8064),
    ("SetThreadPriority", 514, 0x8068),
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def hx(value):
    return f"0x{value:08x}"


def require(actual, expected, label):
    if actual != expected:
        raise ROMError(label + " differs from reviewed VC2 source")


class VC2SourceROM(ROMInventory):
    """Exact-owner parsing only; this is never a runtime admission policy."""

    def validate_identity(self):
        require(len(self.data), KERNEL_BYTES, "Exact kernel size")
        require(sha(self.data), KERNEL_SHA256, "Exact kernel SHA256")


def source_window(rom, module, contract):
    name, rva, size, expected_sha, expected_crc = contract
    data = rom.rva_read(module, rva, size)
    require(sha(data), expected_sha, name + " SHA256")
    require(zlib.crc32(data), expected_crc, name + " CRC32")
    at = rom.rva_address(module, rva, size)
    return dict(id=name, rva=hx(rva), bytes=size, end_rva_exclusive=hx(rva + size),
                nominal_va=hx(module["nominal_virtual_base"] + rva),
                rom_storage_address=hx(at), file_offset=rom.address_offset(at, size),
                sha256=sha(data), crc32_ieee=f"{zlib.crc32(data):08x}")


def continuation_source_metadata(rom):
    """Source-specific self-thread wait facts; no live owner admission."""
    maple=rom.modules["maple.dll"]
    for name,rva,wanted in SHORT_LITERALS:
        require(int.from_bytes(rom.rva_read(maple,rva,2),"little"),wanted,name)
    # GBR-relative MOV.L fields are decoded only inside the complete reviewed
    # CreateThread source window above. Emit displacements, never opcodes.
    store=int.from_bytes(rom.rva_read(maple,0x1d82,2),"little")
    require(store>>8,0xc2,"main worker handle GBR store kind")
    require((store&255)*4,0x180,"main worker handle store displacement")
    core=rom.modules["coredll.dll"]
    export=next((e for e in rom.exports(core) if e["ordinal"]==497),{})
    require(export.get("name"),"WaitForSingleObject","WaitForSingleObject name")
    require(export.get("rva"),WAIT_ABI["export_rva"],"WaitForSingleObject export RVA")
    body=rom.rva_read(core,WAIT_ABI["export_rva"],WAIT_ABI["body_bytes"])
    require(sha(body),WAIT_ABI["body_sha256"],"WaitForSingleObject body SHA256")
    require(sha(body[:WAIT_ABI["prologue_bytes"]]),WAIT_ABI["prologue_sha256"],
            "WaitForSingleObject argument-home SHA256")
    return dict(
        main_worker_handle_offset=hx(0x180), dma_event_offset=hx(0x178),
        secondary_worker_handle_offset=hx(0x184),
        source_thread_wrapper_rva=hx(0x1a08), source_worker_rva=hx(0x71cc),
        shutdown_flag_offset=0, shutdown_wait_ms=1000,
        mapped_bridge_pre_return_rva=hx(0x7c58),
        wait_thunk_rva=hx(0x77a8), wait_iat_rva=hx(0x8024),
        wait=dict(**{k:(hx(v) if k=="export_rva" else v) for k,v in WAIT_ABI.items()},
                  nominal_va=hx(core["nominal_virtual_base"]+WAIT_ABI["export_rva"]),
                  rom_storage_address=hx(rom.rva_address(core,WAIT_ABI["export_rva"],len(body))),
                  body_crc32=f"{zlib.crc32(body):08x}"),
        claims=[
            "CreateThread returns the ordinary worker HANDLE into extension +180; extension +178 is its separate SYSINTR 18 DMA event.",
            "The shutdown path sets extension word 0, signals event +178, then waits up to 1000 ms on thread +180 before possible termination.",
            "The ordinary worker waits on event +178, consumes SYSINTR 18 status, calls completion and then InterruptDone 18; a self-thread wait must not consume event +178.",
            "COREDLL WaitForSingleObject writes all 16 caller argument-home bytes; injected calls require the ordinary CE stack and an explicit ABI adapter."],
        live_continuation_admitted=False)


def check_source_metadata(rom):
    """Recheck parsed source contracts; no returned value enables a hook."""
    require(rom.header_address, 0x8c1d063c, "ROMHDR location")
    for key, value in (("ram_start", 0x8c1d1000), ("ram_free", 0x8c1e0000),
                       ("ram_end", 0x8cfd0000),
                       ("driver_globals_address", 0x8cfd0000),
                       ("driver_globals_bytes", 0x30000)):
        require(rom.header[key], value, "ROMHDR " + key)
    modules = []
    for name, expected in MODULES.items():
        if name not in rom.modules:
            raise ROMError("Missing reviewed VC2 module " + name)
        module = rom.modules[name]
        require(tuple(module[k] for k in MODULE_FIELDS), expected["layout"], name + " layout")
        require(len(module["sections"]), len(expected["sections"]), name + " section count")
        sections = []
        for section, fields in zip(module["sections"], expected["sections"]):
            label = name + " section " + str(section["index"])
            require(tuple(section[k] for k in SECTION_FIELDS), fields[:6], label + " layout")
            contents = rom.address_read(section["data_address"], section["stored_bytes"])
            require(sha(contents), fields[6], label + " stored SHA256")
            sections.append({**{k: hx(section[k]) for k in SECTION_FIELDS},
                             "compressed": section["compressed"],
                             "stored_sha256": sha(contents)})
        modules.append(dict(name=name, **{k: hx(module[k]) for k in MODULE_FIELDS},
                            sections=sections))
    maple = rom.modules["maple.dll"]
    windows = [source_window(rom, maple, c) for c in CONTRACTS]
    literals = []
    for name, rva, expected in LITERALS:
        require(rom.rva_u32(maple, rva), expected, name + " literal")
        literals.append(dict(id=name, rva=hx(rva), value=hx(expected)))
    data = maple["sections"][1]
    require(bool(data["flags"] & 0x80000000), True, "MAPLE data writable flag")
    require(data["rva"] <= 0x82b8 < data["rva"] + data["virtual_bytes"],
            True, "MAPLE root writable virtual range")
    exports = {e["ordinal"]: e for e in rom.exports(rom.modules["coredll.dll"])}
    core = {s["iat_rva"]: s for d in rom.imports(maple)
            if d["dll"].lower() == "coredll.dll" for s in d["symbols"]}
    imports = []
    for name, ordinal, iat in IMPORTS:
        require(core.get(iat, {}).get("ordinal"), ordinal, name + " import ordinal")
        require(exports.get(ordinal, {}).get("name"), name, name + " export name")
        imports.append(dict(name=name, ordinal=ordinal, iat_rva=hx(iat),
                            iat_nominal_va=hx(maple["nominal_virtual_base"] + iat)))
    start, size = 0x7c34, 460
    padding = rom.rva_read(maple, start, size)
    require(any(padding), False, "Candidate aligned text padding")
    return modules, windows, literals, imports, dict(
        rva=hx(start), bytes=size, end_rva_exclusive=hx(start + size), all_zero=True,
        nominal_va=hx(maple["nominal_virtual_base"] + start),
        rom_storage_address=hx(rom.rva_address(maple, start, size)),
        sha256=sha(padding), crc32_ieee=f"{zlib.crc32(padding):08x}",
        patching_admitted=False, relocation_exclusion_proven=False,
        reference_exclusion_proven=False)


def audit(kernel):
    rom = VC2SourceROM(kernel)
    modules, windows, literals, imports, padding = check_source_metadata(rom)
    return dict(
        format="vc2-ce-vmu-owner-source-v1", license="GPL-3.0-only",
        status="reviewed-source-metadata-matched",
        scope="Exact-owner metadata only; no retail bytes or disassembly. Nominal CE and ROM storage addresses are not established live-call addresses.",
        runtime_admitted=False, patching_admitted=False,
        card_read_admitted=False, card_write_admitted=False,
        source_identity=dict(file="VIRTCOP2/0WINCEOS.BIN", bytes=len(rom.data), sha256=sha(rom.data)),
        load_prefix={k: (hx(v) if k in ("address", "entry") else v)
                     for k, v in rom.load_header.items()},
        memory=dict(rom_header_address=hx(rom.header_address),
                    ram_end_word_address=hx(rom.header_address + 28),
                    **{k: hx(rom.header[k]) for k in ("ram_start", "ram_free", "ram_end",
                       "driver_globals_address", "driver_globals_bytes")},
                    candidate_writable_reserve_begin="0x8cfc4000",
                    candidate_writable_reserve_end="0x8cfd0000",
                    candidate_writable_reserve_bytes=48 * 1024,
                    reservation_lifetime_proven=False),
        modules=modules, source_contracts=windows, literals=literals, imports=imports,
        continuation_source=continuation_source_metadata(rom),
        completion_root=dict(rva="0x000082b8", writable_virtual_data=True,
                             initial_value_decoded=False),
        candidate_bridge_padding=padding,
        findings=[
            "VC2 MAPLE and MAPLEDEV differ from Armada; its reviewed offsets and fingerprints cannot be reused unchanged.",
            "The ordinary worker calls completion RVA0x6f64 after SYSINTR18 status bit0x1000; InterruptDone18 follows.",
            "The worker writes1 to mapped controller base+0x88 for list-ready; completion-spanning DMA ownership still needs proof.",
            "Completion begins with extension+0x15c notification state and extension+0x160 event handling, which require a VC2 ownership review.",
            "SetKMode is COREDLL ordinal630; ordinal514 is SetThreadPriority and is not evidence of kernel-mode execution.",
            "The same allocator ceiling as Armada offers candidate space; no helper lifetime or nested-launch reservation has been established."],
        limits=[
            "No game, kernel, hook, Maple packet, SD I/O or bridge is executed by this tool.",
            "Compressed sections are bounded and hashed, not decoded; text padding reference and relocation exclusion remain open.",
            "Smash Pack boots native code before nested CE; this audit establishes neither direct CE startup arguments nor a pre-NK reservation hook.",
            "The working foreground reader remains unchanged; this report is not a VMU-enabled compatibility build."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", type=Path, metavar="0WINCEOS.BIN")
    args = parser.parse_args()
    try:
        report = audit(args.kernel)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    print(json.dumps(report, indent=2) + "\n", end="")


if __name__ == "__main__":
    main()
