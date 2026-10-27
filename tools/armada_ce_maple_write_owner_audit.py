#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Metadata-only audit of the exact Armada CE storage write owner.

Reads privately supplied owner files without executing or changing them.
Exports reviewed RVA ranges, hashes, protocol scalars and control-flow claims;
never exports owner instructions, executable bytes or disassembly.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from armada_ce_maple_audit import identity, relocated_text
from armada_ce_maple_hook_owner_audit import hx, require, window
from armada_ce_maple_rx_slice_owner_audit import IDENTITIES
from armada_ce_rom_audit import ROM
from armada_pe_audio_audit import PE

WINDOWS = (
    ('phase_geometry', 0x38a8, 0x3934,
     'Block bytes are32*(decoded size field+1). Read/write phase counts are the low/high nibbles of the decoded geometry byte. Phase byte sizes are ceiling(block bytes/phase count), stored at state+48/+50.',
     '25fd08566ca81c11058df793fa35db771490008080b1e7a5d69173f6770086b8'),
    ('command13_finalizer', 0x42a0, 0x4324,
     'Copies the supplied phase directly into the location selector, submits MEMCARD CMD13 with8-byte receive capacity, accepts response7, maps FILEERR251 to0x86850010 and every other response including AGAIN252 to0x868500b0. There is no response retry in this function.',
     '64d8eee74cfa8cdc07fcafbe723efadf4636af4620ad17ba353fb0c81152071e'),
    ('packet_submit_wait_and_timeout_poke', 0x4c14, 0x4d8c,
     'Builds the generic raw-frame IOCTL request and derives its payload/receive word counts from caller byte sizes. The loop waits16ms and submits a current-token validation IOCTL after a wait timeout; this historical cancel/poke does not cancel the original raw IRP or replay an AGAIN response. The submitted location block field is restored on the normal completed-response path; early submission/wait errors can bypass that restoration.',
     'c50c932e8960deab431b04f36e4b14f5f705480435f0cdba63fff64deecc4d26'),
    ('whole_read_retry', 0x7c74, 0x7cc0,
     'Invokes the whole block reader at0x7d0c up to3 total attempts, repeating only for0x86850010. It does not retry0x868500b0.',
     'bb47cebab9ea3a00aae37356fc5f5868b0e1237c39f653f4e2b3570830890e3c'),
    ('whole_write_retry', 0x7cc0, 0x7d0c,
     'Invokes the whole block writer at0x7ea0 up to3 total attempts, repeating only for0x86850010. A repeat starts all write phases again; this is not a final-SYNC-only retry.',
     'fee0864eeeb284f7d3f5ad3ae5727e863a6a8611640eba2190f81020da253c14'),
    ('read_phases_and_finalizer', 0x7d0c, 0x7ea0,
     'Reads phase data and checks response8, word count, function and full echoed location. After the loop, the one-phase path passes phase0 to the command13 finalizer. Other phase counts pass the advanced phase byte.',
     'c64c142c3d73960ec0fd965fa49f4977a7fe8a969177c628596c088964edfee1'),
    ('write_phases_and_finalizer', 0x7ea0, 0x801c,
     'Starts location.phase at0; copies/pads one geometry-sized phase into the outgoing request, submits CMD12 with4-byte receive capacity, accepts only response7 with zero payload words, then increments phase and byte offset. After all block bytes, passes the advanced phase byte directly to finalizer0x42a0. A phase response other than this ACK maps to0x86850010; finalizer errors are propagated.',
     '60813770ed4f8ca25c8e476714dac65706c97d95b44e6b533940e7bc16a99769'),
)

