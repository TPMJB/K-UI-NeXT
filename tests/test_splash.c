/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/splash.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
/* RGB565 of the #030913 frame around the 592x444 Dainsleif artwork. */
#define BACKGROUND 0x0042u
static uint16_t guarded[640u*480u+2u];
int main(void) {
    guarded[0]=0x1234;guarded[640u*480u+1u]=0xabcd;
    kui_splash_draw(guarded+1);
    assert(guarded[0]==0x1234 && guarded[640u*480u+1u]==0xabcd);
    assert(guarded[1]==BACKGROUND);
    unsigned different=0;
    for(unsigned i=1;i<=640u*480u;i++) if(guarded[i]!=BACKGROUND) ++different;
    assert(different>100000);
    kui_splash_draw(NULL);
    puts("PASS original splash bounds, background and artwork coverage");
    return 0;
}
