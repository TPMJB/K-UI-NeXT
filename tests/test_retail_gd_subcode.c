/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the pure formatter with outputs overlapping every request role. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#ifndef KUI_GD_SUBCODE_SOURCE
#define KUI_GD_SUBCODE_SOURCE "../src/core/retail_gd.c"
#endif
#include KUI_GD_SUBCODE_SOURCE

static const union kui_retail_slot subcode_tracks[]={
    {.track={.start_lba=0,.end_lba=8,.control=4}},
    {.track={.start_lba=16,.end_lba=24,.control=0}},
    {.track={.start_lba=45000,.end_lba=60000,.control=4}}
};

int main(void) {
    static const size_t destinations[]={
        offsetof(struct kui_retail_gd,tracks),
        offsetof(struct kui_retail_gd,track_count),
        offsetof(struct kui_retail_gd,token),
        offsetof(struct kui_retail_gd,command),
        offsetof(struct kui_retail_gd,area),
        offsetof(struct kui_retail_gd,request_bytes),
        offsetof(struct kui_retail_gd,position_lba),
        offsetof(struct kui_retail_gd,outputs)
    };
    unsigned cases=0;
    for(uint32_t format=0;format<3;format++) {
        uint32_t limit=format==0?100:format==1?14:24;
        for(uint32_t bytes=1;bytes<=limit;bytes++) {
            struct {struct kui_retail_gd state;uint8_t tail[100];} source={0},actual,expected;
            source.state.tracks=subcode_tracks;source.state.track_count=3;
            source.state.position_lba=45035;source.state.area=format;
            source.state.command=KUI_RETAIL_GD_GETSCD;source.state.request_bytes=bytes;
            uint8_t reply[100];memset(reply,0xa5,sizeof(reply));
            subcode(&source.state,reply);
            assert(bytes==sizeof(reply) || reply[bytes]==0xa5);
            for(unsigned i=0;i<sizeof(destinations)/sizeof(destinations[0]);i++) {
                size_t at=destinations[i];actual=source;expected=source;
                assert(at+bytes<=sizeof(actual));
                memcpy((uint8_t *)&expected+at,reply,bytes);
                subcode(&actual.state,(uint8_t *)&actual+at);
                assert(!memcmp(&actual,&expected,sizeof(actual)));++cases;
            }
        }
    }
    printf("GD subcode overlapping request fields: %u cases passed\n",cases);
    return 0;
}