SUPPORT_WINDOWS = (
    ('maple.dll', 'raw_ioctl_dispatch', 0x2170, 0x23b8,
     'IOCTL0x220480 dispatches to raw submission0x2e60; IOCTL0x220484 dispatches to0x2f2c. Neither dispatch path supplies a worker critical-section lease.'),
    ('maple.dll', 'token_lookup', 0x23b8, 0x2448,
     'Decodes and resolves the token to a current unit/function and compares the complete function token. Failure reports an invalid/currently missing owner; success returns its unit/function coordinates.'),
    ('maple.dll', 'raw_request_enqueue', 0x2e60, 0x2f1c,
     'Retains the raw IRP in function+28, sets expected_response+21 to zero, captures request/capacity fields and allocates the pooled RX. A second raw request on that function is refused while its IRP is pending.'),
    ('maple.dll', 'timeout_poke_ioctl', 0x2f2c, 0x2f7c,
     'IOCTL0x220484 loads the supplied token, calls current-token lookup, and on success sets this IOCTL IRP status/information to zero. It neither cancels nor completes the original pending raw IRP and does not clear/free its RX or function state.'),
    ('maple.dll', 'generic_response_completion', 0x2bd4, 0x2c4c,
     'Copies min(received payload bytes+4, caller output capacity) into the original raw IRP output, frees the pooled RX, clears pending request fields, sets successful status and completes the original IRP. No AGAIN-specific replay occurs here.'),
    ('maple.dll', 'response_walker', 0x5b0c, 0x5d28,
     'Expected-response zero bypasses response-code filtering, so raw storage AGAIN/FILEERR reach generic completion. The all-FF timeout path can remove a unit. This ordinary consumer runs on the same main worker; the window does not acquire a critical section.'),
    ('maple.dll', 'main_worker_completion_lifetime', 0x5d4c, 0x5dd6,
     'The main worker invokes completion0x5b0c only after the ordinary interrupt status, then calls InterruptDone18 and reads the mapped port. No Enter/LeaveCriticalSection encloses completion. The call to0x16dc queries interrupt flags through KernelIoControl; it is not a critical-section operation.'),
    ('maple.dll', 'root_shutdown_lifetime', 0x1934, 0x1abe,
     'Root shutdown sets extension word0 to1, signals the worker, waits1000ms and may TerminateThread before releasing transport/RX-pool state. Ordinary timeout-poke behavior does not establish exclusion of this external lifecycle.'),
    ('wdmlib.dll', 'raw_ioctl_request_dispatch', 0x14dc, 0x1634,
     'ControlWDMDevice builds a separate IRP for each IOCTL, assigns its own completion/overlapped context and calls IoCallDriver. STATUS_PENDING handling and optional per-file event serialization do not establish a critical section held by the MAPLE worker. The timeout poke uses a different IOCTL IRP from the pending raw request.'),
    ('wdmlib.dll', 'call_driver', 0x1dbc, 0x1df4,
     'IoCallDriver advances the IRP stack and invokes the selected driver major-function pointer. This wrapper does not acquire or release a critical section around MAPLE dispatch.'),
)

SCI_WINDOWS = (
    ('wsegacd.dll', 'gd_read_submit_and_completion', 0x2d82, 0x2dd8,
     'GD request commands17 and38 call the request vector, retain its returned token in r10, then pass that token as the completion helper argument. A zero request token therefore reaches the same helper as an immediate failure, not a pending job.',
     0x8c0abd82, '1d4cded9d416a9d8a516727ebeacf8655dfa8d07eee010473e937a6125aa33ed'),
    ('wsegacd.dll', 'gd_zero_token_is_io_error', 0x19bc, 0x1a14,
     'The completion helper tests its token argument first. Zero returns STATUS_IO_DEVICE_ERROR0xc0000185 immediately, without resubmitting a GD request. A nonzero token is stored for the ordinary completion path.',
     0x8c0aa9bc, '91fd2f77cf9dca3a899038bd1f6a45e747a2d535715e87bed16ad9ea984f55b8'),
)

CE_WAIT_ABI_METADATA = dict(
    module='coredll.dll', ordinal=497, name='WaitForSingleObject',
    kernel_bytes=1253376,
    kernel_sha256='a7628c78090fce2c93012d5e4db6142096e138dceb7ebbf555142e902ca03c1f',
    export_rva=hx(0x877c), rom_storage_address=hx(0x8c03877c),
    kernel_file_offset=hx(0x28f7c), callee_frame_bytes=8,
    caller_home_bytes=16, caller_home_offsets=[0, 4, 8, 12],
    prologue_rva=hx(0x877c), prologue_end_rva_exclusive=hx(0x8788),
    prologue_bytes=12,
    prologue_sha256='eb4ff033e28645b20333d091360c6f2b3c616b3be66fa24dc1994bb625ebc6ef',
    body_rva=hx(0x877c), body_end_rva_exclusive=hx(0x87bc), body_bytes=64,
    body_sha256='69da824d76fa7ee8c4ccedfc9553fdf2e242d826688e4ebc64a0d1edccc4e598')

CE_WAIT_WINDOWS = (
    ('ce_wait_argument_homes', 0x877c, 0x8788,
     'COREDLL export497 WaitForSingleObject pushes8 callee bytes, then writes all four argument homes at incoming caller SP+0,+4,+8,+12. The caller must reserve16 writable bytes independently of its saved PR/callee-saved registers.',
     CE_WAIT_ABI_METADATA['prologue_sha256']),
    ('ce_wait_body', 0x877c, 0x87bc,
     'Reviewed original WaitForSingleObject body and literals containing the argument-home prologue. An authored GNU-SH caller requires an explicit ABI adapter before entering this WinCE import.',
     CE_WAIT_ABI_METADATA['body_sha256']),
)

