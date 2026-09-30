#!/usr/bin/env python3
"""Render the production UI on the host; emit compact PNG previews for review."""
# SPDX-License-Identifier: GPL-3.0-only
import argparse
import base64
from pathlib import Path
import struct
import subprocess
import zlib

def chunk(tag,body):
    return struct.pack(">I",len(body))+tag+body+struct.pack(">I",zlib.crc32(tag+body)&0xffffffff)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    for mode in ("home","home-games","games","games-list-art","games-compact","games-gallery","games-scan","games-detail-art","games-detail","games-error","games-advanced","games-probe","games-probe-loading","games-image-probe","games-image-probe-loading","games-retail","games-retail-loading","games-retail-invalid","home-music","music","gd-play","gd-confirm","quick-resume","settings","clock","clock-confirm","defaults","vmu","vmu-restore","vmu-restore-confirm","scan-folder","crc-scan","retry","vmu-actions","vmu-delete","vmu-copy","music-clear","safe-area","system-tools","restart","salvage","salvage-confirm","salvage-working","audio-cd",
                 "home-files","files","files-root","files-actions","files-actions-locked","files-pick","files-copy",
                 "files-delete","files-refused","files-info","files-info-file","files-view","files-copying","files-keyboard",
                 "home-network","network","ftp-starting","ftp-ready","ftp-busy","ftp-stopped","ftp-failed"):
        path=args.output/(mode+".ppm")
        subprocess.run(["build/render-shell",mode,str(path)],check=True)
        data=path.read_bytes()
        # Renderer emits exactly P6, size, maxval and packed RGB bytes.
        header=data.split(b"\n",3)
        if len(header)!=4 or header[0]!=b"P6" or header[1]!=b"640 480" or header[2]!=b"255" or len(header[3])!=640*480*3:
            raise ValueError("Unexpected renderer output")
        raw=b"".join(b"\0"+header[3][y*1920:(y+1)*1920] for y in range(480))
        png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",640,480,8,2,0,0,0))+chunk(b"IDAT",zlib.compress(raw))+chunk(b"IEND",b"")
        path.with_suffix(".png").write_bytes(png);path.unlink()
        if mode in ("home","music","gd-confirm","quick-resume","settings","clock","clock-confirm","defaults","vmu","vmu-restore","vmu-restore-confirm","scan-folder","crc-scan","retry","vmu-actions","vmu-delete","vmu-copy","music-clear","safe-area","system-tools","restart","salvage","salvage-confirm","salvage-working","audio-cd"):
            print("KUI_UI_PREVIEW "+mode+" "+base64.b64encode(png).decode(),flush=True)
if __name__=="__main__":main()
