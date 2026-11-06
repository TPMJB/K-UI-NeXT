/* SPDX-License-Identifier: GPL-3.0-only */
/* An independent branch-form specification for the factored descriptor
 * predicate. This proves metadata equivalence, never executable admission. */
#include "kui/vmu_native_exit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"exit shape reference failed at %u\n",__LINE__);exit(1);}}while(0)
static bool reference(const struct kui_vmu_native_exit *e) {
    if(!e || e->version!=2u || e->bytes!=52u)return false;
    switch(e->family) {
    case 1:case 2:case 3:return e->count==3u && !e->entries[3];
    case 4:return e->count==2u && !e->entries[2] && !e->entries[3] && !e->exec_thunk;
    case 5:return e->count==3u && e->entries[2] && !e->entries[3] && e->exec_thunk;
    case 6:return e->count==4u && e->entries[3];
    case 7:return e->count==2u && e->entries[0] && e->entries[1]==e->entries[0]+8u &&
        !e->entries[2] && !e->entries[3] && !e->exec_thunk;
    case 8:return e->count==3u && e->entries[0] && e->entries[1]==e->entries[0]+8u &&
        e->entries[2] && !e->entries[3] && e->exec_thunk==e->entries[2]+76u;
    case 9:return e->count==3u && e->entries[2] && !e->entries[3] && !e->exec_thunk;
    case 10:return e->count==3u && e->entries[0] && e->entries[1]==e->entries[0]+8u &&
        e->entries[2] && !e->entries[3] && !e->exec_thunk;
    default:return false;
    }
}
static void compare(const struct kui_vmu_native_exit *e) {
    CHECK(kui_vmu_native_exit_shape_valid(e)==reference(e));
    if(!e)return;
    const unsigned indices[]={0u,1u,2u,3u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(indices)/sizeof(indices[0]);i++) {
        unsigned expected=!indices[i] && (e->family==7u || e->family==8u || e->family==10u)?8u:12u;
        CHECK(kui_vmu_native_exit_window(e,indices[i])==expected);
    }
}
static uint32_t random_word(uint32_t *state) {
    *state^=*state<<13;*state^=*state>>17;*state^=*state<<5;return *state;
}
int main(void) {
    CHECK(sizeof(struct kui_vmu_native_exit)==52u && KUI_VMU_NATIVE_EXIT_VERSION==2u);
    CHECK(KUI_VMU_NATIVE_EXIT_FAMILY==1u && KUI_VMU_NATIVE_EXIT_PVR_CLEAR==2u && KUI_VMU_NATIVE_EXIT_PVR_COMPACT==3u &&
        KUI_VMU_NATIVE_EXIT_PAIR_ONLY==4u && KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH==5u && KUI_VMU_NATIVE_EXIT_CRT_BOOTSTRAP==6u &&
        KUI_VMU_NATIVE_EXIT_EARLY_PAIR==7u && KUI_VMU_NATIVE_EXIT_EARLY_COMPACT==8u &&
        KUI_VMU_NATIVE_EXIT_FIRMWARE_CHECK==9u && KUI_VMU_NATIVE_EXIT_EARLY_FIRMWARE_CHECK==10u);
    compare(NULL);
    const uint32_t families[]={0,1,2,3,4,5,6,7,8,9,10,11,12,UINT32_MAX};
    const uint32_t counts[]={0,1,2,3,4,5,UINT32_MAX};
    const uint32_t starts[]={0,1,UINT32_C(0x8c020000),UINT32_MAX-7u,UINT32_MAX-75u,UINT32_MAX};
    const uint32_t thirds[]={0,1,UINT32_C(0x8c030000),UINT32_MAX-75u,UINT32_MAX};
    const uint32_t fourths[]={0,1,UINT32_MAX};
    struct kui_vmu_native_exit e={.version=2u,.bytes=52u};
    for(unsigned f=0;f<sizeof(families)/sizeof(families[0]);f++)
    for(unsigned c=0;c<sizeof(counts)/sizeof(counts[0]);c++)
    for(unsigned a=0;a<sizeof(starts)/sizeof(starts[0]);a++) {
        e.family=families[f];e.count=counts[c];e.entries[0]=starts[a];
        const uint32_t seconds[]={0,e.entries[0]+6u,e.entries[0]+8u,e.entries[0]+10u,UINT32_MAX};
        for(unsigned b=0;b<sizeof(seconds)/sizeof(seconds[0]);b++)
        for(unsigned d=0;d<sizeof(thirds)/sizeof(thirds[0]);d++)
        for(unsigned h=0;h<sizeof(fourths)/sizeof(fourths[0]);h++) {
            e.entries[1]=seconds[b];e.entries[2]=thirds[d];e.entries[3]=fourths[h];
            const uint32_t thunks[]={0,1,e.entries[2]+74u,e.entries[2]+76u,UINT32_MAX};
            for(unsigned t=0;t<sizeof(thunks)/sizeof(thunks[0]);t++){e.exec_thunk=thunks[t];compare(&e);}
        }
    }
    uint32_t state=UINT32_C(0x7a12539b);
    for(unsigned i=0;i<200000u;i++) {
        uint32_t words[13];for(unsigned j=0;j<13u;j++)words[j]=random_word(&state);
        memcpy(&e,words,sizeof(e));e.family=random_word(&state)%14u;
        e.version=(i&1u)?2u:random_word(&state);e.bytes=(i&2u)?52u:random_word(&state);
        e.count=random_word(&state)%6u;compare(&e);
    }
    for(unsigned family=1;family<=10;family++) {
        e=(struct kui_vmu_native_exit){.version=2u,.bytes=52u,.family=family,.count=3u,
            .entries={UINT32_C(0x8c020000),UINT32_C(0x8c020008),UINT32_C(0x8c030000),0u},
            .exec_thunk=UINT32_C(0x8c03004c)};
        if(family==4u || family==7u){e.count=2u;e.entries[2]=e.exec_thunk=0u;}
        if(family==6u){e.count=4u;e.entries[3]=UINT32_C(0x8c040000);}
        if(family==9u || family==10u)e.exec_thunk=0u;
        CHECK(reference(&e));compare(&e);
        uint32_t words[13];memcpy(words,&e,sizeof(e));
        for(unsigned word=0;word<13;word++)for(unsigned bit=0;bit<32;bit++) {
            words[word]^=UINT32_C(1)<<bit;memcpy(&e,words,sizeof(e));compare(&e);
            words[word]^=UINT32_C(1)<<bit;
        }
    }
    printf("Exit descriptor reference: %u checks across ten families, null roles, aliases, wrap, invalid tags and bit mutants\n",checks);
    return 0;
}