COMPLETION_WINDOWS = (
    ('maple.dll', 'vblank_dma_configuration', 0x1e24, 0x1ea8,
     'Initializes MDTSEL1, MDSTAR and MDEN1. Re-enabling MDEN permits a later VBlank to replay the current descriptor list without the ordinary worker advancing it.',
     0x8c0afe24, '887bf8212c5aebc0b6237e5de8a48d5ca04d2704b4c2703f4dc137e91d868106'),
    ('maple.dll', 'builder_receive_clear', 0x582c, 0x5a22,
     'Rebuilds the ordinary descriptor list. The call at5964 passes function RX+0c, value0 and capacity+10 to COREDLL memset1047 through stub63b0, zeroing the receive allocation before the next DMA response. Worker call5d84 performs this rebuild before ready-write5d90 and event-wait5d9a.',
     0x8c0b382c, 'df75f1ba377ec110c41daa5d6c4a5b29bfd82f6a3713e27ef928c246ae53f978'),
    ('nk.exe', 'completion_status_query', 0x1c928, 0x1c95a,
     'KernelIoControl10000 obtains the interrupt descriptor cached status+16. When zero, it reads the raw ASIC status through descriptor+0, masks by+12, caches the result at+16 and acknowledges those captured raw bits at1c94c. MAPLE queries this before entering its original completion consumer.',
     0x8c02c928, '1a3df3816153c237d1c3a58e675795080d9de093100a950f441ed6bf869c992d'),
    ('nk.exe', 'normal_completion_interrupt_dispatch', 0x1d158, 0x1d1f0,
     'Masks normal Maple interrupt bits and returns SYSINTR18. Masking interrupt delivery does not clear a later raw completion status bit or prevent a VBlank-triggered DMA while MDEN remains enabled.',
     0x8c02d158, 'aacd327bc5df1f55e1efe74063afc681167b51729b8adb67f3f5091bc8e03b00'),
    ('nk.exe', 'completion_interrupt_done', 0x1c5c8, 0x1c5ec,
     'Re-enables the descriptor interrupt mask and clears its cached status+16. It does not acknowledge a raw ASIC completion bit set after the earlier status query. A late coalesced completion can therefore be delivered to the next worker wait after RX has been rebuilt and zeroed.',
     0x8c02c5c8, 'ee91850d795f509679dba18f1367b53d79c7dce1a158909821383f6e0e26158b'),
)


def reviewed_completion_windows(rom):
    result=[]
    for module, name, start, end, finding, address, digest in COMPLETION_WINDOWS:
        item=window(rom, module, name, start, end, finding)
        require(item['rom_storage_address'], hx(address), 'Reviewed completion owner address ' + name)
        require(item['sha256'], digest, 'Reviewed completion owner window ' + name)
        result.append(item)
    return result


def reviewed_wait_windows(rom):
    result=[]
    for name, start, end, finding, digest in CE_WAIT_WINDOWS:
        item=window(rom, 'coredll.dll', name, start, end, finding)
        require(item['rom_storage_address'], CE_WAIT_ABI_METADATA['rom_storage_address'],
                'Reviewed CE wait owner storage address ' + name)
        require(item['kernel_file_offset'], CE_WAIT_ABI_METADATA['kernel_file_offset'],
                'Reviewed CE wait owner file offset ' + name)
        require(item['sha256'], digest, 'Reviewed CE wait owner window ' + name)
        result.append(item)
    return result


def reviewed_pe_windows(pe):
    """Check the fixed reviewed external ranges independently of identity."""
    result = []
    for name, start, end, _, digest in WINDOWS:
        data = pe.read(pe.rva_offset(start, end - start), end - start)
        actual = hashlib.sha256(data).hexdigest()
        require(actual, digest, 'Reviewed write owner window ' + name)
        result.append(dict(id=name, rva=hx(start), end_rva_exclusive=hx(end),
                           bytes=end-start, sha256=actual))
    return result


def reviewed_sci_windows(rom):
    """Pin the SCI backpressure blocker to the already identified ROM."""
    result = []
    for module, name, start, end, finding, address, digest in SCI_WINDOWS:
        item = window(rom, module, name, start, end, finding)
        require(item['rom_storage_address'], hx(address), 'Reviewed SCI owner address ' + name)
        require(item['sha256'], digest, 'Reviewed SCI owner window ' + name)
        result.append(item)
    return result


