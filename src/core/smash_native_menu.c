/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_native_menu.h"
#include "kui/smash_native_child.h"
#include "kui/retail_image.h"
#include <string.h>

struct menu_selection {const char *folder,*rom,*mode,*skip,*sound,*name;};
/* Exact commands in the SHA-bound original menu. These are selection
 * metadata, not cartridge contents or an inferred per-game save contract. */
static const struct menu_selection selections[]={
    {"ALTBEAST","ALTBEAST.SGA","MODE2","SKIP0","SOUND0","ALTERED BEAST"},
    {"COLUMNS","COLUMNS.SGA","MODE0","SKIP0","SOUND0","COLUMNS"},
    {"GOLDAXE","GOLDAXE.SGA","MODE0","SKIP0","SOUND0","GOLDEN AXE"},
    {"PHSTAR2","PHSTAR2.SGA","MODE0","SKIP0","SOUND0","PHANTASY STAR II"},
    {"REVSHIN","REVSHIN.SGA","MODE0","SKIP1","SOUND0","THE REVENGE OF SHINOBI"},
    {"SHFORCE","SHFORCE.SGA","MODE0","SKIP0","SOUND1","SHINING FORCE"},
    {"SONIC1","SONIC1.SGA","MODE0","SKIP1","SOUND0","SONIC THE HEDGEHOG"},
    {"STRAGE2","STRAGE2.SGA","MODE0","SKIP1","SOUND0","STREETS OF RAGE 2"},
    {"VECTORM","VECTORM.SGA","MODE4","SKIP2","SOUND0","VECTORMAN"},
    {"WRESTWAR","WRESTWAR.SGA","MODE0","SKIP0","SOUND0","WRESTLE WAR"}
};
_Static_assert(sizeof(selections)/sizeof(selections[0])==KUI_SMASH_NATIVE_MENU_SELECTIONS,
    "Original Smash Genesis selection count");
static bool disjoint(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);
}
static bool header(const struct kui_smash_native_menu_seed *s) {
    return s && s->magic0==KUI_SMASH_NATIVE_MENU_SEED_MAGIC0 &&
        s->magic1==KUI_SMASH_NATIVE_MENU_SEED_MAGIC1 && s->version==1u &&
        s->bytes==sizeof(*s) && !s->reserved[0] && !s->reserved[1];
}
bool kui_smash_native_menu_seed_blank(const struct kui_smash_native_menu_seed *s) {
    if(!header(s) || s->selection || s->crc32)return false;
    for(size_t i=0;i<sizeof(s->argv);i++)if(s->argv[i])return false;
    for(size_t i=0;i<sizeof(s->ip_tail);i++)if(s->ip_tail[i])return false;
    return true;
}
size_t kui_smash_native_menu_seed_find(const void *payload,size_t bytes) {
    if(!payload || bytes<sizeof(struct kui_smash_native_menu_seed) ||
       (uintptr_t)payload>UINTPTR_MAX-bytes)return SIZE_MAX;
    const uint8_t *data=payload;size_t found=SIZE_MAX;
    for(size_t at=0;at<=bytes-sizeof(struct kui_smash_native_menu_seed);at++) {
        struct kui_smash_native_menu_seed seed;
        uint32_t words[4];memcpy(words,data+at,sizeof(words));
        if(words[0]!=KUI_SMASH_NATIVE_MENU_SEED_MAGIC0 || words[1]!=KUI_SMASH_NATIVE_MENU_SEED_MAGIC1 ||
           words[2]!=1u || words[3]!=sizeof(seed))continue;
        memcpy(&seed,data+at,sizeof(seed));
        if(found!=SIZE_MAX || (at&3u) || !kui_smash_native_menu_seed_blank(&seed))return SIZE_MAX;
        found=at;
    }
    return found;
}
static unsigned selection(const uint8_t argv[256],const uint8_t ip[16]) {
    if(!argv || !ip || ip[12]!=1u || ip[13]!=4u)return 0u;
    for(unsigned i=0;i<sizeof(selections)/sizeof(selections[0]);i++) {
        const struct menu_selection *p=&selections[i];
        const char *tokens[]={"\\MDE_US.BIN",p->rom,p->mode,p->skip,p->sound};
        size_t at=0;bool match=true;
        for(unsigned j=0;j<5u;j++) {
            size_t n=strlen(tokens[j])+1u;
            if(n>256u-at || memcmp(argv+at,tokens[j],n)) {match=false;break;}
            at+=n;
        }
        if(!match || at>=256u || argv[at])continue;
        /* The original menu parser clears the entire argument field before
         * splitting its command. Admit its exact five-token representation. */
        for(size_t j=at;j<256u;j++)if(argv[j])match=false;
        if(!match)continue;
        size_t n=strlen(p->folder);
        if(memcmp(ip,p->folder,n))continue;
        for(size_t j=n;j<12u;j++)if(ip[j]!=' ')match=false;
        if(match)return i+1u;
    }
    return 0u;
}
static uint32_t seed_crc(const struct kui_smash_native_menu_seed *s) {
    const uint32_t zero=0;
    uint32_t crc=kui_retail_crc32(0u,s,offsetof(struct kui_smash_native_menu_seed,crc32));
    crc=kui_retail_crc32(crc,&zero,sizeof(zero));
    return kui_retail_crc32(crc,s->argv,sizeof(s->argv)+sizeof(s->ip_tail));
}
bool kui_smash_native_menu_seed_capture(struct kui_smash_native_menu_seed *s,
    const uint8_t argv[256],const uint8_t ip[16]) {
    if(!s || !argv || !ip || !disjoint(s,sizeof(*s),argv,256u) ||
       !disjoint(s,sizeof(*s),ip,16u) || !kui_smash_native_menu_seed_blank(s))return false;
    unsigned which=selection(argv,ip);if(!which)return false;
    memcpy(s->argv,argv,sizeof(s->argv));memcpy(s->ip_tail,ip,sizeof(s->ip_tail));
    s->selection=which;s->crc32=seed_crc(s);return true;
}
bool kui_smash_native_menu_seed_valid(const struct kui_smash_native_menu_seed *s) {
    return header(s) && s->selection && s->selection==selection(s->argv,s->ip_tail) && s->crc32==seed_crc(s);
}
const char *kui_smash_native_menu_name(unsigned which) {
    return which && which<=sizeof(selections)/sizeof(selections[0])?selections[which-1u].name:NULL;
}
const char *kui_smash_native_menu_folder(unsigned which) {
    return which && which<=KUI_SMASH_NATIVE_MENU_SELECTIONS?selections[which-1u].folder:NULL;
}
const char *kui_smash_native_menu_rom(unsigned which) {
    return which && which<=KUI_SMASH_NATIVE_MENU_SELECTIONS?selections[which-1u].rom:NULL;
}
bool kui_smash_native_menu_arguments(void *context,size_t bytes,uint8_t ip[16],
    const struct kui_smash_native_menu_seed *s) {
    if(!context || !ip || bytes!=KUI_SMASH_NATIVE_CONTEXT_BYTES ||
       !disjoint(context,bytes,ip,16u) || !disjoint(context,bytes,s,sizeof(*s)) ||
       !disjoint(ip,16u,s,sizeof(*s)) || !kui_smash_native_menu_seed_valid(s) ||
       kui_retail_crc32(0u,context,bytes)!=KUI_SMASH_NATIVE_CONTEXT_CRC32)return false;
    memcpy((uint8_t *)context+0x100u,s->argv,sizeof(s->argv));
    memcpy(ip,s->ip_tail,sizeof(s->ip_tail));return true;
}
