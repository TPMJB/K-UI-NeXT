/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_observe.h"
static void increment(uint32_t *value) {if(*value!=UINT32_MAX) ++*value;}
void kui_retail_observe_play(struct kui_retail_observe *s,uint32_t command,const uint32_t p[3]) {
    if(!s || !p || (command!=20u && command!=21u)) return;
    uint32_t n=command-20u;
    for(unsigned i=0;i<3u;i++) {
        if(!s->play[n]) s->first_play[n][i]=p[i];
        s->last_play[n][i]=p[i];
    }
    increment(&s->play[n]);
}
void kui_retail_observe_sample(struct kui_retail_observe *s,enum kui_retail_observe_group group,const uint32_t *p) {
    if(!s || !p || (unsigned)group>2u) return;
    uint32_t *first,*last,*changed,count;
    if(group==KUI_OBSERVE_CPU) {first=s->cpu_first;last=s->cpu_last;changed=&s->cpu_changed;count=6u;}
    else if(group==KUI_OBSERVE_TMU) {first=s->tmu_first;last=s->tmu_last;changed=&s->tmu_changed;count=11u;}
    else {first=s->audio_first;last=s->audio_last;changed=&s->audio_changed;count=8u;}
    unsigned fresh=!s->samples[group];
    increment(&s->samples[group]);
    if(group==KUI_OBSERVE_AICA) {s->ever_key[0]|=p[6];s->ever_key[1]|=p[7];}
    for(uint32_t i=0;i<count;i++) {
        if(fresh) first[i]=p[i];
        else if(last[i]!=p[i]) *changed|=1u<<i;
        last[i]=p[i];
    }
}
int kui_retail_observe_admit(const struct kui_retail_manifest *m) {
    static const uint32_t lba[15]={0,7257,45000,201900,219712,234091,255599,272017,
        282799,303092,323352,341314,356690,374201,377422};
    if(!m || m->gdi_crc32!=KUI_RETAIL_OBSERVE_GDI_CRC || m->track_count!=15u ||
       m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_STANDARD ||
       m->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED) || m->session_lba!=45000u ||
       !m->extent_count || m->extent_count>KUI_RETAIL_OBSERVE_SLOTS-15u) return 0;
    for(unsigned i=0;i<15u;i++) {
        const struct kui_retail_track *t=&m->slots[i].track;
        uint32_t control=(i==0u || i==2u || i==14u)?4u:0u;
        if(t->start_lba!=lba[i] || t->end_lba<=t->start_lba || t->control!=control ||
           kui_retail_track_sector_bytes(t)!=2352u || kui_retail_track_file_offset(t) ||
           !t->extent_count) return 0;
    }
    return 1;
}
int kui_retail_observe_native_ip(const uint8_t ip[64]) {
    static const char media[]="GD-ROM";
    if(!ip || ip[63]!=' ') return 0;
    for(unsigned i=0;i<6u;i++) if(ip[37u+i]!=(uint8_t)media[i]) return 0;
    uint32_t flags=0;
    for(unsigned i=56u;i<63u;i++) {
        unsigned digit=ip[i];
        if(digit>='0' && digit<='9') digit-='0';
        else if(digit>='A' && digit<='F') digit=digit-'A'+10u;
        else if(digit>='a' && digit<='f') digit=digit-'a'+10u;
        else return 0;
        flags=(flags<<4)|digit;
    }
    return !(flags&1u);
}
