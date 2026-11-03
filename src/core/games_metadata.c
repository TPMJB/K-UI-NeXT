/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_metadata.h"
#include <stdlib.h>
#include <string.h>
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4)<<32;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));}
static bool text(const char *s,size_t cap) {
    const char *end=memchr(s,0,cap);if(!end)return false;
    for(;s<end;s++)if((unsigned char)*s<32 || (unsigned char)*s>126)return false;
    return true;
}
static bool canonical(const char *path,const char *group) {
    struct kui_games_activity_entry key;
    return kui_games_activity_key(&key,path,group) && !strcmp(key.path,path) && !strcmp(key.group,group);
}
bool kui_games_library_edit_valid(const struct kui_games_library_edit *edit) {
    if(!edit || !text(edit->alias,sizeof(edit->alias)) || !memchr(edit->cover_path,0,sizeof(edit->cover_path)))return false;
    if(edit->alias[0]==' ' || (edit->alias[0] && edit->alias[strlen(edit->alias)-1u]==' '))return false;
    return !edit->cover_path[0] || canonical(edit->cover_path,"");
}
static bool entry_valid(const struct kui_games_metadata_entry *e) {
    if(!e || !memchr(e->path,0,sizeof(e->path)) || !text(e->group,sizeof(e->group)) || !memchr(e->anchor,0,sizeof(e->anchor)) ||
       !canonical(e->path,e->group) || !canonical(e->anchor,e->group) || !kui_games_library_edit_valid(&e->edit) ||
       !text(e->product,sizeof(e->product)) || !text(e->version,sizeof(e->version)) || !text(e->region,sizeof(e->region)) || e->disc>99 || e->representation>3)return false;
    unsigned nonzero=0;for(unsigned i=0;i<32;i++)nonzero|=e->identity[i];
    return e->verified?(nonzero && !e->group[0]):!nonzero;
}
static bool valid(const struct kui_games_metadata *table) {
    if(!table || table->count>KUI_GAMES_METADATA_CAP)return false;
    for(unsigned i=0;i<table->count;i++) {
        if(!entry_valid(&table->entries[i]))return false;
        for(unsigned j=0;j<i;j++)if(!strcmp(table->entries[i].path,table->entries[j].path) &&
            !strcmp(table->entries[i].group,table->entries[j].group))return false;
    }
    return true;
}
const struct kui_games_metadata_entry *kui_games_metadata_find(const struct kui_games_metadata *table,const char *path,const char *group) {
    struct kui_games_activity_entry key;
    if(!table || table->count>KUI_GAMES_METADATA_CAP || !kui_games_activity_key(&key,path,group))return NULL;
    for(unsigned i=0;i<table->count;i++)if(!strcmp(table->entries[i].path,key.path) && !strcmp(table->entries[i].group,key.group))return &table->entries[i];
    return NULL;
}
bool kui_games_metadata_upsert(struct kui_games_metadata *table,const struct kui_games_metadata_entry *entry) {
    if(!valid(table) || !entry_valid(entry))return false;
    const struct kui_games_metadata_entry *found=kui_games_metadata_find(table,entry->path,entry->group);
    unsigned at=found?(unsigned)(found-table->entries):table->count;
    if(at==KUI_GAMES_METADATA_CAP)return false;
    table->entries[at]=*entry;if(at==table->count)++table->count;return true;
}
bool kui_games_metadata_encode(void *wire,size_t capacity,size_t *bytes,const struct kui_games_metadata *table,uint64_t sequence) {
    if(!wire || !bytes || !sequence || !valid(table))return false;
    size_t total=40;
    for(unsigned i=0;i<table->count;i++) {
        const struct kui_games_metadata_entry *e=&table->entries[i];
        const char *fields[]={e->path,e->group,e->anchor,e->edit.alias,e->edit.cover_path,e->product,e->version,e->region};
        total+=64;for(unsigned j=0;j<8;j++)total+=strlen(fields[j]);
    }
    if(total>capacity)return false;
    uint8_t *p=wire;memset(p,0,total);memcpy(p,"KUIGMET1",8);put32(p+8,(uint32_t)total);put32(p+12,table->count);put64(p+16,sequence);
    size_t at=40;
    for(unsigned i=0;i<table->count;i++) {
        const struct kui_games_metadata_entry *e=&table->entries[i];
        const char *fields[]={e->path,e->group,e->anchor,e->edit.alias,e->edit.cover_path,e->product,e->version,e->region};
        for(unsigned j=0;j<8;j++) {size_t n=strlen(fields[j]);p[at+2*j]=(uint8_t)n;p[at+2*j+1]=(uint8_t)(n>>8);}
        p[at+16]=e->verified?1u:0u;p[at+17]=(uint8_t)e->disc;p[at+18]=(uint8_t)e->representation;memcpy(p+at+24,e->identity,32);at+=64;
        for(unsigned j=0;j<8;j++) {size_t n=strlen(fields[j]);memcpy(p+at,fields[j],n);at+=n;}
    }
    put32(p+32,kui_crc32(0,p+40,total-40));put32(p+36,kui_crc32(0,p,36));*bytes=total;return true;
}
bool kui_games_metadata_decode(struct kui_games_metadata *out,uint64_t *sequence,const void *wire,size_t bytes) {
    const uint8_t *p=wire;
    if(!out || !sequence || !p || bytes<40 || bytes>KUI_GAMES_METADATA_WIRE_MAX || memcmp(p,"KUIGMET1",8) ||
       get32(p+8)!=bytes || get32(p+12)>KUI_GAMES_METADATA_CAP || !get64(p+16) || get64(p+24) ||
       get32(p+32)!=kui_crc32(0,p+40,bytes-40) || get32(p+36)!=kui_crc32(0,p,36))return false;
    struct kui_games_metadata *draft=calloc(1,sizeof(*draft));if(!draft)return false;
    draft->count=get32(p+12);size_t at=40;bool ok=true;
    for(unsigned i=0;i<draft->count;i++) {
        if(bytes-at<64) {ok=false;break;}
        struct kui_games_metadata_entry *e=&draft->entries[i];
        char *fields[]={e->path,e->group,e->anchor,e->edit.alias,e->edit.cover_path,e->product,e->version,e->region};
        size_t caps[]={sizeof(e->path),sizeof(e->group),sizeof(e->anchor),sizeof(e->edit.alias),sizeof(e->edit.cover_path),sizeof(e->product),sizeof(e->version),sizeof(e->region)};
        unsigned sizes[8];size_t total=64;
        for(unsigned j=0;j<8;j++) {sizes[j]=p[at+2*j]|(unsigned)p[at+2*j+1]<<8;total+=sizes[j];if(sizes[j]>=caps[j])ok=false;}
        for(unsigned j=19;j<24;j++)if(p[at+j])ok=false;
        for(unsigned j=56;j<64;j++)if(p[at+j])ok=false;
        if(!ok || p[at+16]>1 || total>bytes-at || memchr(p+at+64,0,total-64)) {ok=false;break;}
        e->verified=p[at+16]!=0;e->disc=p[at+17];e->representation=p[at+18];memcpy(e->identity,p+at+24,32);at+=64;
        for(unsigned j=0;j<8;j++) {memcpy(fields[j],p+at,sizes[j]);at+=sizes[j];}
    }
    ok=ok && at==bytes && valid(draft);
    if(ok) {*out=*draft;*sequence=get64(p+16);}
    free(draft);return ok;
}
