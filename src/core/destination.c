/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/destination.h"
#include <stdio.h>
#include <string.h>

static unsigned char lower(unsigned char c) {return c>='A' && c<='Z'?(unsigned char)(c+32):c;}
static bool ascii_equal(const char *a,const char *b,size_t n) {
    for(size_t i=0;i<n;i++) if(lower((unsigned char)a[i])!=lower((unsigned char)b[i])) return false;
    return true;
}
static size_t bounded_length(const char *text,size_t limit) {
    size_t n=0;if(text) while(n<limit && text[n]) ++n;return n;
}
static bool reserved(const char *text,size_t length) {
    size_t stem=0;while(stem<length && text[stem]!='.') ++stem;
    if(stem==3 && (ascii_equal(text,"con",3) || ascii_equal(text,"prn",3) ||
                   ascii_equal(text,"aux",3) || ascii_equal(text,"nul",3))) return true;
    return stem==4 && (ascii_equal(text,"com",3) || ascii_equal(text,"lpt",3)) && text[3]>='1' && text[3]<='9';
}
static bool utf8(const char *text,size_t size,size_t *length,uint32_t *code) {
    if(!size) return false;
    const unsigned char *p=(const unsigned char *)text;
    unsigned n;uint32_t v,minimum;
    if(p[0]<0x80) {n=1;v=p[0];minimum=0;}
    else if(p[0]>=0xc2 && p[0]<=0xdf) {n=2;v=p[0]&31u;minimum=0x80;}
    else if(p[0]>=0xe0 && p[0]<=0xef) {n=3;v=p[0]&15u;minimum=0x800;}
    else if(p[0]>=0xf0 && p[0]<=0xf4) {n=4;v=p[0]&7u;minimum=0x10000;}
    else return false;
    if(n>size) return false;
    for(unsigned i=1;i<n;i++) {if((p[i]&0xc0)!=0x80) return false;v=(v<<6)|(p[i]&63u);}
    if(v<minimum || v>0x10ffff || (v>=0xd800 && v<=0xdfff)) return false;
    *length=n;*code=v;return true;
}
static bool invalid_code(uint32_t code) {
    return code<32 || (code>=0x7f && code<=0x9f) ||
        (code<128 && strchr("<>:\\|?*/\"",(int)code)!=NULL);
}
static bool component(const char *name,size_t size) {
    if(!size || (size==1 && name[0]=='.') || (size==2 && !memcmp(name,"..",2)) ||
       name[size-1]=='.' || name[size-1]==' ' || reserved(name,size)) return false;
    for(size_t at=0;at<size;) {
        size_t n;uint32_t code;
        if(!utf8(name+at,size-at,&n,&code) || invalid_code(code)) return false;
        at+=n;
    }
    return true;
}
void kui_destination_default(char out[KUI_DEST_ROOT_CAP]) {if(out) strcpy(out,"/Games");}
bool kui_destination_name_valid(const char *name) {
    size_t n=bounded_length(name,KUI_DEST_NAME_CAP);
    return name && n<KUI_DEST_NAME_CAP && component(name,n);
}
bool kui_destination_normalize(char out[KUI_DEST_ROOT_CAP],const char *path) {
    if(!out) return false;
    char result[KUI_DEST_ROOT_CAP];size_t used=1;
    size_t length=bounded_length(path,KUI_DEST_PATH_CAP);
    if(!path || !length || length==KUI_DEST_PATH_CAP || path[0]!='/') {out[0]=0;return false;}
    result[0]='/';
    for(size_t at=1;at<length;) {
        while(at<length && path[at]=='/') ++at;
        size_t start=at;while(at<length && path[at]!='/') ++at;
        size_t n=at-start;
        if(!n || (n==1 && path[start]=='.')) continue;
        if(!component(path+start,n) || used+(used>1?1:0)+n>=sizeof(result)) {out[0]=0;return false;}
        if(used>1) result[used++]='/';
        memcpy(result+used,path+start,n);used+=n;
    }
    result[used]=0;memcpy(out,result,used+1);return true;
}
bool kui_destination_parent(char out[KUI_DEST_ROOT_CAP],const char *path) {
    char normalized[KUI_DEST_ROOT_CAP];
    if(!out || !kui_destination_normalize(normalized,path)) {if(out) out[0]=0;return false;}
    char *slash=strrchr(normalized,'/');
    if(slash==normalized) normalized[1]=0;else *slash=0;
    strcpy(out,normalized);return true;
}
bool kui_destination_join(char out[KUI_DEST_ROOT_CAP],const char *parent,const char *name) {
    if(!out) return false;
    char root[KUI_DEST_ROOT_CAP],path[KUI_DEST_PATH_CAP];
    size_t n=bounded_length(name,KUI_DEST_NAME_CAP);
    if(!name || n==KUI_DEST_NAME_CAP || !component(name,n) || !kui_destination_normalize(root,parent)) {out[0]=0;return false;}
    int size=snprintf(path,sizeof(path),"%s%s%s",root,strcmp(root,"/")?"/":"",name);
    if(size<0 || size>=(int)sizeof(path)) {out[0]=0;return false;}
    return kui_destination_normalize(out,path);
}
void kui_destination_title(char out[KUI_DEST_TITLE_CAP],const char *title) {
    if(!out) return;
    char result[KUI_DEST_TITLE_CAP];size_t used=0,at=0;
    size_t size=bounded_length(title,KUI_DEST_PATH_CAP);
    bool space=false;
    while(title && at<size) {
        unsigned char first=(unsigned char)title[at];
        if(first==' ' || (first>=9 && first<=13)) {space=used!=0;++at;continue;}
        size_t n=1;uint32_t code=0;
        bool valid=utf8(title+at,size-at,&n,&code) && !invalid_code(code);
        if(!valid) n=1;
        size_t bytes=valid?n:1;
        if(used+(space?1u:0u)+bytes>=sizeof(result)) break;
        if(space) result[used++]=' ';
        space=false;
        if(valid) {memcpy(result+used,title+at,n);used+=n;} else result[used++]='_';
        at+=n;
    }
    while(used && (result[used-1]==' ' || result[used-1]=='.')) --used;
    size_t leading=0;while(leading<used && (result[leading]=='.' || result[leading]==' ')) ++leading;
    if(leading) {memmove(result,result+leading,used-leading);used-=leading;}
    result[used]=0;
    if(!used || reserved(result,used) || (used==1 && result[0]=='.') ||
       (used==2 && !memcmp(result,"..",2))) strcpy(result,"DreamcastDisc");
    strcpy(out,result);
}
static bool valid_title(const char *title) {
    size_t n=bounded_length(title,KUI_DEST_TITLE_CAP);
    if(!title || !n || n==KUI_DEST_TITLE_CAP) return false;
    char normalized[KUI_DEST_TITLE_CAP];kui_destination_title(normalized,title);
    return !strcmp(normalized,title) && component(title,n);
}
bool kui_destination_folder_name(char out[KUI_DEST_NAME_CAP],const char *title,unsigned index) {
    if(!out) return false;
    if(!valid_title(title) || !index || index>KUI_DEST_INDEX_MAX) {out[0]=0;return false;}
    char name[KUI_DEST_NAME_CAP];int n=index==1?snprintf(name,sizeof(name),"%s",title):snprintf(name,sizeof(name),"%s (%u)",title,index);
    if(n<0 || n>=(int)sizeof(name)) {out[0]=0;return false;}
    strcpy(out,name);return true;
}
bool kui_destination_folder_index(const char *name,const char *title,unsigned *index) {
    if(!name || !index || !valid_title(title)) return false;
    size_t n=bounded_length(name,KUI_DEST_NAME_CAP),base=strlen(title);
    if(n==KUI_DEST_NAME_CAP || n<base || !ascii_equal(name,title,base)) return false;
    if(n==base) {*index=1;return true;}
    if(n<base+4 || name[base]!=' ' || name[base+1]!='(' || name[n-1]!=')' || name[base+2]=='0') return false;
    unsigned value=0;
    for(size_t at=base+2;at<n-1;at++) {
        if(name[at]<'0' || name[at]>'9') return false;
        value=value*10u+(unsigned)(name[at]-'0');
        if(value>KUI_DEST_INDEX_MAX) return false;
    }
    if(value<2) return false;
    *index=value;return true;
}
bool kui_destination_job_path(char out[KUI_DEST_JOB_CAP],const char *root,const char *folder) {
    if(!out) return false;
    char normalized[KUI_DEST_ROOT_CAP],path[KUI_DEST_JOB_CAP];size_t n=bounded_length(folder,KUI_DEST_NAME_CAP);
    if(!folder || n==KUI_DEST_NAME_CAP || !component(folder,n) || !kui_destination_normalize(normalized,root)) {out[0]=0;return false;}
    int count=snprintf(path,sizeof(path),"0:%s%s%s",normalized,strcmp(normalized,"/")?"/":"",folder);
    if(count<0 || count>=(int)sizeof(path)) {out[0]=0;return false;}
    strcpy(out,path);return true;
}
static void put32(uint8_t *p,uint32_t value) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(8*i));}
static uint32_t get32(const uint8_t *p) {uint32_t v=0;for(unsigned i=0;i<4;i++) v|=(uint32_t)p[i]<<(8*i);return v;}
static void put64(uint8_t *p,uint64_t value) {for(unsigned i=0;i<8;i++) p[i]=(uint8_t)(value>>(8*i));}
static uint64_t get64(const uint8_t *p) {uint64_t v=0;for(unsigned i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i);return v;}
bool kui_destination_encode(uint8_t out[KUI_DEST_RECORD_SIZE],const char *root,uint64_t sequence) {
    char normalized[KUI_DEST_ROOT_CAP];
    if(!out || !sequence || !kui_destination_normalize(normalized,root)) return false;
    memset(out,0,KUI_DEST_RECORD_SIZE);memcpy(out,"KUIDEST1",8);
    put32(out+8,1);put32(out+12,KUI_DEST_RECORD_SIZE);put64(out+16,sequence);
    strcpy((char *)out+24,normalized);put32(out+KUI_DEST_RECORD_SIZE-4,kui_crc32(0,out,KUI_DEST_RECORD_SIZE-4));return true;
}
bool kui_destination_decode(char out[KUI_DEST_ROOT_CAP],uint64_t *sequence,const void *record,size_t size) {
    if(!out || !sequence || !record || size!=KUI_DEST_RECORD_SIZE) return false;
    const uint8_t *p=record;char normalized[KUI_DEST_ROOT_CAP];
    if(memcmp(p,"KUIDEST1",8) || get32(p+8)!=1 || get32(p+12)!=KUI_DEST_RECORD_SIZE || !get64(p+16) ||
       get32(p+size-4)!=kui_crc32(0,p,size-4)) return false;
    const char *path=(const char *)p+24;size_t n=bounded_length(path,KUI_DEST_ROOT_CAP);
    if(n==KUI_DEST_ROOT_CAP || !kui_destination_normalize(normalized,path) || strcmp(normalized,path)) return false;
    for(size_t at=n+1;at<KUI_DEST_ROOT_CAP;at++) if(path[at]) return false;
    strcpy(out,normalized);*sequence=get64(p+16);return true;
}
