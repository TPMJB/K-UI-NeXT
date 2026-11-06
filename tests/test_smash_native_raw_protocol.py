#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native return raw C with real SCI/SD protocol and existing byte-wire oracles."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
def audit():
    with tempfile.TemporaryDirectory(prefix='kui-native-raw-protocol-') as folder:
        temporary=Path(folder)
        card=(ROOT/'tests/sci_stream_model.h').read_text()
        marker='/* ---- SCI, port and DMA channel 1 ---- */'
        assert card.count(marker)==1
        (temporary/'card.inc').write_text(card.split(marker)[0]+'\n#endif\n')
        sci=(ROOT/'tests/test_sci_sd_bus.c').read_text()
        sci=sci[:sci.index('static void restored(void)')]
        first=sci.index('struct profile_clock {');last=sci.index('static bool dma_running(')
        sci=sci[:first]+sci[last:]
        old='hw.receive_value=hw.patterned_rx\n        ? reversed(received_byte(hw.bytes)) : hw.rdr;'
        assert sci.count(old)==1 and sci.count('#define MAX_BYTES 2048u')==1
        sci=sci.replace(old,'hw.receive_value=reversed(kui_smash_native_raw_wire(reversed(value)));')
        sci=sci.replace('#define MAX_BYTES 2048u','#define MAX_BYTES 131072u')
        (temporary/'sci.inc').write_text(sci)
        binary=temporary/'raw'
        sources=['tests/test_smash_native_raw_protocol.c','src/loader/retail_storage.c','src/loader/retail_sd.c',
            'src/loader/sd_reader.c','src/loader/sci_sd_bus.c','src/loader/native_return_hardware.c']
        command=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
            '-ffunction-sections','-fdata-sections','-Iinclude','-Isrc/loader',
            '-DKUI_SCI_SD_TEST=1','-DKUI_RETAIL_TRANSPORT=1','-DKUI_RETAIL_FAST_IO=1',
            '-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1','-DKUI_NATIVE_VMU_SCI_RX_PACED=1','-DKUI_SCI_SD_PIO_ONLY=0',
            '-DKUI_NATIVE_VMU_ONLY=1','-DKUI_NATIVE_VMU_LITE=1','-DKUI_RETAIL_LOW_RESIDENT=1',
            '-DKUI_NATIVE_RETURN_HARDWARE_HOST_TEST=1',
            '-DKUI_SMASH_NATIVE_RAW_CARD_FIXTURE="'+str(temporary/'card.inc')+'"',
            '-DKUI_SMASH_NATIVE_RAW_SCI_FIXTURE="'+str(temporary/'sci.inc')+'"',
            *sources,'-Wl,--gc-sections','-o',str(binary)]
        subprocess.run(command,cwd=ROOT,check=True)
        records=[]
        for args in ((),('--crc',),('--dma-fault',)):
            completed=subprocess.run([str(binary),*args],capture_output=True,text=True,
                env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
            if completed.returncode:raise RuntimeError(completed.stdout+completed.stderr)
            output=completed.stdout
            records += [json.loads(line.split(' ',1)[1]) for line in output.splitlines() if line.startswith('NATIVE_RAW_PROTOCOL')]
        assert len(records)==3
        return {'schema':'kui.smash.native-raw-protocol.v1','status':'PASS','results':records,
            'source_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources+[
                'src/loader/smash_menu_return_raw.inc','tests/sci_stream_model.h','tests/test_sci_sd_bus.c',
                'tests/test_smash_native_raw_protocol.py']},
            'substitution_boundary':'Authored SCI/DMAC/card registers, IRQ state and cache-off callback; actual raw/storage/protocol/CRC C executes.',
            'hardware_timing_measured':False,'sh_instruction_or_cache_execution_proved':False}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',type=Path);args=parser.parse_args()
    result=audit();output=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.write_text(output)
    print(output,end='')
if __name__=='__main__':main()
