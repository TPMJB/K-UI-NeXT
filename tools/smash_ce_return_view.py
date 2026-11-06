#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind a read-only CE terminal view to exact frozen helper/reader ELFs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import zlib

from check_vmu_native_worker_gateway import read_elf, symbol_bytes

ROOT = Path(__file__).resolve().parents[1]
LAYOUT_NAMES = (
    'runtime_bytes','loan_bytes','region_bytes','manifest_bytes',
    'runtime.started','runtime.ready','runtime.owner','runtime.handling','runtime.worker',
    'runtime.faulted','runtime.busy_rounds','runtime.endpoint.self','runtime.endpoint.magic',
    'runtime.endpoint.started','runtime.endpoint.handling','runtime.endpoint.protocol.initialized',
    'runtime.endpoint.protocol.killed','runtime.endpoint.protocol.active',
    'runtime.endpoint.protocol.commit_attempted','runtime.endpoint.protocol.phases',
    'runtime.queue.self','runtime.queue.magic','runtime.queue.state','runtime.queue.failures',
    'runtime.sd.self','runtime.sd.magic','runtime.sd.started','runtime.sd.busy','runtime.sd.faulted',
    'runtime.sd.bound','runtime.sd.store.io.context','runtime.sd.pio.self','runtime.sd.pio.magic',
    'runtime.sd.pio.fault','runtime.sd.pio.acquired','runtime.sd.pio.captured','runtime.sd.pio.selected',
    'runtime.sd.pio.mode','runtime.sd.pio.data','runtime.sd.pio.scr','runtime.sd.pio.smr',
    'runtime.sd.pio.brr','runtime.sd.pio.ptr','runtime.sd.pio.standby','runtime.sd.pio.stage',
    'loan.region','loan.manifest','loan.active','loan.fault','loan.begin','loan.end','loan.magic',
    'loan.started','loan.state','loan.claims','loan.releases','loan.first_fault',
    'region.service.initialized','region.service.ops.context','region.service.tracks',
    'region.service.pending','region.service.executing','region.service.callback_due',
    'region.service.error','region.engine.manifest','region.engine.active','region.engine.failed',
    'region.engine.in_irq','region.engine.opened','region.engine.hooked','region.engine.release.armed',
    'region.engine.release.depth','region.port.acquired','region.port.fault','region.stream.state',
    'region.card.stream.active','region.card.transport','region.card.sd.ready','region.card.sd.slow',
    'region.card.sd.blocks','manifest.storage_transport','manifest.card_sectors','manifest.slots',
    'manifest.fault','manifest.active')


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def target_layout(helper_elf, cc=None):
    """Compile constants using the frozen helper's actual native ABI flags."""
    config = Path(helper_elf).parent/'helper-config'
    rows = config.read_text().splitlines()
    require(len(rows) == 2, 'Missing exact helper/native compilation configuration')
    flags = [x for x in shlex.split(rows[0])
             if not x.startswith('-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME=')]
    flags += ['-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME=1']+shlex.split(rows[1])
    compiler = str(cc or os.environ.get('SH_CC') or ROOT/'.deps/sh-elf/bin/sh-elf-gcc')
    source = ROOT/'src/loader/smash_ce_return_view.c'
    with tempfile.TemporaryDirectory(prefix='kui-ce-return-layout-') as name:
        directory = Path(name)
        obj, elf, script = (directory/n for n in ('view.o','layout.elf','layout.ld'))
        script.write_text('OUTPUT_FORMAT("elf32-shl")\nOUTPUT_ARCH(sh)\n'
            'ENTRY(_kui_smash_ce_return_view_layout)\nSECTIONS { . = 0x8ce00000; '
            '.rodata : { KEEP(*(.rodata.kui_smash_ce_return_view_layout)) } '
            '/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }\n')
        subprocess.run([compiler,*flags,'-DKUI_SMASH_CE_RETURN_VIEW_LAYOUT_PROBE=1',
                        '-c',str(source),'-o',str(obj)],cwd=ROOT,check=True,capture_output=True)
        subprocess.run([compiler,'-m4-single','-ml','-nostdlib','-Wl,--gc-sections',
                        '-Wl,--build-id=none','-T'+str(script),str(obj),'-o',str(elf)],
                       cwd=ROOT,check=True,capture_output=True)
        _, data, _ = symbol_bytes(read_elf(elf),'_kui_smash_ce_return_view_layout',1)
    require(len(data) == 4*len(LAYOUT_NAMES), 'Target layout probe field count changed')
    values = dict(zip(LAYOUT_NAMES,struct.unpack('<%dI' % len(LAYOUT_NAMES),data)))
    require(tuple(values[k] for k in LAYOUT_NAMES[:4]) == (0x2240,56,0x640,828),
            'Frozen runtime/loan/native-reader C layouts changed')
    return dict(offsets=values,source_sha256=sha(source.read_bytes()),
                helper_config_sha256=sha(config.read_bytes()),compiler=compiler)


