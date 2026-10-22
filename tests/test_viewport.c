/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/viewport.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    uint16_t p[2*640+2];p[0]=123;p[1281]=456;
    for(unsigned i=0;i<1280;i++) p[i+1]=(uint16_t)i;
    kui_viewport_inset(p+1,640,2,0,0xffff);assert(p[101]==100);
    kui_viewport_inset(p+1,640,2,32,0xffff);
    assert(p[0]==123 && p[1281]==456);
    assert(p[1]==0xffff && p[32]==0xffff && p[33]==0 && p[608]==639 && p[609]==0xffff);
    assert(p[673]==640 && p[1248]==1279);
    for(unsigned y=0;y<2;y++) for(unsigned x=1;x<576;x++)
        assert(p[1+y*640+32+x]>=p[y*640+32+x]);
    puts("viewport: bounds, preserved endpoints and ordered pixels passed");
}
