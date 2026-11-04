/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/fmv_pcm_silence.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    _Alignas(32) uint8_t bytes[1024];
    for(unsigned alignment=0;alignment<32;++alignment) for(unsigned n=0;n<=768;++n) {
        memset(bytes,0xa5,sizeof(bytes));
        unsigned first=64u+alignment,last=first+n;
        kui_fmv_pcm_silence(bytes+first,n);
        for(unsigned at=0;at<sizeof(bytes);++at)
            assert(bytes[at]==(at>=first && at<last?0u:0xa5u));
    }
    kui_fmv_pcm_silence(NULL,0u);
    puts("PASS: PCM silence touches only the supplied output span across alignment, tails and zero length");
}
