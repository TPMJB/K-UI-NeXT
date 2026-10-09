#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production DATA-only observer with modeled clocks/callbacks.

The fixture proves passthrough/ownership and exact report contracts; modeled
durations do not establish hardware performance or interrupt fidelity.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def gd_replay(args, flags, temporary, env):
    """Reuse the actual GD/adapter replay, supplying a real block callback.

    The off/on copies have identical fake-storage work and clock costs. Only
    the production observer flag differs, and all existing protocol assertions
    and digest checks still run. A counter proves instrumented cooked callbacks
    actually execute, so this is more than an unbound decline-path comparison.
    """
    source = (ROOT / 'tests/test_toy_loader_trace.c').read_text()
    source = source.replace('../src/loader/toy_loader_trace.c',
                            str(ROOT / 'src/loader/toy_loader_trace.c')).replace(
        '../src/loader/toy_loader_trace_report.c', str(ROOT / 'src/loader/toy_loader_trace_report.c'))
    source = source.replace('#include "kui/toy_loader_trace.h"',
                            '#include "kui/toy_loader_trace.h"\n'
                            '#include "kui/toy_loader_data_probe.h"\n#include "retail_storage.h"')
    helpers = '''static struct kui_retail_storage data_card;
static uint32_t data_caller[2]={UINT32_C(0x8c555000),UINT32_C(0x8c333000)};
static unsigned data_real_probed_reads;
uint32_t kui_toy_loader_data_probe_host_read(uint32_t address,unsigned bytes) {
    return kui_toy_loader_trace_host_read(address,bytes);
}
static bool data_block(void *context,const uint8_t *tx,uint8_t *rx,size_t count,bool slow,uint16_t *crc) {
    assert(context==&data_card && !tx && rx && count==512u && !slow && crc);
    tick(100u);memset(rx,0x5a,count);*crc=0x1234u;return true;
}
'''
    marker = 'static int read_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *output) {'
    if source.count(marker) != 1:
        raise RuntimeError('Production replay read callback marker changed')
    source = source.replace(marker, helpers + marker)
    before = '    tick(count*(bytes==2048u?4u:5u)*100u);'
    after = '''    assert(service.ops.read==read_data);
    if(bytes==2048u) {
        if(data_card.device.sd.bus.transfer_block!=data_block) ++data_real_probed_reads;
        for(uint32_t block=0;block<count*4u;++block) {
            uint8_t payload[512];uint16_t crc=0u;
            assert(data_card.device.sd.bus.transfer_block(&data_card,NULL,payload,512u,false,&crc));
            assert(crc==0x1234u && payload[0]==0x5au);
        }
    } else tick(count*5u*100u);'''
    if source.count(before) != 1:
        raise RuntimeError('Production replay modeled storage duration marker changed')
    source = source.replace(before, after)
    bind = '''    data_card=(struct kui_retail_storage){0};
    data_card.transport=KUI_STORAGE_SCI;
    data_card.device.sd.bus.transfer_block=data_block;
#if KUI_TOY_PILOT_DATA_PROBE
    kui_toy_loader_data_probe_host_reset();
    kui_toy_loader_data_probe_host_bind(&data_card,&model_sr,data_caller,read_data,data_block);
#else
    (void)data_caller;
#endif
'''
    marker = '    const struct kui_gd_ops ops={NULL,map,check,read_data};'
    if source.count(marker) != 1:
        raise RuntimeError('Production replay initialization marker changed')
    source = source.replace(marker, bind + marker)
    marker = '    printf("PROTOCOL calls=%u digest=%016llx\\n",protocol_calls,(unsigned long long)protocol_digest);'
    if source.count(marker) != 1:
        raise RuntimeError('Production replay digest marker changed')
    source = source.replace(marker, '''#if KUI_TOY_PILOT_DATA_PROBE
    assert(data_real_probed_reads>0u);
#else
    assert(!data_real_probed_reads);
#endif
''' + marker)
    fixture = Path(temporary) / 'gd-real-callbacks.c'
    fixture.write_text(source)
    replay_flags = [flag for flag in flags if not flag.startswith('-DKUI_TOY_PILOT_DATA_PROBE=')]
    replay_flags += ['-DKUI_TOY_LOADER_TRACE_HOST_TEST=1', '-ffunction-sections', '-fdata-sections']
    signatures = []
    for enabled in (0, 1):
        binary = Path(temporary) / ('gd-replay-' + str(enabled))
        subprocess.run(shlex.split(args.cc) + replay_flags +
                       ['-DKUI_TOY_PILOT_DATA_PROBE=' + str(enabled), str(fixture),
                        'src/loader/toy_loader_data_probe.c', 'src/core/retail_gd.c',
                        'src/loader/toy_pilot_gd.c', '-Wl,--gc-sections', '-o', str(binary)],
                       cwd=ROOT, check=True)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env,
                                check=False, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError('DATA probe production GD replay failed:\n' + result.stdout + result.stderr)
        signatures.append(re.findall(r'^PROTOCOL calls=\d+ digest=[0-9a-f]{16}$', result.stdout, re.MULTILINE))
    if len(signatures[0]) != 1 or signatures[0] != signatures[1]:
        raise RuntimeError('DATA probe changes production GD protocol: ' + repr(signatures))
    print('DATA probe actual GD/adapter off/on real callbacks match: ' + signatures[0][0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--no-sanitizers', action='store_true')
    args = parser.parse_args()
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
             '-fno-pie', '-no-pie', '-Iinclude', '-Isrc/loader',
             '-DKUI_TOY_PILOT_PRIVATE_P2=1', '-DKUI_TOY_PILOT_GD_FIXED_STEP=2',
             '-DKUI_TOY_PILOT_LOADER_TRACE=1', '-DKUI_TOY_PILOT_DATA_PROBE=1',
             '-DKUI_TOY_LOADER_DATA_PROBE_HOST_TEST=1']
    if not args.no_sanitizers:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    with tempfile.TemporaryDirectory(prefix='kui-toy-data-probe-') as temporary:
        binary = Path(temporary) / 'data-probe'
        subprocess.run(shlex.split(args.cc) + flags +
                       ['tests/test_toy_loader_data_probe.c', 'src/loader/toy_loader_data_probe.c',
                        '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
        gd_replay(args, flags, temporary, env)
        for enabled, trace, expected in ((2, 1, 'Toy loader DATA probe must be 0 or 1'),
                                         (1, 0, 'Toy loader DATA probe requires retained loader trace')):
            invalid_flags = [flag for flag in flags if not flag.startswith('-DKUI_TOY_PILOT_DATA_PROBE=')
                             and not flag.startswith('-DKUI_TOY_PILOT_LOADER_TRACE=')]
            result = subprocess.run(shlex.split(args.cc) + invalid_flags +
                                    ['-DKUI_TOY_PILOT_DATA_PROBE=' + str(enabled),
                                     '-DKUI_TOY_PILOT_LOADER_TRACE=' + str(trace),
                                     '-x', 'c', '-c', '-o', os.devnull, '-'],
                                    input='#include "kui/toy_loader_data_probe.h"\n',
                                    cwd=ROOT, capture_output=True, text=True)
            if result.returncode == 0 or expected not in result.stderr:
                raise RuntimeError('Invalid DATA probe profile admitted: ' + result.stderr)
    print('DATA probe invalid profile guards PASS')
    print('Toy loader DATA probe production telemetry and report suites passed')


if __name__ == '__main__':
    main()
