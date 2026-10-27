#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare every decoded low-resident glyph row with the original font."""
import os,re,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'src/loader/retail_display.c').read_text()
a=s.index('static const uint8_t glyphs[36][7]');b=s.index('\n};',a)
expected=bytes(int(x) for row in re.findall(r'\{([^{}]+)\}',s[a:b]) for x in row.split(','))
assert len(expected)==252
a=s.index('static const uint8_t timeline_glyphs[158]');b=s.index('\n};',a)
packed=bytes(int(x) for x in s[s.index('{',a)+1:b].replace('\n','').split(',') if x.strip())
assert len(packed)==158
decoded=[]
for row in range(252):
 bit=row*5;at=bit>>3
 assert at+1<len(packed)
 decoded.append(((packed[at]|packed[at+1]<<8)>>(bit&7))&31)
assert bytes(decoded)==expected
# Four unused bits in the final byte are padding, not another hidden row.
assert not packed[-1]&0xf0
with tempfile.TemporaryDirectory(prefix='kui-compact-font-') as t:
 p=Path(t);source=p/'font.c';exe=p/'font'
 source.write_text('#include <stdio.h>\n#include "'+str(root/'src/loader/retail_display.c')+'"\nint main(void){for(unsigned g=0;g<36u;++g)for(unsigned r=0;r<7u;++r)putchar((int)glyph_row(g,r));return 0;}\n')
 for timeline in (0,1):
  subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-Os','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',
   '-DKUI_NATIVE_COOP_PUMP=1','-DKUI_RETAIL_ASYNC=1','-DKUI_NATIVE_LOADER_TIMELINE='+str(timeline),
   '-Iinclude','-Isrc/loader',str(source),'-Wl,--gc-sections','-o',str(exe)],cwd=root,check=True)
  assert subprocess.check_output([str(exe)])==expected
print('compact resident fonts: original 180B and contiguous 158B forms decode all 252 glyph rows exactly')
