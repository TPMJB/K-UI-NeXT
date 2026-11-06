#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Collect only the ten Genesis save headers from the owner's Smash Pack GDI.

Python 3 standard library only. Reads original raw2352/Mode1 data and emits
512-byte ROM prefixes plus a metadata report, never complete cartridges or
audio. It does not patch the image or add saving to a game. Existing output
paths are refused. A declared SRAM header is not proof that a game saves its
high scores: that still requires checking the original game and dirty state.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import tempfile
import zipfile

RAW, DATA, HEADER_BYTES = 2352, 2048, 512
EXPECTED_IP_SHA256 = '33c9cf893ead0b7bd001f85d4ee1197c35ef7eeef4093fed3a5877eebcb2246e'
ROMS = {
    'ALTBEAST/ALTBEAST.SGA':524288,
    'COLUMNS/COLUMNS.SGA':131072,
    'GOLDAXE/GOLDAXE.SGA':524288,
    'PHSTAR2/PHSTAR2.SGA':786432,
    'REVSHIN/REVSHIN.SGA':524288,
    'SHFORCE/SHFORCE.SGA':1572864,
    'SONIC1/SONIC1.SGA':524288,
    'STRAGE2/STRAGE2.SGA':2097152,
    'VECTORM/VECTORM.SGA':2097152,
    'WRESTWAR/WRESTWAR.SGA':524288,
}


def sha(data):return hashlib.sha256(data).hexdigest()


def engine_sram_bytes(header):
    """Scalar interpretation of the SHA-bound original MDE header parser.

    MDE checks RA at 1B0/1B1, attribute byte 1B3 == 20, then type F0/F8/E0.
    Its size calculation uses header 1BA, not the complete start/end pair.
    This deliberately reports the engine's narrow admission, not a generic
    Genesis cartridge or high-score compatibility decision.
    """
    if len(header)!=HEADER_BYTES:raise ValueError('A ROM header must contain exactly 512 bytes')
    if header[0x1b0:0x1b2]!=b'RA' or header[0x1b3]!=0x20:return 0
    kind=header[0x1b2]
    if kind not in (0xf0,0xf8,0xe0):return 0
    size=(header[0x1ba]+1)*256
    return size//2 if kind in (0xf0,0xf8) else size


def header_info(header):
    size=engine_sram_bytes(header)
    text=lambda a,b:header[a:b].decode('ascii',errors='replace').strip(' \0')
    return dict(domestic_title=text(0x120,0x150),international_title=text(0x150,0x180),
        product=text(0x180,0x18e),signature=header[0x1b0:0x1b2].hex(),
        attribute=f'{int.from_bytes(header[0x1b2:0x1b4],"big"):04x}',
        declared_start=f'{int.from_bytes(header[0x1b4:0x1b8],"big"):08x}',
        declared_end=f'{int.from_bytes(header[0x1b8:0x1bc],"big"):08x}',
        engine_sram_bytes=size,engine_sram_declared=bool(size))


def both(data,offset,width):
    if len(data)<offset+2*width:raise ValueError('Truncated ISO field')
    little=int.from_bytes(data[offset:offset+width],'little')
    if little!=int.from_bytes(data[offset+width:offset+2*width],'big'):
        raise ValueError('ISO metadata byte orders disagree')
    return little


def identifier(raw):
    name=raw.decode('ascii').upper()
    if ';' in name:
        name,version=name.split(';',1)
        if version!='1':raise ValueError('Unsupported ISO version')
    if (not name or name in ('.','..') or
        any(ord(c)<32 or ord(c)>=127 or c in '/\\:' for c in name)):
        raise ValueError('Unsafe ISO filename')
    return name


def record(entry):
    if len(entry)<34 or entry[0]!=len(entry) or not entry[32] or 33+entry[32]>len(entry):
        raise ValueError('Invalid ISO directory record')
    if entry[1] or entry[26] or entry[27] or both(entry,28,2)!=1:
        raise ValueError('Unsupported ISO extended/interleaved/volume record')
    return both(entry,2,4),both(entry,10,4),entry[25],entry[33:33+entry[32]]


