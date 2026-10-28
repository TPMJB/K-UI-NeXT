/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_game_card.h"
#include "kui/vmu_persist_profile.h"
#include "kui/retail_image.h"
#include "kui/hash.h"
#include <string.h>

static const uint8_t game_card_magic[8]={'K','U','I','V','M','U','G','1'};
static const uint8_t game_card_template_cid[16]={'K','U','I','-','P','2','-','T','E','M','P','L','A','T','E','!'};

static void game_card_put32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(v>>(8u*i));
}
bool kui_vmu_game_card_key(const char *product,char out[KUI_VMU_GAME_CARD_KEY_BYTES]) {
    if(!out) return false;
    memset(out,0,KUI_VMU_GAME_CARD_KEY_BYTES);
    if(!product) return false;
    size_t first=0,last=strlen(product);
    while(first<last && product[first]==' ') ++first;
    while(last>first && product[last-1u]==' ') --last;
    if(last==first || last-first>10u) return false;
    char key[KUI_VMU_GAME_CARD_KEY_BYTES]={0};bool significant=false;
    for(size_t i=first;i<last;i++) {
        char c=product[i];
        if(c>='a' && c<='z') c=(char)(c-'a'+'A');
        if(!((c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-')) return false;
        if((c>='A' && c<='Z') || (c>='1' && c<='9')) significant=true;
        key[i-first]=c;
    }
    if(!significant) return false;
    memcpy(out,key,sizeof(key));
    return true;
}
bool kui_vmu_game_card_binding_encode(const char key[KUI_VMU_GAME_CARD_KEY_BYTES],
    uint8_t out[KUI_VMU_GAME_CARD_BINDING_BYTES]) {
    char check[KUI_VMU_GAME_CARD_KEY_BYTES];
    if(!out) return false;
    memset(out,0,KUI_VMU_GAME_CARD_BINDING_BYTES);
    if(!key || memchr(key,0,KUI_VMU_GAME_CARD_KEY_BYTES)==NULL ||
       !kui_vmu_game_card_key(key,check) || memcmp(check,key,KUI_VMU_GAME_CARD_KEY_BYTES)) return false;
    memcpy(out,game_card_magic,8u);game_card_put32(out+8u,1u);
    game_card_put32(out+12u,KUI_VMU_GAME_CARD_BINDING_BYTES);
    memcpy(out+16u,key,KUI_VMU_GAME_CARD_KEY_BYTES);
    game_card_put32(out+60u,kui_retail_crc32(0,out,60u));
    return true;
}
bool kui_vmu_game_card_binding_decode(const void *wire,size_t bytes,
    char key[KUI_VMU_GAME_CARD_KEY_BYTES]) {
    const uint8_t *p=wire;uint8_t again[KUI_VMU_GAME_CARD_BINDING_BYTES];
    char candidate[KUI_VMU_GAME_CARD_KEY_BYTES];
    if(!key) return false;
    memset(key,0,KUI_VMU_GAME_CARD_KEY_BYTES);
    if(!p || bytes!=KUI_VMU_GAME_CARD_BINDING_BYTES) return false;
    memcpy(candidate,p+16u,sizeof(candidate));
    if(!kui_vmu_game_card_binding_encode(candidate,again) || memcmp(again,p,sizeof(again))) return false;
    memcpy(key,candidate,sizeof(candidate));
    return true;
}
size_t kui_vmu_game_card_profile_slot(const void *payload,size_t bytes) {
    const uint8_t *p=payload;size_t found=SIZE_MAX;
    if(!p || bytes<KUI_VMU_PERSIST_PROFILE_WIRE_BYTES) return SIZE_MAX;
    static struct kui_vmu_persist_profile scratch;
    for(size_t at=0;at<=bytes-KUI_VMU_PERSIST_PROFILE_WIRE_BYTES;at+=4u) {
        if(memcmp(p+at,"KUIVMUP2",8u) ||
           kui_vmu_persist_profile_decode(&scratch,p+at,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES)) continue;
        if(found!=SIZE_MAX) return SIZE_MAX;
        found=at;
    }
    return found;
}
bool kui_vmu_game_card_template(const uint8_t cid[16]) {
    return cid && !memcmp(cid,game_card_template_cid,16u);
}
void kui_vmu_game_card_identity(const char key[KUI_VMU_GAME_CARD_KEY_BYTES],unsigned pair,
    const uint8_t cid[16],const uint8_t salt[16],uint8_t out[16]) {
    static const char label[]="K-UI game card identity v1";
    uint8_t number[4],digest[32];struct kui_sha256 hash;
    game_card_put32(number,pair);kui_sha256_init(&hash);
    kui_sha256_update(&hash,label,sizeof(label)-1u);
    kui_sha256_update(&hash,key,KUI_VMU_GAME_CARD_KEY_BYTES);
    kui_sha256_update(&hash,number,sizeof(number));
    kui_sha256_update(&hash,cid,16u);kui_sha256_update(&hash,salt,16u);
    kui_sha256_digest(&hash,digest);
    unsigned any=0;for(unsigned i=0;i<16u;i++) any|=digest[i];
    if(!any) digest[0]=1u;
    memcpy(out,digest,16u);
}
unsigned kui_vmu_game_card_pair_number(const char *name) {
    if(!name || name[0]!='p') return 0;
    unsigned number=0;
    for(unsigned i=1;i<5u;i++) {
        if(name[i]<'0' || name[i]>'9') return 0;
        number=number*10u+(unsigned)(name[i]-'0');
    }
    return name[5]?0:number;
}
