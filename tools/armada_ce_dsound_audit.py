#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the exact privately supplied Armada DirectSound COM contracts.

Reports API addresses, ABI metadata, hashes and reviewed ownership facts only.
No executable bytes, PCM assets or disassembly are written. Nominal PE/ROM
addresses are not a substitute for live process/MMU binding.
"""
import argparse
import hashlib
import json
from pathlib import Path

from armada_pe_audio_audit import PE, PEError
from armada_ce_rom_audit import ROM, ROMError


GAME_SHA = "168b0764f59776f6d2422451da1f1840051659ee1f64ec7e776c9caad3e80a3a"
DSOUND_SHA = "1e06f20f68e6d4af2e2202d5a1f7ad5655dff8eacfb42b565a0c9c5a75574117"

DEVICE_METHODS = [
    ("QueryInterface", 0x18e0), ("AddRef", 0x1994), ("Release", 0x199c),
    ("CreateSoundBuffer", 0x19dc), ("GetCaps", 0x1a98),
    ("DuplicateSoundBuffer", 0x1b64), ("SetCooperativeLevel", 0x1bcc),
    ("Compact", 0x1be8), ("GetSpeakerConfig", 0x1c18),
    ("SetSpeakerConfig", 0x1c20), ("Initialize", 0x1c48),
]
BUFFER_METHODS = [
    ("QueryInterface", 0x2790), ("AddRef", 0x54ec), ("Release", 0x2910),
    ("GetCaps", 0x29fc), ("GetCurrentPosition", 0x2a44), ("GetFormat", 0x2b8c),
    ("GetVolume", 0x2bf0), ("GetPan", 0x2bfc), ("GetFrequency", 0x2c20),
    ("GetStatus", 0x2c2c), ("Initialize", 0x1c48), ("Lock", 0x2cbc),
    ("Play", 0x2e54), ("SetCurrentPosition", 0x3154), ("SetFormat", 0x3280),
    ("SetVolume", 0x32d8), ("SetPan", 0x3700), ("SetFrequency", 0x3844),
    ("Stop", 0x3898), ("Unlock", 0x39ac), ("Restore", 0x3d48),
]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def region(pe, start, end):
    data = pe.read(pe.rva_offset(start, end - start), end - start)
    return dict(rva=start, bytes=len(data), sha256=sha(data))


def vtable(pe, rom, module, start, methods):
    result = []
    base = module["nominal_virtual_base"]
    for slot, (name, expected_rva) in enumerate(methods):
        pe_target = pe.rva_u32(start + slot * 4)
        rom_target = rom.rva_u32(module, start + slot * 4)
        if pe_target - pe.image_base != expected_rva or rom_target - base != expected_rva:
            raise ValueError("Exact DirectSound vtable contract changed at slot " + str(slot))
        result.append(dict(name=name, slot=slot, byte_offset=slot * 4,
                           rva=expected_rva, external_nominal_va=pe_target,
                           rom_nominal_va=rom_target,
                           rom_storage_address=rom.rva_address(module, expected_rva, 32),
                           entry32_sha256=sha(rom.rva_read(module, expected_rva, 32))))
    return dict(rva=start, external_nominal_va=pe.image_base + start,
                rom_nominal_va=base + start, methods=result)


def audit(kernel, game_path, dsound_path):
    game, dsound, rom = PE(game_path), PE(dsound_path), ROM(kernel)
    if sha(game.data) != GAME_SHA or sha(dsound.data) != DSOUND_SHA:
        raise ValueError("This contract accepts only the exact Armada game and DSOUND hashes")
    module = rom.modules["dsound.dll"]
    device = vtable(dsound, rom, module, 0x116c, DEVICE_METHODS)
    buffer = vtable(dsound, rom, module, 0x11cc, BUFFER_METHODS)
    entry = next(x for x in dsound.exports() if x["name"] == "DirectSoundCreate")
    if entry["rva"] != 0x15ec:
        raise ValueError("DirectSoundCreate entry changed")
    return {
        "format": "armada-ce-dsound-audit-v1", "date": "2026-10-14",
        "method": "Exact file identity, bounded PE/ROM tables and manually reviewed SH4 contracts; output contains metadata only.",
        "inputs": [{"file": pe.path.name, "bytes": len(pe.data), "sha256": sha(pe.data)}
                   for pe in (game, dsound)],
        "kernel_sha256": sha(rom.data),
        "address_domains": {
            "game_pe_image_base": game.image_base,
            "dsound_external_pe_image_base": dsound.image_base,
            "dsound_rom_nominal_base": module["nominal_virtual_base"],
            "live_binding": "Unproven. Resolve the owner's thread/process slot, page tables, actual IAT entry, object and vtable with checked MMU reads before calling. ROM storage addresses identify bytes, not callable code.",
        },
        "direct_sound_create": {
            "rva": entry["rva"], "rom_nominal_va": module["nominal_virtual_base"] + entry["rva"],
            "abi": {"r4": "Device GUID pointer or NULL", "r5": "Output IDirectSound**", "r6": "Outer IUnknown* or NULL", "r0": "HRESULT"},
            "review_region": region(dsound, 0x15ec, 0x16a0),
            "ownership": "Returns the existing module-instance singleton with AddRef if present. Otherwise allocates its device object and initializes/refcounts SNDCORE. A failed HRESULT is not a sound lease.",
            "singleton_holder_rva": 0x8110,
        },
        "game_create_call": {
            "function_rva": 0x89560, "call_rva": 0x895e0, "delay_slot_rva": 0x895e2,
            "return_rva": 0x895e4, "import_thunk_rva": 0xbe900,
            "iat_rva": 0xe21a8, "output_holder_rva": 0xe7180,
            "call_nominal_va": 0x995e0, "return_nominal_va": 0x995e4,
            "output_holder_nominal_va": 0xf7180,
            "hresult_holder_nominal_va": 0x208174,
            "abi_at_call": {"r4": 0, "r5": "R10, address of output holder", "r6": 0,
                            "r10": "Output holder address", "r8": "HRESULT holder address", "r0": "Import thunk"},
            "return_effect": "At return, R0 HRESULT is stored through R8. The game then reads [R10], calls device vtable slot6 with cooperative level3, and creates its primary buffer through slot3.",
            "review_region": region(game, 0x895d6, 0x89624),
            "capture_preconditions": [
                "Match exact source identity and live call/thunk/return instruction guards; reject any other build or call path.",
                "Preserve original call result and the game's store through R8, registers, delay slot, stack/home area, PR, SR, FPSCR/FPU, MMU ASID/page tables, and interrupt state.",
                "For read-only capture save R0/R8/R10/SP/PR plus process/MMU identity before any optional reads; do not invoke DirectSound while capturing.",
                "Only after HRESULT success, checked-read the output holder, object vtable and selected method pointers. Pointer translations must retain the captured process context and verify all bounds.",
                "Snapshotting a pointer creates no COM reference. Do not retain or call it after process/object teardown without obtaining and tracking an owner-context reference.",
                "A sound-owner process is not established by a WSEGACD callback context or a nominal ROM address.",
            ],
        },
        "device_vtable": device, "secondary_buffer_vtable": buffer,
        "public_method_abi": {
            "CreateSoundBuffer": {"r4": "Device this", "r5": "DSBUFFERDESC* (exact20-byte layout)", "r6": "Output IDirectSoundBuffer**", "r7": "NULL outer unknown", "r0": "HRESULT"},
            "GetCaps": {"r4": "Buffer this", "r5": "DSBCAPS* (20-byte output)", "r0": "HRESULT"},
            "Lock": {"r4": "Buffer this", "r5": "Byte offset", "r6": "Byte count", "r7": "Output first pointer**", "sp_plus_16": "Output first bytes*", "sp_plus_20": "Output second pointer**", "sp_plus_24": "Output second bytes*", "sp_plus_28": "Lock flags", "r0": "HRESULT"},
            "Unlock": {"r4": "Buffer this", "r5": "First locked pointer", "r6": "First byte count", "r7": "Second locked pointer or NULL", "sp_plus_16": "Second byte count", "r0": "HRESULT"},
            "Play": {"r4": "Buffer this", "r5": 0, "r6": 0, "r7": "Flags; reviewed bit0 enables looping", "r0": "HRESULT"},
            "GetCurrentPosition": {"r4": "Buffer this", "r5": "Output play cursor* or NULL", "r6": "Output write cursor* or NULL", "r0": "HRESULT"},
            "Stop": {"r4": "Buffer this", "r0": "HRESULT"},
            "Release": {"r4": "Object this", "r0": "Remaining reference count"},
            "home_area": "SH4 call sites reserve16 bytes of argument home area before stack arguments; all buffers/pointers must be in the captured CE caller address domain.",
        },
        "secondary_buffer_contract": {
            "constructor_rva": 0x3e8c, "destructor_rva": 0x42b4,
            "descriptor": {"bytes": 20, "size_offset": 0, "flags_offset": 4,
                           "buffer_bytes_offset": 8, "reserved_offset": 12, "format_pointer_offset": 16},
            "wave_format": "Constructor copies18 bytes of WAVEFORMATEX. PCM tag1 supports8/16-bit. Channel count2 sets its stereo branch; PCM16 requires block alignment2*channels.44100Hz passes the reviewed format-frequency range.",
            "review_regions": [region(dsound, 0x3e8c, 0x3f6e), region(dsound, 0x403c, 0x407a),
                               region(dsound, 0x4176, 0x41aa), region(dsound, 0x42b4, 0x439e)],
            "memory": "With the reviewed flagsbit0x04, constructor requests shared sound-heap storage via its CE sound-memory interface. Failure returns8007000e. Without forcedhardware, construction may fall back to ordinary aligned CE RAM; object creation alone does not prove sound-RAM storage.",
            "port_lease": "CreateSoundBuffer does not reserve a PCM port. Play later claims a shared device pool resource and calls SndOpenPort. Stereo claims two compatible resources. No permanently free channel is assumed.",
            "pcm_pool": {"entries": 16, "device_owner_table_offset": 24,
                         "device_masks_offsets": [128, 132], "pool_count_offset": 136,
                         "pool_smb_table_offset": 140,
                         "selection_helpers_rvas": [0x205c, 0x2130]},
            "looping": "Play observes flagsbit0. Hardware loop sizes must be divisible by32 bytes for mono or64 for stereo; otherwise887800ff. A32KiB stereo ring satisfies that alignment; ring refill safety still needs live cursor/transfer validation.",
            "stereo_transfer": "The native Unlock/transfer path handles stereo conversion and hardware transfer. Feed interleaved PCM through the public locked regions and pass exactly the returned pointers/counts to Unlock; do not treat sound-RAM layout as an interleaved host buffer.",
            "voice_management": "Play can invoke native voice/resource management while claiming ports; a public method is not proof that existing SFX cannot be displaced when the pool is exhausted. Allocation-only testing must not call Play.",
            "release": "Final secondary-buffer Release releases its device reference and runs the buffer destructor: native Stop/cleanup, sound-memory/host-memory release, unlinking and object free. Track only references acquired by the adapter. Last device/SNDCORE release must remain the native owner's decision.",
        },
        "allocation_only_candidate": {
            "status": "Design only; live sound-owner binding and caller-context capture must precede it.",
            "sequence": ["DirectSoundCreate(NULL,&own_device,NULL)",
                         "CreateSoundBuffer(20-byte descriptor, forcedhardware flag0x04,32KiB PCM16 stereo44100)",
                         "GetCaps; record hardware/software flags and actual size", "Release own buffer", "Release own device reference"],
            "does_not_call": ["Play", "Lock/Unlock", "SetCooperativeLevel", "SetFormat on the game's primary buffer", "Direct SNDCORE/ARM/hardware commands"],
            "result_meaning": "Successful correctly released hardware-buffer allocation proves a temporary CE sound-memory/object lease only, not PCM port availability or audible music.",
        },
        "primary_api_reference": "https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms897764(v=msdn.10)",
        "validation": {"vtable_entries_matched_external_and_rom": len(DEVICE_METHODS) + len(BUFFER_METHODS),
                       "live_binding_validated": False, "owner_bytes_modified": False,
                       "playback_source_modified": False},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--dsound", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        encoded = json.dumps(audit(args.kernel, args.game, args.dsound), indent=2) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8")
        else:
            print(encoded, end="")
    except (OSError, PEError, ROMError, ValueError, StopIteration) as error:
        parser.exit(1, "Audit failed: " + str(error) + "\n")


if __name__ == "__main__":
    main()
