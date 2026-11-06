#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute carried-video validation, first restore and the actual photo renderer.

PVR register values and VRAM are authored hardware fixtures. Every display
capture, VID1 checksum/import, restore, glyph and pixel-store instruction is
the actual linked code. This does not certify a television's scanout timing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_retail_stack import base, load
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from test_smash_menu_loader_runtime import PHASE_VIDEO_REGS, phase_video_seed
from test_smash_phase_clock_linked import ClockCPU
from test_smash_phase_stage import PHASES, trace

OFFSETS = (0x44, 0x48, 0x50, 0x54, 0x5c, 0xcc, 0xd0,
           0xd4, 0xd8, 0xdc, 0xe8, 0xec, 0xf0, 0x40)
REGS = tuple(0xa05f8000 + offset for offset in OFFSETS)
VRAM, VRAM_BYTES, FRAME_BYTES = 0xa5000000, 8 * 1024 * 1024, 640 * 480 * 2
STACK, CALLER, STATE, VIDEO, WIRE = 0x8c00d000, 0x8c003000, 0x8cc00000, 0x8cc00100, 0x8cc00200


def require(value, message):
    if not value:
        raise ValueError(message)


class VideoCPU(ClockCPU):
    def __init__(self, image, regs):
        self.video_ready = False
        super().__init__(image)
        self.symbols = image[4]
        self.pvr = dict(zip(REGS, regs))
        self.pvr_reads, self.pvr_writes = [], []
        self.vram = bytearray(b'\x5a') * VRAM_BYTES
        self.vram_writes = 0
        self.vram_begin, self.vram_end = VRAM + VRAM_BYTES, VRAM
        self.stop = None
        self.lines = []
        self.resets = 0
        self.video_ready = True

    def read(self, address, count=4):
        if not self.video_ready:
            return super().read(address, count)
        if address in self.pvr:
            require(count == 4, 'Renderer used a nonword PVR access')
            self.pvr_reads.append(address)
            return self.pvr[address]
        if VRAM <= address <= VRAM + VRAM_BYTES - count:
            return int.from_bytes(self.vram[address - VRAM:address - VRAM + count], 'little')
        require(0x0c000000 <= address & 0x1fffffff < 0x0d000000,
                f'Renderer read unlisted hardware {address:08x}/{count}')
        return super().read(address, count)

    def write(self, address, value, count=4):
        if not self.video_ready:
            return super().write(address, value, count)
        if address in self.pvr:
            require(count == 4, 'Renderer used a nonword PVR write')
            self.pvr[address] = value & 0xffffffff
            self.pvr_writes.append((address, value & 0xffffffff))
            return
        if VRAM <= address <= VRAM + VRAM_BYTES - count:
            require(count in (2, 4) and address % count == 0, 'Renderer used an unaligned/nonpixel VRAM write')
            value &= (1 << (8 * count)) - 1
            self.vram[address - VRAM:address - VRAM + count] = value.to_bytes(count, 'little')
            self.vram_writes += 1
            self.vram_begin = min(self.vram_begin, address)
            self.vram_end = max(self.vram_end, address + count)
            return
        require(0x0c000000 <= address & 0x1fffffff < 0x0d000000,
                f'Renderer wrote unlisted hardware {address:08x}/{count}')
        return super().write(address, value, count)

    def string(self, address):
        out = bytearray()
        for i in range(96):
            byte = self.read(address + i, 1)
            if not byte:
                return out.decode('ascii')
            out.append(byte)
        raise ValueError('Actual renderer received an unterminated diagnostic line')

    def invoke(self, name, args=(), before_restore=False, report=False):
        self.r = [(0x53530000 + i * 0x12345) & 0xffffffff for i in range(16)]
        self.r[4:4 + len(args)] = args
        self.r[15], self.pr, self.pc, self.sr = STACK, CALLER, self.symbols[name][0], 0x700000f0
        before = (self.r[8:15].copy(), self.gbr, self.vbr, self.fr.copy(), self.fpscr)
        self.writes = []
        self.instructions = 0
        reset_return, reporting, recent = None, False, []
        for _ in range(5000000):
            if self.pc == CALLER:
                require(not report and not before_restore, 'Terminal/first-restore diagnostic unexpectedly returned')
                require((self.r[8:15], self.gbr, self.vbr, self.fr, self.fpscr) == before and
                        self.r[15] == STACK and self.sr & ~0x301 == 0x700000f0 & ~0x301,
                        'Video helper changed caller nonvolatile/FPU/IRQ state')
                return 'returned'
            if self.pc == self.symbols['_stopped'][0]:
                self.stop = (self.string(self.r[4]), self.r[5])
                return 'refused'
            if before_restore and self.pc == self.symbols['_retail_display_restore'][0]:
                return 'before_first_restore'
            if report:
                if self.pc == self.symbols['_retail_display_phase_reset'][0]:
                    self.resets += 1
                    reset_return = self.pr
                if reset_return is not None and self.pc == reset_return:
                    reporting = True
                if reporting and self.pc == self.symbols['_retail_display_line'][0]:
                    self.lines.append(self.string(self.r[4]))
                recent.append(self.pc)
                if len(recent) > 24:
                    del recent[0]
            self.step()
            if report and len(self.lines) >= 20 and len(recent) == 24 and len(set(recent)) <= 3:
                require(len(self.lines) == 20, 'Actual photo renderer produced more than20 report rows')
                return 'held'
        raise ValueError('Actual video import/renderer exceeded its finite instruction bound')


