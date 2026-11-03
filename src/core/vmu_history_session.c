/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_history.h"
#include "kui/vmu_library.h"
#include "kui/destination.h"
#include "kui/hash.h"
#include <stdio.h>
#include <string.h>

struct receipt {
    uint64_t sequence;
    int slot;
    bool pending;
    struct kui_vmu_game_selection card;
};
static const char *const paths[2]={KUI_VMU_HISTORY_ROOT "/session-a.bin",KUI_VMU_HISTORY_ROOT "/session-b.bin"};
static uint32_t get32(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4u)<<32;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(i*8u));}
static void put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4u,(uint32_t)(n>>32));}
static bool zero(const void *data,size_t bytes) {const uint8_t *p=data;while(bytes--)if(*p++)return false;return true;}
static bool stopped(kui_cancel_fn cancel) {return cancel && cancel();}
static bool valid_card(const struct kui_vmu_game_selection *c) {
    char key[12];
    return c && c->selected && c->pair && c->pair<=9999u && !zero(c->identity,16u) &&
        memchr(c->product,0,12u) && kui_vmu_game_card_key(c->product,key) && !memcmp(c->product,key,12u) &&
        !zero(c->storage.cid,16u) && c->storage.card_sectors &&
        c->storage.partition_first<c->storage.partition_end && c->storage.partition_end<=c->storage.card_sectors &&
        c->storage.high_capacity<=1u;
}
static bool pinned(const struct kui_vmu_history_platform *platform,const struct kui_vmu_game_selection *c) {
    struct kui_vmu_game_candidates_storage now={0};
    return platform && platform->storage && platform->storage(platform->context,&now) &&
        !memcmp(now.cid,c->storage.cid,16u) && now.card_sectors==c->storage.card_sectors &&
        now.partition_first==c->storage.partition_first && now.partition_end==c->storage.partition_end &&
        now.high_capacity==c->storage.high_capacity;
}
static void encode(uint8_t wire[512],uint64_t sequence,const struct kui_vmu_game_selection *card) {
    memset(wire,0,512u);memcpy(wire,"KUIVMUL1",8u);put32(wire+8u,1u);put32(wire+12u,512u);put64(wire+16u,sequence);
    if(card) {
        put32(wire+24u,1u);put32(wire+28u,card->pair);memcpy(wire+32u,card->identity,16u);
        memcpy(wire+48u,card->storage.cid,16u);put64(wire+64u,card->storage.card_sectors);
        put64(wire+72u,card->storage.partition_first);put64(wire+80u,card->storage.partition_end);
        put32(wire+88u,card->storage.high_capacity);memcpy(wire+92u,card->product,12u);
    }
    put32(wire+508u,kui_crc32(0,wire,508u));
}
static bool decode(const uint8_t wire[512],struct receipt *out) {
    if(memcmp(wire,"KUIVMUL1",8u) || get32(wire+8u)!=1u || get32(wire+12u)!=512u ||
       !get64(wire+16u) || get32(wire+24u)>1u || !zero(wire+104u,404u) ||
       get32(wire+508u)!=kui_crc32(0,wire,508u))return false;
    struct receipt r={.sequence=get64(wire+16u),.pending=get32(wire+24u)!=0};
    if(r.pending) {
        r.card.selected=true;r.card.pair=get32(wire+28u);memcpy(r.card.identity,wire+32u,16u);
        memcpy(r.card.storage.cid,wire+48u,16u);r.card.storage.card_sectors=get64(wire+64u);
        r.card.storage.partition_first=get64(wire+72u);r.card.storage.partition_end=get64(wire+80u);
        r.card.storage.high_capacity=get32(wire+88u);memcpy(r.card.product,wire+92u,12u);
        if(!valid_card(&r.card))return false;
    } else if(!zero(wire+28u,76u))return false;
    *out=r;return true;
}
static int read_record(const char *path,uint8_t wire[512],bool *present,bool *sized) {
    FILINFO info;*present=*sized=false;FRESULT r=f_stat(path,&info);
    if(r==FR_NO_FILE || r==FR_NO_PATH)return KUI_VMU_HISTORY_OK;
    if(r!=FR_OK)return KUI_VMU_HISTORY_IO;
    *present=true;
    if((info.fattrib&AM_DIR) || info.fsize!=512u)return KUI_VMU_HISTORY_OK;
    FIL file;UINT got=0;if(f_open(&file,path,FA_READ)!=FR_OK)return KUI_VMU_HISTORY_IO;
    bool ok=f_size(&file)==512u && f_read(&file,wire,512u,&got)==FR_OK && got==512u;
    if(f_close(&file)!=FR_OK)ok=false;
    *sized=ok;return ok?KUI_VMU_HISTORY_OK:KUI_VMU_HISTORY_IO;
}
static int load(struct receipt *out,kui_cancel_fn cancel) {
    memset(out,0,sizeof(*out));out->slot=-1;bool any=false;uint8_t selected[512]={0};
    for(unsigned i=0;i<2u;i++) {
        if(stopped(cancel))return KUI_VMU_HISTORY_CANCELLED;
        uint8_t wire[512];bool present,sized;int result=read_record(paths[i],wire,&present,&sized);
        if(result)return result;
        any|=present;struct receipt candidate;
        if(!sized || !decode(wire,&candidate))continue;
        if(out->slot>=0 && candidate.sequence==out->sequence && memcmp(wire,selected,512u))return KUI_VMU_HISTORY_INVALID;
        if(out->slot<0 || candidate.sequence>out->sequence){*out=candidate;out->slot=(int)i;memcpy(selected,wire,512u);}
    }
    return any && out->slot<0?KUI_VMU_HISTORY_INVALID:KUI_VMU_HISTORY_OK;
}
static int publish(const struct receipt *old,const struct kui_vmu_game_selection *pending,
    const struct kui_vmu_game_selection *storage_card,const struct kui_vmu_history_platform *platform,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(old->sequence==UINT64_MAX)return KUI_VMU_HISTORY_FULL;
    if(stopped(cancel))return KUI_VMU_HISTORY_CANCELLED;
    if(!pinned(platform,storage_card))return KUI_VMU_HISTORY_STALE;
    int result=kui_vmu_library_check_allocation(cancel);
    if(result)return result==KUI_VMU_LIBRARY_CANCELLED?KUI_VMU_HISTORY_CANCELLED:KUI_VMU_HISTORY_INVALID;
    if(!kui_destination_mkdirs(KUI_VMU_HISTORY_ROOT+2,log))return KUI_VMU_HISTORY_IO;
    uint8_t wire[512],check[512];encode(wire,old->sequence+1u,pending);
    unsigned slot=old->slot==0?1u:0u;FIL file;UINT written=0;
    /* The prior complete receipt remains untouched. Cancellation is deferred
     * across the bounded publication and exact readback. No card is cleared. */
    if(stopped(cancel))return KUI_VMU_HISTORY_CANCELLED;
    if(f_open(&file,paths[slot],FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK)return KUI_VMU_HISTORY_IO;
    bool ok=f_write(&file,wire,512u,&written)==FR_OK && written==512u;
    if(f_sync(&file)!=FR_OK)ok=false;
    if(f_close(&file)!=FR_OK)ok=false;
    bool present,sized;
    if(!ok || read_record(paths[slot],check,&present,&sized) || !present || !sized || memcmp(check,wire,512u))return KUI_VMU_HISTORY_IO;
    return pinned(platform,storage_card)?KUI_VMU_HISTORY_OK:KUI_VMU_HISTORY_STALE;
}
int kui_vmu_history_session_resume(uint64_t seconds,const struct kui_vmu_history_platform *platform,
    struct kui_vmu_history_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out || !platform || !platform->storage || !platform->salt || !log)return KUI_VMU_HISTORY_ARGUMENT;
    memset(out,0,sizeof(*out));struct receipt receipt;int result=load(&receipt,cancel);
    if(result)goto failed;
    if(!receipt.pending){snprintf(out->notice,sizeof(out->notice),"No previous virtual-card session is pending.");return KUI_VMU_HISTORY_UNCHANGED;}
    if(!pinned(platform,&receipt.card)){result=KUI_VMU_HISTORY_STALE;goto failed;}
    struct kui_vmu_history_request request={.action=KUI_VMU_HISTORY_CHECKPOINT,.card=receipt.card};
    result=kui_vmu_history_apply(&request,KUI_VMU_HISTORY_AFTER_RETURN,seconds,platform,out,log,cancel);
    if(result<0)goto failed;
    result=publish(&receipt,NULL,&receipt.card,platform,log,cancel);
    if(result)goto failed;
    snprintf(out->notice,sizeof(out->notice),"Previous session checkpoint verified. Original card kept.");return KUI_VMU_HISTORY_OK;
failed:
    out->known=false;
    snprintf(out->notice,sizeof(out->notice),"%s",result==KUI_VMU_HISTORY_STALE?
        "Previous-session SD or card identity differs; receipt retained.":
        "Previous-session history is not confirmed; receipt retained for retry.");
    return result;
}
int kui_vmu_history_session_begin(const struct kui_vmu_game_selection *card,uint64_t seconds,
    const struct kui_vmu_history_platform *platform,struct kui_vmu_history_view *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!out || !valid_card(card) || !platform || !platform->storage || !platform->salt || !log)return KUI_VMU_HISTORY_ARGUMENT;
    int result=kui_vmu_history_session_resume(seconds,platform,out,log,cancel);
    if(result<0)return result;
    struct kui_vmu_history_request request={.action=KUI_VMU_HISTORY_CHECKPOINT,.card=*card};
    result=kui_vmu_history_apply(&request,KUI_VMU_HISTORY_BEFORE_LAUNCH,seconds,platform,out,log,cancel);
    if(result<0)return result;
    struct receipt receipt;result=load(&receipt,cancel);
    if(result){out->known=false;snprintf(out->notice,sizeof(out->notice),"Session receipt could not be read; launch refused. Checkpoint kept.");return result;}
    result=publish(&receipt,card,card,platform,log,cancel);
    if(result<0){out->known=false;snprintf(out->notice,sizeof(out->notice),"Session receipt was not verified; launch refused. Checkpoint kept.");return result;}
    snprintf(out->notice,sizeof(out->notice),"Pre-launch checkpoint and session receipt verified.");return KUI_VMU_HISTORY_OK;
}
