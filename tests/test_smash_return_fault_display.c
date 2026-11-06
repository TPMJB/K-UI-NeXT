/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/loader/smash_return_fault_display.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum {VRAM=0x800000};
static uint8_t memory[VRAM];
static uint32_t registers[5],border,stores,reads,mask,cases;
static uint32_t reason=1u,detail=UINT32_C(0x89abcdef);
static bool keep_pixels;
uint32_t kui_smash_return_fault_test_read(uint32_t offset) {
    unsigned index;
    switch(offset) {
        case 0x44u:index=0u;break;case 0x5cu:index=1u;break;case 0xd0u:index=2u;break;
        case 0x50u:index=3u;break;case 0x54u:index=4u;break;default:assert(0);return 0u;
    }
    ++reads;mask|=1u<<index;return registers[index];
}
void kui_smash_return_fault_test_store(uint32_t at,uint32_t value,unsigned bytes) {
    /* Independent64-bit range/row oracle checks every single target store,
     * including arbitrary high-bit register values and both field bases. */
    assert((bytes==2u || bytes==4u) && at<=VRAM-bytes && !(at&(bytes-1u)));
    uint64_t words=(registers[1]&1023u)+1u,line=words*4u;
    uint64_t stride=(words+(registers[1]>>20u&1023u)-1u)*4u;
    assert(stride && line>=160u*bytes && !border);
    bool inside=false;
    for(unsigned field=0u;field<((registers[2]&16u)?2u:1u);field++) {
        uint64_t base=registers[3u+field];if(at<base)continue;
        uint64_t delta=at-base,y=delta/stride,xbytes=delta%stride;
        if(y>=8u && y<48u && !(xbytes%bytes) && xbytes/bytes>=8u && xbytes/bytes<160u &&
           xbytes+bytes<=line)inside=true;
    }
    assert(inside);++stores;
    if(keep_pixels)memcpy(memory+at,&value,bytes);
}
void kui_smash_return_fault_test_border(uint32_t value) {
    assert(!stores && !border && value==UINT32_C(0xff0000));border=value;
}
static void invoke(void) {
    border=stores=reads=mask=0u;++cases;kui_smash_return_fault_display(reason,detail);
    assert(reads==5u && mask==31u && (!!stores!=!!border));
}
static void mode(unsigned format,bool interlace,uint32_t base) {
    unsigned bytes=format==3u?4u:2u;uint32_t words=640u*bytes/4u;
    registers[0]=1u|(format<<2u)|UINT32_C(0x800000);
    registers[1]=(words-1u)|((interlace?239u:479u)<<10u)|((interlace?words+1u:1u)<<20u);
    registers[2]=interlace?UINT32_C(0x150):UINT32_C(0x100);
    registers[3]=base;registers[4]=base+640u*bytes;
}
static uint32_t pixel(uint32_t base,unsigned x,unsigned y,unsigned bytes,unsigned stride) {
    uint32_t value=0u;memcpy(&value,memory+base+y*stride+x*bytes,bytes);return value;
}
static uint32_t random_word(void) {
    static uint32_t state=UINT32_C(0x72ac0f12);
    state^=state<<13u;state^=state>>17u;state^=state<<5u;return state;
}
int main(void) {
    kui_smash_return_progress(UINT32_C(0x101),UINT32_C(0x89abcdef));
    assert(kui_smash_return_progress_state[0]==UINT32_C(0x101) &&
        kui_smash_return_progress_state[1]==UINT32_C(0x89abcdef));
    kui_smash_return_progress(UINT32_MAX,0u);
    assert(kui_smash_return_progress_state[0]==UINT32_MAX && !kui_smash_return_progress_state[1]);
    assert(!reads && !stores && !border);
    keep_pixels=true;
    for(unsigned f=0u;f<4u;f++)for(unsigned interlace=0u;interlace<2u;interlace++) {
        if(f==2u)continue;
        uint32_t base=UINT32_C(0x120000);mode(f,interlace!=0u,base);memset(memory,0x5b,sizeof(memory));
        invoke();assert(stores && !border);
        unsigned bytes=f==3u?4u:2u,stride=640u*bytes*(interlace?2u:1u);
        uint32_t white=f==0u?0x7fffu:f==1u?0xffffu:0xffffffu;
        uint32_t red=f==0u?0x5000u:f==1u?0xa000u:0xa00000u;
        for(unsigned field=0u;field<(interlace?2u:1u);field++) {
            uint32_t start=registers[3u+field];
            assert(pixel(start,12u,12u,bytes,stride)==white); /* R top-left */
            assert(pixel(start,20u,12u,bytes,stride)==red);   /* R top-right gap */
            assert(pixel(start,144u,12u,bytes,stride)==red); /* reason1 left gap */
            assert(pixel(start,148u,12u,bytes,stride)==white);
            assert(pixel(start,12u,30u,bytes,stride)==red);  /* detail8 left gap */
            assert(pixel(start,14u,30u,bytes,stride)==white);
        }
        assert(memory[base]==0x5bu && memory[base+7u*stride]==0x5bu &&
            memory[base+48u*stride]==0x5bu && memory[base+160u*bytes+8u*stride]==0x5bu);
    }
    keep_pixels=false;
    mode(1u,false,0u);uint32_t original[5];memcpy(original,registers,sizeof(original));
    for(unsigned word=0u;word<5u;word++)for(unsigned bit=0u;bit<32u;bit++) {
        memcpy(registers,original,sizeof(original));registers[word]^=UINT32_C(1)<<bit;invoke();
    }
    mode(1u,false,VRAM-640u*2u*480u);invoke();assert(stores);
    registers[3]+=4u;invoke();assert(border && !stores);
    mode(1u,true,0u);registers[4]=VRAM;invoke();assert(border && !stores);
    mode(1u,false,0u);registers[3]=UINT32_MAX;invoke();assert(border && !stores);
    mode(1u,false,0u);registers[0]&=~1u;invoke();assert(border && !stores);
    mode(2u,false,0u);invoke();assert(border && !stores);
    mode(1u,false,0u);registers[1]&=UINT32_C(0x000fffff);invoke();assert(border && !stores);
    for(unsigned i=0u;i<100000u;i++) {
        for(unsigned j=0u;j<5u;j++)registers[j]=random_word();
        reason=random_word();detail=random_word();
        invoke();
    }
    for(unsigned i=0u;i<1024u;i++) {
        unsigned f=random_word()%3u;if(f==2u)f=3u;
        unsigned bytes=f==3u?4u:2u,words=160u*bytes/4u+random_word()%800u;
        if(words>1024u)words=1024u;
        unsigned rows=48u+random_word()%977u,modulo=1u+random_word()%1023u;
        registers[0]=1u|(f<<2u);registers[1]=(words-1u)|((rows-1u)<<10u)|(modulo<<20u);
        registers[2]=(random_word()&1u)?16u:0u;
        registers[3]=random_word()&UINT32_C(0x7ffffc);registers[4]=random_word()&UINT32_C(0x7ffffc);
        reason=random_word();detail=random_word();
        invoke();
    }
    printf("PASS early return fault display: RAM-only progress, %lu register cases, RGB555/565/0888 and both fields, exact reason/detail points, whole-frame/VRAM bounds and border-only refusals\n",(unsigned long)cases);
    return 0;
}
