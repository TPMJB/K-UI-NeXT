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
with tempfile.TemporaryDirectory(prefix='kui-compact-font-') as t:
 p=Path(t);source=p/'font.c';exe=p/'font'
 source.write_text('#include <stdio.h>\n#include "'+str(root/'src/loader/retail_display.c')+'"\nint main(void){for(unsigned g=0;g<36u;++g)for(unsigned r=0;r<7u;++r)putchar((int)glyph_row(g,r));return 0;}\n')
 subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-Os','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',
  '-DKUI_NATIVE_COOP_PUMP=1','-DKUI_RETAIL_ASYNC=1','-Iinclude','-Isrc/loader',str(source),'-Wl,--gc-sections','-o',str(exe)],cwd=root,check=True)
 assert subprocess.check_output([str(exe)])==expected
print('compact resident font: all 252 original glyph rows remain exact')
