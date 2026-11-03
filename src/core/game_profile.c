/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_profile.h"
#include "kui/hash.h"
#include "kui/wince_cdda_profiles.h"
#include <stdio.h>
#include <string.h>

struct builtin_profile {
    unsigned audio_candidate;
    uint32_t boot_bytes,ip_crc32;
    const char *product,*version,*name,*note,*evidence,*build;
};
/* Evidence is intentionally limited to the exact executable/release and
 * historical tested setup. New linked packages retain their own acceptance.
 * Toy's existing tested audio label includes its unresolved FMV limitation. */
static const struct builtin_profile builtin[]={
    {0u,3915142u,0u,"T1204N","V1.000","Code Veronica / SCI New reader",
     "Historical boot/save/reload and disc 2 passed; current build still needs console acceptance.",
     "docs/evidence/native-code-veronica-console-2026-10-28.json","2322fda6f92e"},
    {1u,1253376u,UINT32_C(0x684f8040),NULL,NULL,"Armada / SCI CD music",
     "Historical CD music evidence; virtual VMU and this rebuilt package are not certified.",
     "docs/experimental-cdda.md",""},
    {2u,1830912u,UINT32_C(0x8fb049c5),NULL,NULL,"Bust-A-Move 4 / SCI CD music",
     "Historical CD music evidence; virtual VMU and this rebuilt package are not certified.",
     "docs/experimental-cdda.md",""},
    {3u,1110016u,UINT32_C(0x86bbb334),NULL,NULL,"Worms Armageddon / SCI CD music",
     "Historical raw/2048 CD music evidence; virtual VMU and this package are not certified.",
     "docs/experimental-cdda.md",""},
    {4u,748444u,UINT32_C(0x38ba2868),NULL,NULL,"Toy Commander / SCI CD music",
     "Historical raw-disc music passed. FMV stalls remain; virtual VMU and this package are not certified.",
     "docs/experimental-cdda.md",""}
};
static const uint8_t veronica_sha[32]={
    0x19,0x9b,0x72,0xaf,0x9b,0xcd,0x7f,0xb8,0x0d,0x40,0x1d,0x46,0x45,0xc7,0x31,0xe5,
    0x95,0x97,0x0a,0x0d,0xcb,0xcf,0xd9,0x8e,0x1b,0xec,0x18,0x7f,0x43,0x82,0xfb,0xbb};
static const uint8_t toy_sha[32]={
    0xee,0x68,0xa3,0x26,0xda,0x96,0x4f,0xec,0x58,0x92,0xb7,0x22,0x63,0x11,0xf2,0x29,
    0x3f,0xd3,0xa0,0xbb,0xf5,0xba,0xc4,0x96,0xec,0x82,0x99,0x19,0x64,0xd4,0x9b,0xbd};
