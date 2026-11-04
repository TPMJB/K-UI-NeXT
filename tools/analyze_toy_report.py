#!/usr/bin/env python3
"""Validate and decode a completed frozen Toy TXT report; never infer missing timings."""
import argparse
import json
from pathlib import Path
import re
import zlib

SIZE=32768
BODY=SIZE-512
FOOTER=re.compile(rb"^KUI REPORT COMPLETE V1\nPROFILE=([0-9A-F]{8})\nBYTES=([0-9A-F]{8})\nCRC32=([0-9A-F]{8})\n")
HEADER_FIELDS="magic,version,enabled,frozen,head,count,epoch,badclock,first_vbr,last_vbr,expected_vbr,nested,last_cmd,last_lba,last_count,sector_bytes,expired,stop_tick,stop_valid,last_age".split(',')
STOP_FIELDS="ap_calls,ap_irq_calls,ap_call_blocks,ap_irq_blocks,errors,retries,token,card_owned,audio_state,audio_fault,raw_bytes,updater,ap_enabled,ap_retired,quiesces,quiesce_failed".split(',')
BIN_FIELDS="seq,epoch,first,last,flags,gd_n,gd_valid,data_n,bytes,gd_ticks,gd_max,gap_n,gap_ticks,gap_max,service_n,service_valid,service_ticks,service_max,wait_n,credit_n,wait_ticks,credit_ticks,other_ticks,zero_services,ap_call_blocks,ap_irq_blocks,errors,retries,mask0_6,mask7_14,mask15,bl,own_vbr,other_vbr,priority,updater,accepted,completed,modes,max_sectors".split(',')


def decode(data):
    if len(data)!=SIZE:
        raise ValueError('Report must retain its complete 32768-byte preallocated file')
    match=FOOTER.match(data[BODY:])
    if not match:
        raise ValueError('Missing completion footer: save did not finish or this is the blank slot')
    profile,n,checksum=(int(item,16) for item in match.groups())
    if not 0<n<=BODY or zlib.crc32(data[:n])!=checksum or data[n:BODY]!=b' '*(BODY-n):
        raise ValueError('Report body size, padding or CRC does not match completion')
    fields={}
    for line in data[:n].decode('ascii').splitlines():
        if '=' not in line:
            raise ValueError('Malformed report line')
        key,value=line.split('=',1)
        if key in fields:
            raise ValueError('Duplicate report field '+key)
        fields[key]=value
    if fields.get('format')!='KUI_TOY_REPORT_V1' or fields.get('end')!='KUI_TOY_REPORT_V1':
        raise ValueError('Unsupported or truncated report format')
    if not re.fullmatch('[0-9a-f]{12}',fields.get('build','')) or fields.get('radix')!='16' or fields.get('ticks_per_second')!='000bebc2':
        raise ValueError('Unsupported identity or clock units')
    if profile not in (1,2) or fields.get('profile')!=('TB' if profile==1 else 'TC'):
        raise ValueError('Report profile differs from its completion footer')
    if fields.get('data_storage_sector_bytes') not in ('00000800','00000930') or fields.get('logical_data_sector_bytes')!='00000800' or fields.get('raw_audio_sector_bytes')!='00000930':
        raise ValueError('Unsupported image storage geometry')

    def row(name,labels):
        words=fields.get(name,'').split(',')
        if len(words)!=len(labels) or any(not re.fullmatch('[0-9a-f]{8}',w) for w in words):
            raise ValueError('Invalid integer row '+name)
        return dict(zip(labels,(int(w,16) for w in words)))

    for name,labels in [('report_fields',HEADER_FIELDS),('stopped_fields',STOP_FIELDS),('bin_fields',BIN_FIELDS)]:
        if fields.get(name)!=','.join(labels):
            raise ValueError('Unrecognized word layout '+name)
    report=row('report',HEADER_FIELDS)
    stopped=row('stopped',STOP_FIELDS)
    bins=[row('bin'+str(i),BIN_FIELDS) for i in range(8)]
    if report['magic']!=0x54444f31 or report['version']!=1 or report['frozen']!=1 or report['enabled']!=profile-1 or report['head']>=8 or report['count']>8:
        raise ValueError('Invalid frozen observer header')
    kept=[b for b in bins if b['seq']]
    if len(kept)!=report['count']:
        raise ValueError('Retained bin count differs from header')
    kept.sort(key=lambda b:(b['epoch'],b['seq']))
    for b in kept:
        b['gd_max_ms']=b['gd_max']*1000/781250 if b['gd_valid'] and not b['flags']&1 else None
        b['service_max_ms']=b['service_max']*1000/781250 if b['service_valid'] and not b['flags']&1 else None
    return dict(schema='kui.toy.decoded-report.v1',build=fields['build'],profile=fields['profile'],
        data_storage_sector_bytes=int(fields['data_storage_sector_bytes'],16),
        footer_crc_verified=True,header=report,stopped=stopped,bins=kept,
        all_raw_bins=bins,
        interpretation_limits=['AP block counters exclude the separate low fallback reader',
          'GD and service times are nested; do not sum',
          'Step classes include admission work and elapsed hardware waits',
          'Cross-bin and invalid-clock flags preclude naive per-bin CPU percentages',
          'Updater entries are a proxy, not native sound interrupt or audible quality'])


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('report',type=Path);args=p.parse_args()
    try:result=decode(args.report.read_bytes())
    except (OSError,ValueError,UnicodeError) as error:p.exit(2,'Report refused: '+str(error)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()

