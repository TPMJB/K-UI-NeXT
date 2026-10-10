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
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def typed_low_admission():
    """Reject altered diagnostic/image ABI before making low pointers."""
    sys.path.insert(0, str(ROOT / 'tools'))
    import toy_pilot_symbols as symbols
    bss = (0, 8, 3, 0xac004000, 0, 0x2000, 0, 0, 32, 0)
    text = (0, 1, 6, 0x8c004000, 0, 0x1000, 0, 0, 2, 0)
    typed = {
        '_card': (0xac004100, 72, 1, bss),
        '_kui_retail_hook_sr': (0xac004180, 4, 1, bss),
        '_kui_retail_native_caller': (0xac004190, 8, 1, bss),
        '_diagnostic': (0xac004200, 64, 1, bss),
        '_image': (0xac004240, 544, 1, bss),
        '_read_sectors': (0x8c004100, 32, 2, text),
        '_transfer_block': (0x8c004200, 64, 2, text),
    }
    image = {'symbols': {'__retail_resident_bss_begin': 0xac004000,
                         '__retail_resident_bss_end': 0xac006000},
             'payload': bytes(0x1000)}
    with patch.object(symbols, '_linked_symbols', return_value=typed):
        result = symbols._data_probe_symbols(b'', image, True)
    assert result['LOW_PROBE_DIAGNOSTIC'] == 0xac004200
    assert result['LOW_PROBE_IMAGE_BLOCK'] == 0xac004260
    mutations = [('_diagnostic', (0xac004200, 60, 1, bss)),
                 ('_diagnostic', (0xac004200, 64, 2, bss)),
                 ('_diagnostic', (0xac004202, 64, 1, bss)),
                 ('_diagnostic', (0x8c004200, 64, 1, bss)),
                 ('_diagnostic', (0xac005fe0, 64, 1, bss)),
                 ('_diagnostic', (0xac004200, 64, 1, (*bss[:2], 7, *bss[3:]))),
                 ('_image', (0xac004240, 512, 1, bss)),
                 ('_image', (0xac004244, 544, 1, bss)),
                 ('_image', (0xac005e00, 544, 1, bss)),
                 ('_image', (0xac004240, 544, 1, text))]
    for name, changed in mutations:
        altered = dict(typed);altered[name] = changed
        with patch.object(symbols, '_linked_symbols', return_value=altered):
            try:
                symbols._data_probe_symbols(b'', image, True)
            except ValueError as exc:
                assert name in str(exc)
            else:
                raise AssertionError('Altered low object admitted: ' + repr(changed))
    print('DATA probe exact diagnostic/image typed low admission PASS')