def extract(gdi,expected_ip_sha256=EXPECTED_IP_SHA256):
    gdi=Path(gdi)
    if gdi.is_symlink() or not gdi.is_file() or not 0<gdi.stat().st_size<=65536:
        raise ValueError('GDI must be a regular file between 1 byte and 64 KiB')
    original=gdi.read_bytes()
    rows=[shlex.split(line) for line in original.decode('utf-8-sig').splitlines() if line.strip()]
    if (not rows or len(rows[0])!=1 or not 1<=int(rows[0][0])<=99 or
        len(rows)!=int(rows[0][0])+1):raise ValueError('Invalid GDI track count')
    tracks=[]
    for index,row in enumerate(rows[1:],1):
        if len(row)!=6:raise ValueError('Malformed GDI track')
        number,start,control,stride=map(int,row[:4]);name,offset=row[4],int(row[5])
        if (number!=index or start<0 or control not in (0,4) or stride!=RAW or offset or
            '/' in name or '\\' in name or ':' in name or name in ('','.','..')):
            raise ValueError('Requires original raw2352 GDI tracks with zero file offsets')
        path=gdi.parent/name
        if path.is_symlink() or not path.is_file():raise ValueError('Track is not a regular file')
        length=path.stat().st_size
        if not length or length%RAW:raise ValueError('Track is not a whole number of raw sectors')
        end=start+length//RAW
        if end>719850 or (tracks and tracks[-1][1]>start):raise ValueError('Invalid or overlapping track range')
        tracks.append((start,end,control,path))
    sessions=[t[0] for t in tracks if t[2]==4 and t[0]>=45000]
    if not sessions:raise ValueError('No GD high-density data session found')
    session=sessions[0]

    def check_extent(lba,size):
        # Validate complete metadata extents without opening or reading their
        # payload. A readable prefix alone cannot validate a declared ROM.
        if size<=0 or lba<session:raise ValueError('Extent leaves data session')
        count=(size+DATA-1)//DATA
        while count:
            track=next((t for t in tracks if t[0]<=lba<t[1]),None)
            if track is None or track[2]!=4:raise ValueError('Extent leaves data tracks')
            take=min(count,track[1]-lba)
            lba+=take;count-=take

    def read(lba,size):
        if not 0<size<=1024*1024 or lba<session:raise ValueError('Read leaves bounded data session')
        check_extent(lba,size)
        count=(size+DATA-1)//DATA;out=bytearray()
        while count:
            track=next((t for t in tracks if t[0]<=lba<t[1]),None)
            if track is None or track[2]!=4:raise ValueError('Read leaves data tracks')
            take=min(count,track[1]-lba)
            with track[3].open('rb') as source:
                source.seek((lba-track[0])*RAW)
                for _ in range(take):
                    sector=source.read(RAW)
                    if (len(sector)!=RAW or sector[:12]!=b'\0'+b'\xff'*10+b'\0' or sector[15]!=1):
                        raise ValueError('Truncated or non-Mode1 raw data sector')
                    out.extend(sector[16:16+DATA])
            lba+=take;count-=take
        return bytes(out[:size])

    ip=read(session,32768)
    if sha(ip)!=expected_ip_sha256:raise ValueError('IP header does not match the reviewed Smash Pack image')
    if ip[:16]!=b'SEGA SEGAKATANA ' or ip[37:43]!=b'GD-ROM':raise ValueError('Invalid native GD IP')
    pvd=None
    for at in range(16):
        block=read(session+16+at,DATA)
        if block[1:7]!=b'CD001\1':raise ValueError('Invalid ISO volume descriptor')
        if block[0]==1:pvd=block;break
        if block[0]==255:break
    if pvd is None or both(pvd,128,2)!=DATA:raise ValueError('No supported primary volume descriptor')
    lba,size,flags,name=record(pvd[156:190])
    if flags!=2 or name!=b'\0':raise ValueError('Invalid ISO root record')
    pending=[('',lba,size)];seen=set();paths=set();selected={};directory_total=0;scanned=0
    while pending:
        prefix,lba,size=pending.pop(0)
        if (lba,size) in seen:raise ValueError('ISO directory cycle or aliased directory extent')
        seen.add((lba,size));directory_total+=size
        if len(seen)>512 or not 0<size<=1024*1024 or directory_total>16*1024*1024:
            raise ValueError('ISO directory traversal exceeds bounded limits')
        directory=read(lba,size);at=0
        while at<len(directory):
            n=directory[at]
            if not n:at=(at//DATA+1)*DATA;continue
            if n<34 or n>min(len(directory)-at,DATA-at%DATA):raise ValueError('Invalid ISO directory entry')
            extent,length,kind,raw=record(directory[at:at+n]);at+=n;scanned+=1
            if scanned>32768:raise ValueError('ISO entry count exceeds bounded limits')
            if raw in (b'\0',b'\1'):
                if kind!=2:raise ValueError('Invalid ISO dot directory')
                continue
            path=prefix+identifier(raw)
            if len(path)>1024 or path in paths:raise ValueError('Duplicate or oversized ISO path')
            paths.add(path)
            if kind&2:
                if kind!=2:raise ValueError('Unsupported ISO directory flags')
                pending.append((path+'/',extent,length))
            elif path in ROMS:
                if kind:raise ValueError('Selected ROM has unsupported or multi-extent flags')
                if length!=ROMS[path] or length<HEADER_BYTES:
                    raise ValueError('Selected ROM size differs from reviewed catalog: '+path)
                check_extent(extent,length)
                selected[path]=(extent,length)
    missing=sorted(set(ROMS)-set(selected))
    if missing:raise ValueError('Required ROM headers were not found: '+', '.join(missing))
    entries={'disc.gdi':original};headers=[]
    for path,(lba,size) in sorted(selected.items()):
        prefix=read(lba,HEADER_BYTES)
        entries['headers/'+path+'.header.bin']=prefix
        headers.append(dict(path=path,lba=lba,rom_bytes=size,header_bytes=HEADER_BYTES,
            header_sha256=sha(prefix),engine_sram_bytes=engine_sram_bytes(prefix),
            save_descriptor=header_info(prefix)))
    report=dict(schema='kui.smash-genesis-save-headers.v1',ip_sha256=sha(ip),session_lba=session,
        headers=headers,header_payload_bytes=len(headers)*HEADER_BYTES,
        whole_ROM_payload_copied=False,cdda_track_payload_copied=False,
        hash_scope='Each header SHA256 covers only its 512-byte prefix, not the full ROM',
        purpose='Determine original Genesis SRAM/save eligibility; not a patch or high-score compatibility claim')
    return entries,report


def write_bundle(destination,entries,report):
    destination=Path(destination)
    if os.path.lexists(destination):raise ValueError('Output already exists; choose a new filename')
    if not destination.parent.is_dir():raise ValueError('Output parent directory does not exist')
    files={**entries,'save-headers.json':(json.dumps(report,indent=2)+'\n').encode()}
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(prefix='.Smash-save-headers-',suffix='.zip.tmp',
            dir=destination.parent,delete=False) as tmp:temporary=Path(tmp.name)
        with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED) as archive:
            for name,data in files.items():archive.writestr(name,data)
        with zipfile.ZipFile(temporary) as archive:
            if archive.testzip() is not None or archive.namelist()!=list(files):
                raise ValueError('Header ZIP verification failed')
            if any(archive.read(name)!=data for name,data in files.items()):
                raise ValueError('Header ZIP bytes differ')
        with temporary.open('rb') as file:os.fsync(file.fileno())
        # Hard-link publication gives an atomic no-overwrite operation even if
        # another file appears after the initial existence check.
        os.link(temporary,destination)
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gdi',type=Path,help='The original raw2352 Sega Smash Pack GDI')
    parser.add_argument('--output',type=Path,default=Path('SmashPack-Genesis-save-headers.zip'))
    args=parser.parse_args()
    try:
        if os.path.lexists(args.output):raise ValueError('Output already exists; choose a new filename')
        entries,report=extract(args.gdi)
        write_bundle(args.output,entries,report)
        print(f'Created {args.output.resolve()}: {len(report["headers"])} headers, '
              f'{report["header_payload_bytes"]} ROM-prefix bytes; original image unchanged.')
    except (OSError,ValueError,UnicodeError,zipfile.BadZipFile) as error:
        parser.exit(1,'Collection stopped: '+str(error)+'\n')


if __name__=='__main__':main()
