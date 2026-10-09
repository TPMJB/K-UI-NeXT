/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_observe.h"
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
_Static_assert(sizeof(struct kui_retail_observe)==312u,"fixed low observation bytes");
_Static_assert(offsetof(struct kui_retail_observe,cpu_first)==offsetof(struct kui_retail_observe,cpu_words),"CPU overlay");
_Static_assert(offsetof(struct kui_retail_observe,cpu_changed)==offsetof(struct kui_retail_observe,cpu_words)+12u*4u,"CPU mask");
_Static_assert(offsetof(struct kui_retail_observe,tmu_changed)==offsetof(struct kui_retail_observe,tmu_words)+22u*4u,"TMU mask");
_Static_assert(offsetof(struct kui_retail_observe,audio_changed)==offsetof(struct kui_retail_observe,audio_words)+16u*4u,"audio mask");
_Static_assert(offsetof(struct kui_retail_observe,dma_changed)==offsetof(struct kui_retail_observe,dma_words)+3u*4u,"DMA overlay");
static unsigned checks;
#define CHECK(v) do {assert(v);checks++;} while(0)
static struct kui_retail_manifest fixture(void) {
    static const uint32_t first[15]={0,7257,45000,201900,219712,234091,255599,272017,
        282799,303092,323352,341314,356690,374201,377422};
    struct kui_retail_manifest m={.gdi_crc32=KUI_RETAIL_OBSERVE_GDI_CRC,.track_count=15,
        .extent_count=15,.storage_transport=KUI_STORAGE_SCI,.reader=KUI_RETAIL_READER_STANDARD,
        .session_lba=45000};
    for(unsigned i=0;i<15;i++) m.slots[i].track=(struct kui_retail_track){
        .start_lba=first[i],.end_lba=first[i]+1,.extent_count=1,
        .control=(i==0 || i==2 || i==14)?4:0};
    return m;
}
int main(void) {
    struct kui_retail_observe s={0},before;uint32_t p[11]={10,20,30};
    kui_retail_observe_play(&s,20,p);p[0]=99;kui_retail_observe_play(&s,20,p);
    CHECK(s.play[0]==2 && s.first_play[0][0]==10 && s.last_play[0][0]==99 && s.play[1]==0);
    kui_retail_observe_play(&s,21,p);CHECK(s.play[1]==1 && s.first_play[1][1]==20);
    before=s;kui_retail_observe_play(&s,22,p);kui_retail_observe_play(&s,20,NULL);
    CHECK(!memcmp(&before,&s,sizeof(s)));s.play[0]=UINT32_MAX;kui_retail_observe_play(&s,20,p);
    CHECK(s.play[0]==UINT32_MAX && s.last_play[0][0]==99);
    for(unsigned group=0;group<3;group++) {
        kui_retail_observe_sample(&s,(enum kui_retail_observe_group)group,p);
        CHECK(s.samples[group]==1);
    }
    p[0]=88;p[6]=1;p[7]=0x80000000u;kui_retail_observe_sample(&s,KUI_OBSERVE_AICA,p);
    CHECK(s.audio_first[0]==99 && s.audio_last[0]==88 && (s.audio_changed&1));
    CHECK(s.cpu_words[6]==s.cpu_last[0] && s.cpu_words[12]==s.cpu_changed);
    CHECK(s.tmu_words[11]==s.tmu_last[0] && s.tmu_words[22]==s.tmu_changed);
    CHECK(s.audio_words[8]==88 && s.audio_words[16]==s.audio_changed);
    s.dma_first=1;s.dma_last=2;s.dma_ever=3;s.dma_changed=4;
    CHECK(s.dma_words[0]==1 && s.dma_words[1]==2 && s.dma_words[2]==3 && s.dma_words[3]==4);
    CHECK(s.ever_key[0]==1 && s.ever_key[1]==0x80000000u);
    before=s;kui_retail_observe_sample(&s,(enum kui_retail_observe_group)3,p);
    kui_retail_observe_sample(&s,KUI_OBSERVE_CPU,NULL);CHECK(!memcmp(&s,&before,sizeof(s)));
    s.samples[0]=UINT32_MAX;kui_retail_observe_sample(&s,KUI_OBSERVE_CPU,p);CHECK(s.samples[0]==UINT32_MAX);
    struct kui_retail_manifest m=fixture();CHECK(kui_retail_observe_admit(&m));
    m.extent_count=49;CHECK(kui_retail_observe_admit(&m));m.extent_count=50;CHECK(!kui_retail_observe_admit(&m));
    for(unsigned field=0;field<8;field++) {
        m=fixture();switch(field) {
        case 0:m.gdi_crc32^=1;break;case 1:m.track_count=14;break;
        case 2:m.storage_transport=KUI_STORAGE_SCIF;break;case 3:m.reader=KUI_RETAIL_READER_ASYNC;break;
        case 4:m.flags=KUI_RETAIL_IMAGE_CD;break;case 5:m.flags=KUI_RETAIL_IMAGE_SCRAMBLED;break;
        case 6:m.session_lba=0;break;default:m.extent_count=0;break;
        } CHECK(!kui_retail_observe_admit(&m));
    }
    for(unsigned track=0;track<15;track++) {
        for(unsigned field=0;field<5;field++) {
            m=fixture();struct kui_retail_track *t=&m.slots[track].track;
            switch(field) {
            case 0:t->start_lba++;break;case 1:t->end_lba=t->start_lba;break;
            case 2:t->control^=4;break;case 3:t->control|=KUI_RETAIL_TRACK_COOKED;break;
            default:t->extent_count=0;break;
            } CHECK(!kui_retail_observe_admit(&m));
        }
        m=fixture();m.slots[track].track.first_extent=0x100;CHECK(!kui_retail_observe_admit(&m));
    }
    CHECK(!kui_retail_observe_admit(NULL));
    uint8_t ip[64]={0};memcpy(ip+37,"GD-ROM",6);memcpy(ip+56,"0000000 ",8);
    CHECK(kui_retail_observe_native_ip(ip));ip[62]='1';CHECK(!kui_retail_observe_native_ip(ip));
    ip[62]='a';CHECK(kui_retail_observe_native_ip(ip));ip[62]='F';CHECK(!kui_retail_observe_native_ip(ip));
    for(unsigned i=56;i<63;i++) {
        memcpy(ip+56,"0000000 ",8);ip[i]='G';CHECK(!kui_retail_observe_native_ip(ip));
    }
    memcpy(ip+56,"0000000 ",8);ip[63]=0;CHECK(!kui_retail_observe_native_ip(ip));
    ip[63]=' ';memcpy(ip+37,"CD-ROM",6);CHECK(!kui_retail_observe_native_ip(ip));
    CHECK(!kui_retail_observe_native_ip(NULL));
    printf("retail observation core: %u checks passed\n",checks);return 0;
}