def payload_mode_integration(args, flags, temporary, env):
    """A controlled wrapper models setup/publication around the real probe.

    The real payload controls have separate SD/image tests. This fixture checks
    that the observer encloses the complete selected call and freezes its
    independent counters, including their conservative saturation signal.
    """
    source = r'''#include <assert.h>
#include <string.h>
#include "kui/toy_loader_data_probe.h"
#include "kui/toy_loader_payload_control.h"
#include "retail_storage.h"
#include "sci_sd_bus.h"
static struct kui_retail_storage card;
static struct kui_retail_gd service;
static struct kui_sci_sd_diagnostic diagnostic;
static struct kui_toy_loader_payload_control_counts counts;
static uint32_t tick=1000u,sr=UINT32_C(0x100000f0),caller[2];
static uint8_t payload[512];
uint32_t kui_toy_loader_data_probe_host_read(uint32_t address,unsigned bytes) {
    (void)bytes;
    switch(address) {
    case 0xffc00000u:return 0xe0au;
    case 0xffd80008u:return UINT32_MAX;
    case 0xffd80010u:return 2u;
    case 0xffd80004u:return 1u;
    case 0xffd8000cu:return tick;
    default:assert(0);return 0u;
    }
}
static bool block(void *context,const uint8_t *tx,uint8_t *rx,size_t n,bool slow,uint16_t *crc) {
    assert(context==&card && !tx && rx==payload && n==512u && !slow && crc);
    tick-=10u;++diagnostic.started;++diagnostic.success;*crc=0xbeefu;rx[0]=0xabu;return true;
}
bool kui_toy_loader_payload_call(kui_toy_loader_payload_fn original,void *context,
    const uint8_t *tx,uint8_t *rx,size_t n,bool slow,uint16_t *crc) {
    assert(original==block);++counts.attempts;tick-=2u;
    bool result=original(context,tx,rx,n,slow,crc);
    tick-=4u;++counts.publications;return result;
}
const struct kui_toy_loader_payload_control_counts *kui_toy_loader_payload_control_counts(void) {
    return &counts;
}
static int read_data(void *context,uint32_t lba,uint32_t n,uint32_t bytes,void *out) {
    assert(context==&card && lba==901u && n==1u && bytes==2048u && out==payload);
    assert(service.ops.read==read_data);tick-=3u;
    for(unsigned i=0;i<2u;++i) {
        uint16_t crc=0u;if(i) tick-=5u;
        assert(card.device.sd.bus.transfer_block(&card,NULL,payload,512u,false,&crc));
        assert(crc==0xbeefu && payload[0]==0xabu);
    }
    tick-=7u;return 0;
}
static const struct kui_toy_loader_data_probe_report *view(void) {
    return (const struct kui_toy_loader_data_probe_report *)(const void *)kui_toy_loader_data_probe_words();
}
int main(void) {
    kui_toy_loader_data_probe_host_reset();
    card.transport=KUI_STORAGE_SCI;card.device.sd.bus.transfer_block=block;
    service.ops.read=read_data;service.command=KUI_GD_DMAREAD;
    kui_toy_loader_data_probe_host_bind(&card,&sr,caller,read_data,block);
    kui_toy_loader_data_probe_host_bind_diagnostic(&diagnostic);
    kui_toy_loader_data_probe_begin(&service,KUI_GD_EXEC,0u);
    assert(service.ops.read(&card,901u,1u,2048u,payload)==0);
    kui_toy_loader_data_probe_end(&service);
    const struct kui_toy_loader_data_probe_report *r=view();
    assert(r->payload_mode==KUI_TOY_PILOT_DATA_PAYLOAD_MODE);
    assert(r->feature_flags==(1u|(KUI_TOY_PILOT_DATA_PAYLOAD_MODE==1?2u:4u)));
    assert(!r->payload_attempts && !r->payload_publications);
    assert(!r->payload_declines && !r->payload_failed && !r->saturated);
    assert(r->phase[0].payload_body.ticks_total==32u && r->phase[0].read_body.ticks_total==47u);
    assert(r->phase[0].dma_payload_body.ticks_total==32u && r->phase[0].dma_started==2u);
    counts.declines=UINT32_MAX;assert(!view()->saturated && !view()->payload_declines);
    kui_toy_loader_data_probe_freeze();r=view();
    assert(r->saturated && r->payload_declines==UINT32_MAX);
    assert(r->payload_attempts==2u && r->payload_publications==2u && !r->payload_failed);
    uint32_t frozen[192];memcpy(frozen,view(),sizeof frozen);
    counts.attempts=99u;counts.declines=0u;kui_toy_loader_data_probe_freeze();
    assert(!memcmp(frozen,view(),sizeof frozen));return 0;
}
'''
    fixture = Path(temporary) / 'payload-mode-integration.c'
    fixture.write_text(source)
    for mode in (1, 2):
        binary = Path(temporary) / ('payload-mode-integration-' + str(mode))
        subprocess.run(shlex.split(args.cc) + flags +
                       ['-DKUI_TOY_PILOT_DATA_PAYLOAD_MODE=' + str(mode), str(fixture),
                        'src/loader/toy_loader_data_probe.c', '-o', str(binary)],
                       cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
    print('DATA probe selected wrapper span, feature modes, counter saturation and frozen snapshot PASS')


def gd_replay(args, flags, temporary, env):
    """Reuse the actual GD/adapter replay, supplying a real block callback.

    The off/on and mode copies have identical fake-storage work and clock costs.
    Existing protocol assertions and digest checks still run. A modeled payload
    control counts selected calls, proving rejected PLAY retains startup work
    and first accepted PLAY stops all later W/X calls through the real adapter.
    """
    source = (ROOT / 'tests/test_toy_loader_trace.c').read_text()
    source = source.replace('../src/loader/toy_loader_trace.c',
                            str(ROOT / 'src/loader/toy_loader_trace.c')).replace(
        '../src/loader/toy_loader_trace_report.c', str(ROOT / 'src/loader/toy_loader_trace_report.c'))
    source = source.replace('#include "kui/toy_loader_trace.h"',
                            '#include "kui/toy_loader_trace.h"\n'
                            '#include "kui/toy_loader_data_probe.h"\n#include "kui/toy_loader_payload_control.h"\n'
                            '#include "retail_storage.h"\n#include "sci_sd_bus.h"')
    helpers = '''static struct kui_retail_storage data_card;
static struct kui_sci_sd_diagnostic data_diagnostic;
static uint32_t data_caller[2]={UINT32_C(0x8c555000),UINT32_C(0x8c333000)};
static unsigned data_real_probed_reads;
uint32_t kui_toy_loader_data_probe_host_read(uint32_t address,unsigned bytes) {
    return kui_toy_loader_trace_host_read(address,bytes);
}
static bool data_block(void *context,const uint8_t *tx,uint8_t *rx,size_t count,bool slow,uint16_t *crc) {
    assert(context==&data_card && !tx && rx && count==512u && !slow && crc);
    tick(100u);++data_diagnostic.started;++data_diagnostic.success;
    memset(rx,0x5a,count);*crc=0x1234u;return true;
}
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE > 0
static struct kui_toy_loader_payload_control_counts data_payload_counts;
bool kui_toy_loader_payload_call(kui_toy_loader_payload_fn original,void *context,
    const uint8_t *tx,uint8_t *rx,size_t count,bool slow,uint16_t *crc) {
    assert(original==data_block);++data_payload_counts.attempts;
    bool result=original(context,tx,rx,count,slow,crc);
    ++data_payload_counts.publications;return result;
}
const struct kui_toy_loader_payload_control_counts *kui_toy_loader_payload_control_counts(void) {
    return &data_payload_counts;
}
#endif
#if KUI_TOY_PILOT_DATA_PROBE
static const struct kui_toy_loader_data_probe_report *data_view(void) {
    return (const struct kui_toy_loader_data_probe_report *)(const void *)kui_toy_loader_data_probe_words();
}
#endif
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
    } else {
        assert(data_card.device.sd.bus.transfer_block==data_block);
        tick(count*5u*100u);
    }'''
    if source.count(before) != 1:
        raise RuntimeError('Production replay modeled storage duration marker changed')
    source = source.replace(before, after)
    bind = '''    data_card=(struct kui_retail_storage){0};
    data_card.transport=KUI_STORAGE_SCI;
    data_card.device.sd.bus.transfer_block=data_block;
#if KUI_TOY_PILOT_DATA_PROBE
    kui_toy_loader_data_probe_host_reset();
    kui_toy_loader_data_probe_host_bind(&data_card,&model_sr,data_caller,read_data,data_block);
    kui_toy_loader_data_probe_host_bind_diagnostic(&data_diagnostic);
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE > 0
    data_payload_counts=(struct kui_toy_loader_payload_control_counts){0};
#endif
#else
    (void)data_caller;
#endif
'''
    marker = '    const struct kui_gd_ops ops={NULL,map,check,read_data};'
    if source.count(marker) != 1:
        raise RuntimeError('Production replay initialization marker changed')
    source = source.replace(marker, bind + marker)
    marker = '    terminal_checks((uint32_t)bad,KUI_GD_FAILED);refuse_audio=0u;'
    if source.count(marker) != 1:
        raise RuntimeError('Production replay rejected PLAY marker changed')
    source = source.replace(marker, marker + '''
#if KUI_TOY_PILOT_DATA_PROBE
    assert(!data_view()->frozen); /* Busy, invalid arguments and refused mailbox do not stop. */
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE > 0
    uint32_t startup_payload_calls=data_payload_counts.attempts;
    assert(startup_payload_calls>0u);
#endif
#endif
''')
    marker = '    assert(!call(KUI_GD_EXEC,0u,0u));terminal_checks((uint32_t)accepted,KUI_GD_COMPLETED);'
    if source.count(marker) != 2:
        raise RuntimeError('Production replay accepted PLAY marker changed')
    source = source.replace(marker, marker + '''
#if KUI_TOY_PILOT_DATA_PROBE
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE > 0
    assert(data_view()->frozen && data_payload_counts.attempts==startup_payload_calls);
    assert(service.ops.read==read_data && data_card.device.sd.bus.transfer_block==data_block);
#else
    assert(!data_view()->frozen);
#endif
#endif
''', 1)
    marker = '''    assert(audio.applied_generation!=generation); /* Acceptance is not audible-start proof. */
    token=submit_data(1u,KUI_GD_PIOREAD);assert(!call(KUI_GD_EXEC,0u,0u));
    terminal_checks(token,KUI_GD_COMPLETED);'''
    if source.count(marker) != 1:
        raise RuntimeError('Production replay post-PLAY cooked marker changed')
    source = source.replace(marker, marker + '''
#if KUI_TOY_PILOT_DATA_PROBE
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE > 0
    assert(data_payload_counts.attempts==startup_payload_calls && !data_view()->phase[1].payload_calls);
    assert(service.ops.read==read_data && data_card.device.sd.bus.transfer_block==data_block);
#else
    assert(data_view()->phase[1].payload_calls>0u); /* V retains both observational phases. */
#endif
#endif
''')
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
    for enabled, mode in ((0, 0), (1, 0), (1, 1), (1, 2)):
        binary = Path(temporary) / ('gd-replay-' + str(enabled) + '-' + str(mode))
        subprocess.run(shlex.split(args.cc) + replay_flags +
                       ['-DKUI_TOY_PILOT_DATA_PROBE=' + str(enabled),
                        '-DKUI_TOY_PILOT_DATA_PAYLOAD_MODE=' + str(mode), str(fixture),
                        'src/loader/toy_loader_data_probe.c', 'src/core/retail_gd.c',
                        'src/loader/toy_pilot_gd.c', '-Wl,--gc-sections', '-o', str(binary)],
                       cwd=ROOT, check=True)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env,
                                check=False, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError('DATA probe production GD replay failed:\n' + result.stdout + result.stderr)
        signatures.append(re.findall(r'^PROTOCOL calls=\d+ digest=[0-9a-f]{16}$', result.stdout, re.MULTILINE))
    if len(signatures[0]) != 1 or any(signature != signatures[0] for signature in signatures[1:]):
        raise RuntimeError('DATA probe changes production GD protocol: ' + repr(signatures))
    print('DATA probe actual GD/adapter off/V/W/X protocol match and accepted PLAY cutoff: ' + signatures[0][0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--no-sanitizers', action='store_true')
    args = parser.parse_args()
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
             '-fno-pie', '-no-pie', '-Iinclude', '-Isrc/loader',
             '-DKUI_TOY_PILOT_PRIVATE_P2=1', '-DKUI_TOY_PILOT_GD_FIXED_STEP=2',
             '-DKUI_TOY_PILOT_LOADER_TRACE=1', '-DKUI_TOY_PILOT_DATA_PROBE=1',
             '-DKUI_TOY_LOADER_DATA_PROBE_HOST_TEST=1',
             '-DKUI_RETAIL_SCI_DIAGNOSTIC=1', '-DKUI_SCI_SD_TEST=1']
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
        payload_mode_integration(args, flags, temporary, env)
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
        for mode, enabled, expected in ((-1, 1, 'Toy loader DATA payload mode must be 0, 1 or 2'),
                                        (3, 1, 'Toy loader DATA payload mode must be 0, 1 or 2'),
                                        (1, 0, 'Toy loader DATA payload control requires DATA probe')):
            invalid_flags = [flag for flag in flags if not flag.startswith('-DKUI_TOY_PILOT_DATA_PROBE=')]
            result = subprocess.run(shlex.split(args.cc) + invalid_flags +
                                    ['-DKUI_TOY_PILOT_DATA_PROBE=' + str(enabled),
                                     '-DKUI_TOY_PILOT_DATA_PAYLOAD_MODE=' + str(mode),
                                     '-x', 'c', '-c', '-o', os.devnull, '-'],
                                    input='#include "kui/toy_loader_data_probe.h"\n',
                                    cwd=ROOT, capture_output=True, text=True)
            if result.returncode == 0 or expected not in result.stderr:
                raise RuntimeError('Invalid DATA payload mode admitted: ' + result.stderr)
    print('DATA probe invalid profile guards PASS')
    typed_low_admission()
    print('Toy loader DATA probe production telemetry and report suites passed')


if __name__ == '__main__':
    main()
