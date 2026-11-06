/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_fat_read_cache.h"
#include <string.h>
#define FAT_CACHE_MAGIC UINT32_C(0x43465453)
static uint16_t le16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t le64(const uint8_t *p) {return le32(p)|(uint64_t)le32(p+4u)<<32;}
static bool separate(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);
}
void kui_smash_fat_cache_begin_epoch(struct kui_smash_fat_read_cache *c) {
    if(!c)return;
    c->fat_count=c->fat_blocks=0u;
    for(unsigned i=0;i<KUI_SMASH_FAT_CACHE_COPIES;i++)c->windows[i].count=0u;
    ++c->epoch;
}
bool kui_smash_fat_cache_configure(struct kui_smash_fat_read_cache *c,uint64_t card,
    uint64_t first,uint64_t end) {
    if(!c || !card || card>UINT64_C(0x100000000) || first>=end || end>card ||
       end-first>UINT32_MAX)return false;
    memset(c,0,sizeof(*c));c->magic=FAT_CACHE_MAGIC;c->card_sectors=card;
    c->partition_first=first;c->partition_end=end;kui_smash_fat_cache_begin_epoch(c);return true;
}
/* This only selects bounded read-only physical runs. The ordinary complete
 * FAT/exFAT parser still independently checks every geometry/ownership rule
 * before any write is admitted. No cached profile authorizes a FAT address. */
static void live_geometry(struct kui_smash_fat_read_cache *c,const uint8_t vbr[512]) {
    kui_smash_fat_cache_begin_epoch(c);
    if(vbr[510]!=0x55u || vbr[511]!=0xaau)return;
    uint64_t first=0u,blocks=0u,total=0u;uint32_t copies=0u;
    if(!memcmp(vbr+3u,"EXFAT   ",8u)) {
        if(vbr[108]!=9u || vbr[110]!=1u || le32(vbr+80u)<24u ||
           (le64(vbr+64u) && le64(vbr+64u)!=c->partition_first))return;
        total=le64(vbr+72u);first=c->partition_first+le32(vbr+80u);
        blocks=le32(vbr+84u);copies=1u;
        if((uint64_t)le32(vbr+80u)+blocks>le32(vbr+88u))return;
    } else {
        if(le16(vbr+11u)!=512u || !le16(vbr+14u) || (vbr[16]!=1u && vbr[16]!=2u) ||
           le16(vbr+17u) || le16(vbr+19u) || le16(vbr+22u) ||
           le32(vbr+28u)!=c->partition_first)return;
        total=le32(vbr+32u);first=c->partition_first+le16(vbr+14u);
        blocks=le32(vbr+36u);copies=vbr[16];
    }
    if(total!=c->partition_end-c->partition_first || !blocks ||
       first<c->partition_first || first>=c->partition_end ||
       blocks*copies>c->partition_end-first)return;
    c->fat_blocks=(uint32_t)blocks;c->fat_count=copies;
    for(unsigned i=0;i<copies;i++)c->fat_first[i]=(uint32_t)(first+i*blocks);
}
int kui_smash_fat_cache_read(struct kui_smash_fat_read_cache *c,
    const struct kui_smash_fat_read_ops *ops,uint32_t lba,uint8_t out[512]) {
    if(!c || !ops || !ops->read_one || !ops->read_window || !out || c->magic!=FAT_CACHE_MAGIC ||
       c->fat_count>KUI_SMASH_FAT_CACHE_COPIES ||
       !separate(c,sizeof(*c),out,512u) || (uint64_t)lba>=c->card_sectors)return -1;
    for(unsigned i=0;i<c->fat_count;i++) {
        if(!c->fat_blocks || c->fat_first[i]<c->partition_first ||
           (uint64_t)c->fat_first[i]+c->fat_blocks>c->partition_end ||
           c->partition_end>c->card_sectors)return -1;
        uint32_t offset=lba-c->fat_first[i];
        if(lba<c->fat_first[i] || offset>=c->fat_blocks)continue;
        struct kui_smash_fat_read_window *w=&c->windows[i];
        if(w->count>KUI_SMASH_FAT_CACHE_WINDOW_BLOCKS || (w->count &&
           (w->first<c->fat_first[i] ||
            (uint64_t)w->first+w->count>(uint64_t)c->fat_first[i]+c->fat_blocks)))return -1;
        if(lba>=w->first && lba-w->first<w->count) {
            memcpy(out,w->data+(lba-w->first)*512u,512u);return 0;
        }
        uint32_t count=c->fat_blocks-offset;
        if(count>KUI_SMASH_FAT_CACHE_WINDOW_BLOCKS)count=KUI_SMASH_FAT_CACHE_WINDOW_BLOCKS;
        w->count=0u;uint32_t verified=0u;
        int result=ops->read_window(ops->context,lba,count,w->data,&verified);
        if(result || !verified || verified>count)return result?result:-1;
        w->first=lba;w->count=verified;memcpy(out,w->data,512u);return 0;
    }
    int result=ops->read_one(ops->context,lba,out);
    if(!result && (uint64_t)lba==c->partition_first)live_geometry(c,out);
    return result;
}
