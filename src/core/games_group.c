/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_group.h"
#include "kui/destination.h"
#include <stdint.h>
#include <string.h>

static unsigned char lower(unsigned char c) {
    return c>='A' && c<='Z'?(unsigned char)(c+'a'-'A'):c;
}
static bool equal(const char *text,const char *word,size_t count) {
    for(size_t i=0;i<count;i++)
        if(lower((unsigned char)text[i])!=(unsigned char)word[i]) return false;
    return true;
}
static bool space(unsigned char c) {return c==' ' || c=='_';}
static bool join(unsigned char c) {return space(c) || c=='-';}
static bool boundary(unsigned char c) {
    return join(c) || c=='(' || c==')' || c=='[' || c==']' || c=='{' || c=='}';
}
static bool digit(unsigned char c) {return c>='0' && c<='9';}
static size_t image_stem(const char *name,size_t size) {
    static const char *const extensions[]={".gdi",".cue",".cdi",".iso",".bin",
        ".img",".cso",".zso",".chd"};
    for(unsigned i=0;i<sizeof(extensions)/sizeof(extensions[0]);i++) {
        size_t length=strlen(extensions[i]);
        if(size>length && equal(name+size-length,extensions[i],length)) return size-length;
    }
    return size;
}
static bool number(const char *name,size_t size,size_t *at,unsigned *out) {
    size_t start=*at;unsigned value=0;
    while(*at<size && digit((unsigned char)name[*at])) {
        value=value*10u+(unsigned)(name[(*at)++]-'0');
        /* More than two digits is ambiguous even if padded with zeros. */
        if(*at-start>2u) return false;
    }
    if(*at==start || !value || value>99u) return false;
    *out=value;return true;
}
static bool title_before(const char *name,size_t at) {
    for(size_t i=0;i<at;i++)
        if(!boundary((unsigned char)name[i])) return true;
    return false;
}
static bool marker(const char *name,size_t size,size_t start,size_t *end,unsigned *disc) {
    if(start && !boundary((unsigned char)name[start-1u])) return false;
    size_t at=start;
    if(size-at>=4u && (equal(name+at,"disc",4) || equal(name+at,"disk",4))) at+=4u;
    else if(size-at>=2u && equal(name+at,"cd",2)) at+=2u;
    /* D2 is also a Dreamcast game title. An abbreviated number can qualify
     * an existing title, but cannot establish a leading title as a disc. */
    else if(lower((unsigned char)name[at])=='d' && at+1u<size &&
        digit((unsigned char)name[at+1u]) && title_before(name,start)) ++at;
    else return false;
    while(at<size && join((unsigned char)name[at])) ++at;
    unsigned value;
    if(!number(name,size,&at,&value) || (at<size && !boundary((unsigned char)name[at]))) return false;
    size_t tail=at;
    while(tail<size && space((unsigned char)name[tail])) ++tail;
    if(size-tail>=2u && equal(name+tail,"of",2) &&
       tail+2u<size && space((unsigned char)name[tail+2u])) {
        tail+=2u;
        while(tail<size && space((unsigned char)name[tail])) ++tail;
        unsigned total;
        if(!number(name,size,&tail,&total) || value>total ||
           (tail<size && !boundary((unsigned char)name[tail]))) return false;
        at=tail;
    }
    *end=at;*disc=value;return true;
}
bool kui_games_group_parse(const char *name,char base[KUI_GAMES_GROUP_NAME_CAP],unsigned *disc) {
    if(!base || !disc || !kui_destination_name_valid(name)) return false;
    size_t size=image_stem(name,strlen(name)),start=0,end=0;unsigned found=0,value=0;
    for(size_t i=0;i<size;i++) {
        size_t tail;unsigned candidate;
        if(marker(name,size,i,&tail,&candidate)) {
            if(++found>1u) return false;
            start=i;end=tail;value=candidate;i=tail-1u;
        }
    }
    if(!found) return false;
    size_t left=start,right=end;
    while(left && space((unsigned char)name[left-1u])) --left;
    while(right<size && space((unsigned char)name[right])) ++right;
    if(left && right<size && ((name[left-1u]=='(' && name[right]==')') ||
       (name[left-1u]=='[' && name[right]==']') || (name[left-1u]=='{' && name[right]=='}'))) {
        start=left-1u;end=right+1u;
    }
    while(start && join((unsigned char)name[start-1u])) --start;
    while(end<size && join((unsigned char)name[end])) ++end;
    char result[KUI_GAMES_GROUP_NAME_CAP];size_t used=0;bool pending=false;
    for(size_t i=0;i<size;i++) {
        if(i==start) {i=end;if(used) pending=true;}
        if(i>=size) break;
        unsigned char c=(unsigned char)name[i];
        if(space(c)) {if(used) pending=true;continue;}
        if(pending) result[used++]=' ';
        pending=false;result[used++]=(char)c;
    }
    result[used]=0;
    if(!used || !kui_destination_name_valid(result)) return false;
    memcpy(base,result,used+1u);*disc=value;return true;
}
bool kui_games_group_capture_base(const char *name,char base[KUI_GAMES_GROUP_NAME_CAP]) {
    if(!base || !kui_destination_name_valid(name)) return false;
    size_t size=image_stem(name,strlen(name));
    if(size && name[size-1u]==')') {
        size_t at=size-1u;
        while(at && digit((unsigned char)name[at-1u])) --at;
        size_t digits=size-1u-at;
        if(digits && digits<=4u && name[at]!='0' && at>=2u &&
           name[at-1u]=='(' && name[at-2u]==' ') {
            unsigned ordinal=0;
            for(size_t i=at;i<size-1u;i++) ordinal=ordinal*10u+(unsigned)(name[i]-'0');
            if(ordinal>=2u && ordinal<=9999u) size=at-2u;
        }
    }
    size_t start=0;
    while(start<size && name[start]==' ') ++start;
    while(size>start && name[size-1u]==' ') --size;
    if(size==start) return false;
    char result[KUI_GAMES_GROUP_NAME_CAP];
    memcpy(result,name+start,size-start);result[size-start]=0;
    if(!kui_destination_name_valid(result)) return false;
    memcpy(base,result,size-start+1u);return true;
}
static bool evidence_field(char *out,const uint8_t *input,size_t size) {
    for(size_t i=0;i<size;i++) if(input[i]<32u || input[i]>126u) return false;
    size_t start=0,end=size;
    while(start<end && input[start]==' ') ++start;
    while(end>start && input[end-1u]==' ') --end;
    if(start==end) return false;
    memcpy(out,input+start,end-start);out[end-start]=0;return true;
}
static bool evidence_text(const char *text,size_t cap) {
    if(!text || !text[0] || text[0]==' ') return false;
    size_t size=0;
    while(size<cap && text[size]) {
        if((unsigned char)text[size]<32u || (unsigned char)text[size]>126u) return false;
        ++size;
    }
    return size<cap && text[size-1u]!=' ';
}
static bool evidence_product(const char *product) {
    bool alpha=false,numeric=false;
    for(size_t i=0;product[i];i++) {
        unsigned char c=(unsigned char)product[i];
        if(c>='A' && c<='Z') alpha=true;
        else if(digit(c)) numeric=true;
        else if(c!='-' || !i || !product[i+1u]) return false;
    }
    return alpha && numeric;
}
static bool evidence_valid(const struct kui_games_group_evidence *e) {
    return e && e->disc>=1u && e->total>=2u && e->total<=9u && e->disc<=e->total &&
        evidence_text(e->title,sizeof(e->title)) && evidence_text(e->product,sizeof(e->product)) &&
        evidence_text(e->version,sizeof(e->version)) && evidence_text(e->region,sizeof(e->region)) &&
        evidence_product(e->product);
}
bool kui_games_group_evidence_read(const uint8_t ip[2048],struct kui_games_group_evidence *out) {
    if(!ip || !out || memcmp(ip,"SEGA SEGAKATANA ",16u) ||
       ip[36]!=' ' || memcmp(ip+37u,"GD-ROM",6u) || ip[43]<'1' || ip[43]>'9' ||
       ip[44]!='/' || ip[45]<'2' || ip[45]>'9' || ip[43]>ip[45] ||
       ip[46]!=' ' || ip[47]!=' ' || ip[63]!=' ') return false;
    unsigned flags=0;
    for(unsigned i=56u;i<63u;i++) {
        unsigned char c=lower(ip[i]);unsigned value;
        if(digit(c)) value=(unsigned)(c-'0');
        else if(c>='a' && c<='f') value=(unsigned)(c-'a')+10u;
        else return false;
        flags=(flags<<4)|value;
    }
    if(flags&1u) return false;
    struct kui_games_group_evidence result={0};
    result.disc=(unsigned)(ip[43]-'0');result.total=(unsigned)(ip[45]-'0');
    if(!evidence_field(result.title,ip+128u,128u) ||
       !evidence_field(result.product,ip+64u,10u) ||
       !evidence_field(result.version,ip+74u,6u) ||
       !evidence_field(result.region,ip+48u,8u) || !evidence_valid(&result)) return false;
    *out=result;return true;
}
bool kui_games_group_evidence_equal(const struct kui_games_group_evidence *a,
    const struct kui_games_group_evidence *b) {
    return evidence_valid(a) && evidence_valid(b) && a->total==b->total &&
        !strcmp(a->title,b->title) && !strcmp(a->product,b->product) &&
        !strcmp(a->version,b->version) && !strcmp(a->region,b->region);
}
bool kui_games_group_key(const char *base,char key[KUI_GAMES_GROUP_KEY_CAP]) {
    if(!key || !kui_destination_name_valid(base)) return false;
    uint64_t a=UINT64_C(14695981039346656037),b=UINT64_C(7809847782465536322);
    for(const unsigned char *at=(const unsigned char *)base;*at;at++) {
        unsigned char c=lower(*at);
        a=(a^c)*UINT64_C(1099511628211);
        b=(b^c)*UINT64_C(14029467366897019727);
    }
    static const char hex[]="0123456789abcdef";
    char result[KUI_GAMES_GROUP_KEY_CAP];
    for(unsigned i=0;i<16u;i++) {
        result[i]=hex[(a>>(60u-i*4u))&15u];
        result[16u+i]=hex[(b>>(60u-i*4u))&15u];
    }
    result[32]=0;memcpy(key,result,sizeof(result));return true;
}