class ROMModuleView:
    """Read a bounded ROM module with the scalar reader's RVA interface."""
    def __init__(self, rom, module):
        self.rom = rom
        self.module = rom.modules[module]
        self.image_base = self.module['nominal_virtual_base']

    def rva_offset(self, rva, size=1):
        self.rom.rva_read(self.module, rva, size)
        return rva

    def u16(self, rva):
        return struct.unpack('<H', self.rom.rva_read(self.module, rva, 2))[0]

    def rva_u32(self, rva):
        return struct.unpack('<I', self.rom.rva_read(self.module, rva, 4))[0]


class Scalars:
    """Decode only generic SH operand forms needed for reviewed scalars.

    ISA masks below describe instruction families, not copied owner bytes.
    The returned metadata contains protocol meaning rather than instructions.
    """
    def __init__(self, pe, module='mapledev.dll'):
        self.pe = pe
        self.module = module
        self.checks = []

    def word(self, at):
        return self.pe.u16(self.pe.rva_offset(at, 2))

    def record(self, name, at, value, expected):
        require(value, expected, name)
        self.checks.append(dict(id=name, module=self.module, rva=hx(at), value=value))

    def immediate(self, name, at, register, expected, add=False):
        word = self.word(at)
        require(word >> 12, 7 if add else 14, name + ' operand family')
        require((word >> 8) & 15, register, name + ' register')
        value = word & 255
        if value & 128:
            value -= 256
        self.record(name, at, value, expected)

    def literal(self, name, at, expected, pointer=False):
        word = self.word(at)
        require(word >> 12, 13, name + ' literal family')
        literal = ((at + 4) & ~3) + 4 * (word & 255)
        value = self.pe.rva_u32(literal)
        if pointer:
            value -= self.pe.image_base
        self.record(name, at, hx(value), hx(expected))

    def compare_byte(self, name, at, expected):
        word = self.word(at)
        require(word >> 8, 0x88, name + ' compare family')
        self.record(name, at, word & 255, expected)

    def short_literal(self, name, at, expected):
        word = self.word(at)
        require(word >> 12, 9, name + ' short literal family')
        literal = at + 4 + 2 * (word & 255)
        value = self.word(literal)
        self.record(name, at, value, expected)

    def gbr_byte(self, name, at, expected, store=False):
        word = self.word(at)
        require(word >> 8, 0xc0 if store else 0xc4, name + ' byte family')
        self.record(name, at, word & 255, expected)

    def register_move(self, name, at, source, destination):
        word = self.word(at)
        require(word & 0xf00f, 0x6003, name + ' move family')
        self.record(name, at, dict(source=(word >> 4) & 15, destination=(word >> 8) & 15),
                    dict(source=source, destination=destination))

    def register_test(self, name, at, first, second):
        word = self.word(at)
        require(word & 0xf00f, 0x2008, name + ' test family')
        self.record(name, at, [(word >> 8) & 15, (word >> 4) & 15], [first, second])

    def push(self, name, at, source):
        word=self.word(at)
        if source=='pr':
            require(word & 0xf0ff, 0x4022, name + ' push PR family')
        else:
            require(word & 0xf00f, 0x2006, name + ' push family')
            require((word >> 4) & 15, source, name + ' source register')
        require((word >> 8) & 15, 15, name + ' stack register')
        self.record(name, at, dict(source=source, stack_delta=-4),
                    dict(source=source, stack_delta=-4))

    def long_store(self, name, at, source, base, byte_offset):
        word=self.word(at)
        require(word >> 12, 1, name + ' long-store family')
        value=dict(source=(word >> 4) & 15, base=(word >> 8) & 15,
                   byte_offset=(word & 15)*4)
        self.record(name, at, value, dict(source=source, base=base, byte_offset=byte_offset))

    def branch(self, name, at, expected, condition):
        word = self.word(at)
        families = {'true': 0x89, 'false': 0x8b, 'true_delayed': 0x8d, 'false_delayed': 0x8f}
        require(word >> 8, families[condition], name + ' branch family')
        offset = word & 255
        if offset & 128:
            offset -= 256
        self.record(name, at, hx(at + 4 + 2 * offset), hx(expected))


