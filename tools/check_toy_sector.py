#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read one GDI sector and compare its CRC32 with a SCHED first-sector CRC.

All input files are read-only. LBA is the SCHED request LBA, not disc FAD.
The retained image reader selects MODE1 bytes [16,2064) from a 2352-byte track.
"""
import argparse
import binascii
import json
from pathlib import Path
import shlex


def inspect(gdi, lba, output_bytes, expected=None):
    gdi = Path(gdi).resolve()
    lines = [line.strip() for line in gdi.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    count = int(lines[0]);tracks = []
    for line in lines[1:]:
        fields = shlex.split(line)
        if len(fields) != 6:
            raise ValueError('GDI track must have six fields')
        number,start,kind,stored = (int(fields[i], 10) for i in range(4))
        tracks.append((number,start,kind,stored,gdi.parent / fields[4],int(fields[5],10)))
    if count != len(tracks) or not tracks or lba < 0 or output_bytes not in (2048,2352):
        raise ValueError('Unsupported GDI geometry or output sector size')
    candidates = [track for track in tracks if track[1] <= lba]
    if not candidates:
        raise ValueError('LBA precedes the first track')
    number,start,kind,stored,path,offset = max(candidates,key=lambda track:track[1])
    if stored not in (2048,2352) or (output_bytes == 2352 and stored != 2352):
        raise ValueError('Raw output requires an original 2352-byte track')
    if output_bytes == 2048 and kind != 4:
        raise ValueError('MODE1 output must select a DATA track')
    position=offset+(lba-start)*stored
    with path.open('rb') as stream:
        stream.seek(position);sector=stream.read(stored)
    if len(sector) != stored:
        raise ValueError('Selected LBA is outside the track file')
    logical=sector[16:2064] if stored==2352 and output_bytes==2048 else sector
    crc=binascii.crc32(logical)&0xffffffff
    result={'lba':lba,'lba_hex':f'{lba:08X}','track':number,
            'stored_bytes':stored,'output_bytes':output_bytes,'file_offset':position,
            'first_sector_crc32':f'{crc:08X}'}
    if expected is not None:result['matches']=crc==expected
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gdi',type=Path)
    parser.add_argument('--lba-hex',required=True,help='SCHED LBA, hexadecimal without FAD adjustment')
    parser.add_argument('--bytes',type=int,choices=(2048,2352),default=2048)
    parser.add_argument('--crc-hex',help='optional SCHED CRC32 to compare')
    args=parser.parse_args()
    result=inspect(args.gdi,int(args.lba_hex,16),args.bytes,
                   int(args.crc_hex,16) if args.crc_hex else None)
    print(json.dumps(result,indent=2))
    if result.get('matches') is False:raise SystemExit(2)


if __name__=='__main__':main()
