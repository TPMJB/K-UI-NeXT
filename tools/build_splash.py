#!/usr/bin/env python3
"""Encode TPMJB's original splash and synthesize the original startup notes."""
# SPDX-License-Identifier: GPL-3.0-only
import argparse
import hashlib
import math
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
PNG_BLOB = "33b49462795c7db399c1a5e9af7b9b96b1378140"

def decode_png(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Not PNG")
    at = 8
    compressed = bytearray()
    width = height = channels = 0
    ended = False
    while at + 12 <= len(data):
        size = struct.unpack_from(">I", data, at)[0]
        tag = data[at+4:at+8]
        if size > len(data)-at-12:
            raise ValueError("Truncated PNG")
        payload = data[at+8:at+8+size]
        crc = struct.unpack_from(">I", data, at+8+size)[0]
        if zlib.crc32(tag+payload) & 0xffffffff != crc:
            raise ValueError("PNG CRC mismatch")
        if tag == b"IHDR":
            if width or len(payload) != 13:
                raise ValueError("Invalid PNG header")
            width,height,depth,color,comp,filt,interlace = struct.unpack(">IIBBBBB",payload)
            if (width,height)!=(640,480) or depth!=8 or color not in (2,6) or comp or filt or interlace:
                raise ValueError("Expected noninterlaced 640x480 RGB/RGBA")
            channels=3 if color==2 else 4
        elif tag == b"IDAT":
            compressed.extend(payload)
        elif tag == b"IEND":
            ended=True
            break
        at += size+12
    if not ended or not channels:
        raise ValueError("Incomplete PNG")
    stride=width*channels
    decoder=zlib.decompressobj()
    raw=decoder.decompress(compressed,(stride+1)*height+1)
    if len(raw)!=(stride+1)*height or not decoder.eof or decoder.unused_data:
        raise ValueError("PNG pixel size mismatch")
    pixels=[]
    previous=bytearray(stride)
    for y in range(height):
        offset=y*(stride+1)
        kind=raw[offset]
        row=bytearray(raw[offset+1:offset+1+stride])
        if kind>4:
            raise ValueError("Invalid PNG filter")
        for x in range(stride):
            a=row[x-channels] if x>=channels else 0
            b=previous[x]
            c=previous[x-channels] if x>=channels else 0
            if kind==1: predictor=a
            elif kind==2: predictor=b
            elif kind==3: predictor=(a+b)//2
            elif kind==4:
                p=a+b-c
                distances=(abs(p-a),abs(p-b),abs(p-c))
                predictor=(a,b,c)[distances.index(min(distances))]
            else: predictor=0
            row[x]=(row[x]+predictor)&255
        for x in range(0,stride,channels):
            r,g,b=row[x:x+3]
            if channels==4:
                alpha=row[x+3]
                r=(r*alpha+8*(255-alpha)+127)//255
                g=(g*alpha+15*(255-alpha)+127)//255
                b=(b*alpha+35*(255-alpha)+127)//255
            pixels.append(((r>>3)<<11)|((g>>2)<<5)|(b>>3))
        previous=row
    return pixels

def startup_samples():
    # Same original notes/envelope as K-UI_DS utils/build_boot_assets.py.
    # The original was dual-mono: preserve one channel, not a new composition.
    rate=44100
    notes=((0.08,440.0,0.22),(0.30,554.365,0.23),(0.52,659.255,0.26))
    for n in range(196608):
        t=n/rate
        value=0.0
        for start,freq,level in notes:
            age=t-start
            if 0<=age<2.3:
                env=min(age/0.025,1.0)*math.exp(-2.5*age)*min((2.3-age)/0.2,1.0)
                value+=level*env*(math.sin(2*math.pi*freq*age)+0.12*math.sin(4*math.pi*freq*age))
        yield round(max(-1.0,min(1.0,value))*26000)

def array_file(path,name,kind,values,prefix=""):
    path.parent.mkdir(parents=True,exist_ok=True)
    items=list(values)
    with path.open("w") as out:
        out.write("/* Generated from original K-UI assets; do not edit. */\n"+prefix)
        out.write(f"static const {kind} {name}[{len(items)}] = {{\n")
        for at in range(0,len(items),16):
            out.write(",".join(str(v) for v in items[at:at+16])+",\n")
        out.write("};\n")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory",type=Path,default=ROOT/"build")
    args=parser.parse_args()
    data=(ROOT/"resources/branding/startup.png").read_bytes()
    if hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()!=PNG_BLOB:
        raise SystemExit("Startup artwork differs from recorded original")
    array_file(args.directory/"splash_pixels.inc","kui_splash_pixels","uint16_t",decode_png(data))
    array_file(args.directory/"startup_pcm.inc","kui_startup_pcm","int16_t",startup_samples(),
        "#define KUI_STARTUP_RATE 44100u\n#define KUI_STARTUP_COUNT 196608u\n")
if __name__=="__main__":
    main()
