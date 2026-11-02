/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_disc_set.h"
#include <string.h>
_Static_assert(KUI_RETAIL_MANIFEST_SLOTS==64u,"Session disc decoder uses compact 64-slot maps");
_Static_assert(sizeof(struct kui_retail_manifest)<=1024u,"Session map storage is bounded");

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static bool compatible(const struct kui_retail_manifest *a,const struct kui_retail_manifest *b) {
    if(b->storage_transport!=KUI_STORAGE_SCI || b->reader!=KUI_RETAIL_READER_ASYNC ||
       (b->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) ||
       b->card_sectors!=a->card_sectors || b->partition_start!=a->partition_start ||
       b->partition_end!=a->partition_end || b->storage_transport!=a->storage_transport ||
       b->reader!=a->reader)return false;
    for(uint32_t i=0;i<b->track_count;++i)
        if(!kui_retail_track_control(&b->slots[i].track) &&
           b->slots[i].track.end_lba>b->session_lba)return false;
    return true;
}
bool kui_native_disc_set_prepare_wire(struct kui_native_disc_set *out,
    const void *wire,size_t bytes,const struct kui_retail_manifest *active) {
    if(!out)return false;
    memset(out,0,sizeof(*out));const uint8_t *p=wire;
    if(!kui_native_session_wire_valid(wire,bytes,true))return false;
    if(!(word(p+KUI_NATIVE_SESSION_FLAGS_OFFSET)&KUI_NATIVE_SESSION_FLAG_DISCS))return true;
    if(!active)return false;
    uint32_t count=word(p+KUI_NATIVE_SESSION_COUNT_OFFSET),selected=word(p+KUI_NATIVE_SESSION_SELECTED_OFFSET);
    for(uint32_t i=0;i<count;++i) {
        struct kui_retail_manifest *m=(void *)out->maps[i];
        if(kui_retail_manifest_decode(p+KUI_NATIVE_SESSION_MAP_OFFSET+i*KUI_NATIVE_SESSION_MAP_BYTES,m)!=KUI_GAME_OK ||
           !compatible(active,m))goto refused;
        /* Two wire records naming an identical logical/physical image cannot
         * invent a second disc. Real disc metadata can differ elsewhere. */
        for(uint32_t j=0;j<i;++j)
            if(!memcmp(out->maps[j],m,sizeof(*m)))goto refused;
    }
    if(memcmp(out->maps[selected],active,sizeof(*active)))goto refused;
    out->count=count;out->selected=selected;return true;
refused:
    memset(out,0,sizeof(*out));return false;
}
const struct kui_retail_manifest *kui_native_disc_set_current(const struct kui_native_disc_set *set) {
    return set && set->count>=2u && set->count<=KUI_NATIVE_SESSION_DISCS_MAX &&
        set->selected<set->count?(const void *)set->maps[set->selected]:NULL;
}
bool kui_native_disc_set_request_next(struct kui_native_disc_set *set) {
    if(!kui_native_disc_set_current(set) || set->phase!=KUI_NATIVE_DISC_IDLE)return false;
    set->phase=KUI_NATIVE_DISC_OPENING;set->visits=0u;return true;
}
int kui_native_disc_set_visit(struct kui_native_disc_set *set,kui_native_session_control control) {
    if(!set || !control || !kui_native_disc_set_current(set))return 0;
    if(set->phase==KUI_NATIVE_DISC_IDLE)return 0;
    if(set->phase==KUI_NATIVE_DISC_FAULT)return -1;
    int result;
    if(set->phase==KUI_NATIVE_DISC_OPENING) {
        result=control(KUI_NATIVE_SESSION_DISC_OPEN,NULL,NULL);
        if(result<0)goto fault;
        if(!result)return 0;
        set->phase=KUI_NATIVE_DISC_WAITING;set->visits=0u;return 0;
    }
    if(set->phase==KUI_NATIVE_DISC_WAITING) {
        if(set->visits<30u)++set->visits;
        result=control(KUI_NATIVE_SESSION_STATUS,NULL,NULL);
        if(result<0)goto fault;
        if(!(result&KUI_NATIVE_SESSION_OBSERVED) || set->visits<30u)return 0;
        set->phase=KUI_NATIVE_DISC_CLOSING;
    }
    if(set->phase!=KUI_NATIVE_DISC_CLOSING)goto fault;
    uint32_t next=(set->selected+1u)%set->count;
    result=control(KUI_NATIVE_SESSION_DISC_CLOSE,(const void *)set->maps[next],NULL);
    if(result<0)goto fault;
    if(!result)return 0;
    set->selected=next;set->phase=KUI_NATIVE_DISC_IDLE;set->visits=0u;return 1;
fault:
    set->phase=KUI_NATIVE_DISC_FAULT;return -1;
}
