#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce metadata-only Armada Maple header source evidence.

Requires the exact privately supplied owner files. Emits addresses, lengths,
hashes and reviewed findings only, never instructions, payloads or disassembly.
The separately labeled probe limits are acceptance policy, not CE guarantees.
"""
import argparse
import json
from pathlib import Path
import struct
import zlib

from armada_ce_maple_audit import identity, pe_highlow_targets, relocated_text, sha
from armada_ce_rom_audit import ROM
from armada_pe_audio_audit import PE


# Instruction meanings were independently reviewed from these exact windows.
# Hashing these windows reproduces provenance; it is not semantic decompilation.
CONTRACTS = (
    ('builder_port_bound', 0x582c, 0x586a,
     'Starts at TX raw VA from extension+0x88; iterates port indices 0..3.'),
    ('missing_main_descriptor', 0x5876, 0x58ce,
     'For a missing main unit, W0=port<<16, W1=the per-port receive VA plus RX bias (or zero), and W2 bytes are command1, (port<<6)|0x20, port<<6, zero.'),
    ('builder_unit_function_bound', 0x58ce, 0x5922,
     'Iterates eight unit slots and each unit object function count. A zero command omits that function. This is not an aggregate descriptor-count bound.'),
    ('ordinary_w0_and_pool_destination', 0x5922, 0x596c,
     'Marks object included; takes the unsigned byte payload word count from function+0x16. W0=(port<<16)|payloadWords. Pool-backed W1=receiveVA+RXbias. Clears the receive allocation before emitting its packet header.'),
    ('explicit_physical_destination', 0x5978, 0x597c,
     'When the function receive VA is zero, emits function+0x20 as an explicit physical W1. The probe does not follow this unsupported destination branch.'),
    ('ordinary_wire_header', 0x597c, 0x59c0,
     'W2 bytes: command from function+0x14, destination=(port<<6)|(unit==0?0x20:1<<(unit-1)), source=port<<6, payloadWords. The source loop has eight unit slots; the probe admits only ordinary one-hot subunit bits1/2/4/8/16 or main0x20.'),
    ('ordinary_payload_stride', 0x59c0, 0x59d8,
     'Copies exactly payloadWords 32-bit words after the three descriptor words. The next descriptor therefore begins 12+4*payloadWords bytes later.'),
    ('last_descriptor_publication', 0x59dc, 0x59ee,
     'ORs bit31 into the most recent descriptor W0, publishes its VA at extension+0x8c, and publishes the end VA at extension+0x90.'),
    ('last_descriptor_mask_literal', 0x5a28, 0x5a2c,
     'The reviewed last-descriptor literal is 0x80000000.'),
    ('missing_main_response_header', 0x5b5a, 0x5b76,
     'Reads response word0 from the per-port receive buffer; 0xffffffff means no response. Command byte5 is the expected main device-info response on this path.'),
    ('function_response_word0', 0x5bd6, 0x5bea,
     'Only included function objects are examined. Reads their receive VA word0; 0xffffffff means no response.'),
    ('function_response_fields', 0x5c28, 0x5c3e,
     'Reads response command byte0, attached-unit mask byte2&0x1f, and payload byte count=byte3*4.'),
    ('expected_response_command', 0x5c54, 0x5c68,
     'A nonzero expected response byte from function+0x15 must equal the received command; zero accepts any command.'),
    ('generic_response_copy_and_release', 0x2bd4, 0x2c4c,
     'Default completion copies response header plus bounded payload into the pending IRP, frees receive storage, clears pending object state and completes the IRP. The passive sample establishes no lease across this release.'),
    ('vblank_hardware_initialization', 0x1e24, 0x1ea8,
     'Configures MDTSEL=1, MDSTAR=the TX physical allocation and MDEN=1. DMA can start on VBlank independently of CPU interrupt masking.'),
    ('main_loop_completion_lifecycle', 0x5d4c, 0x5dd6,
     'Writes mapped+0x88 (not MDST+0x18), waits for the event, gates ordinary completion on interrupt status bit0x1000 and calls InterruptDone(18). No DMA-idle fence is established by the GD callback.'),
)


def audit(startup_dir):
    root = Path(startup_dir)
    identities = [identity(root / name, name)
                  for name in ('0WINCEOS.BIN', 'WINCE/MAPLE.DLL')]
    rom = ROM(root / '0WINCEOS.BIN')
    pe = PE(root / 'WINCE/MAPLE.DLL')
    if sha(pe.data) != identities[1]['sha256']:
        raise ValueError('Owner MAPLE.DLL changed during audit')
    module = rom.modules['maple.dll']
    equivalence = relocated_text(rom, 'maple.dll', pe)
    targets = pe_highlow_targets(pe, 'maple.dll')
    contracts = []
    for name, start, end, finding in CONTRACTS:
        count = end - start
        original = rom.rva_read(module, start, count)
        storage = rom.rva_address(module, start, count)
        offset = pe.rva_offset(start, count)
        external = pe.read(offset, count)
        overlaps = sorted(t for t in targets if t < end and t + 4 > start)
        if any(t < start or t + 4 > end for t in overlaps):
            raise ValueError('HIGHLOW relocation straddles contract ' + name)
        normalized = bytearray(external)
        for target in overlaps:
            at = target - start
            value = struct.unpack_from('<I', normalized, at)[0]
            value = (value + module['nominal_virtual_base'] - pe.image_base) & 0xffffffff
            struct.pack_into('<I', normalized, at, value)
        if normalized != original:
            raise ValueError('Reviewed ROM/PE contract mismatch: ' + name)
        contracts.append(dict(id=name, module='maple.dll', rva=hex(start),
            end_rva_exclusive=hex(end), bytes=count,
            rom_storage_address=hex(storage),
            kernel_file_offset=hex(rom.address_offset(storage, count)),
            rom_sha256=sha(original), rom_crc32_ieee=f'{zlib.crc32(original) & 0xffffffff:08x}',
            external_PE=dict(file='WINCE/MAPLE.DLL', file_offset=hex(offset),
                sha256=sha(external), crc32_ieee=f'{zlib.crc32(external) & 0xffffffff:08x}',
                HIGHLOW_overlap_rvas=[hex(t) for t in overlaps],
                exact_bytes_equal=external == original,
                equal_after_reviewed_HIGHLOW_delta=True), finding=finding))
    if rom.rva_u32(module, 0x5a28) != 0x80000000:
        raise ValueError('Reviewed last-descriptor literal changed')
    return dict(format='armada-ce-maple-header-contracts-v1', license='GPL-3.0-only',
        scope='Metadata-only exact owner ROM/PE findings. No executable bytes, payload, disassembly, callable address, DMA ownership, lifetime or hook contract is exported.',
        source_identities=identities, external_text_equivalence=equivalence,
        source_contracts=contracts,
        source_derived_header_geometry=dict(
            descriptor_header_words=3, descriptor_header_bytes=12,
            source_port_count=4, source_unit_slots_per_port=8,
            payload_word_bits=8, payload_word_mask='0x000000ff',
            port_mask='0x00030000', last_mask='0x80000000',
            ordinary_allowed_w0_bits='0x800300ff',
            descriptor_bytes='12 + 4 * (W0 & 0xff)',
            request_payload_count='(W2 >> 24) == (W0 & 0xff)',
            request_port='(W0 >> 16) & 3',
            request_source='((W2 >> 16) & 0xff) == (port << 6)',
            response_absent_word='0xffffffff',
            wire_A2=dict(port=0, builder_unit_index=2, destination_byte='0x02')),
        conservative_probe_policy=dict(
            classification='Probe acceptance limits, not bounds guaranteed by CE source.',
            maximum_records=8, passes=2,
            words_per_record_per_pass=4, maximum_direct_buffer_word_reads=64,
            direct_read_budget_scope='Counts three TX header words and one RX header word per record per pass only; existing metadata, mapping, cache and final witness reads are separate.',
            contents='TX W0/W1/W2 and RX word0 only. No TX or RX payload is followed, copied or interpreted.',
            TX_bounds='Require the prior uncached, page-aligned 0x1000 TX mapping. Validate all three header words are in the captured used span before reading; validate the payload stride against remaining bytes before advancing. Reject excess records, missing last flag, premature last flag, special descriptors, invalid ordinary bits or disagreement with published last/end.',
            RX_bounds='Follow W1 only if four-byte aligned and the complete word is inside the prior admitted contiguous uncached 0x2000 RX physical allocation. An explicit/zero/outside-pool destination is never followed.',
            destinations='Require matching port in destination upper two bits; low six bits must be a single ordinary main/subunit bit (0x20, 0x01, 0x02, 0x04, 0x08 or 0x10). This is narrower than all eight source unit-loop indices.',
            repeated_observation='Repeat the same header addresses and values, then repeat extension words, mappings, module/process/context witnesses. A mismatch refuses the observation. This is sampled consistency, not DMA idleness or allocation ownership.',
            timing='Existing accepted GD PLAY/PLAY2 sample boundaries are not Maple completion fences and do not identify save traffic.',
            effects='Writes only probe-owned report/scratch memory; no guest code/data, MMIO, driver state, request, response, IRQ, native backend or virtual VMU write is authorized.'),
        limitations=[
            'An idle MDST value or masked CPU interrupts cannot prevent a new VBlank DMA operation.',
            'No aggregate source TX capacity check or eight-record source guarantee was established.',
            'Matching metadata/header samples grant no allocation identity, lifetime, IRQ ownership or hook lease.',
            'RX word0 may change during DMA; a refused changing sample is not proof of game or memory-card corruption.',
            'A sample of controller queries is not evidence that CE VMU save traffic has been intercepted.',
            'The native virtual-A2 backend remains disconnected.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup-dir', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check', type=Path, help='Require exact agreement with checked-in metadata JSON')
    args = parser.parse_args()
    try:
        inputs = [(args.startup_dir / name).resolve()
                  for name in ('0WINCEOS.BIN', 'WINCE/MAPLE.DLL')]
        if args.output and args.output.resolve() in inputs:
            raise ValueError('Metadata output must not replace an owner input')
        result = audit(args.startup_dir)
        if args.check and json.loads(args.check.read_text(encoding='utf-8')) != result:
            raise ValueError('Checked-in metadata differs from exact owner audit')
        encoded = json.dumps(result, indent=2) + '\n'
        if args.output:
            args.output.write_text(encoded, encoding='utf-8')
        elif not args.check:
            print(encoded, end='')
        else:
            print(json.dumps(dict(matched=True, source_files=len(result['source_identities']),
                contracts=len(result['source_contracts']),
                executable_bytes_exported=False)))
    except (OSError, ValueError, UnicodeError, struct.error, KeyError) as error:
        parser.exit(1, 'Header source audit failed: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