def audit(helper_elf, resident_elf, helper_bin=None, cc=None):
    helper_elf, resident_elf = Path(helper_elf), Path(resident_elf)
    helper_bin = Path(helper_bin) if helper_bin else helper_elf.with_suffix('.bin')
    paths = (helper_elf, resident_elf, helper_bin)
    before = {str(p): sha(p.read_bytes()) for p in paths}
    helper, resident = read_elf(helper_elf), read_elf(resident_elf)
    hs, rs = helper[4], resident[4]
    raw = helper_bin.read_bytes()
    start = 0x0cfc4000
    initialized = [p for p in helper[2] if p[0] == 1 and p[4]]
    require(initialized and all(start <= (p[3] & 0x1fffffff) and
            (p[3] & 0x1fffffff) + p[4] <= 0x0cfce200 for p in initialized),
            'Helper initialized bytes escape the accepted immutable reservation')
    end = max((p[3] & 0x1fffffff) + p[4] for p in initialized)
    require(len(raw) == end-start and len(raw) % 4 == 0,
            'Helper binary does not have the exact linked immutable length')
    reconstructed = bytearray(len(raw))
    for p in initialized:
        offset = (p[3] & 0x1fffffff)-start
        body = helper[0][p[1]:p[1]+p[4]]
        reconstructed[offset:offset+len(body)] = body
    require(bytes(reconstructed) == raw, 'Helper binary differs from complete linked initialized bytes')

    def private(name, size, alignment=4):
        require(name in hs, 'Missing helper private symbol: '+name)
        at, length, kind, owner = hs[name]
        require(kind == 1 and length == size and owner < len(helper[3]),
                'Unexpected helper object type/size: '+name)
        section = helper[3][owner]
        require(section[1] == 8 and section[2] & 3 == 3 and
                0xacfc0000 <= at and at+length <= 0xacfc4000 and at % alignment == 0,
                'Helper object is not private aligned P2 BSS: '+name)
        return at

    view = dict(runtime=private('_runtime', 0x2240, 32),
                loan=private('_loan', 56), continuation=private('_a2_continuation', 0x3f4),
                fence=private('_fence_state', 28), freeze=0xacfcfe78,
                saved_region=private('_vc2_saved_region', 4))
    freeze = hs.get('_kui_armada_ce_maple_traffic_freeze')
    require(freeze and freeze[0] == view['freeze'] and freeze[1:3] == (0, 0) and
            freeze[3] < len(helper[3]) and helper[3][freeze[3]][1] == 8 and
            helper[3][freeze[3]][3] <= freeze[0] <=
            helper[3][freeze[3]][3]+helper[3][freeze[3]][5]-4,
            'Frozen publication latch moved')
    require(hs.get('__kui_maple_write_runtime', (0,))[0] == 1,
            'Helper is not the accepted persistent writable runtime')
    spans = [(view[k], hs[n][1]) for k, n in (
        ('runtime', '_runtime'), ('loan', '_loan'), ('continuation', '_a2_continuation'),
        ('fence', '_fence_state'), ('saved_region', '_vc2_saved_region'))]
    for i, (at, size) in enumerate(spans):
        require(all(at+size <= p or p+n <= at for p, n in spans[:i]),
                'Helper private view objects overlap')
    require(rs.get('__native_dma_region_bytes', (0,))[0] == 0x640,
            'Reader native foreground region geometry changed')
    require('_standalone_reader_region' in rs and '_manifest' in rs,
            'Reader lacks its exact foreground region/map symbols')
    region, manifest = rs['_standalone_reader_region'], rs['_manifest']
    require(region[3] == 0xfff1 and region[0] % 32 == 0 and manifest[1:3] == (828, 1),
            'Reader region/map symbols have unexpected type or size')
    view.update(region=region[0] | 0x20000000, manifest=manifest[0] | 0x20000000)
    require(0xac004000 <= view['region'] <= 0xac007ea0-0x640 and
            0xac004000 <= view['manifest'] <= 0xac007ea0-828 and
            (view['region']+0x640 <= view['manifest'] or view['manifest']+828 <= view['region']),
            'Reader region/map escapes or overlaps the accepted low reservation')
    require(before == {str(p): sha(p.read_bytes()) for p in paths}, 'Linked inputs changed during view generation')
    layout = target_layout(helper_elf,cc)
    return dict(schema='kui.smash-ce.return-idle-view.v1', status='PASS', view=view,target_layout=layout,
                helper_elf_sha256=before[str(helper_elf)], resident_elf_sha256=before[str(resident_elf)],
                helper_binary_sha256=before[str(helper_bin)], helper_bytes=len(raw),
                helper_crc32=zlib.crc32(raw) & 0xffffffff,
                private_object_bytes=dict(runtime=0x2240, loan=56, continuation=0x3f4, fence=28, saved_region=4),
                reader_region_bytes=0x640, reader_manifest_bytes=828,
                limits='Trusted layout metadata only. Caller must authenticate complete code and terminal CPU/DMA ownership; no retirement or console execution is certified.')


