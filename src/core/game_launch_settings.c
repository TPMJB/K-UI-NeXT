/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_launch_settings.h"
#include <string.h>

static const uint8_t magic[8]={'K','U','I','L','C','H','0','1'};
static size_t bounded(const char *s,size_t cap) {
    size_t n=0;if(s) while(n<cap && s[n]) ++n;return n;
}
static unsigned char lower(unsigned char c) {return c>='A' && c<='Z'?(unsigned char)(c+32):c;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8));}
static uint32_t get32(const uint8_t *p) {uint32_t n=0;for(unsigned i=0;i<4;i++) n|=(uint32_t)p[i]<<(i*8);return n;}
static void put64(uint8_t *p,uint64_t n) {for(unsigned i=0;i<8;i++) p[i]=(uint8_t)(n>>(i*8));}
static uint64_t get64(const uint8_t *p) {uint64_t n=0;for(unsigned i=0;i<8;i++) n|=(uint64_t)p[i]<<(i*8);return n;}
void kui_game_launch_settings_default(struct kui_game_launch_settings *out) {
    if(out) *out=(struct kui_game_launch_settings){KUI_GAME_READER_STANDARD,false,KUI_GAME_ENCODING_AUTO};
}
bool kui_game_launch_settings_valid(const struct kui_game_launch_settings *s) {
    return s && s->reader<KUI_GAME_READER_COUNT && s->encoding<KUI_GAME_ENCODING_COUNT;
}
bool kui_game_launch_settings_normalize(char out[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP],const char *path) {
    if(!out || !path) return false;
    size_t source=bounded(path,KUI_GAME_LAUNCH_SETTINGS_PATH_CAP+2u);
    if(!source || source==KUI_GAME_LAUNCH_SETTINGS_PATH_CAP+2u) return false;
    size_t at=0;
    if(source>=2 && path[0]=='0' && path[1]==':') at=2;
    if(path[at]!='/' && path[at]!='\\') return false;
    char key[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];size_t used=0;
    while(at<source) {
        while(at<source && (path[at]=='/' || path[at]=='\\')) ++at;
        if(at==source) return false;
        size_t start=at;
        while(at<source && path[at]!='/' && path[at]!='\\') {
            unsigned char c=(unsigned char)path[at];
            if(c<32 || c==127 || strchr(":*?\"<>|",c)) return false;
            ++at;
        }
        size_t bytes=at-start;
        if((bytes==1 && path[start]=='.') || (bytes==2 && path[start]=='.' && path[start+1]=='.') ||
           path[start]==' ' || path[at-1]==' ' || path[at-1]=='.' || used+1u+bytes>=sizeof(key)) return false;
        key[used++]='/';
        for(size_t i=0;i<bytes;i++) key[used++]=(char)lower((unsigned char)path[start+i]);
    }
    key[used]=0;memcpy(out,key,used+1u);return true;
}
static bool canonical(const char *path) {
    char key[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    return bounded(path,KUI_GAME_LAUNCH_SETTINGS_PATH_CAP)<KUI_GAME_LAUNCH_SETTINGS_PATH_CAP &&
        kui_game_launch_settings_normalize(key,path) && !strcmp(key,path);
}
static bool table_valid(const struct kui_game_launch_table *t) {
    if(!t || t->count>KUI_GAME_LAUNCH_SETTINGS_ENTRIES) return false;
    for(unsigned i=0;i<t->count;i++) {
        if(!canonical(t->entries[i].path) || !kui_game_launch_settings_valid(&t->entries[i].settings)) return false;
        for(unsigned j=0;j<i;j++) if(!strcmp(t->entries[i].path,t->entries[j].path)) return false;
    }
    return true;
}
void kui_game_launch_table_clear(struct kui_game_launch_table *table) {if(table) memset(table,0,sizeof(*table));}
bool kui_game_launch_table_find(const struct kui_game_launch_table *table,const char *path,struct kui_game_launch_settings *out) {
    char key[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    if(!out || !table_valid(table) || !kui_game_launch_settings_normalize(key,path)) return false;
    for(unsigned i=0;i<table->count;i++) if(!strcmp(table->entries[i].path,key)) {
        *out=table->entries[i].settings;return true;
    }
    return false;
}
bool kui_game_launch_table_upsert(struct kui_game_launch_table *table,const char *path,const struct kui_game_launch_settings *settings) {
    char key[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    if(!table_valid(table) || !kui_game_launch_settings_valid(settings) ||
       !kui_game_launch_settings_normalize(key,path)) return false;
    unsigned index=0;
    while(index<table->count && strcmp(table->entries[index].path,key)) ++index;
    if(index==KUI_GAME_LAUNCH_SETTINGS_ENTRIES) return false;
    memcpy(table->entries[index].path,key,strlen(key)+1u);table->entries[index].settings=*settings;
    if(index==table->count) ++table->count;
    return true;
}
bool kui_game_launch_settings_encode(void *record,size_t capacity,size_t *size,const struct kui_game_launch_table *table,uint64_t sequence) {
    if(!record || !size || !sequence || !table_valid(table)) return false;
    size_t bytes=36;
    for(unsigned i=0;i<table->count;i++) bytes+=8u+strlen(table->entries[i].path);
    if(bytes>capacity || bytes>KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX) return false;
    uint8_t *p=record;memset(p,0,bytes);memcpy(p,magic,8);
    put32(p+8,1);put32(p+12,(uint32_t)bytes);put64(p+16,sequence);put32(p+24,table->count);
    size_t at=32;
    for(unsigned i=0;i<table->count;i++) {
        const struct kui_game_launch_entry *e=&table->entries[i];size_t n=strlen(e->path);
        p[at]=(uint8_t)n;p[at+1]=(uint8_t)(n>>8);p[at+2]=(uint8_t)e->settings.reader;
        p[at+3]=(uint8_t)(e->settings.cdda?1:0);p[at+4]=(uint8_t)e->settings.encoding;
        memcpy(p+at+8,e->path,n);at+=8+n;
    }
    put32(p+bytes-4,kui_crc32(0,p,bytes-4));*size=bytes;return true;
}
bool kui_game_launch_settings_decode(struct kui_game_launch_table *table,uint64_t *sequence,const void *record,size_t size) {
    if(!table || !sequence || !record || size<36 || size>KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX) return false;
    const uint8_t *p=record;
    if(memcmp(p,magic,8) || get32(p+8)!=1 || get32(p+12)!=size || !get64(p+16) ||
       get32(p+24)>KUI_GAME_LAUNCH_SETTINGS_ENTRIES || get32(p+28) ||
       get32(p+size-4)!=kui_crc32(0,p,size-4)) return false;
    unsigned count=get32(p+24);size_t at=32;
    const uint8_t *paths[KUI_GAME_LAUNCH_SETTINGS_ENTRIES];uint16_t lengths[KUI_GAME_LAUNCH_SETTINGS_ENTRIES];
    for(unsigned i=0;i<count;i++) {
        if(at>size-4 || size-4-at<8) return false;
        unsigned n=(unsigned)p[at]|(unsigned)p[at+1]<<8;
        if(!n || n>=KUI_GAME_LAUNCH_SETTINGS_PATH_CAP || n>size-4-at-8 ||
           p[at+2]>=KUI_GAME_READER_COUNT || p[at+3]>1 || p[at+4]>=KUI_GAME_ENCODING_COUNT ||
           p[at+5] || p[at+6] || p[at+7] || memchr(p+at+8,0,n)) return false;
        char key[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];memcpy(key,p+at+8,n);key[n]=0;
        if(!canonical(key)) return false;
        for(unsigned j=0;j<i;j++) if(lengths[j]==n && !memcmp(paths[j],p+at+8,n)) return false;
        paths[i]=p+at+8;lengths[i]=(uint16_t)n;at+=8u+n;
    }
    if(at!=size-4) return false;
    /* The validation pass completes before changing any caller output. */
    kui_game_launch_table_clear(table);table->count=count;at=32;
    for(unsigned i=0;i<count;i++) {
        unsigned n=lengths[i];struct kui_game_launch_entry *e=&table->entries[i];
        memcpy(e->path,paths[i],n);e->path[n]=0;
        e->settings=(struct kui_game_launch_settings){p[at+2],p[at+3]!=0,p[at+4]};at+=8u+n;
    }
    *sequence=get64(p+16);return true;
}