static const uint8_t *builtin_sha(unsigned i) {
    return !i?veronica_sha:i==4u?toy_sha:
        kui_wince_cdda_kernel_sha((enum kui_wince_cdda_profile)i);
}
static bool bounded_text(const char *p,size_t bytes,bool required) {
    const char *end=memchr(p,0,bytes);
    if(!end || (required && end==p))return false;
    for(const char *at=p;at<end;at++)if((unsigned char)*at<32u || (unsigned char)*at>126u)return false;
    for(const char *at=end;at<p+bytes;at++)if(*at)return false;
    return true;
}
static bool identity_valid(const struct kui_game_profile_identity *id) {
    return id && id->transport==KUI_STORAGE_SCI && id->format==KUI_GAME_IMAGE_GDI &&
        id->native_gd!=id->windows_ce &&
        id->boot_bytes && id->boot_bytes<=12u*1024u*1024u && id->audio_candidate<=4u &&
        bounded_text(id->product,sizeof(id->product),true) && bounded_text(id->version,sizeof(id->version),true) &&
        bounded_text(id->region,sizeof(id->region),true);
}
static bool metadata_equal(const struct kui_game_profile_identity *a,const struct kui_game_profile_identity *b) {
    return a->transport==b->transport && a->format==b->format && a->boot_bytes==b->boot_bytes &&
        a->windows_ce==b->windows_ce && !strcmp(a->product,b->product) &&
        !strcmp(a->version,b->version) && !strcmp(a->region,b->region);
}
static bool builtin_candidate(unsigned i,const struct kui_game_profile_identity *id) {
    const struct builtin_profile *b=&builtin[i];
    return id->boot_bytes==b->boot_bytes && id->audio_candidate==b->audio_candidate &&
        id->windows_ce==(i>=1u && i<=3u) && (i || !strcmp(id->region,"U")) &&
        (!b->product || !strcmp(id->product,b->product)) && (!b->version || !strcmp(id->version,b->version));
}
static bool settings_equal(const struct kui_game_launch_settings *a,const struct kui_game_launch_settings *b) {
    if(a->reader!=b->reader || a->cdda!=b->cdda || a->encoding!=b->encoding ||
       a->codec!=b->codec || a->vmu_slot!=b->vmu_slot)return false;
    for(unsigned i=0;i<KUI_GAME_RAM_PATCHES_COUNT;i++) {
        if(a->patches.entries[i].enabled!=b->patches.entries[i].enabled)return false;
        if(a->patches.entries[i].enabled && (a->patches.entries[i].address!=b->patches.entries[i].address ||
           a->patches.entries[i].value!=b->patches.entries[i].value))return false;
    }
    return true;
}
static bool digest_valid(const uint8_t hash[32]) {
    unsigned zeros=0u,ones=0u;
    for(unsigned i=0;i<32u;i++){zeros+=hash[i]==0u;ones+=hash[i]==255u;}
    return zeros!=32u && ones!=32u;
}
static bool pack_row_valid(const struct kui_game_profile_row *r) {
    if(!identity_valid(&r->identity) || r->identity.audio_candidate || !r->identity.ip_crc32 ||
       !digest_valid(r->identity.boot_sha256) || !bounded_text(r->name,sizeof(r->name),true) ||
       !bounded_text(r->note,sizeof(r->note),false) || !kui_game_launch_settings_valid(&r->recommended) ||
       r->recommended.codec!=KUI_ZSO_LZ4 || r->recommended.encoding!=KUI_GAME_ENCODING_AUTO ||
       (r->identity.windows_ce?r->recommended.reader!=KUI_GAME_READER_BACKGROUND:
        r->recommended.cdda && r->recommended.reader!=KUI_GAME_READER_STANDARD))return false;
    for(unsigned i=0;i<KUI_GAME_RAM_PATCHES_COUNT;i++)
        if(r->recommended.patches.entries[i].enabled || r->recommended.patches.entries[i].address ||
           r->recommended.patches.entries[i].value)return false;
    return true;
}
static bool pack_valid(const struct kui_game_profile_pack *pack) {
    if(!pack || pack->count>KUI_GAME_PROFILE_PACK_ROWS)return false;
    for(unsigned i=0;i<pack->count;i++) {
        if(!pack_row_valid(&pack->rows[i]))return false;
        for(unsigned j=0;j<i;j++)if(metadata_equal(&pack->rows[i].identity,&pack->rows[j].identity) &&
           pack->rows[i].identity.ip_crc32==pack->rows[j].identity.ip_crc32 &&
           !memcmp(pack->rows[i].identity.boot_sha256,pack->rows[j].identity.boot_sha256,32u))return false;
    }
    return true;
}
bool kui_game_profile_candidate(const struct kui_game_profile_identity *id,const struct kui_game_profile_pack *pack) {
    if(!identity_valid(id))return false;
    for(unsigned i=0;i<sizeof(builtin)/sizeof(builtin[0]);i++)if(builtin_candidate(i,id))return true;
    if(pack_valid(pack))for(unsigned i=0;i<pack->count;i++)if(metadata_equal(id,&pack->rows[i].identity))return true;
    return false;
}
bool kui_game_profile_query(const struct kui_game_profile_identity *id,const struct kui_game_profile_pack *pack,
        struct kui_game_profile_match *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!identity_valid(id) || !digest_valid(id->boot_sha256))return false;
    for(unsigned i=0;i<sizeof(builtin)/sizeof(builtin[0]);i++) {
        const struct builtin_profile *b=&builtin[i];
        if(!builtin_candidate(i,id) || (b->ip_crc32 && id->ip_crc32!=b->ip_crc32) ||
           memcmp(id->boot_sha256,builtin_sha(i),32u))continue;
        out->matched=out->automatic=true;out->status=KUI_GAME_PROFILE_TESTED;
        snprintf(out->name,sizeof(out->name),"%s",b->name);snprintf(out->note,sizeof(out->note),"%s",b->note);
        snprintf(out->evidence,sizeof(out->evidence),"%s",b->evidence);
        snprintf(out->tested_build,sizeof(out->tested_build),"%s",b->build);
        kui_game_launch_settings_default(&out->recommended);
        out->recommended.reader=i==4u?KUI_GAME_READER_STANDARD:KUI_GAME_READER_BACKGROUND;
        out->recommended.cdda=i!=0u;
        return true;
    }
    if(pack_valid(pack))for(unsigned i=0;i<pack->count;i++) {
        const struct kui_game_profile_row *r=&pack->rows[i];
        if(!metadata_equal(id,&r->identity) || id->ip_crc32!=r->identity.ip_crc32 ||
           memcmp(id->boot_sha256,r->identity.boot_sha256,32u))continue;
        out->matched=true;out->status=KUI_GAME_PROFILE_EXPERIMENTAL;
        snprintf(out->name,sizeof(out->name),"%s",r->name);snprintf(out->note,sizeof(out->note),"%s",r->note);
        snprintf(out->evidence,sizeof(out->evidence),"%s",KUI_GAME_PROFILE_PACK_PATH+2);
        out->recommended=r->recommended;return true;
    }
    return false;
}
const char *kui_game_profile_status_name(enum kui_game_profile_status status) {
    return status==KUI_GAME_PROFILE_TESTED?"Tested setup":status==KUI_GAME_PROFILE_CUSTOM?"Custom":"Experimental";
}
bool kui_game_profile_apply(const struct kui_game_profile_match *m,struct kui_game_launch_settings *out) {
    if(!m || !out || !m->matched || (m->status!=KUI_GAME_PROFILE_TESTED && m->status!=KUI_GAME_PROFILE_EXPERIMENTAL) ||
       !kui_game_launch_settings_valid(&m->recommended))return false;
    *out=m->recommended;return true;
}
bool kui_game_profile_resolve(const struct kui_game_profile_match *m,const struct kui_game_launch_settings *baseline,
        const struct kui_game_launch_settings *saved,bool found,struct kui_game_launch_settings *out) {
    if(!out || !baseline || !kui_game_launch_settings_valid(baseline) ||
       (found && (!saved || !kui_game_launch_settings_valid(saved))))return false;
    if(found){*out=*saved;return true;}
    if(m && m->matched && m->automatic && m->status==KUI_GAME_PROFILE_TESTED)return kui_game_profile_apply(m,out);
    *out=*baseline;return true;
}
enum kui_game_profile_status kui_game_profile_settings_status(const struct kui_game_profile_match *m,
        const struct kui_game_launch_settings *settings,bool saved) {
    if(!settings || !kui_game_launch_settings_valid(settings))return KUI_GAME_PROFILE_EXPERIMENTAL;
    if(m && m->matched && kui_game_launch_settings_valid(&m->recommended) && settings_equal(settings,&m->recommended))return m->status;
    return saved?KUI_GAME_PROFILE_CUSTOM:KUI_GAME_PROFILE_EXPERIMENTAL;
}
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void write32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(8u*i));}
static bool zero(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return false;return true;}
bool kui_game_profile_pack_encode(const struct kui_game_profile_pack *pack,void *buffer,size_t cap,size_t *size) {
    if(!buffer || !size || !pack_valid(pack))return false;
    size_t bytes=KUI_GAME_PROFILE_PACK_HEADER+(size_t)pack->count*KUI_GAME_PROFILE_PACK_ROW_BYTES+4u;
    if(cap<bytes)return false;
    uint8_t *p=buffer;memset(p,0,bytes);memcpy(p,"KUIGPF01",8u);
    write32(p+8u,1u);write32(p+12u,pack->count);write32(p+16u,KUI_GAME_PROFILE_PACK_ROW_BYTES);
    write32(p+20u,(uint32_t)bytes);
    for(unsigned i=0;i<pack->count;i++) {
        const struct kui_game_profile_row *r=&pack->rows[i];uint8_t *v=p+32u+i*256u;
        memcpy(v,r->identity.product,17u);memcpy(v+17u,r->identity.version,7u);
        memcpy(v+24u,r->name,48u);memcpy(v+72u,r->note,96u);
        write32(v+168u,r->identity.boot_bytes);write32(v+172u,r->identity.ip_crc32);
        write32(v+176u,r->identity.transport);write32(v+180u,r->identity.format);
        memcpy(v+184u,r->identity.boot_sha256,32u);
        v[216]=r->recommended.reader;v[217]=r->recommended.cdda;v[218]=r->recommended.encoding;
        v[219]=r->recommended.codec;v[220]=r->recommended.vmu_slot;
        v[221]=r->identity.windows_ce;memcpy(v+222u,r->identity.region,9u);
    }
    write32(p+bytes-4u,kui_crc32(0u,p,bytes-4u));*size=bytes;return true;
}
bool kui_game_profile_pack_decode(struct kui_game_profile_pack *out,const void *buffer,size_t bytes) {
    if(!out || !buffer || bytes<36u || bytes>KUI_GAME_PROFILE_PACK_MAX)return false;
    const uint8_t *p=buffer;uint32_t count=read32(p+12u);
    if(memcmp(p,"KUIGPF01",8u) || read32(p+8u)!=1u || count>KUI_GAME_PROFILE_PACK_ROWS ||
       read32(p+16u)!=256u || read32(p+20u)!=bytes || !zero(p+24u,8u) ||
       bytes!=32u+(size_t)count*256u+4u || read32(p+bytes-4u)!=kui_crc32(0u,p,bytes-4u))return false;
    struct kui_game_profile_pack pack={.count=count};
    for(unsigned i=0;i<count;i++) {
        struct kui_game_profile_row *r=&pack.rows[i];const uint8_t *v=p+32u+i*256u;
        if(!zero(v+231u,25u) || v[217]>1u || v[221]>1u)return false;
        memcpy(r->identity.product,v,17u);memcpy(r->identity.version,v+17u,7u);
        memcpy(r->name,v+24u,48u);memcpy(r->note,v+72u,96u);
        r->identity.boot_bytes=read32(v+168u);r->identity.ip_crc32=read32(v+172u);
        r->identity.transport=read32(v+176u);r->identity.format=read32(v+180u);
        memcpy(r->identity.boot_sha256,v+184u,32u);
        r->recommended.reader=v[216];r->recommended.cdda=v[217]!=0u;r->recommended.encoding=v[218];
        r->recommended.codec=v[219];r->recommended.vmu_slot=v[220];
        r->identity.windows_ce=v[221]!=0u;r->identity.native_gd=!r->identity.windows_ce;
        memcpy(r->identity.region,v+222u,9u);
    }
    if(!pack_valid(&pack))return false;
    *out=pack;return true;
}
