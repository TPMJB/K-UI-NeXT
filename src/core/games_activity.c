/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_activity.h"
#include "kui/game_launch_settings.h"
#include "kui/hash.h"
#include <stdlib.h>
#include <string.h>
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4)<<32;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));}
bool kui_games_activity_key(struct kui_games_activity_entry *out,const char *path,const char *group) {
    if(!out || !path || !group)return false;
    struct kui_games_activity_entry key={0};
    if(!strcmp(path,"/") && group[0])strcpy(key.path,"/");
    else {
        char normalized[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
        if(!kui_game_launch_settings_normalize(normalized,path) || strlen(normalized)>=sizeof(key.path))return false;
        strcpy(key.path,normalized);
    }
    size_t n=0;while(n<33 && group[n])++n;
    if(n && n!=32)return false;
    for(unsigned i=0;i<n;i++) {
        unsigned char c=(unsigned char)group[i];
        if(c>='A' && c<='F')c+='a'-'A';
        if(!((c>='0' && c<='9') || (c>='a' && c<='f')))return false;
        key.group[i]=(char)c;
    }
    *out=key;return true;
}
static bool same(const struct kui_games_activity_entry *a,const struct kui_games_activity_entry *b) {return !strcmp(a->path,b->path) && !strcmp(a->group,b->group);}
const struct kui_games_activity_entry *kui_games_activity_find(const struct kui_games_activity *table,const char *path,const char *group) {
    struct kui_games_activity_entry key;
    if(!table || table->count>KUI_GAMES_ACTIVITY_CAP || !kui_games_activity_key(&key,path,group))return NULL;
    for(unsigned i=0;i<table->count;i++)if(same(&table->entries[i],&key))return &table->entries[i];
    return NULL;
}
static bool valid(const struct kui_games_activity *table) {
    if(!table || table->count>KUI_GAMES_ACTIVITY_CAP)return false;
    for(unsigned i=0;i<table->count;i++) {
        const struct kui_games_activity_entry *e=&table->entries[i];struct kui_games_activity_entry key;
        if(!memchr(e->path,0,sizeof(e->path)) || !memchr(e->group,0,sizeof(e->group)) ||
           !kui_games_activity_key(&key,e->path,e->group) || !same(e,&key) || e->played>table->clock || (!e->favorite && !e->played))return false;
        for(unsigned j=0;j<i;j++)if(same(e,&table->entries[j]))return false;
    }
    return true;
}
static struct kui_games_activity_entry *entry(struct kui_games_activity *table,const struct kui_games_activity_entry *key) {
    for(unsigned i=0;i<table->count;i++)if(same(&table->entries[i],key))return &table->entries[i];
    unsigned at=table->count;
    if(at==KUI_GAMES_ACTIVITY_CAP) {
        at=KUI_GAMES_ACTIVITY_CAP;
        for(unsigned i=0;i<table->count;i++)if(!table->entries[i].favorite &&
            (at==KUI_GAMES_ACTIVITY_CAP || table->entries[i].played<table->entries[at].played))at=i;
        if(at==KUI_GAMES_ACTIVITY_CAP)return NULL;
    } else ++table->count;
    table->entries[at]=*key;return &table->entries[at];
}
bool kui_games_activity_favorite(struct kui_games_activity *table,const char *path,const char *group,bool enabled) {
    struct kui_games_activity_entry key;
    if(!valid(table) || !kui_games_activity_key(&key,path,group))return false;
    const struct kui_games_activity_entry *old=kui_games_activity_find(table,path,group);
    if(!enabled && !old)return true;
    struct kui_games_activity_entry *e=entry(table,&key);if(!e)return false;e->favorite=enabled;
    if(!e->favorite && !e->played) {unsigned at=(unsigned)(e-table->entries);memmove(e,e+1,(table->count-at-1u)*sizeof(*e));--table->count;}
    return true;
}
bool kui_games_activity_played(struct kui_games_activity *table,const char *path,const char *group) {
    struct kui_games_activity_entry key;
    if(!valid(table) || table->clock==UINT64_MAX || !kui_games_activity_key(&key,path,group))return false;
    struct kui_games_activity_entry *e=entry(table,&key);if(!e)return false;e->played=++table->clock;return true;
}
bool kui_games_activity_encode(void *wire,size_t capacity,size_t *bytes,const struct kui_games_activity *table,uint64_t sequence) {
    if(!wire || !bytes || !sequence || !valid(table))return false;
    size_t total=40;
    for(unsigned i=0;i<table->count;i++)total+=12u+strlen(table->entries[i].path)+strlen(table->entries[i].group);
    if(total>capacity)return false;
    uint8_t *p=wire;memset(p,0,total);memcpy(p,"KUIACT01",8);put32(p+8,(uint32_t)total);put32(p+12,table->count);
    put64(p+16,sequence);put64(p+24,table->clock);size_t at=40;
    for(unsigned i=0;i<table->count;i++) {
        const struct kui_games_activity_entry *e=&table->entries[i];size_t pn=strlen(e->path),gn=strlen(e->group);
        p[at]=(uint8_t)pn;p[at+1]=(uint8_t)(pn>>8);p[at+2]=(uint8_t)gn;p[at+3]=e->favorite?1u:0u;put64(p+at+4,e->played);at+=12;
        memcpy(p+at,e->path,pn);at+=pn;memcpy(p+at,e->group,gn);at+=gn;
    }
    put32(p+32,kui_crc32(0,p+40,total-40));put32(p+36,kui_crc32(0,p,36));*bytes=total;return true;
}
bool kui_games_activity_decode(struct kui_games_activity *out,uint64_t *sequence,const void *wire,size_t bytes) {
    const uint8_t *p=wire;
    if(!out || !sequence || !p || bytes<40 || bytes>KUI_GAMES_ACTIVITY_WIRE_MAX || memcmp(p,"KUIACT01",8) ||
       get32(p+8)!=bytes || get32(p+12)>KUI_GAMES_ACTIVITY_CAP || !get64(p+16) ||
       get32(p+32)!=kui_crc32(0,p+40,bytes-40) || get32(p+36)!=kui_crc32(0,p,36))return false;
    struct kui_games_activity *draft=calloc(1,sizeof(*draft));if(!draft)return false;
    draft->count=get32(p+12);draft->clock=get64(p+24);size_t at=40;bool ok=true;
    for(unsigned i=0;i<draft->count;i++) {
        if(bytes-at<12) {ok=false;break;}
        unsigned pn=p[at]|(unsigned)p[at+1]<<8,gn=p[at+2];struct kui_games_activity_entry *e=&draft->entries[i];
        if(!pn || pn>=sizeof(e->path) || (gn && gn!=32) || p[at+3]>1 || pn+gn>bytes-at-12u ||
           memchr(p+at+12,0,pn+gn)) {ok=false;break;}
        e->favorite=p[at+3]!=0;e->played=get64(p+at+4);at+=12;memcpy(e->path,p+at,pn);at+=pn;memcpy(e->group,p+at,gn);at+=gn;
    }
    ok=ok && at==bytes && valid(draft);
    if(ok) {*out=*draft;*sequence=get64(p+16);}
    free(draft);return ok;
}