def reviewed_wait_abi(rom):
    """Resolve this DLL's export through its actual ROM section mapping."""
    require(len(rom.data), CE_WAIT_ABI_METADATA['kernel_bytes'], 'CE wait exact kernel size')
    require(hashlib.sha256(rom.data).hexdigest(), CE_WAIT_ABI_METADATA['kernel_sha256'],
            'CE wait exact kernel hash')
    module=rom.modules['coredll.dll']
    exports=[entry for entry in rom.exports(module) if entry['ordinal']==497]
    require(len(exports), 1, 'CE wait unique export')
    export=exports[0]
    require((export.get('name'), export['rva']), ('WaitForSingleObject', 0x877c),
            'CE wait export name/RVA')
    require('forwarder' in export, False, 'CE wait concrete export')
    require(module['nominal_virtual_base'], 0x01e50000, 'CE wait module nominal base')
    require(hx(export['rom_storage_address']), CE_WAIT_ABI_METADATA['rom_storage_address'],
            'CE wait export ROM mapping')
    windows=reviewed_wait_windows(rom)
    scalar=Scalars(ROMModuleView(rom, 'coredll.dll'), 'coredll.dll')
    scalar.push('ce_wait_saved_r8', 0x877c, 8)
    scalar.push('ce_wait_saved_pr', 0x877e, 'pr')
    homes=[]
    for at, source, offset in ((0x8780, 5, 12), (0x8782, 4, 8),
                               (0x8784, 6, 16), (0x8786, 7, 20)):
        scalar.long_store('ce_wait_home_r'+str(source), at, source, 15, offset)
        homes.append(dict(rva=hx(at), source_register=source, callee_sp_offset=offset,
                          caller_sp_offset=offset-CE_WAIT_ABI_METADATA['callee_frame_bytes']))
    require(sorted(home['caller_sp_offset'] for home in homes),
            CE_WAIT_ABI_METADATA['caller_home_offsets'], 'CE wait caller home coverage')
    return dict(CE_WAIT_ABI_METADATA, home_stores=homes,
                source_contracts=windows, decoded_scalar_checks=scalar.checks)


