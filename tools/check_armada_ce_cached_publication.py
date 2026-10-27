#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pin the existing pre-NK cache publication boundary for cached Maple code.

Only the relay C entry and the trampoline's P2 resume literal are relocated.
The complete assembly bodies, both cache-control waits, purge bounds, owner
state restoration and final uncached game-entry transfer remain byte-exact.
This checks publication, not the installer C body or live cache performance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_armada_ce_excerpt import function_sizes
from check_loader_layout import inspect_elf, region

STAGE, LIMIT = 0x8ce10000, 0x8cfca000
WRITE_LIMIT = 0x8cfc4000
RESUME = '_kui_retail_game_resume'
RELAY = '_kui_retail_stage_relay'
TRAMPOLINE = '__retail_trampoline_start'
RESUME_BYTES = 160
RELAY_LITERAL_OFFSET = 152
TRAMPOLINE_BYTES = 128
TRAMPOLINE_LITERAL_OFFSET = 56

# SH little-endian resume body, including all seven literals. In order:
# private high stack, CCR, full RAM begin/end, ICI|OCI, relay, P2 game entry.
# Relay alone is filled from its bounded STT_FUNC symbol below.
RESUME_TEMPLATE = bytes.fromhex(
    'f36820df20d0026920d021d1a30020701230fb8b1cd01fd11220090009000900'
    '09000900090009000900836493651ad00b40090017d09b2013d1022109000900'
    '090009000900090009000900836f164f064f174f274f047f264ff66ef66df66c'
    'f66bf66af669f668f667f666f665f664f663f662f661f360c07002600e4007d0'
    '2b40f6600000ff8c1c0000ff0000008c0000008d0808000000000000000001ac')

# Save the complete 21-word owner frame, mask IRQs and jump through P2.
# This whole 128-byte stage template is pinned, including zero padding.
TRAMPOLINE_PREFIX = bytes.fromhex(
    '062f162f262f362f462f562f662f762f862f962fa62fb62fc62fd62fe62f224f'
    '034f234f134f024f124f0200f0cb0e4001d02b4009000900')


def check(data,write_layout=False):
    """Check stage ELF bytes and return the narrow publication proof."""
    if type(write_layout) is not bool:
        raise ValueError('Cache publication requires an explicit admitted layout profile')
    limit = WRITE_LIMIT if write_layout else LIMIT
    stage = inspect_elf(data, STAGE, limit)
    symbols = stage['symbols']
    binary_end = symbols.get('__retail_stage_binary_end', 0)
    resume = symbols.get(RESUME, 0)
    trampoline = symbols.get(TRAMPOLINE, 0)
    relay = function_sizes(data).get(RELAY)
    if (not STAGE < binary_end <= STAGE + len(stage['payload']) or
            resume % 4 or not STAGE <= resume <= binary_end - RESUME_BYTES or
            trampoline % 4 or not STAGE <= trampoline <= binary_end - TRAMPOLINE_BYTES or
            symbols.get('__retail_trampoline_code_end') != trampoline + 60 or
            symbols.get('__retail_trampoline_end') != trampoline + TRAMPOLINE_BYTES or
            relay is None or relay[0] != symbols.get(RELAY) or relay[0] % 2 or
            relay[1] <= 0 or not STAGE <= relay[0] < relay[0] + relay[1] <= binary_end):
        raise ValueError('Invalid bounded pre-NK cache publication symbols')
    if not (resume + RESUME_BYTES <= trampoline or
            trampoline + TRAMPOLINE_BYTES <= resume):
        raise ValueError('Pre-NK resume and trampoline overlap')
    expected = bytearray(RESUME_TEMPLATE)
    if len(expected) != RESUME_BYTES or len(TRAMPOLINE_PREFIX) != TRAMPOLINE_LITERAL_OFFSET:
        raise ValueError('Invalid authored pre-NK publication template')
    struct.pack_into('<I', expected, RELAY_LITERAL_OFFSET, relay[0])
    body = region(stage['payload'], resume - STAGE, RESUME_BYTES, 'cache publication resume')
    if body != bytes(expected):
        raise ValueError('Pre-NK cache publication resume body/literals changed')
    expected_trampoline = (TRAMPOLINE_PREFIX + struct.pack('<I', resume + 0x20000000) +
                           bytes(TRAMPOLINE_BYTES - TRAMPOLINE_LITERAL_OFFSET - 4))
    trampoline_body = region(stage['payload'], trampoline - STAGE,
                             TRAMPOLINE_BYTES, 'cache publication trampoline')
    if trampoline_body != expected_trampoline:
        raise ValueError('Pre-NK trampoline/P2 cache publication entry changed')
    report = {
        'resume_p2': hex(resume + 0x20000000),
        'resume_bytes': RESUME_BYTES,
        'resume_sha256': hashlib.sha256(body).hexdigest(),
        'trampoline_bytes': TRAMPOLINE_BYTES,
        'trampoline_sha256': hashlib.sha256(trampoline_body).hexdigest(),
        'complete_assembly_bytes_pinned': True,
        'bounded_relay_entry': hex(relay[0]),
        'ram_purge_begin': '0x8c000000',
        'ram_purge_end': '0x8d000000',
        'ram_purge_line_bytes': 32,
        'relay_called_with_caches_disabled': True,
        'restore_saved_ccr_with_instruction_and_operand_invalidation': True,
        'post_ccr_write_nops': [8, 8],
        'owner_frame_restored': True,
        'final_game_entry_p2': '0xac010000',
        'scope': 'Existing pre-NK publication boundary; no runtime cache maintenance added',
    }
    if write_layout:
        report.update(write_layout=True,stage_range_p1=[hex(STAGE),hex(limit)])
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage_elf', type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.stage_elf.read_bytes()), indent=2))


if __name__ == '__main__':
    main()
