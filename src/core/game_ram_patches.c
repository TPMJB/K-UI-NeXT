/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_ram_patches.h"
#include <string.h>

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(i*8));
}
bool kui_game_ram_patches_enabled(const struct kui_game_ram_patches *p) {
    if(!p) return false;
    for(unsigned i=0;i<KUI_GAME_RAM_PATCHES_COUNT;i++) if(p->entries[i].enabled) return true;
    return false;
}
static bool offset(uint32_t address,uint32_t bytes,uint32_t *out) {
    const uint32_t area=address>>29,physical=address&UINT32_C(0x1fffffff);
    const uint32_t start=KUI_GAME_RAM_PATCH_EXEC_ADDRESS&UINT32_C(0x1fffffff);
    if((area!=0 && area!=4 && area!=5) || (address&3u) || physical<start ||
       bytes<4 || bytes>KUI_GAME_RAM_PATCH_EXEC_MAX_BYTES || physical-start>bytes-4u) return false;
    *out=physical-start;return true;
}
bool kui_game_ram_patches_plan(const struct kui_game_ram_patches *p,uint32_t bytes,
    uint32_t offsets[KUI_GAME_RAM_PATCHES_COUNT]) {
    if(!p || !offsets) return false;
    uint32_t planned[KUI_GAME_RAM_PATCHES_COUNT]={UINT32_MAX,UINT32_MAX};
    for(unsigned i=0;i<KUI_GAME_RAM_PATCHES_COUNT;i++) if(p->entries[i].enabled) {
        if(!offset(p->entries[i].address,bytes,&planned[i])) return false;
        for(unsigned j=0;j<i;j++) if(planned[i]==planned[j]) return false;
    }
    memcpy(offsets,planned,sizeof(planned));return true;
}
bool kui_game_ram_patches_valid(const struct kui_game_ram_patches *p) {
    uint32_t offsets[KUI_GAME_RAM_PATCHES_COUNT];
    return kui_game_ram_patches_plan(p,KUI_GAME_RAM_PATCH_EXEC_MAX_BYTES,offsets);
}
bool kui_game_ram_patches_apply(void *executable,uint32_t bytes,const struct kui_game_ram_patches *p) {
    uint32_t offsets[KUI_GAME_RAM_PATCHES_COUNT];
    if(!kui_game_ram_patches_plan(p,bytes,offsets)) return false;
    if(!kui_game_ram_patches_enabled(p)) return true;
    if(!executable || ((uintptr_t)executable&3u)) return false;
    /* Snapshot both values before writing, even if a caller's patch object
     * happens to be inside its test/application buffer. */
    uint32_t values[KUI_GAME_RAM_PATCHES_COUNT]={p->entries[0].value,p->entries[1].value};
    for(unsigned i=0;i<KUI_GAME_RAM_PATCHES_COUNT;i++) if(offsets[i]!=UINT32_MAX)
        *(volatile uint32_t *)((uint8_t *)executable+offsets[i])=values[i];
    return true;
}
static bool header(const uint8_t *seed,size_t bytes) {
    return seed && bytes==KUI_GAME_RAM_PATCH_SEED_BYTES &&
        word(seed)==KUI_GAME_RAM_PATCH_SEED_MAGIC0 && word(seed+4)==KUI_GAME_RAM_PATCH_SEED_MAGIC1 &&
        word(seed+8)==KUI_GAME_RAM_PATCH_SEED_VERSION && word(seed+12)==KUI_GAME_RAM_PATCH_SEED_BYTES;
}
bool kui_game_ram_patch_seed_blank(const void *data,size_t bytes) {
    const uint8_t *seed=data;if(!header(seed,bytes)) return false;
    for(size_t at=16;at<bytes;at++) if(seed[at]) return false;
    return true;
}
static uint32_t checksum(const uint8_t seed[KUI_GAME_RAM_PATCH_SEED_BYTES]) {
    uint32_t crc=UINT32_MAX;
    for(unsigned i=0;i<KUI_GAME_RAM_PATCH_SEED_BYTES;i++) {
        crc^=i>=40u && i<44u?0:seed[i];
        for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1)?UINT32_C(0xedb88320):0);
    }
    return ~crc;
}
bool kui_game_ram_patch_seed_encode(void *seed,size_t bytes,const struct kui_game_ram_patches *p,uint32_t exec_bytes) {
    uint32_t offsets[KUI_GAME_RAM_PATCHES_COUNT];
    if(!kui_game_ram_patch_seed_blank(seed,bytes) || !kui_game_ram_patches_plan(p,exec_bytes,offsets)) return false;
    if(!kui_game_ram_patches_enabled(p)) return true;
    uint8_t encoded[KUI_GAME_RAM_PATCH_SEED_BYTES]={0};memcpy(encoded,seed,16);
    uint32_t mask=0;
    for(unsigned i=0;i<KUI_GAME_RAM_PATCHES_COUNT;i++) if(offsets[i]!=UINT32_MAX) {
        mask|=1u<<i;put(encoded+24u+i*8u,KUI_GAME_RAM_PATCH_EXEC_ADDRESS+offsets[i]);
        put(encoded+28u+i*8u,p->entries[i].value);
    }
    put(encoded+16,mask);put(encoded+20,exec_bytes);put(encoded+40,checksum(encoded));
    memcpy(seed,encoded,sizeof(encoded));return true;
}
bool kui_game_ram_patch_seed_decode(struct kui_game_ram_patches *out,const void *data,size_t bytes,uint32_t exec_bytes) {
    const uint8_t *seed=data;
    if(!out || !header(seed,bytes)) return false;
    struct kui_game_ram_patches decoded={0};
    if(kui_game_ram_patch_seed_blank(seed,bytes)) {*out=decoded;return true;}
    uint32_t mask=word(seed+16);
    if(!mask || mask&~3u || word(seed+20)!=exec_bytes || word(seed+40)!=checksum(seed)) return false;
    for(unsigned at=44;at<KUI_GAME_RAM_PATCH_SEED_BYTES;at++) if(seed[at]) return false;
    for(unsigned i=0;i<KUI_GAME_RAM_PATCHES_COUNT;i++) {
        uint32_t address=word(seed+24u+i*8u),value=word(seed+28u+i*8u);
        if(!(mask&(1u<<i))) {if(address || value) return false;continue;}
        /* Wire canonicality is stricter than accepted UI input aliases. */
        if(address>>29!=4) return false;
        decoded.entries[i]=(struct kui_game_ram_patch){address,value,true};
    }
    if(!kui_game_ram_patches_valid(&decoded)) return false;
    uint32_t offsets[KUI_GAME_RAM_PATCHES_COUNT];
    if(!kui_game_ram_patches_plan(&decoded,exec_bytes,offsets)) return false;
    *out=decoded;return true;
}
