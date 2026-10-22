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
    if(out) *out=(struct kui_game_launch_settings){.reader=KUI_GAME_READER_STANDARD,.encoding=KUI_GAME_ENCODING_AUTO};
}
bool kui_game_launch_settings_valid(const struct kui_game_launch_settings *s) {
    return s && s->reader<KUI_GAME_READER_COUNT && s->encoding<KUI_GAME_ENCODING_COUNT && s->codec<=KUI_ZSO_LZO1X &&
        kui_game_ram_patches_valid(&s->patches);
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
    for(unsigned i=0;i<table->count;i++) bytes+=24u+strlen(table->entries[i].path);
    if(bytes>capacity || bytes>KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX) return false;
    uint8_t *p=record;memset(p,0,bytes);memcpy(p,magic,8);
    put32(p+8,3);put32(p+12,(uint32_t)bytes);put64(p+16,sequence);put32(p+24,table->count);
    size_t at=32;
    for(unsigned i=0;i<table->count;i++) {
        const struct kui_game_launch_entry *e=&table->entries[i];size_t n=strlen(e->path);
        p[at]=(uint8_t)n;p[at+1]=(uint8_t)(n>>8);p[at+2]=(uint8_t)e->settings.reader;
        p[at+6]=(uint8_t)e->settings.codec;
        p[at+3]=(uint8_t)(e->settings.cdda?1:0);p[at+4]=(uint8_t)e->settings.encoding;
        for(unsigned j=0;j<2;j++) {
            const struct kui_game_ram_patch *patch=&e->settings.patches.entries[j];
            if(patch->enabled) p[at+5]|=(uint8_t)(1u<<j);
            put32(p+at+8u+j*8u,patch->address);put32(p+at+12u+j*8u,patch->value);
        }
        memcpy(p+at+24,e->path,n);at+=24+n;
    }
    put32(p+bytes-4,kui_crc32(0,p,bytes-4));*size=bytes;return true;
}
bool kui_game_launch_settings_decode(struct kui_game_launch_table *table,uint64_t *sequence,const void *record,size_t size) {
    if(!table || !sequence || !record || size<36 || size>KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX) return false;
    const uint8_t *p=record;
    uint32_t version=get32(p+8);size_t entry_size=version==1?8u:24u;
    if(memcmp(p,magic,8) || (version!=1 && version!=2 && version!=3) || get32(p+12)!=size || !get64(p+16) ||
       get32(p+24)>KUI_GAME_LAUNCH_SETTINGS_ENTRIES || get32(p+28) ||
       get32(p+size-4)!=kui_crc32(0,p,size-4)) return false;
    unsigned count=get32(p+24);size_t at=32;
    const uint8_t *paths[KUI_GAME_LAUNCH_SETTINGS_ENTRIES];uint16_t lengths[KUI_GAME_LAUNCH_SETTINGS_ENTRIES];
    for(unsigned i=0;i<count;i++) {
        if(at>size-4 || size-4-at<entry_size) return false;
        unsigned n=(unsigned)p[at]|(unsigned)p[at+1]<<8;
        if(!n || n>=KUI_GAME_LAUNCH_SETTINGS_PATH_CAP || n>size-4-at-entry_size ||
           p[at+2]>=KUI_GAME_READER_COUNT || p[at+3]>1 || p[at+4]>=KUI_GAME_ENCODING_COUNT ||
           (version==1?p[at+5]!=0:(p[at+5]&~3u)!=0) || (version==3?p[at+6]>KUI_ZSO_LZO1X:p[at+6]!=0) || p[at+7] ||
           memchr(p+at+entry_size,0,n)) return false;
        if(version>=2) {
            struct kui_game_ram_patches patches={0};
            for(unsigned j=0;j<2;j++) patches.entries[j]=(struct kui_game_ram_patch){
                .address=get32(p+at+8u+j*8u),.value=get32(p+at+12u+j*8u),.enabled=(p[at+5]&(1u<<j))!=0};
            if(!kui_game_ram_patches_valid(&patches)) return false;
        }
        char key[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];memcpy(key,p+at+entry_size,n);key[n]=0;
        if(!canonical(key)) return false;
        for(unsigned j=0;j<i;j++) if(lengths[j]==n && !memcmp(paths[j],p+at+entry_size,n)) return false;
        paths[i]=p+at+entry_size;lengths[i]=(uint16_t)n;at+=entry_size+n;
    }
    if(at!=size-4) return false;
    /* The validation pass completes before changing any caller output. */
    kui_game_launch_table_clear(table);table->count=count;at=32;
    for(unsigned i=0;i<count;i++) {
        unsigned n=lengths[i];struct kui_game_launch_entry *e=&table->entries[i];
        memcpy(e->path,paths[i],n);e->path[n]=0;
        e->settings=(struct kui_game_launch_settings){.reader=p[at+2],.cdda=p[at+3]!=0,.encoding=p[at+4],.codec=version==3?p[at+6]:KUI_ZSO_LZ4};
        if(version>=2) for(unsigned j=0;j<2;j++) e->settings.patches.entries[j]=(struct kui_game_ram_patch){
            .address=get32(p+at+8u+j*8u),.value=get32(p+at+12u+j*8u),.enabled=(p[at+5]&(1u<<j))!=0};
        at+=entry_size+n;
    }
    *sequence=get64(p+16);return true;
}