def checksum(data):
    data = bytearray(data)
    data[72:76] = bytes(4)
    struct.pack_into('<I', data, 72, zlib.crc32(data))
    return bytes(data)


def audit(outerbuild, childbuild):
    require(__debug__, 'Run linked instruction models without Python optimization')
    outerbuild, childbuild = Path(outerbuild), Path(childbuild)
    outer, child = read_elf(outerbuild / 'stage.elf'), read_elf(childbuild / 'stage.elf')
    helpers = {}
    for label, image, names in (
        ('outer', outer, ('_retail_display_capture', '_kui_smash_phase_video_capture')),
        ('child', child, ('_smash_phase_stage_display', '_kui_smash_phase_video_valid',
                          '_retail_display_capture', '_retail_display_restore',
                          '_retail_display_phase_reset', '_retail_display_line')),
    ):
        for name in names:
            address, body, _ = symbol_bytes(image, name, 2)
            helpers[label + '/' + name] = dict(address=hex(address), bytes=len(body),
                                               sha256=hashlib.sha256(body).hexdigest())
    seed_at = child[4]['__smash_phase_video_seed'][0]
    display_at = child[4]['_display'][0]
    require(child[4]['_display'][1] == 56, 'Retained display capture is not the exact14-register object')
    cases = []
    for mode in ('vga_progressive', 'ntsc_interlaced', 'pal_interlaced'):
        original = PHASE_VIDEO_REGS.copy()
        if mode != 'vga_progressive':
            original[0], original[3] = 5, 1280
            original[4] = 319 | (239 << 10) | (321 << 20)
            original[6] = 0x150 if mode == 'ntsc_interlaced' else 0x190
        # Actual outer capture and CRC sealer create the bytes imported below.
        capture = VideoCPU(outer, original)
        require(capture.invoke('_retail_display_capture', [STATE]) == 'returned', 'Outer K-UI capture failed')
        require(capture.block(STATE, 56) == struct.pack('<14I', *original) and
                capture.pvr_reads == list(REGS) and not capture.pvr_writes and not capture.vram_writes,
                'Outer capture did not read exactly the14 original K-UI registers')
        require(capture.invoke('_kui_smash_phase_video_capture', [VIDEO, STATE]) == 'returned' and
                capture.r[0] == 1, 'Original K-UI mode was not sealed by actual VID1 capture')
        require(capture.pvr_reads == list(REGS) and not capture.pvr_writes and not capture.vram_writes,
                'VID1 checksum/sealing unexpectedly touched video hardware')
        video = capture.block(VIDEO, 80)
        require(video == phase_video_seed(original), 'Actual VID1 bytes differ from independent IEEE CRC fixture')
        inherited = original.copy()
        inherited[0] &= ~1
        inherited[2], inherited[3] = 0x200000, 0x200500
        inherited[10] |= 0x108
        inherited[5] ^= 0x10000  # The menu's timing must not replace the captured cable mode.
        cpu = VideoCPU(child, inherited)
        cpu.store_block(seed_at, video)
        require(cpu.invoke('_kui_retail_stage_main', [WIRE], before_restore=True) == 'before_first_restore',
                'Native child did not reach its first restore through the real main entry')
        require(cpu.block(display_at, 56) == struct.pack('<14I', *original) and
                cpu.pvr_reads == list(REGS) and not cpu.pvr_writes and not cpu.vram_writes and
                list(cpu.pvr.values()) == inherited,
                'Child restored hardware before validating/importing original K-UI state')
        cpu.store_block(child[4]['_kui_smash_phase_stage_trace'][0], trace(PHASES))
        require(cpu.invoke('_smash_phase_stage_report', report=True) == 'held', 'Actual video report did not stop')
        final = [cpu.pvr[address] for address in REGS]
        require(final == original and final[2] == 0 and final[0] & 15 == 5 and not final[10] & 0x108,
                'Actual renderer retained blank/nonzero/wrong-format menu scanout')
        require(cpu.pvr_writes == [(REGS[0], original[0] & ~1)] +
                list(zip(REGS[1:], original[1:])) + [(REGS[0], original[0])],
                'Renderer did not disable, restore the carried cable mode and re-enable in order')
        require(cpu.pvr_reads == list(REGS), 'Terminal report made unexpected video-register reads')
        require(cpu.resets == 1 and len(cpu.lines) == 20 and max(map(len, cpu.lines)) <= 49 and
                'BEFORE MDE ENTRY' in cpu.lines[-2] and 'GAME SGA LOAD NOT TIMED' in cpu.lines[-1],
                'Actual rendered page does not preserve the bounded pre-game timing report')
        require(cpu.vram_begin == VRAM and cpu.vram_end == VRAM + FRAME_BYTES and
                cpu.vram[FRAME_BYTES:] == b'\x5a' * (VRAM_BYTES - FRAME_BYTES),
                'Renderer wrote outside its640x480 framebuffer0 reservation')
        frame = cpu.vram[:FRAME_BYTES]
        white = frame.count(b'\xff\xff')
        require(white > 1000 and frame[:20 * 640 * 2] == b'\x64\x08' * (20 * 640),
                'Current RGB565 scanout has no visible glyph pixels or escaped the top margin')
        fields = [sum(cpu.vram[(row * 640) * 2:((row + 1) * 640) * 2].count(b'\xff\xff')
                      for row in range(parity, 480, 2)) for parity in (0, 1)]
        require(all(value > 500 for value in fields), 'One interlaced field lost the doubled visible glyph rows')
        cases.append(dict(mode=mode, original_regs=original, inherited_blank_regs=inherited,
                          actual_capture_crc32=video[72:76].hex(), main_validated_before_first_restore=True,
                          lines=len(cpu.lines), max_columns=max(map(len, cpu.lines)),
                          glyph_pixels=white, field_glyph_pixels=fields, vram_pixel_stores=cpu.vram_writes,
                          renderer_instructions=cpu.instructions, framebuffer_sha256=hashlib.sha256(frame).hexdigest(),
                          timing_words_preserved=True))

    rejected = []
    good = phase_video_seed()
    bad_crc = good[:72] + bytes(4) + good[76:]
    bad_frame = bytearray(good)
    struct.pack_into('<I', bad_frame, 16 + 2 * 4, 0x200000)
    blank = bytearray(good)
    struct.pack_into('<I', blank, 16 + 10 * 4, 0x108)
    wrong_format = bytearray(good)
    struct.pack_into('<I', wrong_format, 16, PHASE_VIDEO_REGS[0] ^ 4)
    for name, video in (('missing_header', bytes(80)), ('bad_crc', bad_crc),
                        ('nonzero_framebuffer', checksum(bad_frame)),
                        ('blank_scanout', checksum(blank)), ('wrong_pixel_mode', checksum(wrong_format))):
        cpu = VideoCPU(child, PHASE_VIDEO_REGS)
        cpu.store_block(seed_at, video)
        require(cpu.invoke('_smash_phase_stage_display', [STATE]) == 'refused' and
                cpu.stop == ('TIMING ORIGINAL VIDEO INVALID', 0), 'Malformed VID1 was accepted: ' + name)
        require(not cpu.pvr_writes and not cpu.vram_writes and cpu.pvr_reads == list(REGS),
                'Malformed VID1 changed hardware before its existing terminal refusal: ' + name)
        require(cpu.block(STATE, 56) == struct.pack('<14I', *PHASE_VIDEO_REGS),
                'Invalid VID1 replaced inherited refusal-display state')
        rejected.append(name)
    graphs = [childbuild / 'stage/src/loader/vmu_native_heap_probe_stage.ci']
    _, calls = load(graphs, qualified=True)
    owners = [name for name in calls if base(name) == 'kui_retail_stage_main']
    require(len(owners) == 1 and {'smash_phase_stage_display', 'retail_display_restore'} <=
            {base(target) for target, _ in calls[owners[0]]}, 'Actual child main lacks its video-import/restore calls')
    return dict(schema='kui.smash-phase.video-renderer.v1', status='PASS',
                outer_stage_elf_sha256=hashlib.sha256(outer[0]).hexdigest(),
                child_stage_elf_sha256=hashlib.sha256(child[0]).hexdigest(), helpers=helpers,
                cases=cases, invalid_video_refusals=rejected,
                actual_renderer_executed=True, video_registers=14, authored_vram_bytes=VRAM_BYTES,
                compiler_graph_sha256={str(p.relative_to(childbuild)): hashlib.sha256(p.read_bytes()).hexdigest()
                                       for p in graphs},
                unlisted_hardware_reads=0, unlisted_hardware_writes=0,
                limits=['Actual outer capture/VID1 CRC and child main/import/report/restore/glyph/pixel-store instructions execute; PVR values and8MiB VRAM are authored fixtures.',
                        'Invalid VID1 reaches the existing terminal refusal before any video write; that refusal renderer is a finite boundary here.',
                        'These checks prove RGB565 framebuffer and carried cable-register consistency in the model, not physical television visibility, electrical timing or console elapsed time.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--outer', type=Path, required=True)
    parser.add_argument('--child', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    rendered = json.dumps(audit(args.outer, args.child), indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end='')


if __name__ == '__main__':
    main()
