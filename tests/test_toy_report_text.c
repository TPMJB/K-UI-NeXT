/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_report_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    (void)argv;
    struct kui_toy_decision_report r={.magic=0x54444f31u,.version=1u,.enabled=1u,.frozen=1u,.count=8u};
    uint32_t stopped[16];for(unsigned i=0;i<16u;i++)stopped[i]=0xa0000000u+i;
    for(unsigned i=0;i<8u;i++) {
        uint32_t words[40];for(unsigned j=0;j<40u;j++)words[j]=0xb0000000u+i*40u+j;
        memcpy(&r.bins[i],words,sizeof(words));
    }
    const struct kui_toy_decision_report original=r;
    struct kui_toy_report_text source={&r,stopped,"0123456789ab"};
    size_t n=kui_toy_report_text_length(&source);assert(n>3000u && n<32256u);
    uint8_t all[8192],pieces[8192];assert(kui_toy_report_text_read(&source,0,all,n)==n);
    if(argc==2) {assert(fwrite(all,1,n,stdout)==n);return 0;}
    assert(memmem(all,n,"build=0123456789ab\nprofile=TC\n",29u));
    for(unsigned width=1u;width<=1024u;width=width*3u+1u) {
        memset(pieces,0,sizeof(pieces));
        for(size_t offset=0;offset<n;offset+=width) {
            size_t count=n-offset<width?n-offset:width;
            assert(kui_toy_report_text_read(&source,offset,pieces+offset,count)==count);
        }
        assert(!memcmp(all,pieces,n));
    }
    for(unsigned i=0;i<8u;i++)for(unsigned j=0;j<40u;j++) {
        char word[9];snprintf(word,sizeof(word),"%08x",0xb0000000u+i*40u+j);
        assert(memmem(all,n,word,8u));
    }
    assert(!memcmp(&r,&original,sizeof(r)));
    assert(kui_toy_report_text_read(&source,n,all,1u)==SIZE_MAX);
    assert(kui_toy_report_text_read(&source,SIZE_MAX,all,1u)==SIZE_MAX);
    assert(kui_toy_report_text_read(&source,0,NULL,1u)==SIZE_MAX);
    r.frozen=0;assert(!kui_toy_report_text_length(&source));r.frozen=1;
    r.enabled=0;assert(kui_toy_report_text_length(&source)==n);
    assert(kui_toy_report_text_read(&source,0,all,n)==n);
    assert(memmem(all,n,"profile=TB\n",11u));
    source.build="LOCAL";assert(!kui_toy_report_text_length(&source));
    puts("PASS frozen Toy text: all356words, chunk boundaries, source immutability, invalid input");
    return 0;
}
