/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_native_child.h"
#include "kui/retail_image.h"
#include <string.h>

static bool child_valid(unsigned child) {
    return child>KUI_SMASH_NATIVE_CHILD_NONE && child<KUI_SMASH_NATIVE_CHILD_COUNT;
}
bool kui_smash_native_child_candidate(const struct kui_game_metadata *m) {
    return m && m->ip_valid && m->boot_valid && m->native_gd && !m->windows_ce &&
        !strcmp(m->product,"MK-51146") && !strcmp(m->version,"V1.002") &&
        !strcmp(m->region,"U") && !strcmp(m->bootfile,"1ST_READ.BIN") &&
        m->boot_bytes==KUI_SMASH_NATIVE_MENU_BYTES;
}
bool kui_smash_native_child_menu_owner(uint32_t ip_crc,uint32_t bytes,const uint8_t sha[32]) {
    static const uint8_t expected[32]={0x3a,0x31,0x1b,0x18,0x9a,0x48,0x70,0xcc,
        0xbe,0xfc,0x4f,0xcd,0x30,0x6e,0x2d,0x38,0xe5,0xae,0x81,0x0a,0xe5,0xed,0xd2,0x25,
        0xaf,0x6b,0x9a,0xa6,0xd5,0xdf,0x5f,0x10};
    return sha && ip_crc==KUI_SMASH_NATIVE_IP_CRC32 && bytes==KUI_SMASH_NATIVE_MENU_BYTES &&
        !memcmp(sha,expected,sizeof(expected));
}
bool kui_smash_native_child_engine_owner(uint32_t bytes,const uint8_t sha[32]) {
    static const uint8_t expected[32]={0x04,0xf4,0x9d,0x58,0x3b,0x39,0xbd,0xc7,
        0x10,0xce,0xc6,0xd0,0xa4,0x34,0xb5,0xd7,0x10,0xb9,0xbc,0xce,0xdd,0x53,0x9b,0xed,
        0xda,0x29,0x75,0x51,0x35,0xf0,0xfe,0x61};
    return sha && bytes==KUI_SMASH_NATIVE_ENGINE_BYTES && !memcmp(sha,expected,sizeof(expected));
}
static uint32_t seed_crc(const struct kui_smash_native_child_seed *s) {
    const uint32_t zero=0u;
    return kui_retail_crc32(kui_retail_crc32(0u,s,offsetof(struct kui_smash_native_child_seed,crc32)),&zero,4u);
}
bool kui_smash_native_child_seed_seal(struct kui_smash_native_child_seed *s) {
    if(!s || !child_valid(s->child))return false;
    s->magic0=KUI_SMASH_NATIVE_CHILD_SEED_MAGIC0;s->magic1=KUI_SMASH_NATIVE_CHILD_SEED_MAGIC1;
    s->version=1u;s->bytes=sizeof(*s);s->reserved[0]=s->reserved[1]=0u;s->crc32=seed_crc(s);
    return true;
}
bool kui_smash_native_child_seed_valid(const struct kui_smash_native_child_seed *s) {
    return s && s->magic0==KUI_SMASH_NATIVE_CHILD_SEED_MAGIC0 &&
        s->magic1==KUI_SMASH_NATIVE_CHILD_SEED_MAGIC1 && s->version==1u && s->bytes==sizeof(*s) &&
        child_valid(s->child) && !s->reserved[0] && !s->reserved[1] && s->crc32==seed_crc(s);
}
const char *kui_smash_native_child_name(unsigned child) {
    return child==KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II?"Phantasy Star II":
        child==KUI_SMASH_NATIVE_CHILD_SHINING_FORCE?"Shining Force":NULL;
}
const char *kui_smash_native_child_folder(unsigned child) {
    return child==KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II?"PHSTAR2":
        child==KUI_SMASH_NATIVE_CHILD_SHINING_FORCE?"SHFORCE":NULL;
}
const char *kui_smash_native_child_rom(unsigned child) {
    return child==KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II?"PHSTAR2.SGA":
        child==KUI_SMASH_NATIVE_CHILD_SHINING_FORCE?"SHFORCE.SGA":NULL;
}
bool kui_smash_native_child_arguments(void *context,size_t bytes,uint8_t ip_tail[16],unsigned child) {
    static const char psii[]="\\MDE_US.BIN\0PHSTAR2.SGA\0MODE0\0SKIP0\0SOUND0\0";
    static const char shining[]="\\MDE_US.BIN\0SHFORCE.SGA\0MODE0\0SKIP0\0SOUND1\0";
    if(!context || !ip_tail || bytes!=KUI_SMASH_NATIVE_CONTEXT_BYTES || !child_valid(child))return false;
    uintptr_t a=(uintptr_t)context,b=(uintptr_t)ip_tail;
    if(a>UINTPTR_MAX-bytes || b>UINTPTR_MAX-16u || (a<b+16u && b<a+bytes) ||
       kui_retail_crc32(0u,context,bytes)!=KUI_SMASH_NATIVE_CONTEXT_CRC32)return false;
    uint8_t *data=context;const char *folder=kui_smash_native_child_folder(child);
    const char *tokens=child==KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II?psii:shining;
    size_t token_bytes=child==KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II?sizeof(psii):sizeof(shining);
    memset(data+0x100u,0u,256u);memcpy(data+0x100u,tokens,token_bytes);
    memset(ip_tail,' ',12u);memcpy(ip_tail,folder,strlen(folder));
    ip_tail[12]=1u;ip_tail[13]=4u;
    return true;
}

bool kui_smash_native_child_frame(uint32_t frame[21],uint32_t address) {
    if(!frame || (address&UINT32_C(0xdfffffff))!=UINT32_C(0x8c00f3ac) ||
       frame[0] || frame[1] || frame[2]!=UINT32_C(0x8c000000) ||
       frame[3]!=UINT32_C(0x8c00f400) || frame[4]!=UINT32_C(0x700000f0) ||
       frame[5]!=UINT32_C(0xac00e0b2))return false;
    for(unsigned i=6u;i<21u;i++)if(frame[i])return false;
    frame[0]=0x28u;frame[5]=0u;frame[18]=frame[19]=0x800u;frame[20]=UINT32_C(0x8c0100a0);
    return true;
}