/* The fixed-schema JSON primitives below are retained from K-UI's own
 * saved-launch implementation, not an external loader. No recursive parser. */
struct legacy_parser {const unsigned char *at,*end;};
static void whitespace(struct legacy_parser *p) {
    while(p->at<p->end && (*p->at==' ' || *p->at=='\t' || *p->at=='\r' || *p->at=='\n')) ++p->at;
}
static bool take(struct legacy_parser *p,unsigned char token) {
    whitespace(p);
    if(p->at==p->end || *p->at!=token) return false;
    ++p->at;return true;
}
static bool literal(struct legacy_parser *p,const char *word) {
    whitespace(p);size_t size=strlen(word);
    if((size_t)(p->end-p->at)<size || memcmp(p->at,word,size)) return false;
    p->at+=size;return true;
}
static int hex(unsigned char c) {
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static bool unicode_quad(struct legacy_parser *p,uint32_t *value) {
    if((size_t)(p->end-p->at)<4) return false;
    uint32_t code=0;
    for(unsigned i=0;i<4;i++) {
        int digit=hex(*p->at++);if(digit<0) return false;
        code=code*16u+(unsigned)digit;
    }
    *value=code;return true;
}
static bool append_code(char *out,size_t cap,size_t *used,uint32_t code) {
    unsigned char bytes[4];size_t n;
    if(!code || code>0x10ffff || (code>=0xd800 && code<=0xdfff)) return false;
    if(code<0x80) {bytes[0]=(unsigned char)code;n=1;}
    else if(code<0x800) {bytes[0]=(unsigned char)(0xc0|(code>>6));bytes[1]=(unsigned char)(0x80|(code&63));n=2;}
    else if(code<0x10000) {
        bytes[0]=(unsigned char)(0xe0|(code>>12));bytes[1]=(unsigned char)(0x80|((code>>6)&63));
        bytes[2]=(unsigned char)(0x80|(code&63));n=3;
    } else {
        bytes[0]=(unsigned char)(0xf0|(code>>18));bytes[1]=(unsigned char)(0x80|((code>>12)&63));
        bytes[2]=(unsigned char)(0x80|((code>>6)&63));bytes[3]=(unsigned char)(0x80|(code&63));n=4;
    }
    if(*used+n>=cap) return false;
    memcpy(out+*used,bytes,n);*used+=n;return true;
}
static bool string(struct legacy_parser *p,char *out,size_t cap) {
    if(!take(p,'"')) return false;
    size_t used=0;
    while(p->at<p->end) {
        unsigned char c=*p->at++;
        if(c=='"') {out[used]=0;return true;}
        if(c<0x20) return false;
        if(c=='\\') {
            if(p->at==p->end) return false;
            c=*p->at++;
            if(c=='u') {
                uint32_t code,low;
                if(!unicode_quad(p,&code)) return false;
                if(code>=0xd800 && code<=0xdbff) {
                    if((size_t)(p->end-p->at)<2 || p->at[0]!='\\' || p->at[1]!='u') return false;
                    p->at+=2;
                    if(!unicode_quad(p,&low) || low<0xdc00 || low>0xdfff) return false;
                    code=0x10000u+((code-0xd800u)<<10)+(low-0xdc00u);
                }
                if(!append_code(out,cap,&used,code)) return false;
                continue;
            }
            switch(c) {
            case '"': case '\\': case '/': break;
            case 'b': c='\b';break;
            case 'f': c='\f';break;
            case 'n': c='\n';break;
            case 'r': c='\r';break;
            case 't': c='\t';break;
            default: return false;
            }
        } else if(c>=0x80) {
            uint32_t code,min;unsigned extra;
            if(c>=0xc2 && c<=0xdf) {code=c&31u;min=0x80;extra=1;}
            else if(c>=0xe0 && c<=0xef) {code=c&15u;min=0x800;extra=2;}
            else if(c>=0xf0 && c<=0xf4) {code=c&7u;min=0x10000;extra=3;}
            else return false;
            if((size_t)(p->end-p->at)<extra) return false;
            while(extra--) {
                unsigned char continuation=*p->at++;
                if((continuation&0xc0u)!=0x80u) return false;
                code=(code<<6)|(continuation&63u);
            }
            if(code<min || !append_code(out,cap,&used,code)) return false;
            continue;
        }
        if(used+1>=cap) return false;
        out[used++]=(char)c;
    }
    return false;
}

static bool legacy_choice(struct legacy_parser *p,struct kui_game_launch_settings *out,bool *unsupported) {
    if(!take(p,'{')) return false;
    unsigned fields=0;bool release=false;
    struct kui_game_launch_settings choice;kui_game_launch_settings_default(&choice);
    do {
        char key[16];if(!string(p,key,sizeof(key)) || !take(p,':')) return false;
        if(!strcmp(key,"reader")) {
            char reader[16];if((fields&1u) || !string(p,reader,sizeof(reader))) return false;
            if(!strcmp(reader,"standard")) choice.reader=KUI_GAME_READER_STANDARD;
            else if(!strcmp(reader,"x")) choice.reader=KUI_GAME_READER_BACKGROUND;
            else if(!strcmp(reader,"y")) choice.reader=KUI_GAME_READER_EAGER;
            else return false;
            fields|=1u;
        } else if(!strcmp(key,"cdda") || !strcmp(key,"release_loader")) {
            unsigned flag=!strcmp(key,"cdda")?2u:4u;bool value;
            if(fields&flag) return false;
            if(literal(p,"true")) value=true;
            else if(literal(p,"false")) value=false;
            else return false;
            if(flag==2u) choice.cdda=value;else release=value;
            fields|=flag;
        } else return false;
        if(take(p,'}')) {
            if((fields&3u)!=3u) return false;
            *out=choice;*unsupported=release;return true;
        }
    } while(take(p,','));
    return false;
}
static bool legacy_profiles(struct legacy_parser *p,struct kui_game_launch_table *table,bool excluded[KUI_GAME_LAUNCH_SETTINGS_ENTRIES]) {
    if(!take(p,'{')) return false;
    if(take(p,'}')) return true;
    do {
        if(table->count==KUI_GAME_LAUNCH_SETTINGS_ENTRIES) return false;
        char path[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP],key[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
        if(!string(p,path,sizeof(path)) || !kui_game_launch_settings_normalize(key,path) || !take(p,':')) return false;
        for(unsigned i=0;i<table->count;i++) if(!strcmp(table->entries[i].path,key)) return false;
        struct kui_game_launch_entry *entry=&table->entries[table->count];
        if(!legacy_choice(p,&entry->settings,&excluded[table->count])) return false;
        memcpy(entry->path,key,strlen(key)+1u);++table->count;
        if(take(p,'}')) return true;
    } while(take(p,','));
    return false;
}
bool kui_game_launch_settings_legacy_decode(struct kui_game_launch_table *table,unsigned *skipped,const void *json,size_t size) {
    if(!table || !skipped || !json || !size || size>KUI_GAME_LAUNCH_LEGACY_JSON_MAX) return false;
    struct legacy_parser p={(const unsigned char *)json,(const unsigned char *)json+size};
    bool excluded[KUI_GAME_LAUNCH_SETTINGS_ENTRIES]={false};unsigned fields=0;
    kui_game_launch_table_clear(table);
    if(!take(&p,'{')) return false;
    do {
        char key[16];if(!string(&p,key,sizeof(key)) || !take(&p,':')) return false;
        if(!strcmp(key,"version")) {
            if((fields&1u) || !literal(&p,"1")) return false;
            fields|=1u;
        } else if(!strcmp(key,"profiles")) {
            if((fields&2u) || !legacy_profiles(&p,table,excluded)) return false;
            fields|=2u;
        } else return false;
        if(take(&p,'}')) {
            whitespace(&p);if(fields!=3u || p.at!=p.end) return false;
            unsigned kept=0,ignored=0;
            for(unsigned i=0;i<table->count;i++) {
                if(excluded[i]) {++ignored;continue;}
                if(kept!=i) table->entries[kept]=table->entries[i];
                ++kept;
            }
            table->count=kept;*skipped=ignored;return true;
        }
    } while(take(&p,','));
    return false;
}
