/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/splash.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
/* RGB565 of the #090102 frame around the 576x432 crimson artwork. */
#define BACKGROUND 0x0800u
static uint16_t guarded[640u*480u+2u];
static uint16_t expected_dimmed[640u*480u];
int main(void) {
    guarded[0]=0x1234;guarded[640u*480u+1u]=0xabcd;
    kui_splash_draw(guarded+1);
    assert(guarded[0]==0x1234 && guarded[640u*480u+1u]==0xabcd);
    assert(guarded[1]==BACKGROUND);
    unsigned different=0;
    for(unsigned i=1;i<=640u*480u;i++) if(guarded[i]!=BACKGROUND) ++different;
    assert(different>100000);
    kui_splash_draw(NULL);

    kui_boot_splash_draw(guarded+1);
    assert(guarded[0]==0x1234 && guarded[640u*480u+1u]==0xabcd);
    for(unsigned i=0;i<640u*480u;i++) {
        expected_dimmed[i]=(uint16_t)((guarded[i+1]&0xe79cu)>>2);
        guarded[i+1]=0xffff; /* The dimmed copy must ignore old framebuffer data. */
    }
    kui_boot_splash_draw_dimmed(guarded+1);
    assert(guarded[0]==0x1234 && guarded[640u*480u+1u]==0xabcd);
    for(unsigned i=0;i<640u*480u;i++) assert(guarded[i+1]==expected_dimmed[i]);
    kui_boot_splash_draw(NULL);
    kui_boot_splash_draw_dimmed(NULL);
    puts("PASS crimson runtime splash and pixel-equivalent boot backdrop without framebuffer reads");
    return 0;
}
