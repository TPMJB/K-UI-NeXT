#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Record hash-bounded Armada CE sound-heap and RAM binding contracts.

This consumes the owner's private kernel via armada_ce_rom_audit. Output is
metadata, addresses and review conclusions only; no executable bytes are
extracted or patched. It does not imply that a lease has been acquired.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from armada_ce_rom_audit import ROM, ROMError


def reviewed_range(rom, module, name, start, end):
    return rom.contract(module, name, start, end, "Manually reviewed contract; addresses are static, not an observed live binding.")


def audit(path):
    rom = ROM(path)
    snd = rom.modules["sndcore.dll"]
    data = next(s for s in snd["sections"] if s["rva"] == 0x5000)
    if data["flags"] & 0x10000000:
        raise ROMError("Unexpected explicit shared data section")
    holders = []
    for name, rva in (("heap", 0x50ac), ("sound_ram", 0x6118),
                      ("sound_reference_count", 0x50d0), ("firmware_interface_offset", 0x50a8),
                      ("firmware_derived_heap_offset", 0x612c)):
        if not data["rva"] <= rva < data["rva"] + data["virtual_bytes"]:
            raise ROMError("Published holder leaves the writable section")
        nominal = snd["nominal_virtual_base"] + rva
        real = data["real_address_field"] + rva - data["rva"]
        holders.append(dict(name=name, rva=rva, nominal_va=nominal, o32_real_va=real,
                            nominal_slot=nominal >> 25, o32_real_slot=real >> 25,
                            block_index=(nominal >> 16) & 511, page_index=(nominal >> 12) & 15,
                            page_offset=nominal & 4095, fixed_p1_backing_proven=False))
    vtable_rva = 0x10b4
    table = struct.unpack("<8I", rom.rva_read(snd, vtable_rva, 32))
    if table[4] != 0x01d53720 or table[5] != 0x01d53688:
        raise ROMError("Unexpected SoundMemHeap vtable")
    if rom.rva_u32(snd, 0x3378) != 0x01d550ac or rom.rva_u32(snd, 0x2b68) != 0x01d56118:
        raise ROMError("Unexpected published holder literals")
    nk = rom.modules["nk.exe"]
    if rom.rva_u32(nk, 0x262c) != 0xff000000:
        raise ROMError("Unexpected MMU register-base literal")
    empty_table=rom.rva_read(nk,0x2b0c,2048)
    if any(empty_table) or rom.rva_u32(nk,0x34e8)!=0x8c012b0c or rom.rva_u32(nk,0x3508)!=0x8c145800:
        raise ROMError("Unexpected initial TTB sentinel/table-base literals")
    code = [reviewed_range(rom, *x) for x in (
        ("nk.exe", "kernel_translate_p0", 0x259c, 0x25fa),
        ("nk.exe", "kernel_positive_block_special_case", 0x25ec, 0x25fe),
        ("nk.exe", "initialize_64_slot_ttb", 0x3404, 0x348c),
        ("sndcore.dll", "DllMain_single_owner_gate", 0x10d8, 0x11c8),
        ("sndcore.dll", "FInitSndCore", 0x11c8, 0x12b8),
        ("sndcore.dll", "ReleaseSndCore", 0x12b8, 0x1324),
        ("sndcore.dll", "heap_constructor", 0x337c, 0x33e8),
        ("sndcore.dll", "heap_allocate", 0x3688, 0x3720),
        ("sndcore.dll", "heap_free", 0x3720, 0x37d0),
        ("sndcore.dll", "best_fit_free_block", 0x32e0, 0x331c),
        ("sndcore.dll", "split_block", 0x3330, 0x3370),
        ("sndcore.dll", "heap_enter_cs", 0x34d0, 0x34e8),
        ("sndcore.dll", "heap_leave_cs", 0x34e8, 0x3500),
        ("sndcore.dll", "heap_lock_compaction", 0x3500, 0x352c),
    )]
    return dict(format="armada-ce-sound-heap-audit-v1", kernel_sha256=hashlib.sha256(rom.data).hexdigest(),
        kernel_bytes=len(rom.data), module_nominal_base=snd["nominal_virtual_base"],
        toc_load_address=snd["load_address"], data_section=data, holders=holders,
        copy_records=rom.copies,
        backing_conclusion="Only nk.exe initialized RAM appears in the ROM copy table. SNDCORE .data is compressed and allocated/mapped by CE; neither TOC load address nor o32 real virtual address supplies a fixed physical backing address.",
        software_page_walk=dict(kernel_routine=0x8c01259c, ttb_register=0xff000008,
            address_input="Aligned P0 virtual address <0x80000000; caller's current CE TTB retained.",
            first_level_index="VA>>25 (slot0 aliases the current process; real holder addresses here use slot1).",
            second_level_index="(VA>>16)&0x1ff",
            memblock_requirement="Second-level word is a negative main-RAM pointer, not a special positive entry.",
            memblock_page_address="MEMBLOCK+12+4*((VA>>12)&15)",
            committed_bit=1, kernel_p1_formula="(page_word&~0xfff)|(VA&0xfff)|0x80000000",
            read_only_helper="include/kui/armada_ce_ram_bind.h",
            callback_bounds="P1 main-RAM words from0x8c010000 through0x8cfffffc only. TTB, slot and MEMBLOCK inputs must themselves be P1; P2 table aliases are refused before callback rather than normalized to a different cache domain. Generic direct-object alias normalization remains separate. Low reserved RAM/G2/MMIO/special mappings refused.",
            callback_max_reads=7,
            consistency="Re-read all three table entries after sampling the holder; publish only if unchanged. Root integration must also ensure TTB did not change.",
            cache_policy="CCR must have OCE1/OIX0/ORA0: (CCR&0xa1)==1; ordinary page_word.C bit3 must be1; ((VA^resolvedP1)&0x3000)==0. Otherwise skip the holder read, validate all three mapping entries again, preserve physical address/page word, zero value and return CACHE(9). No cache purge, uncached alias or virtual dereference is attempted.",
            cache_policy_source="Renesas SH-4 hardware manual sections3.5.5 and4.3.2, independently verified by adapter reviewer: normal16KiB operand-cache index bits13:5 and physical tag bits28:10.",
            permission_limit="Physical read-only observation does not implement CE access-key permission checks, demand paging or an ownership lease.",
            context_limit="Masking SR or clearing MMUCR does not itself select a new TTB; the observed slot0 instance remains the GD caller's process, not necessarily the game sound owner."),
        raw_table_trace=dict(helper="include/kui/armada_ce_table_trace.h",
            function="kui_armada_ce_trace_table(ttb,va,read32,context,out)",
            output_fields=["slot_value","block_address","block_value"],
            slots=64, slot_index="VA>>25", maximum_heap_va=0x7fd550ac,
            initial_ttb=0x8c1458a0, empty_slot_table=0x8c012b0c,
            empty_table_bytes=len(empty_table), empty_table_sha256=hashlib.sha256(empty_table).hexdigest(),
            initialization="Kernel8c013440..8c013446 puts the512-word zero-table pointer in TTB slots1..63;8c013472 writes TTB=KData8c145800+0xa0.",
            positive_block_contract="Kernel8c0125be tests sign of the second-level word. A nonnegative word takes the special path; onlyVA>>10==0x16 identifies the0x5800..0x5bff KData mapping. It is not treated as an encoded physical MEMBLOCK pointer.",
            scan_role="64 address-space roots are not claimed to be64 processes. Slot0 may alias another actual slot; candidates/masks establish table facts only.",
            statuses={"negative_p1_candidate":0,"root_refused":2,"slot_refused":3,
                      "unsupported_block":4,"read_failed":7,"mapping_changed":8},
            scope="Reads only root/second-level words and repeats them for stability. Raw unsupported block words remain visible; no MEMBLOCK,page,holder,G2 orCEAPI access.",
            interpretation_limit="BLOCK alone cannot distinguishzero/special/P2/P3/unaligned/out-of-range values. Candidate0 is only a stable aligned negative P1 pointer, not a sound owner, initialized MEMBLOCK or lease.",
            tests="tests/test_armada_ce_table_trace.c", host_cases=100,
            host_result="Pass withAddressSanitizer/UndefinedBehaviorSanitizer; all64indices, exactsentinel andunsupportedpointers, readfailures andmappingraces tested."),
        heap_layout=dict(bytes=36, fields={"vtable":0,"free_bytes":4,"compaction_lock_count":8,
                                         "critical_section":12,"block_head":32},
            block_bytes=20, block_fields={"free_flag":0,"bytes":4,"mapped_sound_pointer":8,
                                         "previous":12,"next":16}, free_flag_values={"free":1,"allocated":0}),
        allocator=dict(nominal_va=0x01d53688, vtable_offset=20, exported=False,
            abi={"R4":"SoundMemHeap*this","R5":"block_descriptor**out","R6":"requested_bytes","R0":"BOOL"},
            alignment=32, lock="EnterCriticalSection(this+12), then LeaveCriticalSection on success/failure.",
            failure="Returns0 when no suitable free block exists; out argument is not cleared, so caller must initialize it toNULL.",
            allocation="Chooses a smallest suitable free block, rounds request32, optionally creates a remainder descriptor using the CE operator-new API, marks retained descriptor allocated and stores it through out.",
            caveat="Unsigned rounding has no overflow guard. Reject zero/oversized requests before calling. A remainder-descriptor allocation failure is not separately surfaced as BOOL failure; callers must validate the resulting descriptor's full size.",
            sound_data="block+8 is a mapped sound-RAM pointer; holding the descriptor is the lease. Do not invent a separate fixed bank address."),
        deallocator=dict(nominal_va=0x01d53720,vtable_offset=16,exported=False,
            abi={"R4":"same SoundMemHeap*this","R5":"owned block_descriptor*"},
            action="Locks this+12, returns bytes to free count, merges adjacent free blocks, frees obsolete descriptors through CE operator-delete, marks resulting block free, unlocks.",
            requirement="Do not free an arbitrary pointer or a block from another heap/CE process instance."),
        lifetime=dict(refcount_nominal_va=0x01d550d0, module_lock_nominal_va=0x01d550bc,
            acquisition="FInitSndCore() BOOL at0x01d511c8 increments reference under module lock; on a new instance it initializes mapping, sound IRQ, heap and queue.",
            release="ReleaseSndCore() at0x01d512b8 decrements reference; zero tears down sound DMA, IRQ thread/event, mapping, heap and queue.",
            ownership_requirement="Acquire and retain an initialized owner-context reference before allocating; stop and free lease before releasing it.",
            ready_observation="Heap pointer and positive refcount establish an initialized candidate instance only; sound-RAM mapping and all pointers still require validation.",
            data_sharing="The writable section lacks IMAGE_SCN_MEM_SHARED. Shared physical instance across caller slots/processes cannot be assumed; compare actual mappings.",
            single_owner_gate="DllMain processattach creates named auto-reset event SNDCORE_INUSE. ERROR_ALREADY_EXISTS(183) closes the handle, deletes its module critical section and returns FALSE. Loading a fresh SNDCORE instance in another process is explicitly rejected while the first owner exists.",
            first_audible_route="DirectSound public buffer creation in the known game sound-owner context is preferable to a raw internal allocator call when its COM lease is fully validated."),
        reviewed_code=code,
        host_validation=dict(test="tests/test_armada_ce_ram_bind.c", cases=90,
            result="Pass with AddressSanitizer/UndefinedBehaviorSanitizer; leak detector disabled because the environment's ptrace prevents its process-thread inspection.",
            coverage=["All four root-required holders with P1 tables, and early refusal for every P2 table-input combination", "Distinct current-process and slot1 mappings",
                "Read failure at every callback", "Mapping changes at each repeat validation",
                "Low RAM, G2, MMIO, unaligned and unmapped pointer refusal", "Invalid committed bit and out-of-main-RAM page refusal",
                "OCE/OIX/ORA and virtual/physical cache-color mismatches never read holder", "Cache-refusal mapping revalidation and read failure at every stage"]),
        current_scope="Read-only published-holder binding; no CE API calls, G2 operations, allocation, PCM-port reservation or functional CDDA playback occurs here.")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel",type=Path)
    parser.add_argument("--output",type=Path)
    args=parser.parse_args()
    try:
        encoded=json.dumps(audit(args.kernel),indent=2)+"\n"
        if args.output:args.output.write_text(encoded,encoding="utf-8")
        else:print(encoded,end="")
    except (OSError,ValueError,struct.error) as error:
        parser.exit(1,"Audit failed: "+str(error)+"\n")


if __name__=="__main__":main()
