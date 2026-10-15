#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the owner's exact Armada CE ROM without extracting executable bytes.

Only the supported kernel hash is accepted. Reports bounded ROM metadata,
exports/imports, address domains and hashes of manually reviewed contracts.
Compressed sections are never guessed or decoded. Optional external DLLs are
parsed by armada_pe_audio_audit.py to compare their layout and API metadata.
Nominal CE addresses and ROM storage addresses are not live-call addresses.
"""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

KERNEL_SHA256 = "a7628c78090fce2c93012d5e4db6142096e138dceb7ebbf555142e902ca03c1f"
KERNEL_BYTES = 1253376
MAX_MODULES, MAX_SECTIONS, MAX_EXPORTS, MAX_IMPORTS = 64, 16, 4096, 128
MAX_STRING = 256


def sha(data):
    return hashlib.sha256(data).hexdigest()


class ROMError(ValueError):
    pass


class ROM:
    def __init__(self, path):
        p = Path(path)
        if p.stat().st_size != KERNEL_BYTES:
            raise ROMError("This audit accepts only the exact Armada kernel size")
        self.data = p.read_bytes()
        if sha(self.data) != KERNEL_SHA256:
            raise ROMError("This audit accepts only the exact Armada kernel hash")
        count, address, offset, size, entry = struct.unpack("<5I", self.read(16, 20))
        if count != 1 or offset != 2048 or size != len(self.data) - offset or entry != address:
            raise ROMError("Unexpected CE load prefix")
        self.base, self.body_offset, self.body_bytes = address | 0x80000000, offset, size
        self.load_header = dict(count=count, address=address, file_offset=offset,
                                bytes=size, entry=entry)
        if self.read(offset + 64, 4) != b"ECEC":
            raise ROMError("Missing CE ROM marker")
        self.header_address = self.u32(offset + 68)
        h = struct.unpack("<21I", self.address_read(self.header_address, 84))
        keys = ("dllfirst", "dlllast", "physfirst", "physlast", "nummods", "ram_start",
                "ram_free", "ram_end", "copy_count", "copy_address", "profile_bytes",
                "profile_address", "files_count", "kernel_flags", "fs_ram_percent",
                "driver_globals_address", "driver_globals_bytes", "interrupt_stack_address",
                "interrupt_stack_bytes", "tracking_address", "tracking_bytes")
        self.header = dict(zip(keys, h))
        if (h[2] != self.base or h[3] > self.base + size or not 1 <= h[4] <= MAX_MODULES or
                not self.base <= h[5] <= h[6] <= h[7] <= 0x8d000000 or h[8] > 64 or h[12] > 64):
            raise ROMError("Invalid bounded ROM header")
        self.modules = {}
        for index in range(h[4]):
            at = self.header_address + 84 + index * 32
            toc = struct.unpack("<8I", self.address_read(at, 32))
            name = self.address_string(toc[4]).lower()
            if name in self.modules:
                raise ROMError("Duplicate ROM module name")
            e32 = self.address_read(toc[5], 100)
            n, flags = struct.unpack_from("<HH", e32)
            if not 1 <= n <= MAX_SECTIONS:
                raise ROMError("Invalid section count")
            entry_rva, virtual_base = struct.unpack_from("<II", e32, 4)
            virtual_bytes = struct.unpack_from("<I", e32, 20)[0]
            if not virtual_bytes or virtual_bytes > 16 * 1024 * 1024 or entry_rva >= virtual_bytes:
                raise ROMError("Invalid module virtual range")
            sections = []
            for section_index in range(n):
                values = struct.unpack("<6I", self.address_read(toc[6] + section_index * 24, 24))
                vsize, rva, psize, ptr, real, sflags = values
                if rva > virtual_bytes or vsize > virtual_bytes - rva or psize > 16 * 1024 * 1024:
                    raise ROMError("Section exceeds module virtual range")
                contents = self.address_read(ptr, psize) if psize else b""
                sections.append(dict(index=section_index, virtual_bytes=vsize, rva=rva,
                                     stored_bytes=psize, data_address=ptr, real_address_field=real,
                                     flags=sflags, compressed=bool(sflags & 0x2000),
                                     file_offset=self.address_offset(ptr, psize) if psize else None,
                                     stored_sha256=sha(contents)))
            self.modules[name] = dict(name=name, toc_address=at, e32_address=toc[5],
                                      o32_address=toc[6], load_address=toc[7],
                                      nominal_virtual_base=virtual_base, entry_rva=entry_rva,
                                      virtual_bytes=virtual_bytes, object_flags=flags,
                                      directories=[struct.unpack_from("<II", e32, 28 + i * 8)
                                                   for i in range(9)], sections=sections)
        self.files = []
        at = self.header_address + 84 + h[4] * 32
        for i in range(h[12]):
            fields = struct.unpack("<7I", self.address_read(at + i * 28, 28))
            attributes, _, _, full_bytes, stored_bytes, name, address = fields
            contents = self.address_read(address, stored_bytes)
            self.files.append(dict(name=self.address_string(name), attributes=attributes,
                                   bytes=full_bytes, stored_bytes=stored_bytes,
                                   data_address=address, file_offset=self.address_offset(address, stored_bytes),
                                   compressed=full_bytes != stored_bytes, stored_sha256=sha(contents)))
        self.copies = []
        for i in range(h[8]):
            src, dst, n, total = struct.unpack("<4I", self.address_read(h[9] + i * 16, 16))
            if n > total or dst < self.base or dst + total > 0x8d000000:
                raise ROMError("Invalid copy record")
            self.copies.append(dict(source=src, destination=dst, copied_bytes=n, total_bytes=total,
                                    initialized_sha256=sha(self.address_read(src, n))))

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ROMError("Read leaves file-backed ROM")
        return self.data[offset:offset + size]

    def u32(self, offset):
        return struct.unpack("<I", self.read(offset, 4))[0]

    def address_offset(self, address, size=1):
        if address < self.base or address + size > self.base + self.body_bytes:
            raise ROMError("Address leaves CE load body")
        return self.body_offset + address - self.base

    def address_read(self, address, size):
        return self.read(self.address_offset(address, size), size)

    def address_string(self, address):
        result = bytearray()
        for i in range(MAX_STRING):
            value = self.address_read(address + i, 1)[0]
            if value == 0:
                return result.decode("ascii", "strict")
            result.append(value)
        raise ROMError("ROM name exceeds bounded string length")

    def rva_section(self, module, rva, size=1):
        matches = [s for s in module["sections"] if s["rva"] <= rva and
                   rva + size <= s["rva"] + s["stored_bytes"] and not s["compressed"]]
        if len(matches) != 1:
            raise ROMError("RVA does not name exactly one uncompressed ROM section")
        return matches[0]

    def rva_address(self, module, rva, size=1):
        section = self.rva_section(module, rva, size)
        return section["data_address"] + rva - section["rva"]

    def rva_read(self, module, rva, size):
        return self.address_read(self.rva_address(module, rva, size), size)

    def rva_u32(self, module, rva):
        return struct.unpack("<I", self.rva_read(module, rva, 4))[0]

    def rva_string(self, module, rva):
        return self.address_string(self.rva_address(module, rva))

    def exports(self, module):
        rva, size = module["directories"][0]
        if not rva:
            return []
        fields = struct.unpack("<IIHH7I", self.rva_read(module, rva, 40))
        _, _, _, _, _, base, count, names_count, functions, names, ordinals = fields
        if count > MAX_EXPORTS or names_count > count:
            raise ROMError("Unbounded export table")
        named = {}
        for i in range(names_count):
            index = struct.unpack("<H", self.rva_read(module, ordinals + i * 2, 2))[0]
            if index >= count:
                raise ROMError("Invalid export ordinal index")
            named[index] = self.rva_string(module, self.rva_u32(module, names + i * 4))
        result = []
        for i in range(count):
            target = self.rva_u32(module, functions + i * 4)
            if target:
                row = dict(ordinal=base + i, name=named.get(i), rva=target,
                           nominal_va=module["nominal_virtual_base"] + target)
                if rva <= target < rva + size:
                    row["forwarder"] = self.rva_string(module, target)
                else:
                    try:
                        address = self.rva_address(module, target, 32)
                    except ROMError:
                        row["uncompressed_entry"] = False
                    else:
                        row.update(rom_storage_address=address,
                                   entry32_sha256=sha(self.address_read(address, 32)))
                result.append(row)
        return result

    def imports(self, module):
        rva, size = module["directories"][1]
        if not rva:
            return []
        result = []
        for i in range(min(MAX_IMPORTS, size // 20)):
            original, timestamp, chain, name, first = struct.unpack(
                "<5I", self.rva_read(module, rva + i * 20, 20))
            if not any((original, timestamp, chain, name, first)):
                return result
            if not name or not first:
                raise ROMError("Incomplete import descriptor")
            symbols = []
            for j in range(MAX_EXPORTS):
                value = self.rva_u32(module, (original or first) + j * 4)
                if not value:
                    break
                symbol = dict(iat_rva=first + j * 4,
                              iat_nominal_va=module["nominal_virtual_base"] + first + j * 4)
                if value & 0x80000000:
                    symbol["ordinal"] = value & 0xffff
                else:
                    symbol["name"] = self.rva_string(module, value + 2)
                symbols.append(symbol)
            else:
                raise ROMError("Unbounded import thunks")
            result.append(dict(dll=self.rva_string(module, name), symbols=symbols))
        raise ROMError("Missing bounded import terminator")

    def contract(self, module, name, start, end, description):
        m = self.modules[module]
        contents = self.rva_read(m, start, end - start)
        return dict(module=module, name=name, rva=start, bytes=end - start,
                    nominal_va=m["nominal_virtual_base"] + start,
                    rom_storage_address=self.rva_address(m, start, len(contents)),
                    sha256=sha(contents), crc32=f"{zlib.crc32(contents):08x}", description=description)


CONTRACTS = [
    ("nk.exe", "initialize_tmu0", 0x1cb48, 0x1cb94,
     "Writes TCR0=0x20, TCOR0/TCNT0=0x4c4b4, enables TMU0 interrupt; periodic25ms CE tick under50MHz PCLK, not a full-reload free counter."),
    ("wsegacd.dll", "map_hardware_cdda_mixer", 0x1544, 0x155c,
     "Maps physical0x00702040 bytes8 and stores mapped pointers in context+28/+32; those fields are hardware pointers, not cached volumes."),
    ("wsegacd.dll", "set_hardware_cdda_mixer", 0x24e8, 0x2514,
     "IOCTL0x24028 writes mapped hardware externalCD-input mixer words directly using input high nibbles and fixed pans; no emulated PCM volume implementation."),
    ("sndcore.dll", "FInitSndCore", 0x11c8, 0x12b8,
     "Initializes shared sound mapping, command queue, interrupt machinery and CE SoundMemHeap; reference-counted."),
    ("sndcore.dll", "command_queue", 0x1408, 0x14d4,
     "Queues a 16-byte packet under a CE critical section; may wait, uses existing firmware queue and event machinery."),
    ("sndcore.dll", "SndOpenPort", 0x1714, 0x1764,
     "ARM command0x90: port byte+2, buffer address32+4, length32+8, format byte+12, mixer byte+13."),
    ("sndcore.dll", "SndClosePort", 0x1764, 0x1798,
     "ARM command0x91; port byte+2."),
    ("sndcore.dll", "SndPortPlay", 0x1a70, 0x1aac,
     "ARM command0x93; port byte+2 plus R5/R6 32-bit fields at+4/+8."),
    ("sndcore.dll", "SndPortStop", 0x1aac, 0x1ae0,
     "ARM command0x94; port byte+2."),
    ("sndcore.dll", "SndSetCDDAVolume", 0x1e28, 0x1e58,
     "ARM command0x35; this wrapper queues zeroed payload fields without copying input arguments."),
    ("sndcore.dll", "map_sound_hardware", 0x2950, 0x2a14,
     "Maps physical sound RAM0x00800000 bytes0x00200000 plus ARM interrupt register and ASIC status registers."),
    ("sndcore.dll", "construct_sound_heap", 0x2a14, 0x2a70,
     "Constructs shared SoundMemHeap over mapped sound RAM from firmware-derived offset to2MiB end."),
    ("sndcore.dll", "PbSndMemGlobal", 0x2b60, 0x2b6c,
     "Returns the mapped2MiB sound-RAM base; does not allocate or reserve memory."),
    ("sndcore.dll", "PsmhGlobal", 0x3370, 0x337c,
     "Returns shared SoundMemHeap object; does not allocate or reserve memory."),
    ("sndcore.dll", "read_firmware_interface", 0x3924, 0x3a30,
     "Reads ARM header+0xe0/+0xe4/+0xe8/+0xec/+0xf0, derives shared interface and heap boundary; mapped address domains matter."),
    ("sndcore.dll", "install_sound_interrupt", 0x3aa4, 0x3b70,
     "Creates sound event/thread and InterruptInitialize(23); preserves CE-owned sound interrupt lifecycle."),
]


def audit(path, external_dir=None):
    rom = ROM(path)
    coredll = {x["ordinal"]: x for x in rom.exports(rom.modules["coredll.dll"])}
    selected = []
    for name in ("wsegacd.dll", "sndcore.dll", "dsound.dll", "timer.dll"):
        module = dict(rom.modules[name])
        module["exports"], module["imports"] = rom.exports(module), rom.imports(module)
        for descriptor in module["imports"]:
            if descriptor["dll"].lower() == "coredll.dll":
                for symbol in descriptor["symbols"]:
                    if "ordinal" in symbol:
                        export = coredll.get(symbol["ordinal"])
                        symbol["resolved_name"] = export.get("name") if export else None
        if external_dir and name != "timer.dll":
            from armada_pe_audio_audit import PE
            external = PE(Path(external_dir) / name.upper())
            module["external_file"] = dict(bytes=len(external.data), sha256=sha(external.data),
                                           entry_rva=external.entry_rva, image_base=external.image_base,
                                           exports_same_name_rva=[(x.get("name"), x["rva"]) for x in module["exports"]] ==
                                           [(x.get("name"), x["rva"]) for x in external.exports()])
        selected.append(module)
    arm = next(f for f in rom.files if f["name"].lower() == "armcom.drv")
    if arm["compressed"]:
        raise ROMError("ARM firmware unexpectedly compressed")
    firmware = rom.address_read(arm["data_address"], arm["stored_bytes"])
    arm_contracts = []
    for name, start, end, description in (
            ("initialize_all_channels", 0x610c, 0x617c,
             "Initializes all64 AICA channel registers, then shared PCM tables. No permanently free channel assumption."),
            ("open_pcm_port", 0x64ac, 0x650c,
             "Command0x90 sets port state atARM0x4100+port*0x48, buffer low/high address, format, length and mixer."),
            ("play_pcm_port", 0x6570, 0x66c4,
             "Command0x93 copies PCM state into AICA hardware channel48+port. Port range16 is serviced elsewhere."),
            ("stop_pcm_port", 0x66c8, 0x670c,
             "Command0x94 keys off hardware channel48+port and updates firmware state."),
            ("hardware_cdda_volume", 0x5c50, 0x5c80,
             "Command0x35 writes hardware externalCD-input level bytes ARM0x802041 and0x802045; does not mix emulated PCM.")):
        data = firmware[start:end]
        arm_contracts.append(dict(name=name, arm_address=start, bytes=len(data),
                                  kernel_file_offset=arm["file_offset"] + start,
                                  sha256=sha(data), crc32=f"{zlib.crc32(data):08x}", description=description))
    if external_dir:
        external_arm = (Path(external_dir) / "ARMCOM.DRV").read_bytes()
        arm["same_as_external"] = firmware == external_arm
    ordinal_names = {str(n): coredll[n].get("name") for n in
                     (2, 3, 168, 170, 171, 175, 179, 181, 196, 492, 494, 495, 496,
                      497, 516, 524, 525, 557, 627, 628, 629)}
    return dict(format="armada-ce-rom-audio-audit-v1", kernel_bytes=len(rom.data),
                kernel_sha256=sha(rom.data), load_header=rom.load_header,
                prefix_crc32=f"{zlib.crc32(rom.data[:2048]):08x}",
                body_crc32=f"{zlib.crc32(rom.data[2048:]):08x}",
                rom_header_address=rom.header_address, rom_header=rom.header,
                rom_files=rom.files, copy_records=rom.copies, modules=selected,
                coredll_ordinals=ordinal_names,
                sh4_contracts=[rom.contract(*x) for x in CONTRACTS], arm_contracts=arm_contracts,
                arm_header={f"0x{o:x}": struct.unpack_from("<I", firmware, o)[0]
                            for o in range(0xcc, 0x100, 4)},
                ce_interrupt_contract=dict(dispatch=0x8c0126f0, pending_dispatch=0x8c01270e,
                    pending_mask=0x8c145b40, ring_head=0x8c145aa8, reschedule_flag=0x8c145884,
                    handler_table=0x8c145c04,
                    resident_sci_eri_slot=0x8c145ca0, resident_sci_rxi_slot=0x8c145ca4,
                    initial_eri_handler=0x8c012654, initial_rxi_handler=0x8c012654,
                    sound_sysintr=23, disc_sysintr=20, disc_dma_sysintr=21,
                    protection="Existing CE reader owns only SCI ERI/RXI table slots. CDDA must not replace sound/timer/ASIC dispatch or acknowledge unrelated IRQs."),
                conclusions=dict(native_aica_backend_compatible=False,
                    native_full_reload_tmu0_clock_compatible=False,
                    generic_raw_sound_ram_bank_lease_proven=False,
                    native_fixed_channel_pair_lease_proven=False,
                    observer_first_recommended=True,
                    first_audible_test_requires=["CE-owned SoundMemHeap allocation before concurrent use",
                        "CE PCM-port lease and lifetime, or independently validated firmware cooperation",
                        "CE clock with wrap-safe millisecond/TMU sampling; no reprogramming owned TMUs",
                        "Concurrent SD reader scheduling and sound-G2 transfer ownership",
                        "Live CE MMU/calling environment validation for nominal APIs"],
                    address_warning="ROM storage addresses identify preboot bytes; nominal VAs and o32 real fields are distinct domains, not observed live addresses.",
                    packaged_arm_firmware_proven_active=False,
                    packaged_arm_header_heap_warning="ARMCOM's static+0xec=0x3efc does not establish the live SNDCORE heap boundary; deriving0x3f20 from it overlaps other packaged firmware code/state. Observe the active firmware header.",
                    evidence_limit="Static analysis establishes conflicts and candidate interfaces, not active packaged firmware, live ownership or stable runtime mapping."))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", type=Path)
    parser.add_argument("--external-dir", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        encoded = json.dumps(audit(args.kernel, args.external_dir), indent=2) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8")
        else:
            print(encoded, end="")
    except (OSError, ValueError, UnicodeError, struct.error) as error:
        parser.exit(1, "Audit failed: " + str(error) + "\n")


if __name__ == "__main__":
    main()