def render_header(report):
    order = ('runtime', 'loan', 'continuation', 'fence', 'freeze', 'saved_region', 'region', 'manifest')
    values = ','.join('UINT32_C(0x%08x)' % report['view'][name] for name in order)
    digest = ','.join('0x'+report['helper_binary_sha256'][i:i+2] for i in range(0, 64, 2))
    return ('/* Generated by tools/smash_ce_return_view.py; exact linked inputs. */\n'
            '#ifndef KUI_SMASH_CE_RETURN_VIEW_SYMBOLS_H\n#define KUI_SMASH_CE_RETURN_VIEW_SYMBOLS_H\n'
            '#include "kui/smash_ce_return_view.h"\n'
            '#define KUI_SMASH_CE_RETURN_VIEW_INITIALIZER {'+values+'}\n'
            '#define KUI_SMASH_CE_RETURN_HELPER_BYTES UINT32_C(%d)\n' % report['helper_bytes']+
            '#define KUI_SMASH_CE_RETURN_HELPER_CRC32 UINT32_C(0x%08x)\n' % report['helper_crc32']+
            '#define KUI_SMASH_CE_RETURN_HELPER_SHA256_BYTES {'+digest+'}\n'
            '#endif\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('helper_elf', type=Path)
    p.add_argument('resident_elf', type=Path)
    p.add_argument('header', type=Path)
    p.add_argument('--helper-bin', type=Path)
    p.add_argument('--cc', help='SH compiler; defaults to SH_CC or the project SDK')
    p.add_argument('--report', type=Path)
    a = p.parse_args()
    result = audit(a.helper_elf, a.resident_elf, a.helper_bin,a.cc)
    a.header.write_text(render_header(result))
    text = json.dumps(result, indent=2)+'\n'
    if a.report:
        a.report.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
