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