def source_audit(startup_dir):
    root = Path(startup_dir)
    identities = [identity(root / name, name) for name, _, _ in IDENTITIES]
    for result, (name, size, digest) in zip(identities, IDENTITIES):
        require((result['bytes'], result['sha256']), (size, digest), 'Exact write owner ' + name)
    rom = ROM(root / '0WINCEOS.BIN')
    wait=reviewed_wait_abi(rom)
    pe = PE(root / 'WINCE/MAPLEDEV.DLL')
    external_windows = reviewed_pe_windows(pe)
    sci_windows = reviewed_sci_windows(rom)
    completion_windows = reviewed_completion_windows(rom)
    s = Scalars(pe)
    for name, at, register, value, add in (
        ('block_size_shift', 0x38dc, 3, 5, False),
        ('block_size_addend', 0x38e0, 2, 32, True),
        ('write_phase_count_shift', 0x38f0, 1, -4, False),
        ('write_phase_zero', 0x7ee2, 10, 0, False),
        ('write_location_phase_offset', 0x7eea, 0, 17, False),
        ('write_receive_capacity_bytes', 0x7f5a, 3, 4, False),
        ('write_command', 0x7f5e, 5, 12, False),
        ('write_phase_increment', 0x7f9e, 0, 1, True),
        ('finalizer_phase_offset', 0x42b2, 0, 49, False),
        ('finalizer_receive_capacity_bytes', 0x42d2, 1, 8, False),
        ('finalizer_command', 0x42d4, 5, 13, False),
        ('finalizer_request_bytes_before_raw_header_conversion', 0x42d6, 7, 20, False),
        ('single_read_final_phase', 0x7e4e, 6, 0, False),
        ('whole_write_attempt_counter_initial', 0x7cd2, 9, 0, False),
        ('whole_write_attempt_counter_step', 0x7cd6, 9, 1, True),
        ('whole_write_attempt_limit', 0x7cea, 2, 3, False),
        ('whole_read_attempt_limit', 0x7c9e, 2, 3, False),
        ('packet_event_wait_ms', 0x4ce8, 5, 16, False),
        ('packet_wait_failed_result', 0x4d26, 2, -1, False),
    ):
        s.immediate(name, at, register, value, add)
    for name, at, value, pointer in (
        ('write_packet_submit_target', 0x7f54, 0x4c14, True),
        ('write_finalizer_target', 0x7fbe, 0x42a0, True),
        ('read_finalizer_target', 0x7e50, 0x42a0, True),
        ('finalizer_packet_submit_target', 0x42c8, 0x4c14, True),
        ('whole_write_target', 0x7cd4, 0x7ea0, True),
        ('whole_read_target', 0x7c88, 0x7d0c, True),
        ('write_invalid_ack_error', 0x7f92, 0x86850010, False),
        ('finalizer_file_error', 0x42f8, 0x86850010, False),
        ('finalizer_other_response_error', 0x42fe, 0x868500b0, False),
        ('whole_write_retry_error', 0x7ce4, 0x86850010, False),
        ('raw_frame_ioctl', 0x4cca, 0x220480, False),
        ('timeout_poke_frame_ioctl', 0x4d12, 0x220484, False),
        ('memory_card_function', 0x42a4, 0x02000000, False),
        ('packet_submit_error', 0x4ce0, 0x86850050, False),
        ('packet_timeout_poke_error', 0x4d1c, 0x86850050, False),
        ('packet_wait_failed_error', 0x4d2c, 0x868500b0, False),
    ):
        s.literal(name, at, value, pointer)
    s.compare_byte('write_required_ack', 0x7f7e, 7)
    s.gbr_byte('write_phase_increment_load_offset', 0x7f9a, 17)
    s.gbr_byte('write_phase_increment_store_offset', 0x7fa0, 17, True)
    s.gbr_byte('write_finalizer_phase_load_offset', 0x7fba, 17)
    s.gbr_byte('decoded_read_phase_count_offset', 0x3908, 46)
    s.gbr_byte('decoded_write_phase_count_offset', 0x3920, 47)
    s.short_literal('packet_pending_win32_error', 0x4cda, 997)
    s.short_literal('packet_wait_timeout_result', 0x4cf2, 258)
    for name, at, target, condition in (
        ('write_non_ack_error_branch', 0x7f80, 0x7f8c, 'false'),
        ('write_zero_payload_success_branch', 0x7f8a, 0x7f98, 'true'),
        ('finalizer_ack_success_branch', 0x42f0, 0x4304, 'true'),
        ('finalizer_non_file_error_branch', 0x42f6, 0x42fe, 'false'),
        ('whole_write_error_retry_backedge', 0x7cf2, 0x7cd4, 'false'),
        ('whole_read_error_retry_backedge', 0x7ca6, 0x7c88, 'false'),
        ('packet_wait_loop_backedge', 0x4d24, 0x4ce6, 'false'),
        ('packet_pending_submit_enters_wait', 0x4cde, 0x4ce6, 'true'),
        ('packet_non_timeout_wait_branch', 0x4cf8, 0x4d22, 'false'),
        ('packet_successful_timeout_poke_branch', 0x4d1a, 0x4d22, 'false'),
        ('packet_non_failed_wait_branch', 0x4d2a, 0x4d32, 'false'),
    ):
        s.branch(name, at, target, condition)
    maple = Scalars(PE(root / 'WINCE/MAPLE.DLL'), 'maple.dll')
    maple.immediate('raw_function_expected_response_zero', 0x2eb2, 0, 0)
    maple.gbr_byte('raw_function_expected_response_offset', 0x2eb8, 21, True)
    maple.immediate('completion_received_header_bytes', 0x2bfa, 7, 4, True)
    maple.branch('zero_expected_response_accepts_raw_reply', 0x5c5c, 0x5c68, 'true')
    maple.branch('completion_within_caller_capacity_branch', 0x2c02, 0x2c06, 'false')
    maple.literal('timeout_poke_token_lookup', 0x2f44, 0x23b8, True)
    maple.immediate('timeout_poke_success_status', 0x2f60, 0, 0)
    maple.literal('worker_completion_target', 0x5db0, 0x5b0c, True)
    maple.literal('worker_interrupt_status_query_target', 0x5d9e, 0x16dc, True)
    maple.literal('worker_interrupt_status_query_import', 0x16e8, 0x62cc, True)
    maple.literal('worker_interrupt_status_query_ioctl', 0x16f4, 0x10000)
    maple.immediate('worker_dma_event_wait_ms', 0x5d9c, 5, 60)
    maple.immediate('shutdown_requested_value', 0x1992, 3, 1)
    maple.short_literal('shutdown_worker_wait_ms', 0x19b0, 1000)
    sci = Scalars(ROMModuleView(rom, 'wsegacd.dll'), 'wsegacd.dll')
    sci.immediate('gd_dma_read_request_command', 0x2d8a, 4, 17)
    sci.immediate('gd_stream_request_command', 0x2db6, 4, 38)
    sci.literal('gd_dma_read_request_vector', 0x2d88, 0x8c0010f0)
    sci.literal('gd_stream_request_vector', 0x2db4, 0x8c0010f0)
    sci.register_move('gd_dma_read_preserves_returned_token', 0x2d96, 0, 10)
    sci.register_move('gd_stream_preserves_returned_token', 0x2dc2, 0, 10)
    sci.register_move('gd_completion_receives_returned_token', 0x2dca, 10, 5)
    sci.literal('gd_completion_helper_target', 0x2dd2, 0x19bc, True)
    sci.register_test('gd_completion_tests_token', 0x19c6, 5, 5)
    sci.branch('gd_nonzero_token_completion_branch', 0x19c8, 0x19d0, 'false')
    sci.literal('gd_zero_token_error', 0x19ca, 0xc0000185)
    coredll = {entry['ordinal']: entry for entry in rom.exports(rom.modules['coredll.dll'])}
    import_checks = []
    for view, stub, expected_ordinal, expected_name in (
        (s, 0xc598, 179, 'DeviceIoControl'),
        (s, 0xc664, 497, 'WaitForSingleObject'),
        (s, 0xc64c, 516, 'GetLastError'),
        (maple, 0x638c, 535, 'GetTickCount'),
        (maple, 0x6398, 4, 'EnterCriticalSection'),
        (maple, 0x63a4, 5, 'LeaveCriticalSection'),
        (maple, 0x62cc, 557, 'KernelIoControl'),
        (maple, 0x6314, 497, 'WaitForSingleObject'),
    ):
        word = view.word(stub)
        require(word >> 8, 0xd0, expected_name + ' import address load')
        literal = ((stub + 4) & ~3) + 4 * (word & 255)
        iat = view.pe.rva_u32(literal) - view.pe.image_base
        entries = [(imp['dll'], symbol) for imp in view.pe.imports()
                   for symbol in imp['symbols'] if symbol['iat_rva'] == iat]
        require(len(entries), 1, expected_name + ' unique import')
        dll, symbol = entries[0]
        require(dll.lower(), 'coredll.dll', expected_name + ' import DLL')
        require(symbol['ordinal'], expected_ordinal, expected_name + ' ordinal')
        export = coredll[expected_ordinal]
        require(export.get('name'), expected_name, 'ROM export ' + expected_name)
        import_checks.append(dict(module=view.module, stub_rva=hx(stub), iat_rva=hx(iat),
            ordinal=expected_ordinal, resolved_name=expected_name, export_rva=hx(export['rva']),
            export_entry_sha256=export['entry32_sha256']))
    # Our advertised protocol geometry, interpreted by the reviewed decoder.
    definition = 0x000f4100
    block_bytes = 32 * (1 + ((definition >> 16) & 255))
    read_phases, write_phases = (definition >> 8) & 15, (definition >> 12) & 15
    phase_bytes = (block_bytes + write_phases - 1) // write_phases
    return dict(
        format='armada-ce-maple-write-owner-contracts-v1', license='GPL-3.0-only',
        scope='Exact-source static protocol/control-flow evidence; no owner executable bytes or disassembly exported, no owner execution or patching.',
        source_identities=identities,
        external_text_equivalence=[relocated_text(rom, name, PE(root / path)) for name, path in
                                   (('mapledev.dll', 'WINCE/MAPLEDEV.DLL'), ('maple.dll', 'WINCE/MAPLE.DLL'))],
        source_contracts=[window(rom, 'mapledev.dll', name, start, end, finding)
                          for name, start, end, finding, _ in WINDOWS] +
                         [window(rom, *spec) for spec in SUPPORT_WINDOWS] + sci_windows + wait['source_contracts'] +
                         completion_windows,
        external_source_windows=external_windows,
        decoded_scalar_checks=s.checks + maple.checks + sci.checks + wait['decoded_scalar_checks'],
        resolved_coredll_imports=import_checks,
        ce_wait_abi={key:value for key,value in wait.items()
                     if key not in ('source_contracts','decoded_scalar_checks')},
        advertised_geometry=dict(normalized_function_definition=hx(definition),
            dma_function_definition=hx(0x00410f00), block_bytes=block_bytes,
            read_phases=read_phases, write_phases=write_phases, write_phase_bytes=phase_bytes,
            write_selector_phases=list(range(write_phases)), write_payload_words=2+phase_bytes//4,
            write_request_bytes=12+phase_bytes, write_final_selector_phase=write_phases,
            single_read_final_selector_phase=0),
        response_contract=dict(write_phase_success=dict(command=7, payload_words=0, receive_capacity_bytes=4),
            finalizer_success=dict(command=7, receive_capacity_bytes=8),
            write_phase_again='Maps to0x86850010, causing at most3 whole-block attempts in the reviewed outer wrapper.',
            finalizer_again='Maps to0x868500b0, which does not enter the reviewed whole-block retry.',
            finalizer_file_error='Maps to0x86850010, causing at most3 whole-block attempts; it is not final-SYNC-only retry.',
            deferred_commit_busy_is_transparent=False),
        pending_request_lifetime=dict(event_wait_ms=16, pending_error=997, wait_timeout=258,
            timeout_ioctl=hx(0x220484), timeout_ioctl_cancels_original_request=False,
            successful_timeout_poke_repeats_wait=True,
            caller_buffer_retained_through_ordinary_timeout_pokes=True,
            worker_critical_section_lease_proved=False,
            worker_dma_event_wait_ms=60,
            root_shutdown_worker_wait_ms=1000,
            note='These are the reviewed ordinary owner paths. A timeout-poke name does not imply cancellation, and lack of cancellation does not exclude thread/process teardown or root unload.'),
        completion_epoch_contract=dict(status_query_before_consumer=True,
            status_query_acknowledges_captured_raw_bits=True,
            interrupt_done_clears_later_raw_status=False,
            builder_zeros_receive_before_next_completion=True,
            raw_completion_is_coalesced_status=True,
            required_service_policy='Retain the original admitted MDEN0 DMA park while executing the bounded service with CPU interrupts unmasked. Repeat full owner/context/traffic/RX and parked-MMIO witnesses before publication and the one original consumer; perform its paired POST only after that consumer.',
            invalid_source_may_be_skipped=False,
            note='An enabled-DMA interval inside one held completion can create a late completion bit for the next worker cycle. Matching owner pointers alone does not establish that a zeroed RX belongs to a fresh completed DMA; no FIFO of completion events is assumed.'),
        sci_backpressure_contract=dict(request_commands=[17, 38], zero_request_token_error=hx(0xc0000185),
            zero_token_is_transparent_busy=False, completion_helper_reissues_zero_token_request=False,
            requires_loan_aware_gd_wait_or_deferral=True, requires_cdda_exclusion=True,
            hardware_storage_write_admitted=False,
            note='Holding the shared GD/SCI owner busy and returning REQUEST0 can make WSEGACD fail an incoming game read. A separate storage worker or an unmasked Maple service does not itself supply transparent SCI backpressure.'),
        implications=[
            'The current generic four-phase/phase4 commit selector matches this CE write owner for the advertised standard geometry; no phase3 translation is warranted.',
            'Do not acknowledge a merely queued or volatile commit as durable. Returning AGAIN from the finalizer is visible failure to this owner, not a transparent asynchronous completion protocol.',
            'Each whole-block retry restarts phase0; an attempted storage transaction must retain exact immutable bytes and reject changed replacements until its outcome is resolved.',
            'Exact CMD12 has only4 bytes of guest receive capacity. Validate a complete FILEERR privately; its canonical4-byte header may be published for this exact CMD12/capacity4 owner because the caller examines only its response header and the unchanged generic consumer clips to4 bytes. A full8-byte FILEERR must not be published into that4-byte requested allocation. CMD13 finalizer capacity is8 bytes.',
            'A same-worker storage service can leave the original raw IRP pending through ordinary timeout pokes. Its scoped original MDEN0 DMA park must survive the bounded unmasked service so the source status query, consumer, rebuild and InterruptDone cycle cannot gain a replayed completion. CPU interrupts must be restored during I/O, with fresh full source/ownership/capacity and parked-MMIO witnesses before publication; no across-I/O native critical-section lease was established.',
            'Before hardware storage writes, the SCI loan needs an explicit GD wait/deferral path and CDDA exclusion. REQUEST0 is an immediate WSEGACD I/O error, so an idle-GD check followed by a busy return for later reads is insufficient.',
            'A GNU-SH call to the original CE WaitForSingleObject must reserve16 caller argument-home bytes. Without this adapter, its home stores overwrite the caller saved PR/registers before any wait result can be captured.',
        ],
        limitations=[
            'Static analysis of these exact owner files does not establish hardware timing, bus ownership, durable storage, or application-level save success.',
            'Application-level retry outside the reviewed owner chain is not proved. The16ms packet event wait and60ms worker DMA-event wait are not global storage deadlines or permission to hold the CPU interrupt mask across scheduling. A scoped retained DMA park is separate and requires fresh owner/MMIO witnesses; external thread/process teardown remains unproved.',
            'Generic MAPLE response dispatch and every guest publication still require the separately reviewed live ownership/fence/capacity witnesses.',
        ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check', type=Path)
    args = parser.parse_args()
    try:
        if args.output and args.output.resolve().is_relative_to(args.startup_dir.resolve()):
            raise ValueError('Metadata output must not overwrite an owner input')
        result = source_audit(args.startup_dir)
        if args.check and json.loads(args.check.read_text()) != result:
            raise ValueError('Checked-in write owner metadata differs from exact source audit')
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + '\n')
        elif args.check:
            print(json.dumps(dict(matched=True, source_contracts=len(result['source_contracts']),
                                  scalar_checks=len(result['decoded_scalar_checks']), owner_bytes_exported=False)))
        else:
            print(json.dumps(result, indent=2))
    except (OSError, ValueError, UnicodeError, KeyError, struct.error) as error:
        parser.exit(1, 'Write owner audit failed: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
